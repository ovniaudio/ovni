// [telescope][chain] — el motor de análisis: audio thread → AnalysisBus (lock-free, estéreo) →
// AnalysisThread → TripleBuffer<AnalysisFrame> + LoudnessHistory. Verifica el CAMINO COMPLETO desde
// processBlock hasta el frame que lee la UI, más el ciclo de vida (pause / reset / releaseResources) y
// el contador de descartes que la UI muestra como "análisis atrasado".
//
// Los vectores de conformidad del medidor viven en EbuVectorsTest.cpp (miden el módulo suelto, sin
// threads); acá abajo se verifica el CABLEADO de ese módulo dentro de la cadena.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/AnalysisBus.h"
#include "analysis/AnalysisFrame.h"

namespace
{
constexpr double kSr = 48000.0;
constexpr int    kBlock = 512;

// Empuja `seconds` de un seno de 997 Hz a `peak` (lineal) por processBlock, como haría el DAW.
void pushSine (telescope::TelescopeProcessor& proc, double seconds, float peak, double sr = kSr)
{
    const int totalBlocks = (int) std::ceil (seconds * sr / kBlock);
    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < kBlock; ++i, ++n)
        {
            const float v = peak * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 997.0 * (double) n / sr);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
    }
}

// Espera hasta `timeoutMs` a que el AnalysisThread publique un frame que cumpla `pred` (TestHelpers.h:
// espera por CONDICIÓN, no por reloj — ver la nota de L-3 ahí).
template <typename Pred>
bool waitForFrame (telescope::TelescopeProcessor& proc, int timeoutMs, Pred pred)
{
    return telescope::test::waitUntil ([&] { return pred (proc.analysis().read()); }, timeoutMs);
}
}

TEST_CASE ("telescope: el bus estéreo descarta cuando se llena y cuenta lo descartado", "[telescope][chain]")
{
    telescope::AnalysisBus bus;
    // ≥ 2 s a 192 kHz por canal: es el mínimo que fija el diseño (§4 del spec).
    REQUIRE (bus.capacity() >= 2 * 192000);

    std::vector<float> L (4096, 0.5f), R (4096, -0.5f);

    // 10 s a 48 k sin drenar: entra lo que entra, el resto se cuenta.
    const int blocks = (int) (10.0 * kSr) / 4096;
    for (int b = 0; b < blocks; ++b)
        bus.push (L.data(), R.data(), 4096);

    const juce::uint32 dropped = bus.droppedSamples();
    std::printf ("CHAIN_DROPPED=%u (10 s empujados sin drenar, capacidad %d)\n", dropped, bus.capacity());
    REQUIRE (dropped > 0);
    REQUIRE (bus.numReady() > 0);

    bus.reset();
    REQUIRE (bus.droppedSamples() == 0u);
    REQUIRE (bus.numReady() == 0);
}

TEST_CASE ("telescope: processBlock alimenta el motor y llega un frame con el nivel real", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);

    const float peak = std::pow (10.0f, -23.0f / 20.0f);   // -23 dBFS pico
    pushSine (proc, 1.0, peak);

    // Esperar a que el motor haya digerido el SEGUNDO empujado, no al primer frame: abajo se afirma que
    // la historia acumuló más de 5 puntos (= 6 hops), y esperar 1 hop para afirmar 6 es una carrera.
    // (Con el sleep de 5 ms del sondeo viejo el worker se adelantaba y tapaba el agujero; con la espera
    // por condición de TestHelpers.h queda a la vista.) 0.95 y no 1.0: `analysedSeconds` suma 0.1 por hop
    // y 0.1 no es exacto en binario.
    const bool got = waitForFrame (proc, 2000, [] (const telescope::AnalysisFrame& f)
                                   { return f.timeSeconds >= 0.95; });
    REQUIRE (got);

    const auto& f = proc.analysis().read();
    std::printf ("CHAIN_PEAK L=%.5f R=%.5f (esperado %.5f)\n", f.peakL, f.peakR, peak);
    REQUIRE_THAT (f.peakL, Catch::Matchers::WithinAbs (peak, 0.002f));
    REQUIRE_THAT (f.peakR, Catch::Matchers::WithinAbs (peak, 0.002f));
    // RMS de un seno = pico / raíz(2).
    REQUIRE_THAT (f.rmsL, Catch::Matchers::WithinAbs (peak / std::sqrt (2.0f), 0.002f));

    // La historia acumuló puntos a 10 Hz (un hop = 100 ms).
    REQUIRE (proc.loudnessHistory().size() > 5);

    proc.releaseResources();
}

TEST_CASE ("telescope: pause congela el análisis y reset lo vuelve a cero", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);

    const float peak = std::pow (10.0f, -23.0f / 20.0f);
    pushSine (proc, 1.0, peak);
    REQUIRE (waitForFrame (proc, 1000, [] (const telescope::AnalysisFrame& f) { return f.timeSeconds > 0.0; }));

    proc.setAnalysisPaused (true);
    // El worker puede estar terminando el trozo que YA había sacado del bus, y ese resto es legítimo:
    // se espera a que el reloj del análisis DEJE DE MOVERSE, no a un tiempo fijo.
    REQUIRE (telescope::test::waitStable ([&] { return proc.analysis().read().timeSeconds; }, 60, 2000));
    const double frozen = proc.analysis().read().timeSeconds;

    pushSine (proc, 1.0, peak);                     // más audio: en pausa NO tiene que avanzar
    // Aserción NEGATIVA: el timeout es el margen que le damos al bug para aparecer, así que alargarlo
    // hace el test MÁS estricto (nunca más frágil).
    REQUIRE_FALSE (waitForFrame (proc, 300, [frozen] (const telescope::AnalysisFrame& f)
                                 { return f.timeSeconds != frozen; }));

    proc.setAnalysisPaused (false);
    pushSine (proc, 1.0, peak);
    REQUIRE (waitForFrame (proc, 1000, [frozen] (const telescope::AnalysisFrame& f)
                           { return f.timeSeconds > frozen; }));

    proc.resetAnalysis();
    REQUIRE (waitForFrame (proc, 1000, [] (const telescope::AnalysisFrame& f)
                           { return f.timeSeconds == 0.0 && ! f.loudness.integratedValid; }));
    REQUIRE (proc.loudnessHistory().size() == 0);

    proc.releaseResources();
}

TEST_CASE ("telescope: releaseResources detiene el thread sin bloquear", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);
    pushSine (proc, 0.5, 0.5f);

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    proc.releaseResources();
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;

    std::printf ("CHAIN_STOP_MS=%.1f\n", ms);
    REQUIRE (ms < 200.0);
}

// ---------------------------------------------------------------------------------------------
// LOUDNESS sobre la cadena COMPLETA. Los vectores de conformidad viven en EbuVectorsTest.cpp (miden el
// módulo suelto, determinista); esto verifica el CABLEADO: que lo que entra por processBlock llegue
// medido al frame que lee la lente.
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: un seno de -23 dBFS llega al frame como -23 LUFS", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness) != 0u);

    const float peak = std::pow (10.0f, -23.0f / 20.0f);
    pushSine (proc, 5.0, peak);   // 5 s: alcanza para short-term (3 s) e integrado

    // El integrado ya es válido a los 400 ms, pero el short-term recién existe a los 3 s: hay que esperar
    // a que el motor haya digerido los 5 s, no al primer frame con integrado.
    const bool got = waitForFrame (proc, 1000, [] (const telescope::AnalysisFrame& f)
                                   { return f.timeSeconds >= 4.85 && f.loudness.integratedValid; });
    REQUIRE (got);

    const auto& f = proc.analysis().read();
    std::printf ("CHAIN_LUFS M=%.3f S=%.3f I=%.3f  TP=%.3f dBTP\n",
                 f.loudness.momentary, f.loudness.shortTerm, f.loudness.integrated, f.loudness.truePeakMax);
    REQUIRE_THAT (f.loudness.momentary,  Catch::Matchers::WithinAbs (-23.0f, 0.1f));
    REQUIRE_THAT (f.loudness.shortTerm,  Catch::Matchers::WithinAbs (-23.0f, 0.1f));
    REQUIRE_THAT (f.loudness.integrated, Catch::Matchers::WithinAbs (-23.0f, 0.1f));
    REQUIRE_THAT (f.loudness.truePeakMax, Catch::Matchers::WithinAbs (-23.0f, 0.2f));

    // PAUSE congela el integrado; RESET lo invalida.
    proc.setAnalysisPaused (true);
    REQUIRE (telescope::test::waitStable ([&] { return proc.analysis().read().timeSeconds; }, 60, 2000));
    const float frozen = proc.analysis().read().loudness.integrated;
    pushSine (proc, 2.0, 0.5f);            // audio MUCHO más fuerte: en pausa no puede moverlo
    REQUIRE_FALSE (waitForFrame (proc, 300, [frozen] (const telescope::AnalysisFrame& f)
                                 { return f.loudness.integrated != frozen; }));

    proc.setAnalysisPaused (false);
    proc.resetAnalysis();
    REQUIRE (waitForFrame (proc, 1000, [] (const telescope::AnalysisFrame& f)
                           { return ! f.loudness.integratedValid; }));

    proc.releaseResources();
}

// Lente a demanda: con el módulo apagado, la sección de loudness del frame NO se calcula.
TEST_CASE ("telescope: con kLoudness fuera de la máscara el medidor no corre", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);
    proc.setEnabledModules (0u);

    pushSine (proc, 1.0, std::pow (10.0f, -23.0f / 20.0f));
    REQUIRE (waitForFrame (proc, 1000, [] (const telescope::AnalysisFrame& f) { return f.timeSeconds > 0.0; }));

    const auto& f = proc.analysis().read();
    REQUIRE (f.peakL > 0.0f);                       // el motor SÍ corrió
    REQUIRE_FALSE (f.loudness.integratedValid);     // pero el medidor no
    REQUIRE (f.loudness.momentary == telescope::kSilenceDb);

    proc.releaseResources();
}

// ---------------------------------------------------------------------------------------------
// CADENCIA DEL ESPECTRO (prompt 50). El motor publica un AnalysisFrame por hop de 100 ms — 10 Hz, que
// está perfecto para un medidor de loudness y es una muerte para un analizador de espectro (SPAN refresca
// a 30-60). El módulo Spectrum come del chunk drenado, no del hop, y emite a su propia tasa.
//
// Este test mide la tasa REAL contra el reloj de pared, empujando audio a tiempo real: si el espectro
// volviera a colgarse de los hops, llegarían 10 frames por segundo y el test se pondría rojo.
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: con SPECTRUM arriba llegan decenas de frames por segundo", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);

    juce::AudioBuffer<float> buf (2, kBlock);
    juce::MidiBuffer midi;
    telescope::test::Pink pink { telescope::test::kPinkSeedA };

    juce::uint32 last = 0xffffffffu;
    int    distinct = 0;
    long long pushed = 0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    for (;;)
    {
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        if (elapsed >= 1.0) break;

        // Audio a TIEMPO REAL: se empuja sólo lo que el reloj ya "consumió". Empujarlo todo de una
        // mediría la velocidad de la máquina, no la cadencia del motor.
        while ((double) pushed / kSr < elapsed)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float x = 0.25f * pink.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            pushed += kBlock;
        }

        const auto idx = proc.spectrum().read().frameIndex;
        if (idx != last) { last = idx; ++distinct; }
        juce::Thread::sleep (1);
    }

    const auto& f = proc.spectrum().read();
    std::printf ("CHAIN_SPECTRUM frames distintos en 1 s = %d   tasa de emision del motor = %.2f Hz   "
                 "fftSize=%d bins=%d\n", distinct, proc.spectrumEmitRate(), f.fftSize, f.numBins);

    REQUIRE (distinct >= 20);
    REQUIRE (f.fftSize == 4096);          // el default
    REQUIRE (proc.spectrumEmitRate() > 20.0);
    REQUIRE (proc.spectrumEmitRate() <= telescope::Spectrum::kMaxEmitHz + 1.0e-9);

    proc.releaseResources();
}

// Y con la lente que no lo pide, el espectro NO corre (lente a demanda: es el módulo caro del prompt).
TEST_CASE ("telescope: con kSpectrum fuera de la máscara el espectro no corre", "[telescope][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, kBlock);
    proc.setEnabledModules (telescope::kLoudness);

    pushSine (proc, 2.0, std::pow (10.0f, -20.0f / 20.0f));
    REQUIRE (waitForFrame (proc, 2000, [] (const telescope::AnalysisFrame& f) { return f.timeSeconds >= 1.95; }));

    const auto& f = proc.spectrum().read();
    REQUIRE (f.frameIndex == 0u);
    REQUIRE (f.magDb[0][85] == telescope::SpectrumFrame::kFloorDb);

    proc.releaseResources();
}
