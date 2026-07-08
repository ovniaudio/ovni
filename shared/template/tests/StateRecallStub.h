#pragma once

// ========================================================================================================
// StateRecallStub.h — TEST_CASE [staterecall] PARAMETRIZADO: getStateInformation → setStateInformation
// restaura EXACTO. El chasis serializa el APVTS (XML + stateVersion). Este test prueba el round-trip a
// NIVEL de señal (no sólo que los valores vuelvan): una instancia A con params NO-default produce una
// salida; una instancia B fresca a la que le inyectamos el estado de A debe producir la MISMA salida
// (±epsilon). Es la garantía de que abrir un proyecto restaura el sonido que guardaste (DAW recall).
//
// MÉTODO: A = instancia con params barajados (todos los del layout a un valor no-default determinístico) →
// captura su estado + su salida con ruido rosa. B = instancia fresca → setState(estado de A) → misma señal
// → comparamos. Ambas arrancan limpias y reciben la misma excitación, así que la única variable es el
// estado restaurado.
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG y, opcional,
// OVNI_STATERECALL_EPS.
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "StateRecallStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_STATERECALL_EPS
  #define OVNI_STATERECALL_EPS 1e-5f
#endif

namespace ovni::test::staterecall_detail
{
// Setea TODOS los params del layout a un valor no-default determinístico (alterna 0.3 / 0.7 por índice),
// SALVO bypass (que dejaría el plugin inerte) → así el estado guardado es no-trivial y abarca el layout.
inline void shuffleParams (ovni::PluginProcessorBase& proc)
{
    int idx = 0;
    for (auto* p : proc.getParameters())
    {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (withId->paramID == "bypass") { ++idx; continue; }
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost ((idx % 2 == 0) ? 0.3f : 0.7f);
        ++idx;
    }
}

// Corre ruido rosa por el processor (ya configurado) y devuelve el stream L concatenado tras warmup.
inline std::vector<float> render (ovni::PluginProcessorBase& proc, double sr, int block)
{
    proc.prepareToPlay (sr, block);
    Pink pink;
    std::vector<float> out;
    constexpr int kBlocks = 200, kWarmup = 60;
    out.reserve ((size_t) (kBlocks - kWarmup) * (size_t) block);
    for (int blk = 0; blk < kBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        for (int n = 0; n < block; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < kWarmup) continue;
        const float* L = buf.getReadPointer (0);
        for (int n = 0; n < block; ++n) out.push_back (L[n]);
    }
    return out;
}
} // namespace ovni::test::staterecall_detail

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": state recall round-trip (getState->setState)", "[staterecall]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    const double SR = 48000.0; const int N = 256;

    // A: params barajados → estado + salida de referencia.
    OVNI_PLUGIN_PROCESSOR a;
    staterecall_detail::shuffleParams (a);
    juce::MemoryBlock state;
    a.getStateInformation (state);
    REQUIRE (state.getSize() > 0);
    const std::vector<float> outA = staterecall_detail::render (a, SR, N);

    // B: instancia FRESCA (defaults) → le inyectamos el estado de A → debe sonar como A.
    OVNI_PLUGIN_PROCESSOR b;
    b.setStateInformation (state.getData(), (int) state.getSize());
    const std::vector<float> outB = staterecall_detail::render (b, SR, N);

    const size_t cmp = std::min (outA.size(), outB.size());
    float maxDiff = 0.0f; double energy = 0.0;
    for (size_t i = 0; i < cmp; ++i) { maxDiff = juce::jmax (maxDiff, std::abs (outA[i] - outB[i])); energy += (double) outA[i]*outA[i]; }

    std::printf ("STATERECALL[%-10s] stateBytes=%zu  maxDiff(A vs restored)=%.3e  (n=%zu, eps=%.1e)\n",
                 OVNI_PLUGIN_SLUG, (size_t) state.getSize(), maxDiff, cmp, (double) OVNI_STATERECALL_EPS);
    INFO ("state recall maxDiff = " << maxDiff);

    REQUIRE (cmp > 0);
    REQUIRE (energy > 1e-6);                     // la salida de referencia tenía energía (recall no trivial)
    REQUIRE (maxDiff <= OVNI_STATERECALL_EPS);   // GATE: estado restaurado → MISMA salida que el original
}
