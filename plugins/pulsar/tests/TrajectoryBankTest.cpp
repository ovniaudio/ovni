#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "dsp/TrajectoryBank.h"
#include "dsp/Smear.h"

using pulsar::dsp::TrajectoryBank;

TEST_CASE ("TrajectoryBank: salida finita y acotada en todo el morph", "[pulsar]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        TrajectoryBank tb; tb.prepare (sr);
        for (float shape : { 0.0f, 0.33f, 0.66f, 1.0f })
        {
            tb.setShape (shape);
            for (int b = 0; b < 200; ++b)
            {
                tb.advanceBlock (256, /*rateHz*/ 2.0f, /*motion01*/ 0.7f);
                REQUIRE (std::isfinite (tb.x()));
                REQUIRE (std::isfinite (tb.y()));
                REQUIRE (std::abs (tb.x()) <= 1.0001f);
                REQUIRE (std::abs (tb.y()) <= 1.0001f);
            }
        }
    }
}

static int zeroCross (TrajectoryBank& tb, float motion, int blocks)
{
    tb.reset(); tb.setShape (0.66f);  // Lorenz
    float prev = 0.0f; int zc = 0;
    for (int b = 0; b < blocks; ++b) { tb.advanceBlock (64, 1.0f, motion);
        const float x = tb.x(); if ((x >= 0.f) != (prev >= 0.f)) ++zc; prev = x; }
    return zc;
}

TEST_CASE ("TrajectoryBank: MOTION aumenta la actividad del movimiento", "[pulsar]")
{
    TrajectoryBank tb; tb.prepare (48000.0);
    const int lo = zeroCross (tb, 0.1f, 600);
    const int hi = zeroCross (tb, 0.95f, 600);
    REQUIRE (hi > lo);
}

TEST_CASE ("TrajectoryBank: centro del campo sesga la salida sin romper el clamp", "[pulsar]")
{
    TrajectoryBank tb; tb.prepare (48000.0);
    tb.setShape (0.0f);            // Orbit (determinista)
    tb.setCenter (0.8f, -0.6f);

    float sumX = 0.0f, sumY = 0.0f; const int N = 400;
    for (int b = 0; b < N; ++b)
    {
        tb.advanceBlock (64, 1.0f, 0.5f);
        REQUIRE (std::abs (tb.x()) <= 1.0001f);
        REQUIRE (std::abs (tb.y()) <= 1.0001f);
        sumX += tb.x(); sumY += tb.y();
    }
    // El centro corre la media de la órbita hacia (cx, cy): X hacia +, Y hacia -.
    REQUIRE ((sumX / (float) N) > 0.2f);
    REQUIRE ((sumY / (float) N) < -0.1f);
}

// ============================ SMEAR: estabilidad / anti-clip ============================
// El caso que más preocupa al oído de Joaquín: SMEAR a tope NO debe clippear ni autooscilar.

TEST_CASE ("Smear: acotado con full-scale a SMEAR=1.0 (no explota)", "[pulsar]")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        pulsar::dsp::Smear s; s.prepare (sr, 256);
        juce::AudioBuffer<float> buf (2, 256);
        juce::Random rng (1);
        float peak = 0.0f;
        const int blocks = (int) (sr * 3.0 / 256.0);   // ~3 s
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* p = buf.getWritePointer (ch);
                for (int i = 0; i < 256; ++i) p[i] = rng.nextFloat() * 2.0f - 1.0f;  // full-scale
            }
            s.process (buf, 1.0f, std::sin ((float) b * 0.05f));   // SMEAR máx, azimut barriendo
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* p = buf.getReadPointer (ch);
                for (int i = 0; i < 256; ++i) peak = std::max (peak, std::abs (p[i]));
            }
        }
        REQUIRE (std::isfinite (peak));
        // El limiter interno de Smear (techo 0.95, estéreo-linked) acota el peor caso broadband:
        // sin esto el lazo cruzado densifica hasta ~+10 dB y clippearía post-motor.
        REQUIRE (peak <= 0.96f);
    }
}

TEST_CASE ("Smear: la cola DECAE con entrada en silencio (no autooscila)", "[pulsar]")
{
    pulsar::dsp::Smear s; s.prepare (48000.0, 256);
    juce::AudioBuffer<float> buf (2, 256);
    juce::Random rng (7);

    // Cargar el lazo con 1 s de ruido full-scale + SMEAR máx.
    for (int b = 0; b < (int) (48000.0 / 256.0); ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = buf.getWritePointer (ch);
            for (int i = 0; i < 256; ++i) p[i] = rng.nextFloat() * 2.0f - 1.0f;
        }
        s.process (buf, 1.0f, 0.3f);
    }
    // Cortar la entrada: medir el pico al inicio y tras ~5 s de silencio.
    auto silentBlockPeak = [&]
    {
        buf.clear();
        s.process (buf, 1.0f, 0.0f);
        float pk = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* p = buf.getReadPointer (ch);
            for (int i = 0; i < 256; ++i) pk = std::max (pk, std::abs (p[i]));
        }
        return pk;
    };
    const float tailStart = silentBlockPeak();
    float tailEnd = tailStart;
    for (int b = 0; b < (int) (48000.0 * 5.0 / 256.0); ++b) tailEnd = silentBlockPeak();

    // Con feedback ≤0.88 el lazo es contractivo -> la cola decae (no se sostiene ni crece).
    REQUIRE (std::isfinite (tailEnd));
    REQUIRE ((tailEnd < tailStart * 0.5f || tailEnd < 1.0e-4f));
}
