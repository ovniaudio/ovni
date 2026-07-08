#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [gain][aurora] — gate anti-clip del sello: el AuroraProcessor REAL en el PEOR
// caso (todo al máximo) no pasa el techo true-peak (PEAK ≤ 0.85; lo grepea
// tools/gain-staging-check.sh). Peor caso de AURORA: seno full-scale + SPREAD
// 100 (bins hard-panned ganan +3 dB = √2 en un canal por el reparto de potencia)
// + TILT +100 (abre antes en frecuencia) + MOTION 100 al rate máximo (la AM del
// abanico mueve el pico) + MIX 100 (wet pleno) + Mono Safe 0 y DUCK 0 (ninguna
// red que recoja el ángulo). El limiter estéreo-linked del motor (techo 0.85,
// attack instantáneo) debe contenerlo SIN pasarse.
//
// BARRIDO de frecuencia (review 2026-06-10): a 220 Hz el weave está capado por
// kMaxWeavePhasePerHz → el ángulo es modesto y la ganancia por canal queda lejos
// del √2 teórico. El estrés MÁXIMO del reparto vive en medios/agudos, cerca de
// los ANTINODOS del weave (|sin ψ| = 1): con kWeaveCycles = 2.75 sobre 40 Hz→16 k
// log caen en ~1.8 kHz y ~5.4 kHz. Se barren las tres zonas (grave capado /
// antinodo medio / antinodo agudo) y el PEAK= reportado es el PEOR de todas.
// =============================================================================
TEST_CASE ("gain staging AURORA: full-scale, macros al maximo -> no clip", "[gain][aurora]")
{
    const double SR = 48000.0; const int N = 512;
    namespace pid = aurora::params::id;

    float worst = 0.0f;
    for (const float freq : { 220.0f, 1800.0f, 5400.0f })
    {
        aurora::AuroraProcessor proc;
        auto set = [&] (const char* id, float v01) {
            if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
        };
        set (pid::SPREAD,      1.0f);   // despliegue máximo (+3 dB por canal en bins hard-panned)
        set (pid::TILT,        1.0f);   // +100: abre antes en frecuencia + drive a los bordes
        set (pid::MOTION,      1.0f);   // abanico abriendo/cerrando a full
        set (pid::MOTIONRATE,  1.0f);   // rate FREE máximo (8 Hz, bajo la cota anti-AM)
        set (pid::MONOSAFEAMT, 0.0f);   // sin red mono (ángulos plenos hasta abajo)
        set (pid::DUCK,        0.0f);   // sin ducking (el γ no se recoge nunca)
        set (pid::MIX,         1.0f);   // wet pleno

        proc.prepareToPlay (SR, N);

        float peak = 0.0f;
        for (int blk = 0; blk < 400; ++blk)   // ~4.3 s (cubre varios ciclos del MOTION a 8 Hz)
        {
            juce::AudioBuffer<float> buf (2, N);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int n = 0; n < N; ++n)
                    d[n] = std::sin (juce::MathConstants<float>::twoPi * freq * (float) (blk * N + n) / (float) SR);
            }
            proc.processBlock (buf, midi);
            peak = juce::jmax (peak, buf.getMagnitude (0, N), buf.getMagnitude (1, N));
        }

        std::printf ("GAIN[aurora %6.0f Hz] peak=%.6f\n", freq, peak);
        worst = juce::jmax (worst, peak);
    }

    std::printf ("PEAK=%.6f\n", worst);   // <- la línea que grepea gain-staging-check.sh (peor caso del barrido)
    REQUIRE (std::isfinite (worst));
    REQUIRE (worst <= 0.851f);   // techo del sello (0.85) + epsilon de float
}
