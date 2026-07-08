#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [monoaudible][aurora] — EL GATE QUE FALTABA (2026-06-13). El bug real: "no se
// oye en mono" (Joaquín, por oído). La causa raíz medida: TODO el efecto vivía
// en el SIDE (L+R = 2·mid EXACTO) → sumado a mono se cancela y el timbre del
// centro nunca cambiaba. Los [audible]/[measure] viejos medían CORR/WIDTH (ancho
// = SIDE) → pasaban verde sin garantizar NADA audible en mono = FALSO VERDE.
//
// Este test mide la SUMA MONO (L+R)/2 directamente:
//   1) monoDeltaDb  = cuánto difiere la suma mono del wet vs el dry alineado.
//   2) toneMovDb    = MODULACIÓN en el tiempo de la amplitud de tonos fijos en la
//      suma mono (demod en cuadratura). Un peine ESTÁTICO no modula (~0 dB); el
//      barrido barber-pole de AURORA hace que cada tono suba/baje al pasar los
//      notches → movimiento ESPECTRAL que se OYE aunque sumes a mono.
// Antes del fix: toneMovDb ≈ 0 (peine estático) → este REQUIRE fallaba.
// =============================================================================

namespace
{
namespace pid = aurora::params::id;

constexpr double kSR = 48000.0;
constexpr int    kN  = 512;
constexpr int    kLat = 2048;   // latencia OLA @48k

// Frecuencias fijas que sondean el barrido del peine a lo largo del espectro.
const std::vector<double> kTones = { 700.0, 1500.0, 3000.0, 6000.0, 9000.0 };

// Cama estacionaria: suma de tonos fijos + un poco de rosa (bins en todo el rango,
// SIN fades → cualquier modulación en el tiempo es el EFECTO, no el material).
void fillStationary (std::vector<float>& mono)
{
    const int n = (int) mono.size();
    uint32_t rng = 0x51a3c7u;
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

// Modulación pico-a-valle (dB) de la amplitud del tono `f` en x sobre [a,b) por
// demod en cuadratura (I/Q con LP ~6 Hz → envolvente; descarta 0.4 s de asentado).
double toneModDb (const std::vector<float>& x, double f, int a, int b)
{
    const double w   = juce::MathConstants<double>::twoPi * f / kSR;
    const double lp  = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 6.0 / kSR);
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

struct Mono { double deltaDb = 0.0, bestMovDb = 0.0; };

Mono run (float spread, float mix, float motion = 0.5f)
{
    const int total = (int) (kSR * 7.0);
    std::vector<float> mono (total), wL (total), wR (total);
    fillStationary (mono);
    wL = mono; wR = mono;

    aurora::AuroraProcessor proc;
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    set (pid::SPREAD, spread); set (pid::MIX, mix); set (pid::MOTION, motion);   // MOTION engaged: la aurora ondula en mono
    proc.prepareToPlay (kSR, kN);

    for (int off = 0; off < total; off += kN)
    {
        const int len = juce::jmin (kN, total - off);
        juce::AudioBuffer<float> blk (2, len); juce::MidiBuffer m;
        for (int i = 0; i < len; ++i) { blk.setSample (0, i, wL[off + i]); blk.setSample (1, i, wR[off + i]); }
        proc.processBlock (blk, m);
        for (int i = 0; i < len; ++i) { wL[off + i] = blk.getSample (0, i); wR[off + i] = blk.getSample (1, i); }
    }

    // ventana de medición (descarta warmup/latencia)
    const int a = kLat + (int) (kSR * 0.5), b = total - (int) (kSR * 0.2);
    std::vector<float> wetMono (total, 0.0f);
    for (int i = 0; i < total; ++i) wetMono[(size_t) i] = 0.5f * (wL[(size_t) i] + wR[(size_t) i]);

    // 1) delta de la suma mono vs dry alineado
    double dd = 0.0, dr = 0.0;
    for (int i = a; i < b; ++i)
    {
        const double diff = (double) wetMono[(size_t) i] - (double) mono[(size_t) (i - kLat)];
        dd += diff * diff; dr += (double) mono[(size_t) (i - kLat)] * (double) mono[(size_t) (i - kLat)];
    }
    Mono out;
    out.deltaDb = 20.0 * std::log10 (std::sqrt (dd / (dr + 1e-30)) + 1e-12);

    // 2) movimiento: la MAYOR modulación de tono en la suma mono
    for (double f : kTones) out.bestMovDb = juce::jmax (out.bestMovDb, toneModDb (wetMono, f, a, b));
    return out;
}
} // namespace

TEST_CASE ("AURORA mono-audible: el despliegue se OYE sumado a mono (no sólo en el side)", "[monoaudible][aurora]")
{
    const Mono def = run (0.55f, 1.0f);   // default (carga sonando)
    const Mono hi  = run (1.0f,  1.0f);   // SPREAD 100: más profundo

    std::printf ("MONO[default Spr55] deltaMono=%+.1fdB  movMax=%.1fdB\n", def.deltaDb, def.bestMovDb);
    std::printf ("MONO[SPREAD=100]    deltaMono=%+.1fdB  movMax=%.1fdB\n", hi.deltaDb,  hi.bestMovDb);

    // (1) La SUMA MONO cambia de verdad respecto al dry (no es identity en mono).
    //     Antes: el side se cancelaba y el centro quedaba ~igual → delta muy bajo.
    REQUIRE (def.deltaDb > -16.0);

    // (2) Hay MOVIMIENTO espectral en la suma mono: al menos un tono modula con el
    //     barrido (peine estático = ~0 dB). Éste es el discriminador del fix.
    REQUIRE (def.bestMovDb > 2.5);

    // (3) SPREAD profundiza el efecto en mono (más excursión del peine).
    REQUIRE (hi.bestMovDb > def.bestMovDb);
}
