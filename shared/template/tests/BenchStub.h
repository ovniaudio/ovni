#pragma once

// ========================================================================================================
// BenchStub.h — TEST_CASE [bench] PARAMETRIZADO: CPU% de UNA instancia (house-standard §1/§2). Corre N
// bloques 48k/512 por el processor REAL y mide el wall-clock de processBlock vs el tiempo de audio real que
// esos bloques representan → CPU% single-instance. Imprime `CPU_PCT=` (lo que measure-check.sh grepea).
//
// Es DIAGNÓSTICO (uno de los 3 números públicos). El gate vs el budget de categoría (Movimiento ≤3% · FDN
// ≤5%) es WARN en validate.sh, NO un fallo de build: el hardware del runner no es el de producción (el
// medidor de CPU además infla, ver anti-click-clip-truepeak.md §7). Por eso acá REQUIRE sólo finitud + una
// cota ALTA de cordura. El número real se publica, no se infla.
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG y, opcional,
// OVNI_BENCH_SETUP(proc) para fijar el preset que se mide (default activo → costo realista).
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "BenchStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_BENCH_SETUP
  #define OVNI_BENCH_SETUP(proc) ((void) (proc))   // por defecto: defaults del plugin (el preset que carga al instanciar)
#endif

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": CPU de una instancia (bench)", "[bench]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    OVNI_PLUGIN_PROCESSOR proc;
    const double SR = 48000.0; const int N = 512;

    OVNI_BENCH_SETUP (proc);
    proc.prepareToPlay (SR, N);

    // Excitación: seno a 220 Hz (estado estacionario del motor: cola/binaural corriendo).
    auto fill = [&] (juce::AudioBuffer<float>& buf, int blk)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * N + n) / (float) SR);
        }
    };

    // Pre-cargar el motor unos bloques (estado estacionario antes de cronometrar).
    for (int blk = 0; blk < 200; ++blk) { juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer m; fill (buf, blk); proc.processBlock (buf, m); }

    constexpr int kBlocks = 10000;   // ~107 s de audio (house-standard §2)
    juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int blk = 0; blk < kBlocks; ++blk) { fill (buf, blk + 200); proc.processBlock (buf, midi); }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double wallSec  = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec = (double) kBlocks * N / SR;
    const double cpuPct   = 100.0 * wallSec / audioSec;

    std::printf ("CPU_PCT=%.3f\n", cpuPct);   // <- la línea que grepea measure-check.sh
    INFO ("CPU% (single instance, 48k/512) = " << cpuPct);

    REQUIRE (std::isfinite (cpuPct));
    REQUIRE (cpuPct > 0.0);
    REQUIRE (cpuPct < 80.0);   // cota de cordura (NO el budget — eso es WARN en validate, hardware-dependiente)
}
