#pragma once

// ========================================================================================================
// BufferSizeStub.h — TEST_CASE [buffersize] PARAMETRIZADO: INVARIANZA al tamaño de bloque. La MISMA señal
// procesada a block=64 y a block=512 debe dar la MISMA salida (±epsilon). Es la prueba de que el DSP del
// plugin NO depende del block-size del host: nada que se "resetee por bloque", ningún coef que se aplique
// una vez por bloque (esos producen artefactos distintos según el block → además de crackle, sonido
// distinto en cada DAW). Un plugin correcto suena IGUAL a 64 que a 512.
//
// MÉTODO: dos instancias FRESCAS (mismo preset), la misma señal de transientes, concatenamos cada stream y
// comparamos muestra a muestra el máximo |Δ|. Tolerancia: epsilon de float acumulado en la cadena
// (default 2e-3; el plugin puede ajustarlo si su motor tiene caos/aleatoriedad ligada al tiempo-real).
// Ambas corridas tienen la MISMA latencia reportada → no hace falta re-alinear (el offset es idéntico).
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG y, opcional,
// OVNI_BUFFERSIZE_SETUP(proc) (un preset DETERMINÍSTICO: sin modulación atada al wall-clock) y
// OVNI_BUFFERSIZE_EPS. Si el motor tiene un LFO/caos ligado a samples procesados (no a wall-clock) sigue
// siendo invariante; si lo liga al wall-clock o a RNG por-bloque, este test lo caza (y está BIEN que falle).
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "BufferSizeStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_BUFFERSIZE_SETUP
  #define OVNI_BUFFERSIZE_SETUP(proc) ovni::test::setParam ((proc), "mix", 1.0f)
#endif
#ifndef OVNI_BUFFERSIZE_EPS
  #define OVNI_BUFFERSIZE_EPS 2e-3f
#endif

namespace ovni::test::buffersize_detail
{
// Corre la señal de transientes por una instancia FRESCA al block dado; devuelve el stream L concatenado.
template <typename Proc, typename Setup>
inline std::vector<float> renderL (double sr, int block, double seconds, Setup&& setup)
{
    Proc proc;
    setup (proc);
    proc.prepareToPlay (sr, block);

    Transients gen (sr);
    const long total = (long) std::llround (seconds * sr);
    std::vector<float> out; out.reserve ((size_t) total + (size_t) block);

    long g = 0;
    while (g < total)
    {
        const int n = (int) juce::jmin ((long) block, total - g);
        juce::AudioBuffer<float> buf (2, n); juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i) { const float x = gen.sample (g + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < n; ++i) out.push_back (L[i]);
        g += n;
    }
    return out;
}
} // namespace ovni::test::buffersize_detail

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": invarianza al block-size (64 vs 512)", "[buffersize]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    constexpr double SR = 48000.0; constexpr double secs = 1.5;
    auto setup = [] (OVNI_PLUGIN_PROCESSOR& p) { OVNI_BUFFERSIZE_SETUP (p); };

    const std::vector<float> a = buffersize_detail::renderL<OVNI_PLUGIN_PROCESSOR> (SR, 64,  secs, setup);
    const std::vector<float> b = buffersize_detail::renderL<OVNI_PLUGIN_PROCESSOR> (SR, 512, secs, setup);

    const size_t cmp = std::min (a.size(), b.size());
    float maxDiff = 0.0f; double energy = 0.0;
    for (size_t i = 0; i < cmp; ++i) { maxDiff = juce::jmax (maxDiff, std::abs (a[i] - b[i])); energy += (double) a[i]*a[i]; }

    std::printf ("BUFFERSIZE[%-10s] block64 vs block512: maxDiff=%.3e  (n=%zu, eps=%.1e, outEnergy=%.3e)\n",
                 OVNI_PLUGIN_SLUG, maxDiff, cmp, (double) OVNI_BUFFERSIZE_EPS, energy);
    INFO ("maxDiff 64-vs-512 = " << maxDiff);

    REQUIRE (cmp > 0);
    REQUIRE (energy > 1e-6);                    // hubo salida real que comparar (no silencio)
    REQUIRE (maxDiff <= OVNI_BUFFERSIZE_EPS);   // GATE: misma salida a 64 y 512 (±epsilon) → DSP block-agnóstico
}
