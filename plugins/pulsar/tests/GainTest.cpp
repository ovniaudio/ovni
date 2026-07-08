// Test [gain][pulsar] — puerta anti-clip del sello sobre el PulsarProcessor REAL (no el _probe).
// tools/gain-staging-check.sh (S4) grepea la línea `PEAK=<lineal>` de stdout. Pasa una señal
// full-scale por el plugin entero (processor + motor BINAURAL HRIR + SMEAR + limiter del engine)
// en el PEOR CASO de clip (MOTION/SMEAR/WIDTH al máximo: binaural denso + cola larga) y verifica
// que el pico de salida quede ≤ 1.0 (0 dBFS). El gate del orquestador usa techo 0.85.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("gain staging PULSAR: full-scale, macros al máximo -> no clip", "[gain][pulsar]")
{
    pulsar::PulsarProcessor proc;
    const double SR = 48000.0; const int N = 512;

    // peor caso de clip: MOTION/SMEAR/WIDTH al máximo (binaural denso + cola). apvts es público
    // (el editor lo usa como p.apvts); 1.0 normalizado = 100% en estos params (rango 0..100).
    for (const char* id : { "motion", "smear", "width" })
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
