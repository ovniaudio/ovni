// [telescope][lifecycle] — RE-PREPARAR el mismo processor. Todo DAW lo hace: cambiás el sample rate del
// device, cambiás el tamaño de bloque, el host suspende y reanuda. En TELESCOPE ese camino toca lo más
// delicado del plugin: `AnalysisThread::prepare()` PARA el worker y acto seguido REDIMENSIONA los vectores
// (chunkL/R, hopL/R) que el worker estaba usando.
//
// Hallazgo M-1 del revisor del prompt 48: `stop()` descartaba el retorno de `stopThread(1000)`. Si el
// worker no salía en ese segundo, `prepare()` seguía igual y reasignaba vectores que el thread viejo
// todavía estaba escribiendo → use-after-free silencioso (y en release, corrupción de memoria, no crash).
// Ningún test del 48 llamaba `prepareToPlay` dos veces sobre la misma instancia.
//
// Este test recorre 20 veces la secuencia completa (tres sample rates + tres tamaños de bloque +
// releaseResources + re-prepare) verificando que después de cada cambio el medidor SIGUE MIDIENDO bien.
// No prueba que la carrera sea imposible — eso lo da el `jassert(exited)` de stop() —, pero deja el camino
// cubierto y ejercitado, que es lo que no existía.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "TestHelpers.h"

namespace
{
constexpr float kMinus23 = -23.0f;

// Empuja EXACTAMENTE `samples` muestras de un seno de 997 Hz a `peak`, en bloques de `blockSize`.
void pushSine (telescope::TelescopeProcessor& proc, double sr, int blockSize, int samples, float peak)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int done = 0; done < samples; done += blockSize)
    {
        const int m = juce::jmin (blockSize, samples - done);
        buf.clear();
        for (int i = 0; i < m; ++i, ++n)
        {
            const auto v = (float) (peak * std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (double) n / sr));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
    }
}

// Prepara a (sr, blockSize), mete 2 s de seno a -23 dBFS y verifica que el momentary llegue a -23 LUFS.
// (identidad del 48: para un seno de 997 Hz, LUFS == dBFS pico, porque el K-weighting aporta ahí
// exactamente los +0.691 dB que la constante de BS.1770 resta).
void cycle (telescope::TelescopeProcessor& proc, double sr, int blockSize, int iteration)
{
    proc.prepareToPlay (sr, blockSize);

    // El contador de descartes NO se hereda entre prepares: cada prepare es una medición nueva.
    REQUIRE (proc.analysis().read().droppedSamples == 0u);

    const int samples = (int) std::llround (2.0 * sr);
    pushSine (proc, sr, blockSize, samples, std::pow (10.0f, kMinus23 / 20.0f));

    const bool measured = telescope::test::waitUntil (
        [&]
        {
            const auto& f = proc.analysis().read();
            return f.timeSeconds > 1.5 && std::abs (f.loudness.momentary - kMinus23) < 0.1f;
        }, 4000);

    const auto& f = proc.analysis().read();
    INFO ("iteración " << iteration << "  sr=" << sr << "  bloque=" << blockSize
          << "  t=" << f.timeSeconds << "  M=" << f.loudness.momentary
          << "  dropped=" << f.droppedSamples);
    REQUIRE (measured);
    REQUIRE_THAT (f.loudness.momentary, Catch::Matchers::WithinAbs (kMinus23, 0.1f));
    REQUIRE (f.droppedSamples == 0u);
}
}

TEST_CASE ("telescope: re-preparar el mismo processor 20 veces sigue midiendo bien", "[telescope][lifecycle]")
{
    telescope::TelescopeProcessor proc;

    constexpr int kCycles = 20;
    for (int it = 0; it < kCycles; ++it)
    {
        cycle (proc, 44100.0,  512, it);
        cycle (proc, 48000.0,   64, it);
        cycle (proc, 96000.0, 4096, it);

        // releaseResources para el worker; el siguiente prepare lo tiene que volver a levantar.
        proc.releaseResources();
        cycle (proc, 48000.0,  512, it);
    }

    std::printf ("LIFECYCLE ciclos=%d  (44.1k/512 · 48k/64 · 96k/4096 · release · 48k/512)\n", kCycles);
    proc.releaseResources();
}

// stop() tiene que ser idempotente y no colgarse: se lo llama desde prepare(), releaseResources() y el
// destructor, y el chasis puede encadenarlos.
TEST_CASE ("telescope: releaseResources repetido no cuelga ni rompe", "[telescope][lifecycle]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    pushSine (proc, 48000.0, 512, 24000, 0.25f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 5; ++i) proc.releaseResources();
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    std::printf ("LIFECYCLE_STOP_X5_MS=%.1f\n", ms);
    REQUIRE (ms < 1000.0);

    // Y después de todo eso todavía se puede volver a arrancar.
    cycle (proc, 48000.0, 512, -1);
    proc.releaseResources();
}

// M-1 en su forma directa: stop() tiene que CONFIRMAR que el worker salió. Antes del arreglo el retorno de
// stopThread() se descartaba, así que `prepare()` no tenía forma de saber si podía tocar los buffers del
// worker — y este test ni siquiera compilaba (stop() era void).
TEST_CASE ("telescope: AnalysisThread::stop() confirma que el worker salió", "[telescope][lifecycle]")
{
    telescope::AnalysisBus                            bus;
    telescope::TripleBuffer<telescope::AnalysisFrame> frames;
    telescope::LoudnessHistory                        history;
    telescope::AnalysisThread                         th (bus, frames, history);

    th.prepare (48000.0);
    REQUIRE (th.isThreadRunning());

    REQUIRE (th.stop());                 // salió dentro del plazo de gracia
    REQUIRE_FALSE (th.isThreadRunning());
    REQUIRE (th.stop());                 // idempotente: parar lo ya parado también "salió"
}
