#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [audible][horizon] — GATE de AUDIBILIDAD del FREEZE (etapa 2: ahora con REQUIRE).
// Mide wet-vs-dry ALINEADO POR LATENCIA sobre el HorizonProcessor REAL con tres
// materiales y varias configs, y HACE REQUIRE del comportamiento audible que el oído
// de Joaquín exige: el freeze sale a NIVEL PLENO, queda QUIETO (no late), y el SPREAD
// ABRE de verdad. CLAVE: el FREEZE se aprieta DESPUÉS de llenar el FIFO del STFT con
// señal estable (como en Ableton) — apretarlo antes congelaba el FIFO con ceros del
// warmup = frame basura −13..−26 dB (el falso "no hace nada" de la etapa 1).
//
//   · MATERIAL A: cama de firma (3 parciales graves 220/261/329 Hz + 6% rosa +
//     beep 660) — el material sparse/grave que defeatea la decorrelación espectral.
//   · MATERIAL B: BANDA ANCHA (ruido rosa) — muchos bins con energía.
//   · TONO bin-alineado (1007.8 Hz) — estacionario puro: prueba reconstrucción.
//
// Para cada caso reporta:
//   CORR    = correlación L/R del WET (banda ancha; ↓ = abre)
//   WIDTH   = RMS(side)/RMS(mid) del WET
//   dRMS_dB = 20log10( RMS(wet−dryAlineado) / RMS(dryAlineado) )  → cuánto difiere
//   wet_dB  = 20log10( RMS(wet) / RMS(dryAlineado) )              → nivel del wet
//   mod_dB  = modulación pico-a-valle del wet por-hop → un freeze QUIETO ≈ 0 dB
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;

// — Generadores de material (mono, mismo en L/R; el plugin abre desde mono) —
struct SigBed   // cama de firma (igual a RenderTest::fillBed pero estacionaria, sin fades)
{
    std::uint32_t rng = 0x1234567u;
    float pink = 0.0f;
    float at (long g)
    {
        const double t = (double) g / kSR;
        const double vib = 1.0 + 0.004 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * t);
        double s = 0.0;
        for (double f : { 220.0, 261.63, 329.63 })
            s += std::sin (juce::MathConstants<double>::twoPi * f * vib * t);
        s *= 0.18;
        rng = rng * 1664525u + 1013904223u;
        const float noise = (float) ((int32_t) rng) / 2.147483648e9f;
        pink = 0.98f * pink + 0.02f * noise;
        s += 0.06 * pink;
        const double ph = std::fmod (t, 0.5);
        const double env = ph < 0.002 ? ph / 0.002 : std::exp (-(ph - 0.002) / 0.08);
        s += 0.22 * env * std::sin (juce::MathConstants<double>::twoPi * 660.0 * t);
        return (float) (s * 0.8);
    }
};

// Pink de OvniTestHarness expone next() (no at()); lo envolvemos para una interfaz uniforme.
struct PinkBed
{
    Pink p;
    float at (long) { return p.next(); }
};

// Seno BIN-ALINEADO (bin 43 @ N=2048/48k = 1007.8125 Hz): señal estacionaria perfecta.
// Si el freeze de ESTO también cae de nivel, la pérdida es del OLA del frame estático
// (bug de normalización), no de "congelar ruido fluctuante".
struct ToneBed
{
    float at (long g)
    {
        const double f = 43.0 * kSR / 2048.0;
        return 0.7f * (float) std::sin (juce::MathConstants<double>::twoPi * f * (double) g / kSR);
    }
};

struct Result
{
    double corr = 0, width = 0, dRmsDb = 0, wetDb = 0, dryRms = 0, wetRms = 0;
    double modDb = 0;   // modulación pico-a-valle del wet por-hop (un freeze QUIETO ≈ 0 dB)
};

// Corre un material por el processor con la config dada, captura wet (L/R) y el dry
// MONO crudo; alinea el dry por latencySamples() y mide.
template <typename Mat>
Result run (const char* label, float spread01, float whisper01, bool freeze, float mix01,
            float rate01, bool whichDefault)
{
    horizon::HorizonProcessor proc;
    // Config de macros SIN tocar FREEZE todavía: el freeze se aprieta DESPUÉS del warmup,
    // con el FIFO del STFT lleno de señal estable — EXACTAMENTE como Joaquín lo aprieta en
    // Ableton durante la reproducción (la captura sobre un FIFO con ceros del warmup es un
    // artefacto de test que falsea el frame congelado). "Medí en condiciones reales".
    if (! whichDefault)
    {
        setParam (proc, pid::WHISPER, whisper01);
        setParam (proc, pid::SPREAD,  spread01);
        setParam (proc, pid::DUCK,    0.0f);
        setParam (proc, pid::MIX,     mix01);
        setParam (proc, pid::RATE,    rate01);
        setParam (proc, pid::RATESYNC, 0.0f);
    }
    proc.prepareToPlay (kSR, kBlk);
    const int lat = proc.getLatencySamples();

    Mat mat;
    StereoImageMeter meter;
    std::vector<float> dryMono, wetL, wetR;
    // warmFifo: llenar el FIFO con señal estable ANTES de congelar (FIFO lleno).
    // settle: dejar asentar el cross-fade del freeze antes de medir.
    const int warmFifo = 80, settle = 60, meas = 300;
    const int freezeAt = warmFifo;
    const int measAt   = warmFifo + settle;
    long g = 0;
    for (int blk = 0; blk < measAt + meas; ++blk)
    {
        if (blk == freezeAt)
            setParam (proc, pid::FREEZE, freeze ? 1.0f : 0.0f);   // APRETAR FREEZE con FIFO lleno

        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i) { const float x = mat.at (g + i); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        // guardar dry mono ANTES de procesar
        if (blk >= measAt) for (int i = 0; i < kBlk; ++i) dryMono.push_back (buf.getSample (0, i));
        proc.processBlock (buf, midi);
        if (blk >= measAt)
        {
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            meter.addBlock (L, R, kBlk);
            for (int i = 0; i < kBlk; ++i) { wetL.push_back (L[i]); wetR.push_back (R[i]); }
        }
        g += kBlk;
    }

    // Alinear dry por latencia: wet[n] corresponde a dry[n-lat]. Comparamos en la ventana válida.
    Result out;
    const auto im = meter.finish();
    out.corr = im.corr; out.width = im.width;

    double accDiff = 0, accDry = 0, accWet = 0; long cnt = 0;
    const int N = (int) wetL.size();
    for (int n = lat; n < N; ++n)
    {
        const double d = dryMono[(size_t) (n - lat)];
        const double wMid = 0.5 * ((double) wetL[(size_t) n] + (double) wetR[(size_t) n]);
        accDiff += (wMid - d) * (wMid - d);
        accDry  += d * d;
        accWet  += wMid * wMid;
        ++cnt;
    }
    out.dryRms = std::sqrt (accDry / (double) juce::jmax (1L, cnt));
    out.wetRms = std::sqrt (accWet / (double) juce::jmax (1L, cnt));
    out.dRmsDb = 20.0 * std::log10 (std::sqrt (accDiff / (double) juce::jmax (1L, cnt)) / (out.dryRms + 1e-12));
    out.wetDb  = 20.0 * std::log10 (out.wetRms / (out.dryRms + 1e-12));

    // MODULACIÓN del freeze: RMS del wet (mid) por bloque de hop; el freeze QUIETO no late
    // (min≈max → 0 dB). El bug viejo (batido inter-bin del OLA) daba 10–17 dB de pulso. Se
    // mide sobre la mitad final (ya asentado) para no contar el cross-fade de entrada.
    {
        const int hopBlk = (int) kBlk;   // = hop @512 → un valor por hop
        double mn = 1e30, mx = 0.0;
        const int start = N / 2;         // segunda mitad: freeze estable
        for (int p = start; p + hopBlk <= N; p += hopBlk)
        {
            double a = 0.0;
            for (int i = 0; i < hopBlk; ++i)
            {
                const double wMid = 0.5 * ((double) wetL[(size_t) (p + i)] + (double) wetR[(size_t) (p + i)]);
                a += wMid * wMid;
            }
            const double r = std::sqrt (a / hopBlk);
            mn = juce::jmin (mn, r); mx = juce::jmax (mx, r);
        }
        out.modDb = 20.0 * std::log10 ((mx + 1e-12) / (mn + 1e-12));
    }

    std::printf ("AUDIBLE[%-22s] CORR=%+.3f WIDTH=%.3f dRMS=%+.1fdB wet=%+.1fdB mod=%.1fdB (dry=%.4f wet=%.4f)\n",
                 label, out.corr, out.width, out.dRmsDb, out.wetDb, out.modDb, out.dryRms, out.wetRms);
    return out;
}
} // namespace

// =============================================================================
// GATE de AUDIBILIDAD (no instrumento). El oído de Joaquín dijo "no hace nada"; la
// causa medida fue (1) un artefacto de test que congelaba sobre el FIFO con ceros del
// warmup → frame basura −13..−26 dB, y (2) el freeze REAL latía 10–17 dB por batido
// inter-bin del OLA (identity locking incompleto) → sonaba raro/inútil. Tras el fix
// (region/identity phase locking + captura con FIFO lleno + techo de spread más alto)
// estos REQUIRE clavan el comportamiento audible para que el falso verde NO vuelva.
// =============================================================================
TEST_CASE ("HORIZON audible: freeze a nivel pleno, quieto, y SPREAD que abre de verdad", "[audible][horizon]")
{
    std::printf ("\n=== MATERIAL A: cama de FIRMA (sparse/grave) ===\n");
    const auto firmaDef   = run<SigBed> ("FIRMA default+freeze",  0.5f, 0.12f, true, 1.0f, 0.0f, true);
    const auto firmaSp100 = run<SigBed> ("FIRMA spread100 mix100",1.0f, 0.0f,  true, 1.0f, 0.0f, false);
    run<SigBed> ("FIRMA spread100 whis100",1.0f, 1.0f, true, 1.0f, 0.0f, false);

    std::printf ("\n=== MATERIAL B: BANDA ANCHA (ruido rosa) ===\n");
    const auto anchaDef   = run<PinkBed> ("ANCHA default+freeze",   0.5f, 0.12f, true, 1.0f, 0.0f, true);
    run<PinkBed> ("ANCHA spread50 mix100",  0.5f, 0.0f,  true, 1.0f, 0.0f, false);
    const auto anchaSp100 = run<PinkBed> ("ANCHA spread100 mix100", 1.0f, 0.0f,  true, 1.0f, 0.0f, false);
    run<PinkBed> ("ANCHA spread100 whis100",1.0f, 1.0f,  true, 1.0f, 0.0f, false);

    std::printf ("\n=== NIVEL del FREEZE base (spread 0, whisper 0, mix 100) ===\n");
    const auto firmaBase = run<SigBed>  ("FIRMA freeze spread0",  0.0f, 0.0f, true, 1.0f, 0.0f, false);
    const auto anchaBase = run<PinkBed> ("ANCHA freeze spread0",  0.0f, 0.0f, true, 1.0f, 0.0f, false);
    const auto toneBase  = run<ToneBed> ("TONO  freeze spread0",  0.0f, 0.0f, true, 1.0f, 0.0f, false);  // estacionario puro

    std::printf ("\n=== SIN FREEZE (espacializador VIVO: NO identity — CORR baja, WIDTH sube) ===\n");
    const auto noFreeze = run<PinkBed> ("ANCHA nofreeze default", 0.5f, 0.12f, false, 1.0f, 0.0f, true);

    // --- (1) FREEZE A NIVEL PLENO: el congelado base se oye al MISMO nivel que el dry ---
    //   Un estacionario puro debe reconstruir a ~0 dB (era −24.4 dB). Cama/ruido: el freeze
    //   pierde ~1–2 dB de fase-vocoder honesto, no 13–19 dB. Techo generoso (≤4 dB) para
    //   cubrir el material no-perfectamente-estacionario sin volver al falso verde.
    REQUIRE (toneBase.wetDb  > -1.5);   // TONO estacionario: prácticamente 0 dB (reconstrucción perfecta)
    REQUIRE (firmaBase.wetDb > -4.0);   // cama de firma congelada: a nivel
    REQUIRE (anchaBase.wetDb > -4.0);   // banda ancha congelada: a nivel

    // --- (2) FREEZE QUIETO: el congelado NO late (el batido del OLA viejo daba 10–17 dB) ---
    REQUIRE (toneBase.modDb  < 1.5);    // tono estacionario: rock-steady
    REQUIRE (firmaBase.modDb < 4.0);    // cama de firma: prácticamente plano
    REQUIRE (anchaBase.modDb < 4.0);    // banda ancha: prácticamente plano

    // --- (3) SPREAD ABRE DE VERDAD: a spread 100 la imagen se abre (CORR cae claramente) ---
    //   Objetivo del ICP: CORR ≤ 0.6 (idealmente ≤ 0.0). Banda ancha y cama de firma. Con el techo
    //   ±0.6π honesto (WIDTH monótona) mide ANCHA +0.33 / FIRMA −0.12 → margen al objetivo.
    REQUIRE (anchaSp100.corr < 0.50);   // banda ancha spread100: abierto (mide ~+0.33)
    REQUIRE (anchaSp100.width > 0.65);
    REQUIRE (firmaSp100.corr < 0.20);   // cama de firma spread100: abierto (mide ~−0.12)
    REQUIRE (firmaSp100.width > 0.90);

    // --- (4) DEFAULT (spread 50) YA SE NOTA: al congelar en default la imagen abre (no mono) ---
    //   Mono perfecto = CORR +1.0; default+freeze mide ANCHA ~+0.80 / FIRMA ~+0.61 → claramente
    //   despegado de mono (el ancho se OYE), sin forzar un default extremo (curaduría: spread 50).
    REQUIRE (anchaDef.corr < 0.90);     // default+freeze banda ancha (mide ~+0.80)
    REQUIRE (firmaDef.corr < 0.75);     // default+freeze cama de firma (mide ~+0.61)

    // --- (5) SIN FREEZE = ESPACIALIZADOR VIVO (NO identity): el wet vivo ya ABRE al cargar ---
    //   fix audible 2026-06-10: SPREAD+WHISPER actúan sobre el espectro VIVO del mid, no sólo en
    //   el frame congelado. Con FREEZE OFF + default (spread 50) la salida NO es el dry alineado:
    //   se des-correlaciona (CORR cae) y ensancha (WIDTH sube) → "carga sonando". El null perfecto
    //   de antes (dRMS ~−137 dB) era el bug que dejaba la salida mono con MIX 100.
    REQUIRE (noFreeze.dRmsDb > -30.0);  // ya NO es identity (mide ~−17 dB; identity sería ~−137 dB)
    REQUIRE (noFreeze.corr   < 0.90);   // des-correlación viva (mide ~+0.77 vs +1.0 del dry mono-pisado)
    REQUIRE (noFreeze.width  > 0.10);   // ancho vivo (mide ~+0.36): el goniómetro abre sin congelar
}
