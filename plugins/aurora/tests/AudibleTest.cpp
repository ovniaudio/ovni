#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [audible][aurora] — REGRESIÓN PERMANENTE de audibilidad en CONDICIONES REALES
// (qa-listening-checklist §2: medí como se oye, no como conviene). Mide CORR/WIDTH
// del despliegue espectral con:
//   · SEÑAL DE BANDA ANCHA (ruido rosa + agudos) — la decorrelación espectral
//     necesita muchos bins con energía; un acorde grave/sparse la defeatea sola
//     (eso NO es bug del plugin, es física del material) y MENTIRÍA verde.
//   · DRY ALINEADO POR LATENCIA (N=2048 @48k) — el wet sale N tarde; sin alinear
//     el RMS(wet−dry) se infla y el CORR/WIDTH se miden contra el dry equivocado.
//
// Este test nació del bug real "AURORA no hace nada" (Joaquín, por oído, 2026-06-10):
// los [measure] laxos pasaban con WIDTH 0.09 / CORR +0.99 = FALSO VERDE. El fix de
// la causa raíz (drive de excursión band-limitado al cuadrado, AuroraEngine.cpp)
// lleva el DEFAULT a CORR +0.92→+0.61 y SPREAD 100 a +0.62→+0.41. Acá clavamos esos
// targets: si una regresión vuelve a aplanar el efecto, FALLA (no vuelve a pasar
// inadvertido). Comparable/mejor que DUST (la referencia "audible": CORR→0.925).
// =============================================================================

namespace
{
namespace pid = aurora::params::id;

struct Img { double corr = 0.0, width = 0.0, rmsDeltaOverDry = 0.0; };

// Cama de banda ancha: pink + transientes agudos (3k/8k) → bins en TODO el espectro.
void fillBroadband (std::vector<float>& mono, double SR)
{
    const int n = (int) mono.size();
    uint32_t rng = 0x2bd6a7u;
    auto noise = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((int32_t) rng) / 2.147483648e9f; };
    float pink = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / SR;
        pink = 0.96f * pink + 0.04f * noise();
        double s = 0.9 * pink;
        s += 0.15 * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * t);
        s += 0.10 * std::sin (juce::MathConstants<double>::twoPi * 8000.0 * t);
        double g = 1.0; const double fade = 0.05, dur = (double) n / SR;
        if (t < fade) g = t / fade; else if (t > dur - fade) g = (dur - t) / fade;
        mono[(size_t) i] = (float) (s * g * 0.7);
    }
}

// CORR / WIDTH del WET + RMS(wet−dry)/RMS(dry) con el dry RETRASADO `lat` (alineado).
Img measureAligned (const std::vector<float>& wL, const std::vector<float>& wR,
                    const std::vector<float>& dMono, int lat)
{
    const int n = (int) wL.size();
    const int a = (int) (n * 0.25), b = (int) (n * 0.85);   // descartar warmup/fade
    double mL = 0, mR = 0; for (int i = a; i < b; ++i) { mL += wL[i]; mR += wR[i]; } mL /= (b - a); mR /= (b - a);
    double num = 0, sL = 0, sR = 0, mid = 0, side = 0;
    for (int i = a; i < b; ++i)
    {
        const double l = wL[i] - mL, r = wR[i] - mR; num += l * r; sL += l * l; sR += r * r;
        const double mm = (wL[i] + wR[i]) * 0.5, ss = (wL[i] - wR[i]) * 0.5; mid += mm * mm; side += ss * ss;
    }
    Img im;
    im.corr  = (sL * sR > 0) ? num / std::sqrt (sL * sR) : 0.0;
    im.width = (mid > 0) ? std::sqrt (side / mid) : 0.0;
    double dd = 0, dr = 0;
    for (int i = a; i < b; ++i) { const double diff = wL[i] - dMono[i - lat]; dd += diff * diff; dr += (double) dMono[i - lat] * dMono[i - lat]; }
    im.rmsDeltaOverDry = (dr > 0) ? std::sqrt (dd / dr) : 0.0;
    return im;
}

// Corre la cama de banda ancha (mono duplicado) por el AuroraProcessor REAL y mide alineado.
Img run (float spread, float tilt, float mix, float msafe)
{
    const double SR = 48000.0; const int N = 512; const int total = (int) (SR * 6.0);
    std::vector<float> mono (total), wL (total), wR (total);
    fillBroadband (mono, SR);
    wL = mono; wR = mono;

    aurora::AuroraProcessor proc;
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    set (pid::SPREAD, spread); set (pid::TILT, tilt); set (pid::MIX, mix); set (pid::MONOSAFEAMT, msafe);
    set (pid::MOTION, 0.0f);   // mide el ANCHO (side) del SPREAD a abanico ABIERTO: el MOTION (fan LFO + barrido espectral) es otro eje y confunde el promedio (default del plugin ahora >0)
    proc.prepareToPlay (SR, N);

    for (int off = 0; off < total; off += N)
    {
        const int len = juce::jmin (N, total - off);
        juce::AudioBuffer<float> blk (2, len); juce::MidiBuffer m;
        for (int i = 0; i < len; ++i) { blk.setSample (0, i, wL[off + i]); blk.setSample (1, i, wR[off + i]); }
        proc.processBlock (blk, m);
        for (int i = 0; i < len; ++i) { wL[off + i] = blk.getSample (0, i); wR[off + i] = blk.getSample (1, i); }
    }
    return measureAligned (wL, wR, mono, 2048);   // latencia OLA @48k
}
} // namespace

TEST_CASE ("AURORA audible: el despliegue se OYE en banda ancha (dry alineado por latencia)", "[audible][aurora]")
{
    const Img def  = run (0.55f, 0.5f, 1.0f, 0.5f);   // DEFAULT del motor (lo que carga sin tocar nada)
    const Img max  = run (1.0f,  0.5f, 1.0f, 0.0f);   // SPREAD 100, sin red: el máximo del control de ancho
    const Img maxT = run (1.0f,  1.0f, 1.0f, 0.0f);   // + TILT +100: el extremo de carácter

    std::printf ("AUDIBLE[default Spr55]  CORR=%+.3f WIDTH=%.3f RMSd=%.3f\n", def.corr,  def.width,  def.rmsDeltaOverDry);
    std::printf ("AUDIBLE[SPREAD=100]     CORR=%+.3f WIDTH=%.3f RMSd=%.3f\n", max.corr,  max.width,  max.rmsDeltaOverDry);
    std::printf ("AUDIBLE[SPREAD100 T+100] CORR=%+.3f WIDTH=%.3f RMSd=%.3f\n", maxT.corr, maxT.width, maxT.rmsDeltaOverDry);

    // ── DEFAULT: "carga sonando" (curaduría) — el efecto se NOTA sin tocar nada ──
    // No el −0.01 de CORR de antes: cae claramente y el ancho es real (medido +0.61 / 0.63).
    REQUIRE (def.corr  < 0.75);
    REQUIRE (def.width > 0.45);

    // ── MÁXIMO del control de ancho: CORR cae CLARAMENTE (target del fix ≤0.6) y WIDTH
    //    sube de forma obvia. Comparable/mejor que DUST (CORR→0.925). Medido +0.41 / 0.78. ──
    REQUIRE (max.corr  < 0.60);
    REQUIRE (max.width > 0.65);

    // ── TILT +100 abre todavía más (el extremo de carácter; medido +0.27 / 0.88) ──
    REQUIRE (maxT.corr  < max.corr);     // más abierto que el plano
    REQUIRE (maxT.width > max.width);

    // El wet es realmente DISTINTO del dry alineado (no es identity ni un trim de nivel).
    REQUIRE (def.rmsDeltaOverDry > 0.20);
}
