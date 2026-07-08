#pragma once

// ========================================================================================================
// NullStub.h — TEST_CASE [null] PARAMETRIZADO: BYPASS bit-exact. El chasis del sello
// (PluginProcessorBase::processBlock) hace pass-through PURO en bypass (no toca el buffer, ni siquiera el
// gain in/out). Este test lo VERIFICA bit a bit: capturamos la entrada, corremos en bypass y exigimos que
// la salida sea IDÉNTICA muestra a muestra. Es la prueba de honestidad del bypass (el "off" es realmente
// off: no colorea, no retarda, no clampea). Si un plugin agrega algo al bypass, este test lo caza.
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG. Opcional
// OVNI_BYPASS_ID si el id del bool de bypass no es "bypass".
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "NullStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_BYPASS_ID
  #define OVNI_BYPASS_ID "bypass"
#endif

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": bypass bit-exact (null)", "[null]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    OVNI_PLUGIN_PROCESSOR proc;
    const double SR = 48000.0; const int N = 512;

    setParam (proc, OVNI_BYPASS_ID, 1.0f);   // BYPASS ON → pass-through puro
    proc.prepareToPlay (SR, N);

    Pink pink;
    double maxDiff = 0.0; long compared = 0; double inEnergy = 0.0;
    for (int blk = 0; blk < 50; ++blk)
    {
        juce::AudioBuffer<float> in (2, N), buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n)
        {
            const float x = pink.next();
            in.setSample (0, n, x); in.setSample (1, n, x);
            buf.setSample (0, n, x); buf.setSample (1, n, x);
            inEnergy += (double) x * x;
        }
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < N; ++n)
            {
                const double d = std::abs ((double) buf.getSample (ch, n) - (double) in.getSample (ch, n));
                maxDiff = juce::jmax (maxDiff, d);
                ++compared;
            }
    }

    std::printf ("NULL[%-10s] bypass maxDiff=%.3e over %ld samples (inEnergy=%.3e)\n",
                 OVNI_PLUGIN_SLUG, maxDiff, compared, inEnergy);
    INFO ("bypass maxDiff = " << maxDiff);

    REQUIRE (inEnergy > 1e-6);     // la señal de prueba no era silencio (el null sería trivial)
    REQUIRE (maxDiff == 0.0);      // GATE: bypass = pass-through BIT-EXACT (cero diferencia)
}
