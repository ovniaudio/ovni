// Tests [s1] del motor de MOVIMIENTO + DSP compartido. Headless (sin GUI/MessageManager).
//   - Trajectory: finita / sin NaN / acotada a 44.1·48·96k, todas las formas, con caos + Doppler.
//   - constantPowerPan: energía constante (gL^2 + gR^2 ≈ 1).
//   - LinearRamp: rampa por-sample al objetivo sin salto.
//   - StereoLimiter: contiene el techo (peak ≤ ceiling, lastGain < 1).
//   - MovementEngine: no clippea (peak ≤ ceiling), finito, a varios SR.
//   - MovementEngine Doppler: el shift de pitch crece monótono con doppler01.
//   - SofaLoader: maneja archivo ausente + carga un SOFA real.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engines/movement/Trajectory.h"
#include "engines/movement/MovementEngine.h"
#include "dsp/StereoLimiter.h"
#include "dsp/Smoothing.h"
#include "dsp/SofaLoader.h"

#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Cuenta cruces por cero (cambios de signo) de un buffer mono.
int zeroCrossings (const std::vector<float>& x)
{
    int zc = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f))
            ++zc;
    return zc;
}

} // namespace

TEST_CASE ("Trajectory: finita, acotada, sin NaN a varios sample rates", "[s1]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (int shape : { ovni::engines::Circle, ovni::engines::Ellipse, ovni::engines::Spiral,
                           ovni::engines::Pendulum, ovni::engines::PendulumBack })
        {
            ovni::engines::Trajectory traj;
            traj.prepare (sr);

            ovni::engines::TrajectoryParams p;
            p.shape     = shape;
            p.rate      = ovni::engines::Free;
            p.freeHz    = 2.0f;
            p.chaos01   = 0.8f;     // caos alto (peor caso de erraticidad)
            p.doppler01 = 1.0f;     // fly-by al máximo
            p.spread01  = 0.7f;
            traj.setParams (p);

            ovni::engines::TransportInfo tr;
            tr.isPlaying = false;

            const int block = 128;
            for (int b = 0; b < 400; ++b)
            {
                auto t = traj.advance (block, tr);
                REQUIRE (std::isfinite (t.azimuth));
                REQUIRE (std::isfinite (t.elevation));
                REQUIRE (std::isfinite (t.distance));
                REQUIRE (t.distance >= 0.0f);
                REQUIRE (t.distance <= 1.0f);
                REQUIRE (t.azimuth >= 0.0f);
                REQUIRE (t.azimuth <= (float) kTwoPi + 1.0e-3f);
            }
        }
    }
}

TEST_CASE ("constantPowerPan: energía constante (gL^2 + gR^2 ≈ 1)", "[s1]")
{
    for (int i = 0; i <= 20; ++i)
    {
        const float pan = (float) i / 20.0f;
        const auto  g   = ovni::dsp::constantPowerPan (pan);
        REQUIRE (g.left  * g.left + g.right * g.right == Approx (1.0f).margin (1.0e-5));
    }
    // Centro y extremos.
    REQUIRE (ovni::dsp::constantPowerPan (0.5f).left  == Approx (0.70710678f).margin (1.0e-5));
    REQUIRE (ovni::dsp::constantPowerPan (0.5f).right == Approx (0.70710678f).margin (1.0e-5));
    REQUIRE (ovni::dsp::constantPowerPan (0.0f).left  == Approx (1.0f).margin (1.0e-5));   // todo a la izquierda
    REQUIRE (ovni::dsp::constantPowerPan (1.0f).right == Approx (1.0f).margin (1.0e-5));   // todo a la derecha
}

TEST_CASE ("LinearRamp: rampa por-sample al objetivo sin salto", "[s1]")
{
    ovni::dsp::LinearRamp ramp (1.0f);
    std::vector<float> buf (64, 1.0f);
    ramp.applyGain (buf.data(), (int) buf.size(), 2.0f);

    REQUIRE (ramp.value() == Approx (2.0f));        // terminó en el objetivo
    REQUIRE (buf.front()  == Approx (1.0f));        // arrancó en el valor previo
    REQUIRE (buf.back()   < 2.0f);                   // último sample = target - step (aún sin saltar)
    REQUIRE (buf.back()   > 1.9f);
    for (size_t i = 1; i < buf.size(); ++i)
        REQUIRE (buf[i] >= buf[i - 1]);              // monótona, sin escalón
}

TEST_CASE ("StereoLimiter: contiene el techo", "[s1]")
{
    ovni::dsp::StereoLimiter lim;
    ovni::dsp::LimiterTuning tuning; // ceiling 0.85
    lim.setTuning (tuning);
    lim.prepare (48000.0);

    const int n = 2048;
    std::vector<float> L (n), R (n);
    for (int i = 0; i < n; ++i)
    {
        L[i] = 2.0f * std::sin (kTwoPi * 220.0 * i / 48000.0);   // muy por encima del techo
        R[i] = 2.0f * std::sin (kTwoPi * 220.0 * i / 48000.0 + 0.5);
    }
    lim.process (L.data(), R.data(), n);

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (L[i]), std::abs (R[i])));

    REQUIRE (peak <= tuning.ceiling + 1.0e-4f);
    REQUIRE (lim.lastGain() < 1.0f);
}

TEST_CASE ("MovementEngine: no clippea, finito, a varios sample rates", "[s1]")
{
    const ovni::dsp::LimiterTuning tuning; // ceiling 0.85 (default del engine)

    for (auto cfg : { std::pair { 44100.0, 64 }, std::pair { 48000.0, 128 }, std::pair { 96000.0, 256 } })
    {
        const double sr    = cfg.first;
        const int    block = cfg.second;

        ovni::engines::MovementEngine eng;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
        eng.prepare (spec);

        juce::AudioBuffer<float> buf (2, block);
        double phase = 0.0;
        const double w = kTwoPi * 200.0 / sr;
        float peak = 0.0f;
        bool  finite = true;

        for (int b = 0; b < 400; ++b)
        {
            // entrada estéreo fuerte
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* in = buf.getWritePointer (ch);
                double ph = phase;
                for (int i = 0; i < block; ++i) { in[i] = 0.9f * (float) std::sin (ph); ph += w; }
            }
            phase += w * block;

            ovni::engines::MovementParams p;
            p.azimuthRad = (float) (kTwoPi * 3.0 * b / 400.0);          // 3 vueltas
            p.distance01 = 0.5f + 0.5f * (float) std::sin (0.05 * b);   // distancia oscilante
            p.doppler01  = 1.0f;                                         // Doppler al máximo
            p.width01    = 1.0f;
            eng.process (buf, p);

            for (int ch = 0; ch < 2; ++ch)
            {
                const auto* o = buf.getReadPointer (ch);
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (o[i])) finite = false;
                    peak = std::max (peak, std::abs (o[i]));
                }
            }
        }

        REQUIRE (finite);
        REQUIRE (peak <= tuning.ceiling + 1.0e-3f);   // el limiter garantiza sample-peak ≤ techo
    }
}

TEST_CASE ("MovementEngine: Doppler produce shift monótono con el parámetro", "[s1]")
{
    const double sr    = 48000.0;
    const int    block = 128;
    const int    warmBlocks   = 80;    // deja asentar el delay centrado
    const int    sweepBlocks  = 200;   // acercamiento: distancia 1 -> 0

    auto measureApproachZC = [&] (float doppler01) -> int
    {
        ovni::engines::MovementEngine eng;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
        eng.prepare (spec);

        juce::AudioBuffer<float> buf (2, block);
        double phase = 0.0;
        const double w = kTwoPi * 440.0 / sr;

        auto fill = [&] ()
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* in = buf.getWritePointer (ch);
                double ph = phase;
                for (int i = 0; i < block; ++i) { in[i] = 0.5f * (float) std::sin (ph); ph += w; }
            }
            phase += w * block;
        };

        ovni::engines::MovementParams p;
        p.azimuthRad = 0.0f;        // frente (sin paneo): aísla el efecto de pitch
        p.doppler01  = doppler01;
        p.width01    = 1.0f;

        // Warm-up a distancia lejana fija.
        for (int b = 0; b < warmBlocks; ++b) { fill(); p.distance01 = 1.0f; eng.process (buf, p); }

        // Acercamiento: distancia 1 -> 0, capturando la salida (canal L).
        std::vector<float> out;
        out.reserve ((size_t) (sweepBlocks * block));
        for (int b = 0; b < sweepBlocks; ++b)
        {
            fill();
            p.distance01 = 1.0f - (float) b / (float) (sweepBlocks - 1);
            eng.process (buf, p);
            const auto* o = buf.getReadPointer (0);
            for (int i = 0; i < block; ++i) out.push_back (o[i]);
        }
        return zeroCrossings (out);
    };

    const int zc0  = measureApproachZC (0.0f);   // sin Doppler (línea en bypass)
    const int zc05 = measureApproachZC (0.5f);
    const int zc1  = measureApproachZC (1.0f);   // Doppler al máximo

    WARN ("Doppler ZC en el acercamiento: dop0=" << zc0 << " dop0.5=" << zc05 << " dop1=" << zc1);

    // Acercarse comprime los frentes de onda -> sube el pitch -> MÁS cruces por cero. Monótono en doppler01.
    REQUIRE (zc1  >  zc0 + 4);   // el efecto es real (no ruido de borde)
    REQUIRE (zc05 >  zc0);
    REQUIRE (zc1  >= zc05);
}

TEST_CASE ("SofaLoader: maneja archivo ausente y carga un SOFA real", "[s1]")
{
    ovni::dsp::SofaLoader loader;

    // Archivo inexistente -> false, sin crashear.
    REQUIRE_FALSE (loader.open (juce::File ("/no/existe/archivo.sofa"), 48000.0));
    REQUIRE_FALSE (loader.isLoaded());

#ifdef OVNI_TEST_SOFA
    const juce::File sofa (OVNI_TEST_SOFA);
    if (sofa.existsAsFile())
    {
        REQUIRE (loader.open (sofa, 48000.0));
        REQUIRE (loader.isLoaded());
        REQUIRE (loader.filterLength() > 0);

        std::vector<float> irL ((size_t) loader.filterLength());
        std::vector<float> irR ((size_t) loader.filterLength());
        float dL = 0.0f, dR = 0.0f;
        REQUIRE (loader.getFilter (1.0f, 0.0f, 0.0f, irL.data(), irR.data(), dL, dR)); // al frente
        REQUIRE (std::isfinite (dL));
        REQUIRE (std::isfinite (dR));
        bool irFinite = true;
        for (float v : irL) if (! std::isfinite (v)) irFinite = false;
        for (float v : irR) if (! std::isfinite (v)) irFinite = false;
        REQUIRE (irFinite);
    }
    else
    {
        WARN ("SOFA de prueba ausente (" << OVNI_TEST_SOFA << ") — salteando la carga real.");
    }
#else
    WARN ("OVNI_TEST_SOFA no definido — salteando la carga real de SOFA.");
#endif
}
