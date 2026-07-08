// Test [gain][nebula] — puerta anti-clip del sello sobre el NebulaProcessor REAL (no el _probe).
// tools/gain-staging-check.sh (S4) grepea la línea `PEAK=<lineal>` de stdout. Pasa una señal
// full-scale por el plugin entero (processor + motor REVERB FDN + limiter del engine) en el PEOR CASO
// de clip (Size/Decay/Mix al máximo: cola larga + densa + wet pleno) y verifica que el pico de salida
// quede ≤ 1.0 (0 dBFS). El gate del orquestador usa techo 0.85.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("gain staging NEBULA: full-scale, macros al máximo -> no clip", "[gain][nebula]")
{
    nebula::NebulaProcessor proc;
    const double SR = 48000.0; const int N = 512;

    // peor caso de clip: SIZE/DECAY/MIX al máximo (cola larga, densa, wet pleno). apvts es público
    // (el editor lo usa como p.apvts); 1.0 normalizado = 100% en estos params (rango 0..100). DECAY=100%
    // entra en freeze (cola congelada que sostiene): pone a prueba el anti-clip del motor en sostenido.
    for (const char* id : { "size", "decay", "mix" })
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (SR, N);

    float peak = 0.0f;
    for (int blk = 0; blk < 400; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * N + n) / (float) SR);
        }
        proc.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, N));
    }

    std::printf ("PEAK=%.6f\n", peak);   // <- la línea que grepea gain-staging-check.sh
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak <= 1.0f);
}

// El motor reporta latencia al host (PDC) = el lookahead del limiter de salida (~3 ms). Verificamos que
// (a) sea ≈ round(3·sr/1000) a varios sample-rates, y (b) el CHASIS no la pise (getLatencySamples() del
// processor tras prepareToPlay devuelve la del motor). Joaquín aceptó esta latencia a cambio de cero clip.
TEST_CASE ("NEBULA latencia: el lookahead de salida se reporta al host (PDC)", "[gain][nebula]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        nebula::NebulaProcessor proc;
        proc.prepareToPlay (sr, 512);
        const int expected = (int) std::lround (3.0 * sr / 1000.0);   // kLookaheadMs = 3.0
        const int reported = proc.getLatencySamples();
        std::printf ("LATENCY[%.1fk] reported=%d samples (%.3f ms)  expected=%d\n",
                     sr / 1000.0, reported, reported * 1000.0 / sr, expected);
        REQUIRE (reported == expected);   // el chasis (prepareToPlay→prepareEngine) NO pisa la latencia
        REQUIRE (reported > 0);
    }
}
