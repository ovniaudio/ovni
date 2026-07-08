#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [monoaudible][horizon] — EL GATE QUE FALTABA (2026-06-13). Mismo bug que AURORA:
// "no se oye en mono". Causa raíz medida: el SPREAD/WHISPER del vivo vivían en el
// SIDE / fase opuesta L/R (L+R cancela el ancho) y con whisper³≈0 al default el
// vivo era ~identity en mono → "no hace nada" sin congelar. Los [audible] viejos
// medían CORR/WIDTH = ancho = SIDE → FALSO VERDE en mono.
//
// Mide la SUMA MONO (L+R)/2 del VIVO (sin freeze):
//   1) SHIMMER  — modulación en el tiempo de tonos fijos en la suma mono (demod
//      cuadratura): el wash difuso mueve la energía por el espectro → se OYE en mono.
//   2) GATE LIVE — con RATE>0 (sin freeze) el wet vivo late: la RMS mono pulsa
//      (chop rítmico audible en mono). Antes el gate sólo tocaba el frozen.
// =============================================================================

namespace
{
namespace pid = horizon::params::id;

constexpr double kSR = 48000.0;
constexpr int    kN  = 512;
constexpr int    kLat = 2048;

const std::vector<double> kTones = { 800.0, 1600.0, 3200.0, 6400.0 };

void fillStationary (std::vector<float>& mono)
{
    const int n = (int) mono.size();
    uint32_t rng = 0x77c19bu;
    auto noise = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((int32_t) rng) / 2.147483648e9f; };
    float pink = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSR;
        double s = 0.0;
        for (double f : kTones) s += std::sin (juce::MathConstants<double>::twoPi * f * t);
        s /= (double) kTones.size();
        pink = 0.97f * pink + 0.03f * noise();
        s = 0.8 * s + 0.2 * pink;
        mono[(size_t) i] = (float) (s * 0.5);
    }
}

double toneModDb (const std::vector<float>& x, double f, int a, int b)
{
    const double w  = juce::MathConstants<double>::twoPi * f / kSR;
    const double lp = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 6.0 / kSR);
    double I = 0.0, Q = 0.0, mn = 1.0e30, mx = 0.0;
    const int settle = a + (int) (kSR * 0.4);
    for (int n = a; n < b; ++n)
    {
        const double c = std::cos (w * (double) n), s = std::sin (w * (double) n);
        I += lp * ((double) x[(size_t) n] * c - I);
        Q += lp * ((double) x[(size_t) n] * s - Q);
        if (n > settle) { const double amp = std::sqrt (I * I + Q * Q); mn = juce::jmin (mn, amp); mx = juce::jmax (mx, amp); }
    }
    return 20.0 * std::log10 ((mx + 1.0e-12) / (mn + 1.0e-12));
}

// Modulación pico-a-valle (dB) de la RMS de banda ancha por ventana de `winS` s.
double rmsModDb (const std::vector<float>& x, int a, int b, double winS)
{
    const int win = (int) (kSR * winS);
    double mn = 1.0e30, mx = 0.0;
    for (int p = a; p + win <= b; p += win)
    {
        double acc = 0.0;
        for (int i = 0; i < win; ++i) acc += (double) x[(size_t) (p + i)] * (double) x[(size_t) (p + i)];
        const double r = std::sqrt (acc / win);
        mn = juce::jmin (mn, r); mx = juce::jmax (mx, r);
    }
    return 20.0 * std::log10 ((mx + 1.0e-12) / (mn + 1.0e-12));
}

// Corre la cama por el HorizonProcessor REAL. freeze/rate configurables; captura wet.
struct Out { double deltaDb = 0, bestMovDb = 0, rmsMod = 0; };

Out run (bool freeze, float rate01, float mix01)
{
    const int total = (int) (kSR * 7.0);
    std::vector<float> mono (total), wL (total), wR (total);
    fillStationary (mono); wL = mono; wR = mono;

    horizon::HorizonProcessor proc;
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    set (pid::MIX, mix01);
    set (pid::RATE, rate01);
    set (pid::RATESYNC, 0.0f);
    proc.prepareToPlay (kSR, kN);

    // freeze tras un warmup corto (FIFO lleno), como en AudibleTest.
    const int freezeBlk = freeze ? 80 : -1;
    int blk = 0;
    for (int off = 0; off < total; off += kN, ++blk)
    {
        if (blk == freezeBlk) set (pid::FREEZE, 1.0f);
        const int len = juce::jmin (kN, total - off);
        juce::AudioBuffer<float> b (2, len); juce::MidiBuffer m;
        for (int i = 0; i < len; ++i) { b.setSample (0, i, wL[off + i]); b.setSample (1, i, wR[off + i]); }
        proc.processBlock (b, m);
        for (int i = 0; i < len; ++i) { wL[off + i] = b.getSample (0, i); wR[off + i] = b.getSample (1, i); }
    }

    const int a = kLat + (int) (kSR * 0.8), bb = total - (int) (kSR * 0.2);
    std::vector<float> wetMono (total, 0.0f);
    for (int i = 0; i < total; ++i) wetMono[(size_t) i] = 0.5f * (wL[(size_t) i] + wR[(size_t) i]);

    double dd = 0, dr = 0;
    for (int i = a; i < bb; ++i)
    {
        const double diff = (double) wetMono[(size_t) i] - (double) mono[(size_t) (i - kLat)];
        dd += diff * diff; dr += (double) mono[(size_t) (i - kLat)] * (double) mono[(size_t) (i - kLat)];
    }
    Out o;
    o.deltaDb = 20.0 * std::log10 (std::sqrt (dd / (dr + 1e-30)) + 1e-12);
    for (double f : kTones) o.bestMovDb = juce::jmax (o.bestMovDb, toneModDb (wetMono, f, a, bb));
    o.rmsMod = rmsModDb (wetMono, a, bb, 0.08);
    return o;
}
} // namespace

TEST_CASE ("HORIZON mono-audible: el vivo se OYE sumado a mono (shimmer + gate rítmico)", "[monoaudible][horizon]")
{
    const Out live = run (false, 0.0f, 1.0f);   // VIVO default (sin freeze, sin rate): shimmer difuso
    const Out gate = run (false, 0.45f, 1.0f);  // VIVO + RATE: chop rítmico en mono
    const Out frz  = run (true,  0.0f, 1.0f);   // FREEZE: sostiene espectro (toca el mid)

    std::printf ("MONO[live default] deltaMono=%+.1fdB movMax=%.1fdB rmsMod=%.1fdB\n", live.deltaDb, live.bestMovDb, live.rmsMod);
    std::printf ("MONO[live + RATE]  deltaMono=%+.1fdB movMax=%.1fdB rmsMod=%.1fdB\n", gate.deltaDb, gate.bestMovDb, gate.rmsMod);
    std::printf ("MONO[freeze]       deltaMono=%+.1fdB movMax=%.1fdB rmsMod=%.1fdB\n", frz.deltaDb,  frz.bestMovDb,  frz.rmsMod);

    // (1) VIVO default ya se OYE en mono: el shimmer mueve la energía (no es identity).
    REQUIRE (live.deltaDb  > -16.0);
    REQUIRE (live.bestMovDb > 2.5);

    // (2) RATE groovea SIN congelar: el wet vivo late → RMS mono pulsa (chop audible).
    REQUIRE (gate.rmsMod > 6.0);

    // (3) FREEZE toca la suma mono (sostiene el espectro) → claramente audible en mono.
    REQUIRE (frz.deltaDb > -12.0);
}
