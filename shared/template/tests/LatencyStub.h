#pragma once

// ========================================================================================================
// LatencyStub.h — TEST_CASE [latency] PARAMETRIZADO: latencia REPORTADA == REAL (house-standard §1/§2: el
// |real − reportada| ≤ 1 sample). Imprime `LATENCY_REPORTED=` y `LATENCY_REAL=` (lo que measure-check.sh
// grepea).
//
// MÉTODO (vale para latencia 0 Y para latencia por lookahead): MIX=0 → sólo el DRY (la rama wet no
// contribuye). Mandamos un impulso en el sample 0 y buscamos en qué sample SALE (peak del |out|). Ese es
// el retardo REAL del camino directo. Debe coincidir con getLatencySamples() ±1:
//   · Plugins con dry NULO (HALO: el wet no se compensa con PDC, reporta 0) → el impulso sale en sample 0.
//   · Plugins con limiter de salida por lookahead que retarda la SUMA dry+wet (NÉBULA: ~3 ms) → el dry
//     también pasa por ese retardo → el impulso sale en `reported`. El gate es el MISMO: real == reported.
// Así el test NO asume un valor concreto: verifica la PROPIEDAD (lo que reportás al host es lo que pasa).
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG. Opcional
// OVNI_LATENCY_MIX_ID si el id del dry/wet del plugin no es "mix".
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "LatencyStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_LATENCY_MIX_ID
  #define OVNI_LATENCY_MIX_ID "mix"
#endif

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": latencia reportada == real (dry, MIX=0)", "[latency]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    OVNI_PLUGIN_PROCESSOR proc;
    const double SR = 48000.0; const int N = 1024;   // bloque grande: cubre el lookahead de salida sin desbordar

    setParam (proc, OVNI_LATENCY_MIX_ID, 0.0f);   // MIX=0 → sólo el dry (la rama wet no contribuye)
    proc.prepareToPlay (SR, N);

    const int reported = proc.getLatencySamples();

    // Impulso en el sample 0 del primer bloque; buscamos el sample del peak del |out| (= retardo real).
    juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
    buf.clear();
    buf.setSample (0, 0, 1.0f);
    buf.setSample (1, 0, 1.0f);
    proc.processBlock (buf, midi);

    int realLat = -1; float best = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float a = std::abs (buf.getSample (0, i));
        if (a > best) { best = a; realLat = i; }
    }

    std::printf ("LATENCY_REPORTED=%d\n", reported);   // <- líneas que grepea measure-check.sh
    std::printf ("LATENCY_REAL=%d\n", realLat);
    INFO ("reported=" << reported << "  real=" << realLat << "  peak=" << best);

    REQUIRE (realLat >= 0);
    REQUIRE (best > 0.1f);                         // el dry efectivamente salió (no se anuló)
    REQUIRE (std::abs (realLat - reported) <= 1);  // GATE: reportada == real (±1 sample)
}
