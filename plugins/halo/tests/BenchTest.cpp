// Test [bench][halo] — CPU del plugin (house-standard §1/§2). Corre N bloques 48k/512 por el HaloProcessor
// REAL y mide el wall-clock de processBlock vs el tiempo real que esos bloques representan → CPU% de una
// instancia. Imprime `CPU_PCT=<%>` (gate WARN del orquestador vs budget FDN ≤ 5%). Reporta además el delta
// con ORBIT on vs off (el costo del pan binaural ITD/ILD: las líneas de delay interaural + el shelf de sombra).
//
// Modelado en GainTest/StereoMeasure (corre el processor real, imprime la línea que el harness grepea). Es
// DIAGNÓSTICO: REQUIRE sólo finitud + una cota ALTA de cordura (no el budget, que es WARN — el hardware del
// runner no es el de Joaquín; el número se publica, no se hace fallar el build por CPU).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace {

double benchCpuPct (float orbit01)
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    const double SR = 48000.0; const int N = 512;

    // Defaults del preset "VASTEDAD" + ORBIT al valor pedido (mide el costo del pan binaural ITD/ILD).
    if (auto* o = proc.apvts.getParameter (pid::ORBIT)) o->setValueNotifyingHost (orbit01);
    proc.prepareToPlay (SR, N);

    // Pre-cargar el lazo unos bloques (estado estacionario: el FDN + pitch ya corriendo).
    auto fill = [&] (juce::AudioBuffer<float>& buf, int blk)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * N + n) / (float) SR);
        }
    };
    for (int blk = 0; blk < 200; ++blk) { juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer m; fill (buf, blk); proc.processBlock (buf, m); }

    constexpr int kBlocks = 10000;   // ~107 s de audio
    juce::AudioBuffer<float> buf (2, N);
    juce::MidiBuffer midi;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int blk = 0; blk < kBlocks; ++blk)
    {
        fill (buf, blk + 200);
        proc.processBlock (buf, midi);
    }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double wallSec  = std::chrono::duration<double> (t1 - t0).count();
    const double audioSec = (double) kBlocks * N / SR;
    return 100.0 * wallSec / audioSec;
}

} // namespace

TEST_CASE ("halo: CPU de una instancia (bench, budget FDN <= 5%)", "[bench][halo]")
{
    const double cpuOrbit = benchCpuPct (0.40f);   // default (órbita media → pan ITD/ILD activo)
    const double cpuNoOrbit = benchCpuPct (0.0f);  // sin órbita (profundidad del pan = 0 → ITD/ILD casi sin trabajo)

    std::printf ("CPU_PCT=%.3f\n", cpuOrbit);                 // <- la línea que grepea measure-check.sh
    std::printf ("CPU_PCT_NO_ORBIT=%.3f\n", cpuNoOrbit);      // diagnóstico (costo del pan binaural = delta)
    INFO ("CPU% orbit=" << cpuOrbit << "  noOrbit=" << cpuNoOrbit << "  (delta pan ITD/ILD=" << (cpuOrbit - cpuNoOrbit) << ")");

    REQUIRE (std::isfinite (cpuOrbit));
    REQUIRE (cpuOrbit > 0.0);
    REQUIRE (cpuOrbit < 80.0);   // cota de cordura (no el budget — eso es WARN en validate, hardware-dependiente)
}
