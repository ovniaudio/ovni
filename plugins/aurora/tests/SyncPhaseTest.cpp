#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [diccionario][aurora] — SYNC: el abanico late EN el beat tras el PDC del host
// (fix del hallazgo MEDIUM, review 2026-06-10).
//
// El lock de fase del MOTION usaba el ppq del inicio del bloque que se está
// ALIMENTANDO, pero ese contenido se oye en OTRO instante de la línea de tiempo:
// el host corre la salida N samples hacia atrás (PDC) y la ventana Hann² del OLA
// imprime la modulación con centroide N/2 antes del feed → el abanico llegaba
// ANTES del beat (medido pre-fix: −9.2° @ división 1/4, 120 BPM = −12.8 ms; post-fix −0.5°).
// El fix evalúa el lock en el instante en que el PRIMER frame del bloque SE OYE:
//   ppq + rate·[(hop − hopPhase) − N/2 + lagGamma]/sr   (AuroraEngine::process).
//
// MEDICIÓN (mecanismo ENCENDIDO, nada neutralizado): MOTION 100 + SYNC división
// 1/4 @ 120 BPM (ciclo T = 0.5 s) + SPREAD 100 + MIX 100, ruido rosa continuo.
// La envolvente del SIDE (potencia) es ∝ mod² y su fundamental a 1/T conserva la
// fase de cos(θ_motion). Se correlaciona side² contra cos/sin(2π·τ/T) con
// τ = sampleSalida − N (la línea de tiempo del host tras el PDC, integrando un
// número ENTERO de ciclos) → la fase medida debe caer en 0° (abanico ABIERTO en
// el beat: mod = 1 en fase 0). Gate: |error| ≤ 5°. El bloque de 480 samples NO
// es múltiplo del hop (512) a propósito: ejercita el término (hop − hopPhase).
// =============================================================================

namespace
{
namespace pid = aurora::params::id;

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

TEST_CASE ("AURORA SYNC: la fase del MOTION cae EN el beat tras el PDC (compensada)", "[diccionario][aurora]")
{
    using namespace ovni::test;
    aurora::AuroraProcessor proc;

    setParam (proc, pid::MOTION,      1.0f);   // latido pleno (la envolvente se mide fácil)
    setParam (proc, pid::MOTIONSYNC,  1.0f);
    setParam (proc, pid::MOTIONDIV,   0.0f);   // división "1/4" (índice 0) → 2 Hz @120 = el peor caso del subset
    setParam (proc, pid::SPREAD,      1.0f);
    setParam (proc, pid::MIX,         1.0f);
    setParam (proc, pid::MONOSAFEAMT, 0.0f);   // sin red: TODO el espectro aporta side

    proc.prepareToPlay (kSR, kBlk);
    FixedPlayHead ph;
    proc.setPlayHead (&ph);

    const int lat = proc.getLatencySamples();

    // Período del ciclo: 1 beat @120 BPM = 0.5 s = 24000 samples.
    const double beatsPerCycle = aurora::params::sync::beatsForDiv (0);
    const double Tsamps = (60.0 / kBpm) * beatsPerCycle * kSR;
    const double omega  = juce::MathConstants<double>::twoPi / Tsamps;

    // 12 s en total; warmup 4 s (8 ciclos exactos) y medición sobre 16 ciclos ENTEROS.
    const long kWarmSamps  = (long) (8.0  * Tsamps);
    const long kMeasSamps  = (long) (16.0 * Tsamps);
    const int  kTotalBlks  = (int) ((kWarmSamps + kMeasSamps) / kBlk) + 2;

    Pink pink;
    double accC = 0.0, accS = 0.0, accP = 0.0;
    long g = 0;   // contador GLOBAL de samples de salida

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
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < kBlk; ++i, ++g)
        {
            if (g < kWarmSamps || g >= kWarmSamps + kMeasSamps) continue;
            const double side = 0.5 * ((double) L[i] - (double) R[i]);
            const double p2   = side * side;                   // potencia del side ∝ mod²
            const double tau  = (double) (g - lat);            // línea de tiempo del host (PDC)
            accC += p2 * std::cos (omega * tau);
            accS += p2 * std::sin (omega * tau);
            accP += p2;
        }
    }

    REQUIRE (accP > 1e-6);   // hubo side de verdad (el mecanismo estuvo ENCENDIDO)

    // Fase del fundamental de la envolvente vs el beat. 0° = abanico abierto EN el beat.
    // Negativo = la modulación llega ANTES del beat (el bug pre-fix); positivo = tarde.
    const double phaseErrDeg = std::atan2 (accS, accC) * 180.0 / juce::MathConstants<double>::pi;
    const double phaseErrMs  = (phaseErrDeg / 360.0) * (Tsamps / kSR) * 1000.0;
    const double modDepth    = std::sqrt (accC * accC + accS * accS) / accP;   // cordura: hay latido

    std::printf ("SYNC_PHASE[aurora] PHASE_ERR_DEG=%+.2f (%.1f ms @ 1/4 120 BPM) modIndex=%.3f lat=%d\n",
                 phaseErrDeg, phaseErrMs, modDepth, lat);

    REQUIRE (modDepth > 0.2);                  // la envolvente late de verdad (sin falso verde)
    REQUIRE (std::abs (phaseErrDeg) <= 5.0);   // GATE: el abanico cae EN el beat (±5° ≈ ±7 ms)
}
