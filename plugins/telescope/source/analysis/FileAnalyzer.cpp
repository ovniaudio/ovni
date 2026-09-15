#include "analysis/FileAnalyzer.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
// Plazo de gracia de cancel(). El worker chequea la salida una vez por bloque de 4 096 muestras, o sea
// ~85 µs de trabajo real: sale enseguida. El plazo existe para que un I/O trabado no cuelgue la UI.
constexpr int kCancelGraceMs = 2000;
}

FileAnalyzer::FileAnalyzer() : juce::Thread ("TelescopeFileAnalyzer")
{
    // WAV / AIFF / FLAC / Ogg siempre, y en macOS además todo lo que lea CoreAudio (MP3, AAC, ALAC).
    // En Windows, WMA y MP3 por WindowsMediaAudioFormat. Es lo que da JUCE de fábrica: no hay decoder
    // propio acá, y por lo tanto tampoco un formato que "casi" se lee.
    formats.registerBasicFormats();
}

FileAnalyzer::~FileAnalyzer()
{
    cancel();
}

void FileAnalyzer::start (const juce::File& f)
{
    cancel();   // uno a la vez (ver el header)

    {
        const juce::ScopedLock sl (lock);
        pendingName = f.getFileName();
    }

    pending = f;
    progressValue.store (0.0f, std::memory_order_release);
    running.store (true, std::memory_order_release);
    startThread();
}

void FileAnalyzer::cancel()
{
    if (! isThreadRunning() && ! running.load (std::memory_order_acquire)) return;

    if (! stopThread (kCancelGraceMs))
    {
        juce::Logger::writeToLog ("TELESCOPE: el analizador de archivo no salio en "
                                  + juce::String (kCancelGraceMs) + " ms; esperando sin plazo");
        stopThread (-1);
    }
    running.store (false, std::memory_order_release);
}

juce::String FileAnalyzer::currentName() const
{
    const juce::ScopedLock sl (lock);
    return pendingName;
}

FileAnalysis FileAnalyzer::result() const
{
    const juce::ScopedLock sl (lock);
    return latest;
}

void FileAnalyzer::publish (const FileAnalysis& a)
{
    {
        const juce::ScopedLock sl (lock);
        latest = a;
    }
    rev.fetch_add (1, std::memory_order_acq_rel);
}

void FileAnalyzer::run()
{
    const auto a = analyse (pending);
    publish (a);
    progressValue.store (1.0f, std::memory_order_release);
    running.store (false, std::memory_order_release);
    if (onFinished) onFinished();
}

// ========================================================================================================
// EL ANÁLISIS. Es, línea por línea, lo que hace el AnalysisThread con el audio en vivo:
//
//   · el medidor de loudness come HOPS de 100 ms exactos (y de ahí salen las dos series temporales);
//   · el espectro come el bloque ENTERO que se acaba de leer y decide sus propias posiciones de frame
//     contando muestras desde el reset (por eso el tamaño del bloque de lectura no cambia un solo bit);
//   · el acumulador de ⅓ de octava cuelga del FrameSink del espectro: no vuelve a transformar nada.
//
// Lo único que NO se hace acá es publicar frames para la UI: un archivo no se dibuja mientras se lee.
// ========================================================================================================
FileAnalysis FileAnalyzer::analyse (const juce::File& f)
{
    FileAnalysis a;
    a.name = f.getFileName();
    a.path = f.getFullPathName();

    if (! f.existsAsFile())
    {
        a.error = juce::String::fromUTF8 ("el archivo no existe");
        return a;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
    {
        a.error = juce::String::fromUTF8 ("formato no reconocido (se leen WAV, AIFF, FLAC, Ogg y lo que "
                                          "sepa el sistema)");
        return a;
    }

    const auto totalSamples = (juce::int64) reader->lengthInSamples;
    const double sr         = reader->sampleRate;
    const int    numCh      = (int) reader->numChannels;

    if (sr <= 0.0 || numCh <= 0 || totalSamples <= 0)
    {
        a.error = juce::String::fromUTF8 ("el archivo no tiene audio");
        return a;
    }

    a.ok       = true;
    a.sr       = sr;
    a.channels = numCh;
    a.seconds  = (double) totalSamples / sr;

    if (numCh > 2)
        a.warning = juce::String::fromUTF8 ("el archivo tiene ") + juce::String (numCh)
                  + juce::String::fromUTF8 (" canales: se miden los dos primeros (L y R)");

    // ---- el motor, preparado a la SR DEL ARCHIVO ----
    loudness.reset();
    loudness.prepare (sr);
    bands.reset();
    // ===== 55: dos consumidores del mismo FrameSink — el promedio infinito de ⅓ de octava (la
    // referencia de TONAL BALANCE) y el constructor de filas por segundo (VERDICT). Ver FrameSinkFanout.
    secondBuilder.prepare (sr);
    secondBuilder.reset();
    fanout.clear();
    fanout.set (0, &bands);
    fanout.set (1, &secondBuilder);
    spectrum.setFrameSink (&fanout);
    // Los settings del análisis de archivo son FIJOS y no los de la lente: una referencia tiene que dar el
    // mismo número hoy y mañana, mire el usuario el espectro con el orden que mire. Son los defaults del
    // módulo (orden 12 · Hann · 75 % · L+R), que es también con lo que arranca el análisis en vivo.
    spectrum.applySettings (Spectrum::Settings{});
    spectrum.prepare (sr);
    spectrum.reset();

    // ===== 55: el CONSTANT-Q, con los settings de fábrica (canal M, suavizado del cromagrama por
    // default) — los mismos con los que arranca el análisis en vivo. Si el usuario cambió el canal del
    // CQT en la lente 6, la tonalidad de la fila EN VIVO se calcula con ese canal y la del archivo con
    // el de fábrica: es el único campo de la fila que puede diferir, y está dicho en el README.
    cqt.applySettings (Cqt::Settings{});
    cqt.prepare (sr);
    cqt.reset();

    const int hop = juce::jmax (1, (int) std::llround (sr / 10.0));   // 100 ms, igual que AnalysisThread

    juce::AudioBuffer<float> block (juce::jmax (2, numCh), kReadBlock);
    std::vector<float> hopL ((size_t) hop, 0.0f), hopR ((size_t) hop, 0.0f);
    std::vector<float> chunkL ((size_t) kReadBlock, 0.0f), chunkR ((size_t) kReadBlock, 0.0f);
    int hopFill = 0;

    float  tpThisSecond = kSilenceDb;
    int    hopsThisSecond = 0;

    juce::int64 done = 0;
    while (done < totalSamples)
    {
        if (threadShouldExit())
        {
            a.valid = false;
            a.error = juce::String::fromUTF8 ("analisis cancelado");
            return a;
        }

        const int n = (int) juce::jmin ((juce::int64) kReadBlock, totalSamples - done);
        block.clear();
        if (! reader->read (&block, 0, n, done, true, numCh > 1))
        {
            a.ok    = false;
            a.error = juce::String::fromUTF8 ("no se pudo leer el archivo completo");
            return a;
        }

        // Mono → L = R (lo mismo que hace processAudio con una fuente de un canal).
        const float* src0 = block.getReadPointer (0);
        const float* src1 = numCh > 1 ? block.getReadPointer (1) : src0;
        std::copy (src0, src0 + n, chunkL.begin());
        std::copy (src1, src1 + n, chunkR.begin());

        // ---- el espectro come el bloque entero (posiciones de frame propias) ----
        spectrum.process (chunkL.data(), chunkR.data(), n);

        // ---- el medidor come hops de 100 ms exactos ----
        for (int i = 0; i < n; ++i)
        {
            hopL[(size_t) hopFill] = chunkL[(size_t) i];
            hopR[(size_t) hopFill] = chunkR[(size_t) i];

            if (++hopFill == hop)
            {
                loudness.process (hopL.data(), hopR.data(), hop);
                const auto r = loudness.result();
                a.shortTermHistory.push_back (r.shortTerm);

                // ===== 55: el CQT come del hop, DESPUÉS del medidor — el mismo orden que processHop.
                cqt.process (hopL.data(), hopR.data(), hop);
                const auto& c = cqt.frame();

                SecondBuilder::HopInput in;
                // En el archivo TODO se mide: no hay lente a demanda que apague nada.
                in.modules       = kSecondRowModules;
                in.shortTerm     = r.shortTerm;
                in.momentary     = r.momentary;
                in.truePeakHop   = r.truePeakHop;
                in.clipEventsHop = r.clipEventsHop;
                in.dcSumL        = r.dcSumHopL;
                in.dcSumR        = r.dcSumHopR;
                in.hopSamples    = hop;
                in.keyTonic      = c.keyTonic;
                in.keyMode       = c.keyMode;
                in.keyConfidence = c.keyConfidence;

                SecondRow row;
                if (secondBuilder.pushHop (in, row)) a.secondRows.push_back (row);

                tpThisSecond = juce::jmax (tpThisSecond, r.truePeakHop);
                if (++hopsThisSecond == 10)
                {
                    a.truePeakPerSecond.push_back (tpThisSecond);
                    tpThisSecond   = kSilenceDb;
                    hopsThisSecond = 0;
                }

                hopFill = 0;
            }
        }

        done += n;
        progressValue.store (juce::jlimit (0.0f, 1.0f, (float) ((double) done / (double) totalSamples)),
                             std::memory_order_release);
    }

    // La cola de menos de un segundo también cuenta: un archivo de 10.4 s tiene once segundos de
    // true-peak, el último incompleto. Descartarlo escondería justo el final, que es donde suele estar
    // el pico. Un hop incompleto, en cambio, NO se procesa: es lo mismo que hace el análisis en vivo.
    if (hopsThisSecond > 0) a.truePeakPerSecond.push_back (tpThisSecond);

    const auto r = loudness.result();
    a.integratedLufs = r.integrated;
    a.lra            = r.lra;
    a.truePeakDbtp   = r.truePeakMax;
    a.momentaryMax   = r.momentaryMax;
    a.shortTermMax   = r.shortTermMax;
    // ===== 55: los agregados que VERDICT lee (los MISMOS campos que publica el AnalysisFrame en vivo).
    a.clipEvents      = r.clipEvents;
    a.dcL             = r.dcL;
    a.dcR             = r.dcR;
    a.keyTonic        = cqt.frame().keyTonic;
    a.keyMode         = cqt.frame().keyMode;
    a.keyConfidence   = cqt.frame().keyConfidence;
    a.keyTimeFraction = cqt.frame().keyTimeFraction;
    bands.bandsDb (a.bandsDb);

    a.valid = true;
    return a;
}
}
