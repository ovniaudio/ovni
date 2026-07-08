#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [measure][aurora] — la imagen estéreo MEDIDA del despliegue espectral (MEDÍ,
// NO ADIVINES): ruido rosa mono por el AuroraProcessor REAL → CORR/WIDTH/BAL/
// MONOSUM (+ IACC= en el caso canónico, lo grepea measure-check.sh).
//
// Lo que estos números deben probar (curaduría + spec §7):
//   · SPREAD 0 → mono (WIDTH≈0, CORR≈+1). SPREAD sube → WIDTH sube y CORR baja.
//   · MONOSUM nunca colapsa: el reparto por POTENCIA pierde como mucho 3 dB por
//     bin al monoficar (jamás cancela — eso es lo que el Haas/mid-side NO da).
//   · MONO SAFE (knob propio) amarra los graves al centro: el side de baja
//     frecuencia cae al subir el knob (con TILT invertido, que es cuando hay
//     graves en los bordes que proteger).
//   · IN PHASE (chasis) colapsa el ancho de graves aunque la red propia esté a 0.
//   · TILT honesto: mover el knob REUBICA el espectro de forma medible (graves-
//     centro ↔ graves-bordes), no es decorativo.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = aurora::params::id;

struct Img
{
    StereoImage img;          // CORR / WIDTH / BAL / MONOSUM / RMS (medidor del sello)
    double lowSideRms  = 0.0; // RMS del SIDE de GRAVES (2× one-pole LP @80 Hz): ancho abajo
    double highSideRms = 0.0; // RMS del SIDE de AGUDOS (side − one-pole LP @1.5 kHz): ancho arriba
};

// Corre ruido rosa mono-duplicado por el processor REAL con la config dada y mide.
// inPhase = el toggle "monoSafe" del CHASIS (IN PHASE), NO el knob Mono Safe propio.
Img measure (float spread01, float tilt01, float monoSafeAmt01, bool inPhase)
{
    aurora::AuroraProcessor proc;
    const double SR = 48000.0; const int N = 512;

    setParam (proc, pid::SPREAD,      spread01);
    setParam (proc, pid::TILT,        tilt01);        // normalizado: 0=-100 · 0.5=0 · 1=+100
    setParam (proc, pid::MONOSAFEAMT, monoSafeAmt01);
    setParam (proc, pid::MIX,         1.0f);          // wet pleno (el caso canónico de imagen)
    setParam (proc, "monoSafe",       inPhase ? 1.0f : 0.0f);   // IN PHASE del chasis

    proc.prepareToPlay (SR, N);

    Pink pink;
    StereoImageMeter meter;
    // Split de bandas del SIDE: graves = 2× one-pole @80 Hz (aísla DE VERDAD lo de abajo,
    // un solo polo a 150 deja pasar medios y miente); agudos = side − one-pole @1.5 kHz.
    double lo1 = 0.0, lo2 = 0.0, mid1 = 0.0;
    const double loCoef  = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 80.0   / SR);
    const double midCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 1500.0 / SR);
    double sLow = 0.0, sHigh = 0.0; long cnt = 0;

    for (int blk = 0; blk < 600; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 100) continue;   // warmup: latencia OLA (4 bloques) + suavizadores
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        meter.addBlock (L, R, N);
        for (int n = 0; n < N; ++n)
        {
            const double side = 0.5 * ((double) L[n] - (double) R[n]);
            lo1  += loCoef  * (side - lo1);
            lo2  += loCoef  * (lo1  - lo2);            // graves del side (2 polos @80)
            mid1 += midCoef * (side - mid1);
            const double high = side - mid1;           // agudos del side (> ~1.5 kHz)
            sLow += lo2 * lo2; sHigh += high * high; ++cnt;
        }
    }

    Img out;
    out.img         = meter.finish();
    out.lowSideRms  = std::sqrt (sLow  / (double) juce::jmax (1L, cnt));
    out.highSideRms = std::sqrt (sHigh / (double) juce::jmax (1L, cnt));
    return out;
}
} // namespace

// ── 1) SPREAD sweep (caso canónico, tilt 0, Mono Safe default 50) ───────────────────────
TEST_CASE ("AURORA SPREAD sweep: 0=mono, 100=desplegado, mono-sum NUNCA colapsa", "[measure][aurora]")
{
    const Img m0   = measure (0.0f, 0.5f, 0.5f, false);
    const Img m50  = measure (0.5f, 0.5f, 0.5f, false);
    const Img m100 = measure (1.0f, 0.5f, 0.5f, false);

    std::printf ("MEASURE[AURORA SPREAD=0]   CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m0.img.corr, m0.img.width, m0.img.balDb, m0.img.monoSumDb);
    std::printf ("MEASURE[AURORA SPREAD=50]  CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m50.img.corr, m50.img.width, m50.img.balDb, m50.img.monoSumDb);
    std::printf ("MEASURE[AURORA SPREAD=100] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 m100.img.corr, m100.img.width, m100.img.balDb, m100.img.monoSumDb);
    std::printf ("IACC=%.3f\n", m100.img.corr);   // <- caso canónico (lo grepea measure-check.sh)

    REQUIRE (std::isfinite (m100.img.corr));
    REQUIRE (m0.img.rms > 1e-5);
    REQUIRE (m100.img.rms > 1e-5);

    // SPREAD 0 = mono real (todos los bins al centro, gL=gR=1).
    REQUIRE (m0.img.width < 0.05);
    REQUIRE (m0.img.corr  > 0.95);

    // SPREAD audible y monótono: el knob hace lo que dice (honestidad).
    REQUIRE (m50.img.width  > m0.img.width + 0.05);
    REQUIRE (m100.img.width > m50.img.width + 0.05);
    REQUIRE (m100.img.corr  < m0.img.corr - 0.1);

    // ── GATES DE AUDIBILIDAD (ENSANCHADOR REAL de fase + FIR, 2026-06-10) ──
    // Antes estos pasaban con WIDTH ~0.09 / CORR ~0.99 = FALSO VERDE (Joaquín lo oyó "no hace
    // nada"). El fix de la causa raíz (decorrelación de fase: abanico de fase del STFT en el
    // aire + FIR disperso M/S en los low-mids) abre de VERDAD. Banda ancha (ruido rosa, processor
    // REAL): SPREAD 100 → W 0.90 / CORR +0.10; SPREAD 50 → W 0.49 / CORR +0.62. Margen ~15 %.
    REQUIRE (m100.img.width > 0.70);    // SPREAD 100 abre DRAMÁTICO (medido 0.90)
    REQUIRE (m100.img.corr  < 0.40);    // …y decorrelaciona fuerte (medido +0.10; target del fix ≤0.5)
    REQUIRE (m50.img.width  > 0.30);    // a mitad de knob ya se NOTA claramente (medido 0.49) — "carga sonando"

    // Balance L/R ≈ 0 (el side random-por-bin reparte por construcción; nadie carga un lado).
    REQUIRE (std::abs (m100.img.balDb) < 2.5);

    // Mono-sum: TRADE-OFF HONESTO DECLARADO del widener (como cualquier ensanchador de imagen):
    // a SPREAD 100 la suma mono cae unos dB (medido −3.2 dB en banda ancha; sobre material REAL
    // concentrado es ≈ −2.8 dB, RealBeatTest). NUNCA cancela (≫ −∞) y IN PHASE lo recupera a ~0 dB
    // (escape mono-safe, medido abajo). El reparto del side es PURO (L+R = 2·mid en el FIR) → la
    // pérdida viene del abanico de fase del STFT, acotada y declarada.
    REQUIRE (m100.img.monoSumDb > -4.5);
}

// ── 2) MONO SAFE (knob propio) + IN PHASE (chasis): los graves se amarran al centro ─────
//    Caso adversario: TILT invertido (−100, graves a los BORDES) + Mono Safe 0 = graves
//    anchos a propósito. El knob (60→700 Hz) y el IN PHASE deben recogerlos — MEDIDO.
TEST_CASE ("AURORA MONO SAFE / IN PHASE: el ancho de graves colapsa al centro", "[measure][aurora]")
{
    const Img open    = measure (1.0f, 0.0f, 0.0f, false);   // tilt -100, sin red: graves ANCHOS
    const Img safe100 = measure (1.0f, 0.0f, 1.0f, false);   // knob Mono Safe 100 (corte 700 Hz)
    const Img inPhase = measure (1.0f, 0.0f, 0.0f, true);    // red 0 pero IN PHASE del chasis ON

    std::printf ("MEASURE[AURORA MSAFE=0]    lowSide=%.5f  highSide=%.5f  MONOSUM_dB=%+.2f\n",
                 open.lowSideRms, open.highSideRms, open.img.monoSumDb);
    std::printf ("MEASURE[AURORA MSAFE=100]  lowSide=%.5f  highSide=%.5f  MONOSUM_dB=%+.2f\n",
                 safe100.lowSideRms, safe100.highSideRms, safe100.img.monoSumDb);
    std::printf ("MEASURE[AURORA INPHASE]    lowSide=%.5f  highSide=%.5f  MONOSUM_dB=%+.2f\n",
                 inPhase.lowSideRms, inPhase.highSideRms, inPhase.img.monoSumDb);

    REQUIRE (open.lowSideRms > 1e-5);   // el caso adversario efectivamente abre los graves

    // El knob Mono Safe recoge los graves: sube el corte del HP del side del ensanchador
    // (50→400 Hz) + el colapso del weave del STFT → el side bajo cae (medido ~0.58× = ~−4.7 dB
    // de side bajo a Mono Safe 100). Es protección progresiva del low-mid; el colapso DURO total
    // es del IN PHASE (abajo). Gate: reducción clara del side bajo.
    REQUIRE (safe100.lowSideRms < 0.75 * open.lowSideRms);

    // IN PHASE (chasis): el motor COLAPSA su ensanchador (depth → 0) + el bass-mono genérico del
    // chasis → escape mono-safe DURO. El side bajo se desploma (medido ~0.07× = −23 dB). Es el
    // recovery mono-compatible: CORR vuelve ~al dry y la suma mono ~0 dB ([realbeat] lo mide).
    REQUIRE (inPhase.lowSideRms < 0.30 * open.lowSideRms);

    // Y ninguna de las dos redes mata los agudos desplegados (sólo amarra los graves).
    REQUIRE (safe100.highSideRms > 0.5 * open.highSideRms);
}

// ── 3) TILT honesto: inclina el CARÁCTER espectral del side (graves ↔ aire) ──────────────
// Con el ENSANCHADOR REAL (FIR + fase) cargando el ancho de banda ancha, TILT es ahora un
// control de CARÁCTER del despliegue del STFT (más sutil que el viejo paneo de magnitud-único
// que mentía "no hace nada" sobre música real, pero HONESTO): +100 inclina el side hacia el
// AIRE, −100 hacia los GRAVES. Medible y ordenado de punta a punta.
TEST_CASE ("AURORA TILT: inclina el carácter espectral del side (graves <-> aire)", "[measure][aurora]")
{
    const Img t0   = measure (1.0f, 0.5f, 0.0f, false);   // tilt 0
    const Img tNeg = measure (1.0f, 0.0f, 0.0f, false);   // tilt -100 (carácter grave)
    const Img tPos = measure (1.0f, 1.0f, 0.0f, false);   // tilt +100 (carácter aire)

    std::printf ("MEASURE[AURORA TILT=0]    lowSide=%.5f  highSide=%.5f  WIDTH=%.3f\n",
                 t0.lowSideRms, t0.highSideRms, t0.img.width);
    std::printf ("MEASURE[AURORA TILT=-100] lowSide=%.5f  highSide=%.5f  WIDTH=%.3f\n",
                 tNeg.lowSideRms, tNeg.highSideRms, tNeg.img.width);
    std::printf ("MEASURE[AURORA TILT=+100] lowSide=%.5f  highSide=%.5f  WIDTH=%.3f\n",
                 tPos.lowSideRms, tPos.highSideRms, tPos.img.width);

    // El balance graves↔aire del side se INCLINA con TILT: −100 pesa más en graves que +100.
    // (ratio low/high cae al subir TILT = el carácter se va al aire). Honesto y medible.
    REQUIRE ((tNeg.lowSideRms / tNeg.highSideRms) > (tPos.lowSideRms / tPos.highSideRms));

    // +100 abre el aire del side más que 0 (el knob hace lo que dice arriba).
    REQUIRE (tPos.highSideRms > t0.highSideRms);

    // El despliegue sigue ABIERTO en todo el recorrido de TILT (el FIR carga el ancho): nunca
    // colapsa a mono por mover TILT (medido WIDTH ~0.9 en las tres posiciones).
    REQUIRE (tNeg.img.width > 0.6);
    REQUIRE (tPos.img.width > 0.6);
}
