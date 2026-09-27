#include "analysis/RangeAnalysis.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
RangeAnalyzer::RangeAnalyzer()
{
    // WAV / AIFF / FLAC / Ogg siempre, y en macOS además todo lo que lea CoreAudio (MP3, AAC, ALAC).
    // En Windows, WMA y MP3 por WindowsMediaAudioFormat. Es lo que da JUCE de fábrica: no hay decoder
    // propio acá, y por lo tanto tampoco un formato que "casi" se lee.
    formats.registerBasicFormats();
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
//
// Hasta el prompt 101 esto vivía en FileAnalyzer::analyse y medía siempre el archivo entero. Se mudó acá sin
// cambiar una operación: lo único nuevo es que la lectura arranca en `range.start` en vez de en 0, y que el
// bucle suma de paso ΣLL, ΣRR y ΣLR. Con el archivo entero da los mismos bits que antes ([golden]).
// ========================================================================================================
RangeMeasurement RangeAnalyzer::measure (const juce::File& f,
                                         std::optional<double> fromS,
                                         std::optional<double> toS,
                                         const std::function<bool()>& shouldExit,
                                         const std::function<void (float)>& progress)
{
    RangeMeasurement m;
    m.requestedFromS = fromS;
    m.requestedToS   = toS;

    FileAnalysis& a = m.analysis;
    a.name = f.getFileName();
    a.path = f.getFullPathName();

    if (! f.existsAsFile())
    {
        a.error = juce::String::fromUTF8 ("el archivo no existe");
        m.status = RangeMeasurement::Status::fileNotFound;
        return m;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
    {
        a.error = juce::String::fromUTF8 ("formato no reconocido (se leen WAV, AIFF, FLAC, Ogg y lo que "
                                          "sepa el sistema)");
        m.status = RangeMeasurement::Status::notAudio;
        return m;
    }

    const auto totalSamples = (juce::int64) reader->lengthInSamples;
    const double sr         = reader->sampleRate;
    const int    numCh      = (int) reader->numChannels;

    if (sr <= 0.0 || numCh <= 0 || totalSamples <= 0)
    {
        a.error = juce::String::fromUTF8 ("el archivo no tiene audio");
        m.status = RangeMeasurement::Status::noAudio;
        return m;
    }

    a.ok       = true;
    a.sr       = sr;
    a.channels = numCh;
    a.seconds  = (double) totalSamples / sr;
    m.fileSamples = totalSamples;

    if (numCh > 2)
        a.warning = juce::String::fromUTF8 ("el archivo tiene ") + juce::String (numCh)
                  + juce::String::fromUTF8 (" canales: se miden los dos primeros (L y R)");

    // ---- el tramo: segundos del archivo → muestras, recortado al archivo (ver RangeAnalysis.h) ----
    // Un número que no es finito no es un tramo: queda vacío y es una negativa.
    const bool finite = (! fromS || std::isfinite (*fromS)) && (! toS || std::isfinite (*toS));
    juce::int64 start = fromS && finite ? sampleAt (*fromS, sr) : 0;
    juce::int64 end   = toS   && finite ? sampleAt (*toS,   sr) : totalSamples;
    if (! finite) end = start;
    if (start < 0)            { start = 0;            m.clipped = true; }
    if (end > totalSamples)   { end = totalSamples;   m.clipped = true; }
    if (start > totalSamples) { start = totalSamples; m.clipped = true; }
    m.range = { start, end };

    if (end <= start)
    {
        m.range = { start, start };
        m.status = RangeMeasurement::Status::rangeEmpty;
        return m;
    }

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
    //
    // Se re-PREPARA sólo si cambió la frecuencia de muestreo (prompt 101, hito 5). prepare() tira los kernels,
    // y rearmarlos son ~229 FFT de 2^16: ~155 ms por archivo, el 80 % del tiempo de medir los 53 stems de un
    // proyecto. Los kernels dependen sólo de la geometría (la SR y los settings de fábrica), así que con la
    // misma SR alcanza con reset(): mismo estado, mismos números. Lo sostienen MEASURE[determinismo] y
    // MEASURE[orden] (un motor que ya midió otra cosa da lo mismo que uno recién creado).
    cqt.applySettings (Cqt::Settings{});
    if (sr != cqtPreparedSr)
    {
        cqt.prepare (sr);
        cqtPreparedSr = sr;
    }
    cqt.reset();

    const int hop = juce::jmax (1, (int) std::llround (sr / 10.0));   // 100 ms, igual que AnalysisThread

    juce::AudioBuffer<float> block (juce::jmax (2, numCh), kReadBlock);
    std::vector<float> hopL ((size_t) hop, 0.0f), hopR ((size_t) hop, 0.0f);
    std::vector<float> chunkL ((size_t) kReadBlock, 0.0f), chunkR ((size_t) kReadBlock, 0.0f);
    int hopFill = 0;

    float  tpThisSecond = kSilenceDb;
    int    hopsThisSecond = 0;

    const juce::int64 length = end - start;
    double ll = 0.0, rr = 0.0, lr = 0.0;

    juce::int64 done = 0;
    while (done < length)
    {
        if (shouldExit && shouldExit())
        {
            a.valid = false;
            a.error = juce::String::fromUTF8 ("analisis cancelado");
            m.status = RangeMeasurement::Status::cancelled;
            return m;
        }

        const int n = (int) juce::jmin ((juce::int64) kReadBlock, length - done);
        block.clear();
        if (! reader->read (&block, 0, n, start + done, true, numCh > 1))
        {
            a.ok    = false;
            a.error = juce::String::fromUTF8 ("no se pudo leer el archivo completo");
            m.status = RangeMeasurement::Status::readError;
            return m;
        }

        // Mono → L = R (lo mismo que hace processAudio con una fuente de un canal).
        const float* src0 = block.getReadPointer (0);
        const float* src1 = numCh > 1 ? block.getReadPointer (1) : src0;
        std::copy (src0, src0 + n, chunkL.begin());
        std::copy (src1, src1 + n, chunkR.begin());

        // ---- el estéreo de banda ancha del tramo: las tres sumas, en orden (ver RangeAnalysis.h) ----
        for (int i = 0; i < n; ++i)
        {
            const double l = chunkL[(size_t) i], r = chunkR[(size_t) i];
            ll += l * l;
            rr += r * r;
            lr += l * r;
        }

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
        if (progress)
            progress (juce::jlimit (0.0f, 1.0f, (float) ((double) done / (double) length)));
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

    m.sumLL = ll;
    m.sumRR = rr;
    m.sumLR = lr;

    a.valid = true;
    m.status = RangeMeasurement::Status::measured;
    return m;
}
}
