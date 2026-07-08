#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [gain][horizon] — gate anti-clip del sello: el HorizonProcessor REAL en el PEOR
// caso (freeze ENCENDIDO, todo al máximo) no pasa el techo true-peak (PEAK ≤ 0.85;
// lo grepea tools/gain-staging-check.sh). MEDIDO CON EL MECANISMO ENCENDIDO (la
// regla de oro del sello: freeze apagado = falso verde).
//
// Peor caso de HORIZON: seno full-scale + FREEZE on (resíntesis del frame a plena
// magnitud) + WHISPER 0 (fase coherente: la energía se concentra en el bin, no se
// difunde — peor para el pico) + SPREAD 100 (offset de fase L/R máximo) + RATE off
// (frame SOSTENIDO: sin valles del gate que bajen el pico) + MIX 100 (wet pleno) +
// DUCK 0 (nada recoge el wet). El limiter estéreo-linked del motor (techo 0.85)
// debe contenerlo SIN pasarse. Barrido multi-frecuencia (grave/medio/agudo) como
// aurora: el PEAK= reportado es el PEOR de todas.
// =============================================================================
TEST_CASE ("gain staging HORIZON: freeze ON, full-scale, macros al maximo -> no clip", "[gain][horizon]")
{
    const double SR = 48000.0; const int N = 512;
    namespace pid = horizon::params::id;

    float worst = 0.0f;
    for (const float freq : { 110.0f, 1000.0f, 6000.0f })
    {
        horizon::HorizonProcessor proc;
        auto set = [&] (const char* id, float v01) {
            if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
        };
        set (pid::WHISPER, 0.0f);   // fase coherente (energía concentrada = peor pico)
        set (pid::SPREAD,  1.0f);   // des-correlación máxima
        set (pid::DUCK,    0.0f);   // sin ducking (el wet no se recoge)
        set (pid::MIX,     1.0f);   // wet pleno
        // RATE queda en su default 0 (sostenido): sin valles del gate → energía máxima sostenida.

        proc.prepareToPlay (SR, N);

        // FREEZE se aprieta DESPUÉS de llenar el FIFO con la señal full-scale (peor caso REAL:
        // el frame congelado captura energía PLENA, no el FIFO con ceros del warmup — eso
        // subestimaba el pico y era falso verde). Joaquín lo aprieta durante la reproducción.
        constexpr int kFreezeAt = 80;   // FIFO (2048 @512) bien lleno antes de congelar

        float peak = 0.0f;
        for (int blk = 0; blk < 400; ++blk)   // ~4.3 s (el freeze captura y sostiene)
        {
            if (blk == kFreezeAt) set (pid::FREEZE, 1.0f);   // MECANISMO ENCENDIDO con FIFO lleno

            juce::AudioBuffer<float> buf (2, N);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int n = 0; n < N; ++n)
                    d[n] = std::sin (juce::MathConstants<float>::twoPi * freq * (float) (blk * N + n) / (float) SR);
            }
            proc.processBlock (buf, midi);
            if (blk > kFreezeAt + 8)   // medir tras el cross-fade del freeze (ya a plena magnitud)
                peak = juce::jmax (peak, buf.getMagnitude (0, N), buf.getMagnitude (1, N));
        }

        std::printf ("GAIN[horizon %6.0f Hz] peak=%.6f\n", freq, peak);
        worst = juce::jmax (worst, peak);
    }

    std::printf ("PEAK=%.6f\n", worst);   // <- la línea que grepea gain-staging-check.sh (peor caso del barrido)
    REQUIRE (std::isfinite (worst));
    REQUIRE (worst <= 0.851f);   // techo del sello (0.85) + epsilon de float
}

// =============================================================================
// [gain][horizon] — anti-clip del WET VIVO (FREEZE OFF). Tras el fix audible el motor
// espacializa el audio vivo SIEMPRE (SPREAD+WHISPER sobre el espectro vivo), así que
// el camino sin congelar también es un camino de audio REAL que debe respetar el techo.
// Peor caso: seno full-scale + FREEZE OFF + SPREAD 100 + WHISPER 0 (energía concentrada)
// + MIX 100 + DUCK 0. El limiter (0.85) debe contenerlo. (Antes este camino era identity
// y no se medía → el live widener es nuevo y necesita su propia guarda.)
// =============================================================================
TEST_CASE ("gain staging HORIZON: FREEZE OFF live widener full-scale -> no clip", "[gain][horizon]")
{
    const double SR = 48000.0; const int N = 512;
    namespace pid = horizon::params::id;

    float worst = 0.0f;
    for (const float freq : { 110.0f, 1000.0f, 6000.0f })
    {
        horizon::HorizonProcessor proc;
        auto set = [&] (const char* id, float v01) {
            if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
        };
        set (pid::WHISPER, 0.0f);   // fase coherente (energía concentrada = peor pico)
        set (pid::SPREAD,  1.0f);   // des-correlación máxima del vivo
        set (pid::DUCK,    0.0f);
        set (pid::MIX,     1.0f);   // wet vivo pleno
        // FREEZE queda OFF todo el tiempo: medimos el ESPACIALIZADOR vivo.

        proc.prepareToPlay (SR, N);

        float peak = 0.0f;
        for (int blk = 0; blk < 200; ++blk)   // ~2.1 s
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
            if (blk > 40)   // tras el warmup del STFT/PDC
                peak = juce::jmax (peak, buf.getMagnitude (0, N), buf.getMagnitude (1, N));
        }
        std::printf ("GAIN[horizon LIVE %6.0f Hz] peak=%.6f\n", freq, peak);
        worst = juce::jmax (worst, peak);
    }
    std::printf ("PEAK_LIVE=%.6f\n", worst);
    REQUIRE (std::isfinite (worst));
    REQUIRE (worst <= 0.851f);   // el limiter del sello contiene también el camino vivo
}
