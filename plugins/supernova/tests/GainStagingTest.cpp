// [gain][supernova] — puerta anti-clip adaptada a pass-through. gain-staging-check.sh corre este test y grepea
// la línea PEAK=<lineal>. Como SUPERNOVA es bit-exacto (RNF1, tap read-only), out==in → PEAK == pico de la
// entrada (≈1.0 con un seno full-scale). El techo es la UNIDAD (validate.env fija CEILING=1.0): un pass-through
// no puede exceder 0 dBFS porque no agrega nada.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("supernova: full-scale pass-through no agrega ganancia", "[gain][supernova]")
{
    supernova::SupernovaProcessor proc;
    const double SR = 48000.0;
    const int N = 512;
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
                d[n] = std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                 * (float) (blk * N + n) / (float) SR);
        }
        proc.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, N));
    }

    std::printf ("PEAK=%.6f\n", peak);   // ← lo grepea gain-staging-check.sh
    REQUIRE (peak <= 1.0f);              // unidad: pass-through no puede exceder 0 dBFS
}
