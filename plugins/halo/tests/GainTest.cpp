// Test [gain][halo] — puerta anti-clip del sello sobre el HaloProcessor REAL (no el motor aislado, no el
// _probe). tools/gain-staging-check.sh (S4) grepea la línea `PEAK=<lineal>` de stdout. Pasa una señal
// full-scale por el plugin entero (processor + motor SHIMMER espacial: pre-delay + BLOOM + FDN + pitch OS
// en el lazo + órbita binaural + limiter) en el PEOR CASO de clip (todas las macros + ORBIT al máximo) y
// verifica que el pico de salida quede ≤ 1.0 (0 dBFS). Gate orquestador: 0.85.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("gain staging HALO: full-scale, macros + ORBIT al maximo -> no clip", "[gain][halo]")
{
    halo::HaloProcessor proc;
    const double SR = 48000.0; const int N = 512;

    namespace pid = halo::params::id;
    // Peor caso de clip: todas las macros al máximo (lazo cargado, cola larga, wet pleno, órbita plena).
    for (const char* id : { pid::MIX, pid::SIZE, pid::DECAY, pid::SHIMMER, pid::ORBIT })
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (1.0f);
    // TONE bajo (LP del lazo abierto = más agudos = más riesgo de pico) para el peor caso tonal.
    if (auto* t = proc.apvts.getParameter (pid::TONE)) t->setValueNotifyingHost (0.30f);

    proc.prepareToPlay (SR, N);

    float peak = 0.0f;
    for (int blk = 0; blk < 500; ++blk)   // ~5.3 s (el lazo de shimmer se carga del todo)
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
        peak = juce::jmax (peak, buf.getMagnitude (0, N), buf.getMagnitude (1, N));
    }

    std::printf ("PEAK=%.6f\n", peak);   // <- la línea que grepea gain-staging-check.sh
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak <= 1.0f);
}
