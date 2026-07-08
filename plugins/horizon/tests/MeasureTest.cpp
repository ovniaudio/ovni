#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [measure][horizon] — la imagen estéreo MEDIDA del freeze esparcido (MEDÍ, NO
// ADIVINES): ruido rosa CONGELADO por el HorizonProcessor REAL → CORR/WIDTH/BAL/
// MONOSUM (+ IACC= en el caso canónico, lo grepea measure-check.sh).
//
// MECANISMO ENCENDIDO (regla de oro): FREEZE on, RATE off (sostenido = wet continuo),
// MIX 100. Lo que estos números deben probar (curaduría + spec §7):
//   · SPREAD 0 → mono (WIDTH≈0, CORR≈+1: offset de fase L/R = 0 → L==R).
//   · SPREAD sube → WIDTH sube y CORR baja (des-correlación de fase por bin: el
//     freeze se abre al campo).
//   · MONOSUM nunca colapsa destructivamente (el ancho es de FASE con magnitud
//     idéntica por canal → la suma mono pierde, pero no cancela a −∞).
//   · IN PHASE (chasis) colapsa el ancho de graves (bass-mono genérico).
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

struct Img
{
    StereoImage img;          // CORR / WIDTH / BAL / MONOSUM / RMS (medidor del sello)
    double lowSideRms  = 0.0; // RMS del SIDE de GRAVES (2× one-pole LP @80 Hz)
};

// Corre ruido rosa CONGELADO por el processor REAL con la config dada y mide.
Img measure (float spread01, bool inPhase)
{
    horizon::HorizonProcessor proc;
    const double SR = 48000.0; const int N = 512;

    setParam (proc, pid::WHISPER, 0.0f);          // vidrioso (fase coherente; el ancho es del SPREAD)
    setParam (proc, pid::SPREAD,  spread01);
    setParam (proc, pid::MIX,     1.0f);          // wet pleno (el caso canónico de imagen)
    setParam (proc, "monoSafe",   inPhase ? 1.0f : 0.0f);   // IN PHASE del chasis

    proc.prepareToPlay (SR, N);

    Pink pink;
    StereoImageMeter meter;
    double lo1 = 0.0, lo2 = 0.0;
    const double loCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 80.0 / SR);
    double sLow = 0.0; long cnt = 0;

    // FREEZE se aprieta DESPUÉS de llenar el FIFO con ruido rosa estable (block 80): captura el
    // frame REAL, no el FIFO con ceros del warmup — esa captura prematura daba un frame basura y
    // medía una imagen falsa (el falso verde WIDTH 0.09 del diagnóstico). Joaquín lo aprieta
    // durante la reproducción. Luego: latencia OLA + cross-fade del freeze + suavizadores asientan.
    constexpr int kFreezeAt = 80;
    for (int blk = 0; blk < 600; ++blk)
    {
        if (blk == kFreezeAt) setParam (proc, pid::FREEZE, 1.0f);   // CONGELAR con FIFO lleno
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 160) continue;   // warmup: captura (block 80) + cross-fade + latencia
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        meter.addBlock (L, R, N);
        for (int n = 0; n < N; ++n)
        {
            const double side = 0.5 * ((double) L[n] - (double) R[n]);
            lo1 += loCoef * (side - lo1);
            lo2 += loCoef * (lo1  - lo2);
            sLow += lo2 * lo2; ++cnt;
        }
    }

    Img out;
    out.img        = meter.finish();
    out.lowSideRms = std::sqrt (sLow / (double) juce::jmax (1L, cnt));
    return out;
}
} // namespace

// ── 1) SPREAD sweep (caso canónico) ─────────────────────────────────────────────────────
TEST_CASE ("HORIZON SPREAD sweep: 0=mono, 100=esparcido (freeze congelado)", "[measure][horizon]")
{
    const Img m0   = measure (0.0f, false);
    const Img m50  = measure (0.5f, false);
    const Img m100 = measure (1.0f, false);

    std::printf ("MEASURE[HORIZON SPREAD=0]   CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m0.img.corr, m0.img.width, m0.img.balDb, m0.img.monoSumDb);
    std::printf ("MEASURE[HORIZON SPREAD=50]  CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m50.img.corr, m50.img.width, m50.img.balDb, m50.img.monoSumDb);
    std::printf ("MEASURE[HORIZON SPREAD=100] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m100.img.corr, m100.img.width, m100.img.balDb, m100.img.monoSumDb);
    std::printf ("IACC=%.3f\n", m100.img.corr);   // <- caso canónico (lo grepea measure-check.sh)

    REQUIRE (std::isfinite (m100.img.corr));
    REQUIRE (m0.img.rms > 1e-5);
    REQUIRE (m100.img.rms > 1e-5);

    // SPREAD 0 = mono real (offset de fase L/R = 0 → L == R, CORR ≈ +1, WIDTH ≈ 0).
    REQUIRE (m0.img.width < 0.10);
    REQUIRE (m0.img.corr  > 0.92);

    // SPREAD audible y monótono: el knob ABRE de verdad (honestidad — la curaduría: "spread abre").
    // UMBRALES APRETADOS al comportamiento REAL tras el fix (antes el falso verde pasaba con WIDTH
    // 0.09 a spread100; ahora exigimos el ancho audible que se mide: spread100 WIDTH>0.6, CORR<0.5,
    // y default spread50 ya claramente abierto). Si la imagen vuelve a colapsar, ESTO lo caza.
    REQUIRE (m50.img.width  > 0.20);                  // default ya abre (mide ~0.33)
    REQUIRE (m100.img.width > m50.img.width + 0.20);  // y spread100 abre MUCHO más (mide ~0.71)
    REQUIRE (m100.img.width > 0.60);                  // ancho audible real a spread100 (mata el WIDTH 0.09)
    REQUIRE (m100.img.corr  < 0.50);                  // CORR cae claramente (mide ~+0.33; objetivo ICP ≤0.6)

    // Balance L/R ≈ 0 (el offset de fase es simétrico ±: nadie carga un lado).
    REQUIRE (std::abs (m100.img.balDb) < 1.5);
}

// ── 2) IN PHASE (chasis): mono-safe colapsa el ancho de graves ──────────────────────────
TEST_CASE ("HORIZON IN PHASE: el ancho de graves del freeze colapsa al centro", "[measure][horizon]")
{
    const Img open    = measure (1.0f, false);   // spread 100, sin red: graves anchos
    const Img inPhase = measure (1.0f, true);    // IN PHASE del chasis ON

    std::printf ("MEASURE[HORIZON OPEN]     lowSide=%.5f  WIDTH=%.3f  MONOSUM_dB=%+.2f\n",
                 open.lowSideRms, open.img.width, open.img.monoSumDb);
    std::printf ("MEASURE[HORIZON INPHASE]  lowSide=%.5f  WIDTH=%.3f  MONOSUM_dB=%+.2f\n",
                 inPhase.lowSideRms, inPhase.img.width, inPhase.img.monoSumDb);

    REQUIRE (open.lowSideRms > 1e-5);   // el spread efectivamente abre los graves

    // IN PHASE (bass-mono del chasis, one-pole ~120 Hz): recoge el side bajo (≥ ~2.5 dB).
    REQUIRE (inPhase.lowSideRms < 0.75 * open.lowSideRms);
}
