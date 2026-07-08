#pragma once

// ========================================================================================================
// TransientStub.h — TEST_CASE [transient] PARAMETRIZADO: pico INTER-SAMPLE (true-peak, proxy 4× Catmull-Rom
// de shared/dsp/TruePeak.h) + maxJump con boundary-check (proxy de crackle de borde de bloque). Inyecta
// TRANSIENTES AGRESIVAS (impulsos full-scale ±1 + click HF) por el processor REAL y mide lo que mide un
// medidor de DAW. Es el GATE anti-true-peak UNIFORME (antes sólo NÉBULA lo tenía inline).
//
// QUÉ MIDE (ver anti-click-clip-truepeak.md §3/§8):
//   · samplePeak = máx |muestra| (lo que limita el limiter de sample-peak)
//   · truePeak   = pico inter-sample (proxy 4×): el "clip mínimo que sigue" que el sample-peak ignora
//   · maxJump    = mayor |x[i]−x[i−1]|; boundaryHit = el maxJump cae en el borde de un bloque (proxy de
//                  coef no rampeado por-sample, §0). PROXY ruidoso: sirve para AISLAR, no como verdad.
//
// GATE: por defecto exige true-peak ≤ OVNI_TRANSIENT_TP_CEIL (techo true-peak-safe; default 0.97, el del
// lookahead de salida que aceptó Joaquín a cambio de cero clip). El boundary-check sólo es FIABLE sin dry
// crudo (los impulsos ±1 del dry meten saltos enormes en cualquier posición → contaminan el heurístico),
// así que por defecto se MIDE e imprime pero NO se hace fallar por boundary salvo que el plugin lo pida
// (OVNI_TRANSIENT_REQUIRE_NO_BOUNDARY). Cada plugin con un diseño wet-only puede endurecerlo.
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG y, opcional,
// OVNI_TRANSIENT_SETUP(proc) (peor caso: cola larga + wet), OVNI_TRANSIENT_TP_CEIL, y el flag de boundary.
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"
#include "dsp/TruePeak.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "TransientStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_TRANSIENT_SETUP
  #define OVNI_TRANSIENT_SETUP(proc) ovni::test::setParam ((proc), "mix", 1.0f)
#endif
#ifndef OVNI_TRANSIENT_TP_CEIL
  #define OVNI_TRANSIENT_TP_CEIL 0.97f   // techo true-peak-safe (lookahead de salida); ver §3/§6
#endif

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": transientes -> true-peak inter-sample + maxJump", "[transient]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    OVNI_PLUGIN_PROCESSOR proc;
    const double SR = 48000.0; const int block = 128; const double seconds = 3.0;

    OVNI_TRANSIENT_SETUP (proc);
    proc.prepareToPlay (SR, block);

    Transients gen (SR);
    const long total = (long) std::llround (seconds * SR);

    std::vector<float> allL, allR;
    allL.reserve ((size_t) total + (size_t) block);
    allR.reserve ((size_t) total + (size_t) block);

    long g = 0;
    while (g < total)
    {
        const int n = (int) juce::jmin ((long) block, total - g);
        juce::AudioBuffer<float> buf (2, n); juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i) { const float x = gen.sample (g + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        for (int i = 0; i < n; ++i) { allL.push_back (L[i]); allR.push_back (R[i]); }
        g += n;
    }

    // sample-peak.
    float samplePeak = 0.0f;
    for (float v : allL) samplePeak = juce::jmax (samplePeak, std::abs (v));
    for (float v : allR) samplePeak = juce::jmax (samplePeak, std::abs (v));

    // true-peak (inter-sample, proxy 4× Catmull-Rom — shared/dsp/TruePeak.h).
    const float truePeak = juce::jmax (ovni::dsp::truePeakOf (allL), ovni::dsp::truePeakOf (allR));

    // maxJump + boundary-check (proxy de coef no rampeado en el borde de bloque). Boundary-check correcto:
    // i es el índice del SEGUNDO sample del salto (x[i]-x[i-1]); el borde está en i % block == 0 (primera
    // muestra de un bloque, donde el escalón de un coef constante-por-bloque caería). Tolerancia ±1.
    float maxJump = 0.0f; long maxJumpIdx = -1;
    auto scan = [&] (const std::vector<float>& x)
    {
        for (size_t i = 1; i < x.size(); ++i)
        {
            const float j = std::abs (x[i] - x[i-1]);
            if (j > maxJump) { maxJump = j; maxJumpIdx = (long) i; }
        }
    };
    scan (allL); scan (allR);
    int intraBlk = -1; bool boundaryHit = false;
    if (maxJumpIdx >= 0)
    {
        intraBlk    = (int) (maxJumpIdx % block);
        boundaryHit = (intraBlk <= 1) || (intraBlk >= block - 1);
    }

    std::printf ("TRANSIENT[%-10s] sample=%.4f  true=%.4f  maxJump=%.4f @%ld (intra-blk=%d/%d%s)\n",
                 OVNI_PLUGIN_SLUG, samplePeak, truePeak, maxJump, maxJumpIdx, intraBlk, block,
                 boundaryHit ? " <-BORDE" : "");
    INFO ("samplePeak=" << samplePeak << " truePeak=" << truePeak << " maxJump=" << maxJump);

    REQUIRE (std::isfinite (truePeak));
    REQUIRE (std::isfinite (maxJump));
    REQUIRE (truePeak <= OVNI_TRANSIENT_TP_CEIL);   // GATE true-peak (proxy): la salida queda bajo el techo
#ifdef OVNI_TRANSIENT_REQUIRE_NO_BOUNDARY
    REQUIRE_FALSE (boundaryHit);   // sólo fiable en diseños wet-only (sin dry crudo); el plugin lo habilita
#endif
}
