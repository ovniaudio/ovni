// StabilityTest.cpp — [stability][dust]: el DustProcessor REAL bajo abuso.
//
// Dos fases (patrón del StabilityTest de HALO, adaptado al riesgo central de DUST: el feedback de
// DENSIDAD + las rampas de ORIGIN/RATE):
//   1. AUTOMATIZACIÓN BRUTAL: todos los parámetros saltan a valores aleatorios (determinísticos,
//      semilla fija) cada pocos bloques mientras suena ruido rosa — el peor drag de un usuario.
//   2. DENSIDAD 100 SOSTENIDA 60 s con señal: la nube "infinita" queda acotada (sin NaN, sin
//      divergencia) y al cortar la señal la cola DECAE (un lazo divergente no decae aunque el
//      limiter lo enmascare).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    constexpr double SR  = 48000.0;
    constexpr int    BLK = 512;

    namespace pid = dust::params::id;

    bool blockFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i])) return false;
        }
        return true;
    }

    double blockRms (const juce::AudioBuffer<float>& b)
    {
        double acc = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i) acc += (double) d[i] * (double) d[i];
        }
        return std::sqrt (acc / (2.0 * (double) b.getNumSamples()));
    }
}

TEST_CASE ("DUST stability: automatizacion brutal de TODOS los params -> sin NaN ni clip",
           "[stability][dust]")
{
    dust::DustProcessor proc;
    proc.prepareToPlay (SR, BLK);

    using ovni::test::setParam;
    ovni::test::White rnd (0xD057AB);   // saltos determinísticos (reproducible)
    ovni::test::Pink  pink;

    auto rnd01 = [&] { return 0.5f + 0.5f * rnd.next(); };

    juce::AudioBuffer<float> buf (2, BLK);
    bool  finite = true;
    float peak   = 0.0f;

    const int blocks = (int) std::lround (20.0 * SR / BLK);   // 20 s de abuso
    for (int blk = 0; blk < blocks && finite; ++blk)
    {
        // Cada 3 bloques (~32 ms) TODO salta a un valor nuevo: mix, rate (rango completo), densidad,
        // spread, vida, duck, origin (drag frenético) y el modo SYNC/división.
        if (blk % 3 == 0)
        {
            setParam (proc, pid::MIX,      rnd01());
            setParam (proc, pid::RATE,     rnd01());
            setParam (proc, pid::DENSITY,  rnd01());
            setParam (proc, pid::SPREAD,   rnd01());
            setParam (proc, pid::VIDA,     rnd01());
            setParam (proc, pid::DUCK,     rnd01());
            setParam (proc, pid::ORIGINX,  rnd01());
            setParam (proc, pid::ORIGINY,  rnd01());
            setParam (proc, pid::RATEDIV,  rnd01());
            setParam (proc, pid::RATESYNC, (blk % 6 == 0) ? 1.0f : 0.0f);
            setParam (proc, "monoSafe",    (blk % 9 == 0) ? 1.0f : 0.0f);
        }

        for (int i = 0; i < BLK; ++i)
        {
            const float x = 0.5f * pink.next();
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        finite = blockFinite (buf);
        peak   = juce::jmax (peak, buf.getMagnitude (0, BLK), buf.getMagnitude (1, BLK));
    }

    INFO ("peak=" << peak);
    REQUIRE (finite);
    // dry (≤0.5·1.0) + wet (≤0.85) a MIX intermedio puede superar 0.85, pero NUNCA el full-scale.
    REQUIRE (peak <= 1.0f);
    REQUIRE (peak > 0.05f);   // sonó de verdad
}

TEST_CASE ("DUST stability: DENSIDAD 100 sostenida 60 s -> acotada, sin NaN, y la cola decae",
           "[stability][dust]")
{
    dust::DustProcessor proc;
    using ovni::test::setParam;
    setParam (proc, pid::MIX, 1.0f);        // wet pleno: se mide el lazo, no el dry encima
    setParam (proc, pid::DENSITY, 1.0f);    // nube "infinita" (feedback en el piso de estabilidad)
    setParam (proc, pid::SPREAD, 0.8f);
    setParam (proc, pid::VIDA, 0.5f);
    if (auto* r = proc.apvts.getParameter (pid::RATE))
        r->setValueNotifyingHost (proc.apvts.getParameterRange (pid::RATE).convertTo0to1 (50.0f));
    proc.prepareToPlay (SR, BLK);

    ovni::test::Pink pink;
    juce::AudioBuffer<float> buf (2, BLK);
    bool   finite = true;
    float  peak   = 0.0f;
    double rmsEarly = 0.0, rmsLate = 0.0;
    int    nEarly = 0, nLate = 0;

    const int blocks = (int) std::lround (60.0 * SR / BLK);
    for (int blk = 0; blk < blocks && finite; ++blk)
    {
        for (int i = 0; i < BLK; ++i)
        {
            const float x = 0.5f * pink.next();
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        finite = blockFinite (buf);
        peak   = juce::jmax (peak, buf.getMagnitude (0, BLK), buf.getMagnitude (1, BLK));

        const double t = (double) blk * BLK / SR;
        const double r = blockRms (buf);
        if (t >= 5.0 && t < 10.0)  { rmsEarly += r; ++nEarly; }
        if (t >= 55.0 && t < 60.0) { rmsLate  += r; ++nLate;  }
    }
    rmsEarly /= (double) juce::jmax (1, nEarly);
    rmsLate  /= (double) juce::jmax (1, nLate);

    INFO ("peak=" << peak << " rmsEarly=" << rmsEarly << " rmsLate=" << rmsLate);
    REQUIRE (finite);
    REQUIRE (peak <= 0.851f);                      // wet puro: el limiter 0.85 contiene
    REQUIRE (rmsLate > 1.0e-4);                    // suena de verdad a los 60 s
    REQUIRE (rmsLate <= rmsEarly * 2.0 + 1.0e-6);  // acotada: la nube no crece sin techo

    // Cola: 10 s de silencio — la energía BAJA (el piso de estabilidad garantiza fb < 1).
    double tail1 = 0.0, tail10 = 0.0;
    const int tailBlocks = (int) std::lround (10.0 * SR / BLK);
    for (int blk = 0; blk < tailBlocks && finite; ++blk)
    {
        buf.clear();
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        finite = blockFinite (buf);
        const double t = (double) blk * BLK / SR;
        if (t >= 0.5 && t < 1.5) tail1  += blockRms (buf);
        if (t >= 9.0)            tail10 += blockRms (buf);
    }
    REQUIRE (finite);
    REQUIRE (tail10 < tail1 * 0.9);                // la cola DECAE: el lazo no diverge
}
