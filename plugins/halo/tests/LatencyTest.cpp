// Test [latency][halo] — latencia REPORTADA == REAL (house-standard §1/§2: ≤ 1 sample). HALO es un FX de
// cola: el wet lleva el carácter (OS/grano/bloom viven en la rama wet, DENTRO del lazo de feedback) y NO se
// compensa con PDC; el DRY pasa NULO (sin retardo). Por eso el motor reporta 0 samples de latencia y este
// test verifica que el impulso por el DRY (MIX=0) sale en el sample 0 (± 1).
//
// Imprime `LATENCY_REPORTED=<n>` y `LATENCY_REAL=<n>`; gate del orquestador: |real − reported| ≤ 1.
// Modelado en GainTest (corre el processor real, imprime las líneas que validate grepea).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("halo: latencia reportada == real (dry nulo, MIX=0)", "[latency][halo]")
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    const double SR = 48000.0; const int N = 512;

    // MIX=0 → sólo el DRY (la rama wet no contribuye). El dry debe pasar sin retardo.
    if (auto* m = proc.apvts.getParameter (pid::MIX)) m->setValueNotifyingHost (0.0f);
    proc.prepareToPlay (SR, N);

    const int reported = proc.getLatencySamples();

    // Impulso en el sample 0 del primer bloque; buscamos en qué sample sale (peak del |out|).
    juce::AudioBuffer<float> buf (2, N);
    juce::MidiBuffer midi;
    buf.clear();
    buf.setSample (0, 0, 1.0f);
    buf.setSample (1, 0, 1.0f);
    proc.processBlock (buf, midi);

    int realLat = -1;
    float best = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        const float a = std::abs (buf.getSample (0, i));
        if (a > best) { best = a; realLat = i; }
    }

    std::printf ("LATENCY_REPORTED=%d\n", reported);   // <- líneas que grepea measure-check.sh
    std::printf ("LATENCY_REAL=%d\n", realLat);
    INFO ("reported=" << reported << "  real=" << realLat << "  peak=" << best);

    REQUIRE (realLat >= 0);
    REQUIRE (best > 0.1f);                              // el dry efectivamente salió
    REQUIRE (std::abs (realLat - reported) <= 1);       // GATE: reportada == real (±1)
}
