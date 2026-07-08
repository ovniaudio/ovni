#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// ★ [realworld][aurora] — REGRESIÓN EN CONDICIONES REALES (la lección del
// sello: medir a MIX realista con el mecanismo ENCENDIDO, no el caso de
// laboratorio wet-100). Config de uso real: MIX 40% · MOTION 60 · SYNC ON
// (división 1/2, BPM 120 fijo por PlayHead stub) · Mono Safe default 50.
// Señal: ráfagas de ruido rosa (banda ancha) con huecos — como un bus que pega.
//
// Tres gates sobre la SALIDA TOTAL (dry+wet, lo que el productor escucha):
//   (a) el efecto SE OYE a mix realista → energía de SIDE real (el dry mono no
//       aporta side: todo el side que hay lo puso el despliegue).
//   (b) DUCK honesto: con duck=100 el wet (medido por su side) se agacha ≥ 6 dB
//       DURANTE las ráfagas respecto del mismo pasaje con duck=0.
//   (c) Mono Safe honesto: bajo el corte (~205 Hz @ default 50) la suma mono de
//       la salida total NO pierde > 1 dB respecto del mismo material colapsado
//       al centro (spread 0) — los graves desplegados no rompen mono.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = aurora::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr double kBpm = 120.0;

// PlayHead con BPM fijo + transporte corriendo (el SYNC engancha la división a ppq).
struct FixedPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (kBpm); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
    }
};

struct RealWorldRun
{
    double sideRmsBurst = 0.0;   // RMS del side DURANTE las ráfagas (dry+wet presentes)
    double width        = 0.0;   // WIDTH global de la salida total
    double lowMonoRms   = 0.0;   // RMS de la suma mono filtrada baja (bajo el corte Mono Safe)
    double rms          = 0.0;   // energía total (cordura)
};

// Corre el escenario real y mide. duck01/spread01 normalizados (el resto fijo del escenario).
RealWorldRun runScenario (float duck01, float spread01)
{
    aurora::AuroraProcessor proc;
    setParam (proc, pid::MIX,        0.40f);            // MIX realista (40 %)
    setParam (proc, pid::MOTION,     0.60f);            // el abanico late…
    setParam (proc, pid::MOTIONSYNC, 1.0f);             // …enganchado al tempo
    setParam (proc, pid::MOTIONDIV,  1.0f / 3.0f);      // división "1/2" (índice 1 de 4)
    setParam (proc, pid::SPREAD,     spread01);
    setParam (proc, pid::DUCK,       duck01);
    // Mono Safe queda en su default 50 (≈205 Hz): el escenario real de la curaduría.

    proc.prepareToPlay (kSR, kBlk);
    FixedPlayHead ph;
    proc.setPlayHead (&ph);

    // Ráfagas: 24 bloques ON + 24 OFF (~256 ms c/u). La latencia OLA es 4 bloques → el
    // wet de la ráfaga vive en [on+4, on+27]; medimos el side en [on+6, on+22] (dry y wet
    // presentes y el duck ya asentado: attack 5 ms ≪ un bloque).
    constexpr int kPeriod = 48, kOn = 24, kTotal = 480, kWarm = 48;

    Pink pink;
    StereoImageMeter meter;
    double sBurst = 0.0; long nBurst = 0;
    double sLowMono = 0.0; long nLow = 0;
    double lp1 = 0.0, lp2 = 0.0;   // 2× one-pole @80 Hz → aísla bien bajo el corte (205 Hz)
    const double lpCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 80.0 / kSR);

    for (int blk = 0; blk < kTotal; ++blk)
    {
        const int ph01 = blk % kPeriod;
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        buf.clear();
        if (ph01 < kOn)
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.7f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }

        proc.processBlock (buf, midi);
        ph.ppq += (kBpm / 60.0) * ((double) kBlk / kSR);

        if (blk < kWarm) continue;
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        meter.addBlock (L, R, kBlk);
        const bool inBurstWindow = (ph01 >= 6 && ph01 <= 22);
        for (int n = 0; n < kBlk; ++n)
        {
            const double side = 0.5 * ((double) L[n] - (double) R[n]);
            if (inBurstWindow) { sBurst += side * side; ++nBurst; }
            const double mono = 0.5 * ((double) L[n] + (double) R[n]);
            lp1 += lpCoef * (mono - lp1);
            lp2 += lpCoef * (lp1 - lp2);
            sLowMono += lp2 * lp2; ++nLow;
        }
    }

    RealWorldRun out;
    const auto img    = meter.finish();
    out.width         = img.width;
    out.rms           = img.rms;
    out.sideRmsBurst  = std::sqrt (sBurst   / (double) juce::jmax (1L, nBurst));
    out.lowMonoRms    = std::sqrt (sLowMono / (double) juce::jmax (1L, nLow));
    return out;
}
} // namespace

TEST_CASE ("AURORA condiciones reales: MIX 40 + MOTION 60 SYNC -> se oye, el duck agacha, mono integro", "[realworld][aurora]")
{
    const RealWorldRun base      = runScenario (0.0f, 0.55f);   // duck 0, spread default 55
    const RealWorldRun ducked    = runScenario (1.0f, 0.55f);   // duck 100, mismo material
    const RealWorldRun centerRef = runScenario (0.0f, 0.0f);    // spread 0 = referencia mono-centro

    const double duckDropDb = 20.0 * std::log10 ((ducked.sideRmsBurst + 1e-12)
                                               / (base.sideRmsBurst   + 1e-12));
    const double lowMonoDb  = 20.0 * std::log10 ((base.lowMonoRms + 1e-12)
                                               / (centerRef.lowMonoRms + 1e-12));

    std::printf ("REALWORLD[aurora] WIDTH=%.3f sideBurst(duck0)=%.5f sideBurst(duck100)=%.5f duckDrop_dB=%+.2f lowMonoDelta_dB=%+.2f\n",
                 base.width, base.sideRmsBurst, ducked.sideRmsBurst, duckDropDb, lowMonoDb);

    REQUIRE (base.rms > 1e-5);   // hay señal de verdad

    // (a) El efecto SE OYE a MIX 40 %: el dry mono no aporta side — todo el side es del
    // despliegue. Medido en este escenario: WIDTH ≈ 0.060 (side ~24 dB bajo el mid, con
    // el MOTION bombeándolo al tempo — perceptible en bus). Gate con margen del 25 %.
    REQUIRE (base.width > 0.045);
    REQUIRE (base.sideRmsBurst > 1e-4);

    // (b) DUCK honesto: con duck=100 el wet se agacha ≥ 6 dB durante las ráfagas.
    REQUIRE (duckDropDb <= -6.0);

    // (c) Mono Safe honesto: bajo el corte la suma mono de la salida total queda a < 1 dB
    // de la referencia colapsada al centro (el despliegue NO rompe los graves en mono).
    REQUIRE (std::abs (lowMonoDb) < 1.0);
}
