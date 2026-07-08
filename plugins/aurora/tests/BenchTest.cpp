#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [bench][aurora] — CPU% wall-clock del AuroraProcessor REAL: 10 000 bloques
// 48k/512 (house-standard: budget STFT ≤ 6% single instance). Con MOTION
// encendido (la curva por bin se recalcula con γ vivo cada frame: el costo real
// del despliegue, no el caso quieto). Imprime CPU_PCT= (measure-check.sh).
// =============================================================================
TEST_CASE ("AURORA bench: CPU% sobre 10000 bloques 48k/512", "[bench][aurora]")
{
    aurora::AuroraProcessor proc;
    const double SR = 48000.0; const int N = 512;

    namespace pid = aurora::params::id;
    auto set = [&] (const char* id, float v01) {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
    };
    set (pid::MOTION, 0.5f);   // γ vivo (el caso de uso real, no el knob quieto)

    proc.prepareToPlay (SR, N);

    juce::AudioBuffer<float> buf (2, N);
    juce::MidiBuffer midi;
    long g = 0;
    auto fill = [&] {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (g + n) / (float) SR);
        }
        g += N;
    };

    for (int blk = 0; blk < 200; ++blk) { fill(); proc.processBlock (buf, midi); }   // pre-carga

    constexpr int kBlocks = 10000;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int blk = 0; blk < kBlocks; ++blk) { fill(); proc.processBlock (buf, midi); }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double wallSec  = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec = (double) kBlocks * N / SR;
    const double cpuPct   = 100.0 * wallSec / audioSec;

    std::printf ("CPU_PCT=%.3f\n", cpuPct);   // <- la línea que grepea measure-check.sh
    REQUIRE (std::isfinite (cpuPct));
    REQUIRE (cpuPct < 6.0);   // budget de curaduría para el eje espectral (≤ 6%)
}
