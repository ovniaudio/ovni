#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// ★ [real][horizon] — REGRESIÓN EN CONDICIONES REALES (la lección del sello: medir
// a MIX realista con el mecanismo ENCENDIDO, no el caso de laboratorio wet-100).
// Config de uso real: FREEZE ON · RATE SYNC 1/8 (BPM 120 fijo por PlayHead stub) ·
// MIX 40% · banda ancha (ruido rosa). Cuatro gates sobre la SALIDA TOTAL:
//
//   (a) EL LATIDO CAE EN GRILLA: el gate está enganchado al ppq → la envolvente del
//       wet pulsa con PERÍODO == la división (1/8 @120 = 0.25 s), medido ±5%.
//   (b) CADA DISPARO SIN CLICK: la derivada sample-a-sample en los bordes del gate
//       queda acotada (raised-cosine + cross-fade → sin chasquido por disparo).
//   (c) DUCK 100 baja el wet ≥ 6 dB en la ráfaga del dry (el freeze respira).
//   (d) EL FREEZE SE OYE a MIX realista: la energía del wet (vía su side, que el dry
//       mono no aporta) supera un umbral → no es un efecto inaudible a mix 40.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr double kBpm = 120.0;

struct FixedPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (kBpm); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
    }
};
} // namespace

// ── (a) latido en grilla + (b) sin click por disparo: gate SYNC 1/8, freeze sostenido. ──
TEST_CASE ("HORIZON real: el latido SYNC 1/8 cae en grilla y cada disparo no clickea", "[real][horizon]")
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,   1.0f);
    setParam (proc, pid::RATESYNC, 1.0f);
    // división "1/8" = índice 1 de 6 → valor normalizado 1/(kCount-1).
    setParam (proc, pid::RATEDIV,  1.0f / (float) (horizon::params::sync::kCount - 1));
    setParam (proc, pid::WHISPER,  0.0f);    // vidrioso (el latido se mide limpio)
    setParam (proc, pid::SPREAD,   0.0f);    // mono (la envolvente del wet ≈ amplitud total)
    setParam (proc, pid::MIX,      1.0f);    // wet pleno para AISLAR el latido del gate (no la mezcla)

    proc.prepareToPlay (kSR, kBlk);
    FixedPlayHead ph;
    proc.setPlayHead (&ph);

    // Período esperado de la división 1/8 @120 BPM: 0.5 beat = 0.25 s = 12000 samples.
    const double beats   = horizon::params::sync::beatsForDiv (1);   // 1/8 = 0.5 beats
    const double Tsamps  = (60.0 / kBpm) * beats * kSR;

    // Warmup 4 s (captura + asentar el gate) y medición sobre ~3 s de envolvente.
    const long kWarm  = (long) (4.0 * kSR);
    const long kMeas  = (long) (3.0 * kSR);
    const int  kTotal = (int) ((kWarm + kMeas) / kBlk) + 2;

    Pink pink;
    std::vector<float> env;            // |salida| por sample en la ventana de medición
    env.reserve ((size_t) kMeas);
    float maxJump = 0.0f, prevL = 0.0f; bool first = true;
    long g = 0;

    for (int blk = 0; blk < kTotal; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            const float x = 0.5f * pink.next();
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        ph.ppq += (kBpm / 60.0) * ((double) kBlk / kSR);

        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < kBlk; ++i, ++g)
        {
            if (! first) maxJump = juce::jmax (maxJump, std::abs (L[i] - prevL));
            prevL = L[i]; first = false;
            if (g >= kWarm && g < kWarm + kMeas) env.push_back (std::abs (L[i]));
        }
    }

    REQUIRE (env.size() > (size_t) (2 * Tsamps));

    // Suavizar la envolvente (one-pole ~3 ms) y restarle la media → señal del latido.
    const float envCoef = 1.0f - std::exp (-1.0f / (0.003f * (float) kSR));
    double envSm = 0.0, mean = 0.0;
    std::vector<float> e (env.size());
    for (size_t i = 0; i < env.size(); ++i) { envSm += envCoef * (env[i] - envSm); e[i] = (float) envSm; mean += envSm; }
    mean /= (double) e.size();
    for (auto& v : e) v -= (float) mean;

    // Período del latido por autocorrelación alrededor de la división esperada (±25%).
    const int lagLo = (int) (Tsamps * 0.75);
    const int lagHi = (int) (Tsamps * 1.25);
    double bestCorr = -1e30; int bestLag = lagLo;
    for (int lag = lagLo; lag <= lagHi; ++lag)
    {
        double acc = 0.0;
        for (size_t i = 0; i + (size_t) lag < e.size(); ++i) acc += (double) e[i] * e[i + (size_t) lag];
        if (acc > bestCorr) { bestCorr = acc; bestLag = lag; }
    }
    const double periodErr = (double) bestLag / Tsamps - 1.0;

    std::printf ("REAL[horizon GATE] T_expected=%.0f T_measured=%d periodErr=%+.1f%% maxJump=%.4f\n",
                 Tsamps, bestLag, periodErr * 100.0, maxJump);

    // (a) el latido cae en grilla (período == división ±5%).
    REQUIRE (std::abs (periodErr) < 0.05);
    // (b) cada disparo SIN click: la derivada queda acotada (gate raised-cosine + cross-fade).
    // El ruido rosa frozen ya salta solo; el gate no agrega chasquidos → cota 0.15.
    REQUIRE (maxJump < 0.15f);
}

// ── (c) DUCK honesto + (d) el freeze SE OYE a MIX realista (40%) ────────────────────────
TEST_CASE ("HORIZON real: a MIX 40 el freeze se oye y DUCK 100 lo agacha en la ráfaga", "[real][horizon]")
{
    // Mide el RMS del SIDE del wet durante las ráfagas del dry (el dry mono no aporta side:
    // todo el side es del freeze esparcido). duck01 0 vs 100 sobre el MISMO material.
    auto runScenario = [] (float duck01) -> double
    {
        horizon::HorizonProcessor proc;
        setParam (proc, pid::FREEZE,   1.0f);
        setParam (proc, pid::RATESYNC, 1.0f);
        setParam (proc, pid::RATEDIV,  1.0f / (float) (horizon::params::sync::kCount - 1));   // 1/8
        setParam (proc, pid::SPREAD,   0.7f);    // freeze esparcido → side audible
        setParam (proc, pid::DUCK,     duck01);
        setParam (proc, pid::MIX,      0.40f);   // MIX realista 40 %

        proc.prepareToPlay (kSR, kBlk);
        FixedPlayHead ph;
        proc.setPlayHead (&ph);

        // Primero congelar con ruido continuo (4 s), luego ráfagas del dry para el ducking.
        // Ráfagas: 24 bloques ON + 24 OFF. Medimos el side en la ventana de la ráfaga
        // (dry presente → el duck agacha el wet).
        constexpr int kWarm = 380, kPeriod = 48, kOn = 24, kTotal = 760;
        Pink pink;
        double sBurst = 0.0; long nBurst = 0;
        for (int blk = 0; blk < kTotal; ++blk)
        {
            const int ph01 = (blk - kWarm) % kPeriod;
            const bool dryOn = (blk < kWarm) ? true : (ph01 >= 0 && ph01 < kOn);
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            buf.clear();
            if (dryOn)
                for (int n = 0; n < kBlk; ++n) { const float x = 0.7f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            ph.ppq += (kBpm / 60.0) * ((double) kBlk / kSR);

            if (blk < kWarm) continue;
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            const bool inBurst = (ph01 >= 6 && ph01 <= 22);   // dry presente + duck asentado
            if (inBurst)
                for (int n = 0; n < kBlk; ++n)
                {
                    const double side = 0.5 * ((double) L[n] - (double) R[n]);
                    sBurst += side * side; ++nBurst;
                }
        }
        return std::sqrt (sBurst / (double) juce::jmax (1L, nBurst));
    };

    const double sideBase   = runScenario (0.0f);   // duck 0
    const double sideDucked = runScenario (1.0f);   // duck 100, mismo material
    const double duckDropDb = 20.0 * std::log10 ((sideDucked + 1e-12) / (sideBase + 1e-12));

    std::printf ("REAL[horizon MIX40] sideWet(duck0)=%.5f sideWet(duck100)=%.5f duckDrop_dB=%+.2f\n",
                 sideBase, sideDucked, duckDropDb);

    // (d) el freeze SE OYE a MIX 40 %: el side del wet supera un umbral (el dry mono no
    // aporta side → todo el side es del freeze esparcido). Wet-energy > umbral.
    REQUIRE (sideBase > 1e-4);
    // (c) DUCK honesto: con duck=100 el wet se agacha ≥ 6 dB durante las ráfagas del dry.
    REQUIRE (duckDropDb <= -6.0);
}
