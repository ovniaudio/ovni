#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [latency][aurora] — gate H8 del sello: latencia REPORTADA == REAL (±1 sample).
// AURORA tiene latencia OLA REAL = N samples (2048 @48k, el frame entero del
// STFT — NO N/2): el processor la declara con setLatencySamples() Y el dry pasa
// por el MISMO retardo dentro del motor. Método (como LatencyStub, pero multi-
// bloque porque 2048 > un bloque): MIX=0 → sólo el dry retrasado; impulso en el
// sample 0; el pico de |out| a lo largo de VARIOS bloques marca el retardo real.
// Imprime LATENCY_REPORTED= / LATENCY_REAL= (lo que grepea measure-check.sh).
// =============================================================================
TEST_CASE ("AURORA: latencia reportada == real (dry retrasado N, MIX=0)", "[latency][aurora]")
{
    aurora::AuroraProcessor proc;
    const double SR = 48000.0; const int N = 512;

    namespace pid = aurora::params::id;
    if (auto* p = proc.apvts.getParameter (pid::MIX)) p->setValueNotifyingHost (0.0f);   // sólo dry

    proc.prepareToPlay (SR, N);
    const int reported = proc.getLatencySamples();

    // 8 bloques de 512 = 4096 samples ≥ 2·N: el impulso retrasado N cae adentro seguro.
    const int kBlocks = 8;
    int realLat = -1; float best = 0.0f;
    for (int blk = 0; blk < kBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        buf.clear();
        if (blk == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
        proc.processBlock (buf, midi);
        for (int i = 0; i < N; ++i)
        {
            const float a = std::abs (buf.getSample (0, i));
            if (a > best) { best = a; realLat = blk * N + i; }
        }
    }

    std::printf ("LATENCY_REPORTED=%d\n", reported);   // <- líneas que grepea measure-check.sh
    std::printf ("LATENCY_REAL=%d\n", realLat);
    INFO ("reported=" << reported << "  real=" << realLat << "  peak=" << best);

    REQUIRE (realLat >= 0);
    REQUIRE (best > 0.1f);                         // el dry efectivamente salió (no se anuló)
    REQUIRE (std::abs (realLat - reported) <= 1);  // GATE: reportada == real (±1 sample)
}
