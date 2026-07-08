#pragma once

// ========================================================================================================
// MeasureStub.h — TEST_CASE [measure] PARAMETRIZADO. Ruido rosa por el processor REAL → imagen estéreo
// (CORR/WIDTH/BAL/MONOSUM) + IACC (correlación interaural, el cue de envolvimiento). Imprime la línea
// `IACC=` que tools/measure-check.sh grepea, + la línea MEASURE[...] de diagnóstico.
//
// CONTRATO (ver _README.md): el .cpp del plugin define ANTES de incluir esto:
//   #define OVNI_PLUGIN_PROCESSOR  pulsar::PulsarProcessor
//   #define OVNI_PLUGIN_SLUG       "pulsar"
//   #define OVNI_PLUGIN_TAG        "[pulsar]"
//   #include "PluginProcessor.h"
//   #include "template/tests/MeasureStub.h"
//
// El IACC es DIAGNÓSTICO (lo publica el sello como uno de los números). El gate fino lo juzga el oído + el
// número publicado; acá REQUIRE sólo finitud + energía real (que la salida no sea silencio). Cada plugin
// puede sumar su propio TEST_CASE [measure][slug] específico (sweep de su macro de ancho, etc.) además de
// éste — son acumulables (Catch2 permite varios TEST_CASE con el mismo tag).
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "MeasureStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

// Permite al plugin sobreescribir qué macros llevar al máximo para "abrir" la imagen (ancho/wet). Por
// defecto setea mix=1 (wet pleno → medimos el wet, no el dry); el plugin define OVNI_MEASURE_SETUP(proc)
// para fijar SUS params (ej. width=1, motion bajo). Si no lo define, sólo se pone mix=1 si ese id existe.
#ifndef OVNI_MEASURE_SETUP
  #define OVNI_MEASURE_SETUP(proc) ovni::test::setParam ((proc), "mix", 1.0f)
#endif

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": imagen estereo / IACC con ruido rosa", "[measure]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    OVNI_PLUGIN_PROCESSOR proc;
    const double SR = 48000.0; const int N = 512;

    OVNI_MEASURE_SETUP (proc);
    proc.prepareToPlay (SR, N);

    Pink pink;
    StereoImageMeter meter;
    constexpr int kBlocks = 700, kWarmup = 250;   // warmup: la cola/binaural se asientan antes de medir
    for (int blk = 0; blk < kBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < kWarmup) continue;
        meter.addBlock (buf.getReadPointer (0), buf.getReadPointer (1), N);
    }
    const StereoImage m = meter.finish();

    std::printf ("MEASURE[%-10s] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 OVNI_PLUGIN_SLUG, m.corr, m.width, m.balDb, m.monoSumDb);
    std::printf ("IACC=%.3f\n", m.corr);   // <- la línea que grepea measure-check.sh (IACC == CORR interaural)
    INFO ("CORR/IACC=" << m.corr << " WIDTH=" << m.width << " rms=" << m.rms);

    REQUIRE (std::isfinite (m.corr));
    REQUIRE (std::isfinite (m.width));
    REQUIRE (m.rms > 1e-5);   // la salida tiene energía real (no se anuló)
}
