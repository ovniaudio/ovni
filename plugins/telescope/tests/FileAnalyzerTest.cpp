// [telescope][file] — FileAnalyzer (spec §5.7, prompt 54): EL MISMO MOTOR sobre un archivo, offline.
//
// LA PRUEBA CENTRAL es la de IDENTIDAD. El motor de TELESCOPE es independiente del tamaño de bloque por
// construcción (contratos casa-4 y SPEC[bloque]): las posiciones de hop y de frame las decide un contador
// de muestras desde el reset, no el host. Consecuencia: analizar un archivo offline tiene que dar
// EXACTAMENTE los mismos números que analizarlo en vivo — no "parecidos", idénticos AL BIT. Si algún día
// dejan de serlo, es que alguien metió una segunda implementación por el medio, y este test lo dice.
//
//   FILE[identidad]    ruido rosa estéreo 10 s a ~-14 LUFS: I / LRA / TP offline == los del processor con
//                      el MISMO WAV empujado por processBlock en bloques de 512. Y las 30 bandas de
//                      ⅓ de octava, contra el mismo motor alimentado en bloques de 512 en vez de 4 096.
//   FILE[ebu]          seno de 997 Hz a -23 dBFS de pico, 20 s, POR ARCHIVO → -23.0 ± 0.1 LUFS. El medidor
//                      ya está verificado contra los vectores de la EBU ([ebu]); lo que se prueba acá es
//                      el CAMINO del archivo (lectura, canales, hops), que es nuevo.
//   FILE[mono]         un archivo de 1 canal da los mismos números que el mismo material con L = R.
//   FILE[sr]           el mismo rosa generado a 44.1 k y a 48 k: I ± 0.1 y bandas ± 0.3 (la generación NO
//                      es la misma señal — el ruido depende de la cantidad de muestras — así que acá la
//                      tolerancia es declarada, no un "al bit" disfrazado).
//   FILE[progreso]     monótono y termina en 1.0.
//   FILE[cancel]       cancelar a mitad: vuelve en < 200 ms, valid = false, sin crash.
//   FILE[error]        archivo inexistente y archivo que no es audio: ok = false con un error legible.
//   FILE[determinismo] dos corridas del mismo archivo dan un FileAnalysis IDÉNTICO (huella serializada).
//   FILE[velocidad]    60 s de audio en menos de 6 s en el M4. Informativo (WARN, no falla): la máquina
//                      puede estar con tres obreras compilando al lado.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalyzer.h"
#include "analysis/modules/Spectrum.h"

using telescope::FileAnalysis;
using telescope::FileAnalyzer;
using telescope::Spectrum;
using telescope::ThirdOctaveAverage;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;

// Ruido rosa estéreo (dos semillas independientes) a un pico dado. Es la señal de la casa.
struct PinkStereo
{
    explicit PinkStereo (float peakIn) : peak (peakIn) {}
    std::pair<float, float> operator() (juce::int64) { return { peak * a.next(), peak * b.next() }; }
    float peak;
    Pink  a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
};

// Corre el analizador y espera a que termine (por condición, no por reloj: ver TestHelpers.h).
FileAnalysis analyseBlocking (FileAnalyzer& fa, const juce::File& f, int timeoutMs = 60000)
{
    fa.start (f);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, timeoutMs));
    return fa.result();
}

// El MISMO motor que usa FileAnalyzer para las bandas, alimentado a mano con el tamaño de bloque que se
// quiera. Sirve para probar que el resultado no depende de en qué pedazos entró el audio.
void bandsFromBuffer (const juce::AudioBuffer<float>& audio, double sr, int block, float* dst)
{
    Spectrum           sp;
    ThirdOctaveAverage acc;
    sp.setFrameSink (&acc);
    sp.applySettings (Spectrum::Settings{});
    sp.prepare (sr);
    sp.reset();
    acc.reset();

    const int n = audio.getNumSamples();
    for (int done = 0; done < n; done += block)
    {
        const int k = juce::jmin (block, n - done);
        sp.process (audio.getReadPointer (0) + done, audio.getReadPointer (1) + done, k);
    }
    acc.bandsDb (dst);
}

// Empuja el buffer por processBlock en bloques de `block`, frenando para que el bus NUNCA descarte.
void pushThroughProcessor (telescope::TelescopeProcessor& proc, const juce::AudioBuffer<float>& audio,
                           double sr, int block)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int n = audio.getNumSamples();

    for (int done = 0; done < n; done += block)
    {
        const int k = juce::jmin (block, n - done);
        buf.clear();
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, audio, c, done, k);
        buf.setSize (2, k, true, false, true);
        proc.processBlock (buf, midi);
        buf.setSize (2, block, false, false, true);

        // 30 s de tope y no 8: el timeout es un TECHO de seguridad, no el tiempo que se espera
        // (TestHelpers.h). Subirlo nunca hace el test más frágil, sólo más paciente con la máquina.
        const double pushed = (double) (done + k) / sr;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 30000));
    }
}

const char* okNo (bool b) { return b ? "si" : "NO"; }
}

// ============================================================================== 1 · identidad offline/vivo
TEST_CASE ("telescope: el analisis offline da los mismos numeros que el vivo", "[telescope][file]")
{
    constexpr double kSeconds = 10.0;
    const float peak = std::pow (10.0f, -2.4f / 20.0f);   // rosa con este pico ≈ -14 LUFS (medido)

    const auto wav = telescope::test::writeWav ("file_identity_pink_48k.wav", kSr, 2,
                                                (juce::int64) (kSeconds * kSr), PinkStereo { peak });
    REQUIRE (wav.existsAsFile());
    std::printf ("FILE[archivo] %s  (%.1f KB)\n", wav.getFullPathName().toRawUTF8(),
                 (double) wav.getSize() / 1024.0);

    // ---- offline ----
    FileAnalyzer fa;
    const auto off = analyseBlocking (fa, wav);
    REQUIRE (off.ok);
    REQUIRE (off.valid);

    // ---- vivo: LAS MISMAS MUESTRAS decodificadas, empujadas por processBlock ----
    double fileSr = 0.0;
    const auto audio = telescope::test::readWavStereo (wav, fileSr);
    REQUIRE (fileSr == kSr);
    REQUIRE (audio.getNumSamples() == (int) (kSeconds * kSr));

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    // La lente TONAL BALANCE pide kSpectrum | kReference: sin eso el acumulador vivo no corre y la
    // comparación de las bandas no mediría nada (lente a demanda).
    proc.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);
    pushThroughProcessor (proc, audio, kSr, 512);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= kSeconds - 0.05; },
                                         10000));

    const auto live = proc.analysis().read();
    // Si el bus descartó una sola muestra, los dos lados NO midieron el mismo audio y comparar al bit
    // dejaría de significar algo. Se verifica, no se supone.
    REQUIRE (live.droppedSamples == 0u);

    std::printf ("FILE[identidad] offline  I=%.6f  LRA=%.6f  TP=%.6f\n",
                 off.integratedLufs, off.lra, off.truePeakDbtp);
    std::printf ("FILE[identidad] vivo     I=%.6f  LRA=%.6f  TP=%.6f\n",
                 live.loudness.integrated, live.loudness.lra, live.loudness.truePeakMax);
    std::printf ("FILE[identidad] iguales al bit: I=%s  LRA=%s  TP=%s\n",
                 okNo (off.integratedLufs == live.loudness.integrated),
                 okNo (off.lra == live.loudness.lra),
                 okNo (off.truePeakDbtp == live.loudness.truePeakMax));

    REQUIRE (off.integratedLufs == live.loudness.integrated);
    REQUIRE (off.lra            == live.loudness.lra);
    REQUIRE (off.truePeakDbtp   == live.loudness.truePeakMax);
    REQUIRE (off.momentaryMax   == live.loudness.momentaryMax);
    REQUIRE (off.shortTermMax   == live.loudness.shortTermMax);
    REQUIRE (off.integratedLufs < -12.0f);
    REQUIRE (off.integratedLufs > -16.0f);
    REQUIRE (off.truePeakDbtp   < 0.0f);      // que la señal no esté recortando: el test mide, no clipea

    // ---- las 30 bandas: el mismo motor alimentado en bloques de 512 en vez de 4 096 ----
    float bands512[FileAnalysis::kNumBands];
    bandsFromBuffer (audio, kSr, 512, bands512);

    int same = 0, measured = 0;
    float worst = 0.0f;
    for (int b = 0; b < FileAnalysis::kNumBands; ++b)
    {
        if (off.bandsDb[b] <= telescope::SpectrumFrame::kFloorDb) continue;
        ++measured;
        if (off.bandsDb[b] == bands512[b]) ++same;
        worst = juce::jmax (worst, std::abs (off.bandsDb[b] - bands512[b]));
    }
    std::printf ("FILE[identidad] bandas: %d de %d medidas iguales AL BIT entre bloque 4096 y 512 "
                 "(peor diferencia %.9f dB)\n", same, measured, worst);
    REQUIRE (measured >= 25);
    REQUIRE (same == measured);

    // ---- Y CONTRA EL MÓDULO VIVO, por el camino de verdad: processBlock → bus → AnalysisThread →
    //      Spectrum → FrameSink → Reference. Es la prueba que sostiene la promesa de TONAL BALANCE.
    const auto& refFrame = proc.reference().read();
    int sameLive = 0, measuredLive = 0;
    float worstLive = 0.0f;
    for (int b = 0; b < FileAnalysis::kNumBands; ++b)
    {
        if (off.bandsDb[b] <= telescope::SpectrumFrame::kFloorDb) continue;
        ++measuredLive;
        if (off.bandsDb[b] == refFrame.liveBands[b]) ++sameLive;
        worstLive = juce::jmax (worstLive, std::abs (off.bandsDb[b] - refFrame.liveBands[b]));
    }
    std::printf ("FILE[identidad] bandas OFFLINE vs MODULO VIVO: %d de %d iguales AL BIT "
                 "(peor diferencia %.9f dB)  ·  vivo: %.3f s promediados, I=%+.6f, valida=%d\n",
                 sameLive, measuredLive, worstLive, refFrame.liveSeconds, refFrame.liveIntegrated,
                 (int) refFrame.liveValid);
    REQUIRE (refFrame.liveValid);
    REQUIRE (refFrame.liveIntegrated == off.integratedLufs);
    REQUIRE (measuredLive >= 25);
    REQUIRE (sameLive == measuredLive);

    // Las series temporales tienen la cadencia declarada: 10 Hz y 1 Hz EXACTOS.
    std::printf ("FILE[series] short-term=%d puntos (esperado %d)  true-peak/s=%d puntos (esperado %d)\n",
                 (int) off.shortTermHistory.size(), (int) (kSeconds * 10.0),
                 (int) off.truePeakPerSecond.size(), (int) kSeconds);
    REQUIRE ((int) off.shortTermHistory.size()  == (int) (kSeconds * 10.0));
    REQUIRE ((int) off.truePeakPerSecond.size() == (int) kSeconds);

    proc.releaseResources();
}

// ==================================================================================== 2 · EBU 3341-1 en WAV
TEST_CASE ("telescope: un seno de -23 dBFS en un archivo lee -23.0 LUFS", "[telescope][file]")
{
    constexpr double kSeconds = 20.0;
    const float amp = std::pow (10.0f, -23.0f / 20.0f);   // PICO por canal, como el resto de [ebu]

    const auto wav = telescope::test::writeWav (
        "file_ebu_997_-23.wav", kSr, 2, (juce::int64) (kSeconds * kSr),
        [amp] (juce::int64 i)
        {
            const auto v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0
                                                    * (double) i / kSr));
            return std::pair<float, float> { v, v };
        });

    FileAnalyzer fa;
    const auto a = analyseBlocking (fa, wav);
    REQUIRE (a.ok);
    REQUIRE (a.valid);

    std::printf ("FILE[ebu] I=%+.3f LUFS (esperado -23.0 ± 0.1)  TP=%+.3f dBTP  LRA=%.3f LU  %.1f s  %d ch\n",
                 a.integratedLufs, a.truePeakDbtp, a.lra, a.seconds, a.channels);
    REQUIRE (std::abs (a.integratedLufs + 23.0f) <= 0.1f);
    REQUIRE (a.channels == 2);
    REQUIRE (std::abs (a.seconds - kSeconds) < 0.001);
}

// ============================================================================================ 3 · mono
TEST_CASE ("telescope: un archivo mono da los mismos numeros que el mismo material con L = R",
           "[telescope][file]")
{
    constexpr double kSeconds = 6.0;
    const float peak = std::pow (10.0f, -10.0f / 20.0f);

    // La MISMA secuencia en los dos archivos: un solo generador, y R = L en el estéreo.
    const auto gen = [peak] (Pink& p) { return [peak, &p] (juce::int64) { const float v = peak * p.next();
                                                                          return std::pair<float, float> { v, v }; }; };
    Pink p1 { telescope::test::kPinkSeedA }, p2 { telescope::test::kPinkSeedA };

    const auto monoFile   = telescope::test::writeWav ("file_mono.wav",   kSr, 1, (juce::int64) (kSeconds * kSr), gen (p1));
    const auto stereoFile = telescope::test::writeWav ("file_mono_lr.wav", kSr, 2, (juce::int64) (kSeconds * kSr), gen (p2));

    FileAnalyzer fa;
    const auto mono   = analyseBlocking (fa, monoFile);
    const auto stereo = analyseBlocking (fa, stereoFile);

    REQUIRE (mono.ok);
    REQUIRE (stereo.ok);
    REQUIRE (mono.channels == 1);
    REQUIRE (stereo.channels == 2);

    float worstBand = 0.0f;
    for (int b = 0; b < FileAnalysis::kNumBands; ++b)
        worstBand = juce::jmax (worstBand, std::abs (mono.bandsDb[b] - stereo.bandsDb[b]));

    std::printf ("FILE[mono] mono I=%.6f TP=%.6f  |  L=R I=%.6f TP=%.6f  |  peor banda %.9f dB  |  "
                 "iguales al bit: %s\n", mono.integratedLufs, mono.truePeakDbtp,
                 stereo.integratedLufs, stereo.truePeakDbtp, worstBand,
                 okNo (mono.integratedLufs == stereo.integratedLufs && worstBand == 0.0f));

    REQUIRE (mono.integratedLufs == stereo.integratedLufs);
    REQUIRE (mono.truePeakDbtp   == stereo.truePeakDbtp);
    REQUIRE (mono.lra            == stereo.lra);
    REQUIRE (worstBand == 0.0f);
}

// =========================================================================================== 4 · 44.1 / 48
TEST_CASE ("telescope: el mismo rosa a 44.1 k y a 48 k mide lo mismo dentro de la tolerancia declarada",
           "[telescope][file]")
{
    constexpr double kSeconds = 10.0;
    const float peak = std::pow (10.0f, -8.0f / 20.0f);

    const auto a48 = telescope::test::writeWav ("file_pink_48k.wav", 48000.0, 2,
                                                (juce::int64) (kSeconds * 48000.0), PinkStereo { peak });
    const auto a44 = telescope::test::writeWav ("file_pink_44k.wav", 44100.0, 2,
                                                (juce::int64) (kSeconds * 44100.0), PinkStereo { peak });

    FileAnalyzer fa;
    const auto r48 = analyseBlocking (fa, a48);
    const auto r44 = analyseBlocking (fa, a44);
    REQUIRE (r48.ok);
    REQUIRE (r44.ok);
    REQUIRE (r48.sr == 48000.0);
    REQUIRE (r44.sr == 44100.0);

    // Las bandas se comparan sólo donde las DOS tienen medición: a 44.1 k la banda de 20 kHz queda por
    // encima de Nyquist/2 y no hay nada que comparar (decir "0 dB de diferencia" ahí sería inventar).
    //
    // Y el criterio de ±0.3 dB se pide donde la banda ESTÁ MEDIDA de verdad: con CUATRO bins o más de
    // cada lado (o sea de 200 Hz para arriba con FFT de 4 096). Abajo de eso lo que sobra no es un sesgo
    // del analizador —la corrección de ancho cubierto ya se lo comió: sin ella el peor caso era 3.415 dB
    // y con ella 0.333— sino la VARIANZA del propio estimador: la potencia de una banda medida con dos
    // bins sobre 10 s de ruido tiene desviación propia, y los dos archivos son ruido DISTINTO (no es la
    // misma señal remuestreada, es la misma receta generada a dos sample rates). Esas bandas se imprimen
    // igual, con su cuenta de bins al lado: el número está a la vista, lo que no se hace es fingir que
    // dos bins miden con la misma precisión que doscientos.
    const auto binsFor = [] (double sr, int band)
    {
        constexpr double lo6 = 0.8908987181403393, hi6 = 1.1224620483093730;
        const double binHz = sr / 4096.0;   // el análisis de archivo es orden 12 fijo
        const int k0 = juce::jmax (0, (int) std::ceil (telescope::kThirdOctaveHz[band] * lo6 / binHz));
        const int k1 = juce::jmin (2049, (int) std::ceil (telescope::kThirdOctaveHz[band] * hi6 / binHz));
        return juce::jmax (0, k1 - k0);
    };

    float worst = 0.0f, worstThin = 0.0f;
    int   worstBand = -1, worstThinBand = -1, compared = 0, thin = 0;
    for (int b = 0; b < FileAnalysis::kNumBands; ++b)
    {
        if (r48.bandsDb[b] <= telescope::SpectrumFrame::kFloorDb) continue;
        if (r44.bandsDb[b] <= telescope::SpectrumFrame::kFloorDb) continue;
        if (telescope::kThirdOctaveHz[b] > 16000.0) continue;   // el rosa ya no manda arriba de 16k a 44.1

        const int n48 = binsFor (48000.0, b), n44 = binsFor (44100.0, b);
        const float d = std::abs (r48.bandsDb[b] - r44.bandsDb[b]);
        std::printf ("FILE[sr]   %6.0f Hz  48k=%+8.3f (%2d bins)  44.1k=%+8.3f (%2d bins)  delta=%.3f dB%s\n",
                     telescope::kThirdOctaveHz[b], r48.bandsDb[b], n48, r44.bandsDb[b], n44, d,
                     juce::jmin (n48, n44) < 4 ? "   (menos de 4 bins: informativa)" : "");

        if (juce::jmin (n48, n44) < 4) { ++thin; if (d > worstThin) { worstThin = d; worstThinBand = b; } continue; }
        ++compared;
        if (d > worst) { worst = d; worstBand = b; }
    }

    std::printf ("FILE[sr] 48k I=%+.3f  44.1k I=%+.3f  delta=%+.3f (tol 0.1)  |  peor banda medida: %.0f Hz "
                 "%.3f dB (tol 0.3, %d bandas)  |  peor banda de menos de 4 bins: %.0f Hz %.3f dB (%d bandas, informativo)\n",
                 r48.integratedLufs, r44.integratedLufs, r44.integratedLufs - r48.integratedLufs,
                 worstBand >= 0 ? telescope::kThirdOctaveHz[worstBand] : 0.0, worst, compared,
                 worstThinBand >= 0 ? telescope::kThirdOctaveHz[worstThinBand] : 0.0, worstThin, thin);

    REQUIRE (std::abs (r44.integratedLufs - r48.integratedLufs) <= 0.1f);
    REQUIRE (compared >= 18);
    REQUIRE (worstThin <= 1.0f);   // informativas, pero tampoco pueden irse a cualquier lado
    REQUIRE (worst <= 0.3f);
}

// =========================================================================================== 5 · progreso
TEST_CASE ("telescope: el progreso del analisis es monotono y termina en 1.0", "[telescope][file]")
{
    const auto wav = telescope::test::writeWav ("file_progress.wav", kSr, 2, (juce::int64) (20.0 * kSr),
                                                PinkStereo { 0.2f });

    FileAnalyzer fa;
    fa.start (wav);

    float last = 0.0f;
    int   samples = 0, regressions = 0;
    while (fa.busy() && samples < 20000)
    {
        const float p = fa.progress();
        if (p < last) ++regressions;
        last = juce::jmax (last, p);
        ++samples;
        juce::Thread::sleep (1);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 30000));

    std::printf ("FILE[progreso] %d muestras  ·  retrocesos: %d  ·  final: %.3f\n",
                 samples, regressions, fa.progress());
    REQUIRE (regressions == 0);
    REQUIRE (fa.progress() == 1.0f);
    REQUIRE (fa.result().valid);
}

// ============================================================================================ 6 · cancelar
TEST_CASE ("telescope: cancelar el analisis vuelve enseguida y deja el resultado invalido",
           "[telescope][file]")
{
    // Un archivo largo, para que la cancelación caiga de verdad a mitad de camino.
    const auto wav = telescope::test::writeWav ("file_long.wav", kSr, 2, (juce::int64) (60.0 * kSr),
                                                PinkStereo { 0.2f });

    FileAnalyzer fa;
    fa.start (wav);
    // Esperar a que EMPIECE (que haya progreso real), si no se cancelaría algo que todavía no arrancó.
    REQUIRE (telescope::test::waitUntil ([&] { return fa.progress() > 0.02f; }, 5000));
    const float atCancel = fa.progress();

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    fa.cancel();
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    const auto a = fa.result();
    std::printf ("FILE[cancel] cancelado en %.1f %% ·  cancel() tardo %.1f ms (criterio < 200)  ·  "
                 "valid=%d  error=\"%s\"\n", 100.0 * (double) atCancel, ms, (int) a.valid,
                 a.error.toRawUTF8());

    REQUIRE (ms < 200.0);
    REQUIRE_FALSE (fa.busy());
    REQUIRE_FALSE (a.valid);
    REQUIRE (a.error.isNotEmpty());
}

// ============================================================================================= 7 · errores
TEST_CASE ("telescope: un archivo inexistente o que no es audio da un error legible", "[telescope][file]")
{
    FileAnalyzer fa;

    const auto missing = telescope::test::tempDir().getChildFile ("no_existe_este_archivo.wav");
    missing.deleteFile();
    const auto a = analyseBlocking (fa, missing, 5000);
    std::printf ("FILE[error] inexistente: ok=%d  error=\"%s\"\n", (int) a.ok, a.error.toRawUTF8());
    REQUIRE_FALSE (a.ok);
    REQUIRE_FALSE (a.valid);
    REQUIRE (a.error.isNotEmpty());

    auto txt = telescope::test::tempDir().getChildFile ("no_soy_audio.txt");
    txt.deleteFile();
    txt.replaceWithText ("esto no es un archivo de audio, es un texto\n");
    const auto b = analyseBlocking (fa, txt, 5000);
    std::printf ("FILE[error] .txt: ok=%d  error=\"%s\"\n", (int) b.ok, b.error.toRawUTF8());
    REQUIRE_FALSE (b.ok);
    REQUIRE_FALSE (b.valid);
    REQUIRE (b.error.isNotEmpty());
}

// ======================================================================================= 8 · determinismo
TEST_CASE ("telescope: dos analisis del mismo archivo dan un resultado identico", "[telescope][file]")
{
    const auto wav = telescope::test::writeWav ("file_determinism.wav", kSr, 2, (juce::int64) (8.0 * kSr),
                                                PinkStereo { std::pow (10.0f, -10.0f / 20.0f) });

    FileAnalyzer fa;
    const auto a = analyseBlocking (fa, wav);
    const auto b = analyseBlocking (fa, wav);
    REQUIRE (a.valid);
    REQUIRE (b.valid);

    const auto fa1 = a.fingerprint(), fb1 = b.fingerprint();
    std::printf ("FILE[determinismo] huella de %d caracteres  ·  identicas: %s\n",
                 fa1.length(), okNo (fa1 == fb1));
    REQUIRE (fa1 == fb1);

    // Y con OTRO objeto analizador (que el resultado no dependa del estado que quedó adentro del anterior).
    FileAnalyzer fa2;
    const auto c = analyseBlocking (fa2, wav);
    REQUIRE (c.fingerprint() == fa1);
}

// ========================================================================================== 9 · velocidad
TEST_CASE ("telescope: el analisis offline es mucho mas rapido que tiempo real", "[telescope][file]")
{
    constexpr double kSeconds = 60.0;
    const auto wav = telescope::test::writeWav ("file_long.wav", kSr, 2, (juce::int64) (kSeconds * kSr),
                                                PinkStereo { 0.2f });

    FileAnalyzer fa;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto a  = analyseBlocking (fa, wav, 120000);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    REQUIRE (a.valid);
    const double factor = kSeconds * 1000.0 / juce::jmax (1.0, ms);
    std::printf ("FILE[velocidad] %.0f s de audio en %.0f ms  ·  %.0f veces mas rapido que tiempo real  "
                 "(criterio < 6000 ms)\n", kSeconds, ms, factor);

    // INFORMATIVO: la máquina puede estar con tres obreras compilando al lado. El WARN queda en el log.
    if (ms >= 6000.0)
        WARN ("el analisis de 60 s tardo " << ms << " ms (criterio 6000): maquina cargada?");
    REQUIRE (ms < 60000.0);   // lo que SÍ falla: que no llegue a ser más rápido que tiempo real
}
