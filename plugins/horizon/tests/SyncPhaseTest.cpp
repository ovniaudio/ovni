#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [diccionario][horizon] — SYNC: el LATIDO del gate cae EN el beat tras el PDC del
// host (no deriva libre). El scheduler del re-trigger usa el ppq del host (no su
// propio reloj): el gate abre (envolvente raised-cosine = 1) en el downbeat.
//
// El lock evalúa el ppq en el instante en que el frame del bloque SE OYE (compensado
// por el PDC = N samples y el offset feed→1er-frame), igual que AURORA. Sin compensar,
// el latido llegaría antes/después del beat.
//
// MEDICIÓN (mecanismo ENCENDIDO): FREEZE on + RATE SYNC división 1/4 @120 BPM (ciclo
// T = 0.5 s) + WHISPER 0 + SPREAD 0 (mono → la amplitud total ≈ la envolvente del wet
// gateado) + MIX 100. La envolvente |salida| ∝ gateAmp(t); su fundamental a 1/T conserva
// la fase del gate. Se correlaciona contra cos/sin(2π·τ/T) con τ = sampleSalida − N
// (la línea de tiempo del host tras el PDC). El gate raised-cosine abre EN fase 0 →
// el pico de la envolvente cae cerca del beat. Gate: |error| ≤ 8° (raised-cosine es
// más ancho que un seno puro; margen acorde).
// =============================================================================

namespace
{
namespace pid = horizon::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 480;     // 10 ms; NO múltiplo del hop (512) → jitter de lock real
constexpr double kBpm = 120.0;

struct FixedPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (kBpm); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
    }
};
}

TEST_CASE ("HORIZON SYNC: el latido del gate cae EN el beat tras el PDC (compensado)", "[diccionario][horizon]")
{
    using namespace ovni::test;
    horizon::HorizonProcessor proc;

    setParam (proc, pid::FREEZE,   1.0f);
    setParam (proc, pid::RATESYNC, 1.0f);
    // división "1/4" = índice 2 de 6 → 2/(kCount-1).
    setParam (proc, pid::RATEDIV,  2.0f / (float) (horizon::params::sync::kCount - 1));
    setParam (proc, pid::WHISPER,  0.0f);
    setParam (proc, pid::SPREAD,   0.0f);   // mono → amplitud total ≈ envolvente del gate
    setParam (proc, pid::MIX,      1.0f);

    proc.prepareToPlay (kSR, kBlk);
    FixedPlayHead ph;
    proc.setPlayHead (&ph);

    const int lat = proc.getLatencySamples();

    // Período del ciclo: 1 beat @120 BPM (división 1/4 = 1 beat) = 0.5 s = 24000 samples.
    const double beatsPerCycle = horizon::params::sync::beatsForDiv (2);   // 1/4 = 1 beat
    const double Tsamps = (60.0 / kBpm) * beatsPerCycle * kSR;
    const double omega  = juce::MathConstants<double>::twoPi / Tsamps;

    // 12 s; warmup 4 s (8 ciclos) y medición sobre 16 ciclos ENTEROS.
    const long kWarmSamps = (long) (8.0  * Tsamps);
    const long kMeasSamps = (long) (16.0 * Tsamps);
    const int  kTotalBlks = (int) ((kWarmSamps + kMeasSamps) / kBlk) + 2;

    Pink pink;
    double accC = 0.0, accS = 0.0, accP = 0.0;
    long g = 0;

    for (int blk = 0; blk < kTotalBlks; ++blk)
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
            if (g < kWarmSamps || g >= kWarmSamps + kMeasSamps) continue;
            const double a   = std::abs ((double) L[i]);        // amplitud ∝ gateAmp(t)
            const double tau = (double) (g - lat);              // línea de tiempo del host (PDC)
            accC += a * std::cos (omega * tau);
            accS += a * std::sin (omega * tau);
            accP += a;
        }
    }

    REQUIRE (accP > 1e-3);   // hubo señal gateada de verdad (el mecanismo estuvo ENCENDIDO)

    // Fase MEDIDA del fundamental de la envolvente (el centroide de la ventana abierta del gate).
    const double measuredDeg = std::atan2 (accS, accC) * 180.0 / juce::MathConstants<double>::pi;
    const double modDepth    = std::sqrt (accC * accC + accS * accS) / accP;

    // FASE ESPERADA: la envolvente raised-cosine del gate NO está centrada en la fase 0 (abre EN
    // el beat y SOSTIENE después) → su centroide cae ~0.4 del ciclo. Lo computamos analíticamente
    // con la MISMA forma del motor (rise 0.18 + hold 0.45 + fall 0.18 + valle) → el fundamental de
    // esa envolvente tiene una fase conocida. Comparar contra ELLA prueba LOCK (no drift, no offset
    // arbitrario): si el gate enganchara mal al ppq, la fase medida se alejaría de la esperada.
    auto gateEnv = [] (double p) -> double {
        const double rise = 0.18, hold = 0.45, fall = 0.18;
        if (p < rise)              return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (p / rise));
        if (p < rise + hold)       return 1.0;
        if (p < rise + hold + fall)return 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * ((p - rise - hold) / fall));
        return 0.0;
    };
    double eC = 0.0, eS = 0.0; const int kSteps = 20000;
    for (int i = 0; i < kSteps; ++i)
    {
        const double p = (double) i / (double) kSteps;
        const double a = gateEnv (p);
        eC += a * std::cos (juce::MathConstants<double>::twoPi * p);
        eS += a * std::sin (juce::MathConstants<double>::twoPi * p);
    }
    const double expectedDeg = std::atan2 (eS, eC) * 180.0 / juce::MathConstants<double>::pi;

    // Error de LOCK = cuánto se aparta la fase medida de la esperada (envuelto a ±180°).
    double lockErrDeg = measuredDeg - expectedDeg;
    while (lockErrDeg >  180.0) lockErrDeg -= 360.0;
    while (lockErrDeg < -180.0) lockErrDeg += 360.0;
    const double lockErrMs = (lockErrDeg / 360.0) * (Tsamps / kSR) * 1000.0;

    std::printf ("SYNC_PHASE[horizon] MEASURED_DEG=%+.2f EXPECTED_DEG=%+.2f LOCK_ERR_DEG=%+.2f (%.1f ms @ 1/4 120 BPM) modIndex=%.3f lat=%d\n",
                 measuredDeg, expectedDeg, lockErrDeg, lockErrMs, modDepth, lat);

    REQUIRE (modDepth > 0.1);                  // la envolvente late de verdad (sin falso verde)
    REQUIRE (std::abs (lockErrDeg) <= 8.0);    // GATE: el latido está LOCKEADO al ppq (±8° del centroide esperado)
}
