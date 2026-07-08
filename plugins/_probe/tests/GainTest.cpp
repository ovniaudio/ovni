// Test [gain] — puerta anti-clip del sello sobre el _probe ENSAMBLADO. tools/gain-staging-check.sh (S4)
// grepea la línea `PEAK=<lineal>` de stdout. Pasa una señal full-scale por el plugin entero (processor +
// MovementEngine + limiter estéreo) y verifica que el pico de salida quede ≤ 1.0 (0 dBFS).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("gain staging: full-scale -> no clip", "[gain]")
{
    PluginProcessor proc;
    const double SR = 48000.0; const int N = 512;
    proc.prepareToPlay (SR, N);

    float peak = 0.0f;
    for (int blk = 0; blk < 200; ++blk)
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
