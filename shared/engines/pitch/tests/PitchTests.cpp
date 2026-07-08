// [pitch] — PitchShifter: exactitud de transposición + anti-clip + multi-voz (spec §3.1, §6).
//
// Técnica: pitch-shift granular / splice-overlap (overlap-add + crossfade), POLIFÓNICO, multi-voz.
// Medimos la frecuencia de salida por AUTOCORRELACIÓN (robusta para un seno limpio): un seno a 220 Hz
// transpuesto +12 st debe medir ~440 Hz (con margen). Anti-clip: peak ≤ techo. Sin NaN/denormal.
// Multi-voz: la suma de varias voces NO clippea (la pasa el limiter compartido).
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "PitchShifter.h"
#include <cmath>
#include <vector>
#include <array>

using namespace ovni::engines;

namespace {

constexpr double kSr = 48000.0;
constexpr int    kB  = 512;

// Genera un bloque estéreo de seno (mismo en L y R) a 'freq', fase continua vía 'phase' (rad).
void fillSine (juce::AudioBuffer<float>& buf, double freq, double& phase, float amp = 0.8f) {
    const double inc = juce::MathConstants<double>::twoPi * freq / kSr;
    for (int i = 0; i < buf.getNumSamples(); ++i) {
        const float s = amp * (float) std::sin (phase);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch) buf.setSample (ch, i, s);
        phase += inc; if (phase > juce::MathConstants<double>::twoPi) phase -= juce::MathConstants<double>::twoPi;
    }
}

// Corre 'shifter' con las voces dadas sobre un seno a inFreq durante ~'seconds'. Descarta el primer
// tramo (warm-up del grano/latencia) y devuelve la cola del canal 0 (mono) + el peak absoluto visto.
struct RunResult { std::vector<float> tail; float peak = 0.0f; bool finite = true; };

RunResult runSine (PitchShifter& shifter, const std::vector<float>& voicesSemis,
                   double inFreq, double seconds, float amp = 0.8f) {
    shifter.setVoices (voicesSemis.data(), (int) voicesSemis.size());
    const int totalBlocks = (int) std::ceil (seconds * kSr / kB);
    const int warmBlocks  = (int) std::ceil (0.30 * kSr / kB);   // descarta ~300 ms de warm-up
    RunResult r;
    double phase = 0.0;
    for (int blk = 0; blk < totalBlocks; ++blk) {
        juce::AudioBuffer<float> buf (2, kB);
        fillSine (buf, inFreq, phase, amp);
        shifter.process (buf);
        for (int ch = 0; ch < 2; ++ch) {
            auto* d = buf.getReadPointer (ch);
            for (int i = 0; i < kB; ++i) {
                if (! std::isfinite (d[i])) r.finite = false;
                r.peak = juce::jmax (r.peak, std::abs (d[i]));
            }
        }
        if (blk >= warmBlocks) {
            auto* d = buf.getReadPointer (0);
            for (int i = 0; i < kB; ++i) r.tail.push_back (d[i]);
        }
    }
    return r;
}

// Frecuencia fundamental por AUTOCORRELACIÓN sobre una señal mono. Busca el primer pico claro de la
// autocorrelación en el rango [fMin,fMax] Hz e interpola parabólicamente el lag para sub-precisión.
double estimateFreqAutocorr (const std::vector<float>& sig, double sr, double fMin, double fMax) {
    const int N = (int) sig.size();
    const int lagMin = (int) std::floor (sr / fMax);
    const int lagMax = (int) std::ceil  (sr / fMin);
    REQUIRE (N > lagMax + 2);
    auto ac = [&] (int lag) {
        double s = 0.0; for (int i = 0; i + lag < N; ++i) s += (double) sig[(size_t) i] * sig[(size_t) (i + lag)];
        return s;
    };
    const double ac0 = ac (0);
    if (ac0 <= 0.0) return 0.0;
    // Busca el lag con mayor autocorrelación en el rango (saltando el lóbulo central en lag 0).
    int   bestLag = lagMin; double bestVal = -1e30;
    for (int lag = lagMin; lag <= lagMax; ++lag) {
        const double v = ac (lag);
        if (v > bestVal) { bestVal = v; bestLag = lag; }
    }
    // Interpolación parabólica con los vecinos para refinar el lag.
    const double ym1 = ac (bestLag - 1), y0 = ac (bestLag), yp1 = ac (bestLag + 1);
    const double denom = (ym1 - 2.0 * y0 + yp1);
    const double delta = (std::abs (denom) > 1e-12) ? 0.5 * (ym1 - yp1) / denom : 0.0;
    const double lag = (double) bestLag + delta;
    return sr / lag;
}

} // namespace

// ── Exactitud +12 st: 220 Hz -> ~440 Hz (la octava medida) ───────────────────────────────────────
TEST_CASE ("pitch: 220 Hz +12 st mide ~440 Hz (autocorrelación)", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, /*maxVoices*/ 3);
    auto r = runSine (shifter, { 12.0f }, 220.0, /*seconds*/ 1.2);
    REQUIRE (r.finite);
    const double f = estimateFreqAutocorr (r.tail, kSr, 300.0, 600.0);
    INFO ("medido = " << f << " Hz (esperado ~440)");
    REQUIRE (f > 440.0 * 0.97);     // ±3% (margen para el splice)
    REQUIRE (f < 440.0 * 1.03);
}

// ── Exactitud +7 st (5ta): 220 Hz -> ~329.6 Hz ───────────────────────────────────────────────────
TEST_CASE ("pitch: 220 Hz +7 st mide ~329.6 Hz", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    auto r = runSine (shifter, { 7.0f }, 220.0, 1.2);
    REQUIRE (r.finite);
    const double expected = 220.0 * std::pow (2.0, 7.0 / 12.0);   // ~329.63
    const double f = estimateFreqAutocorr (r.tail, kSr, 250.0, 420.0);
    INFO ("medido = " << f << " Hz (esperado ~" << expected << ")");
    REQUIRE (f > expected * 0.97);
    REQUIRE (f < expected * 1.03);
}

// ── Exactitud -12 st (octava abajo): 440 Hz -> ~220 Hz ───────────────────────────────────────────
TEST_CASE ("pitch: 440 Hz -12 st mide ~220 Hz (transpone hacia abajo)", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    auto r = runSine (shifter, { -12.0f }, 440.0, 1.2);
    REQUIRE (r.finite);
    const double f = estimateFreqAutocorr (r.tail, kSr, 150.0, 300.0);
    INFO ("medido = " << f << " Hz (esperado ~220)");
    REQUIRE (f > 220.0 * 0.97);
    REQUIRE (f < 220.0 * 1.03);
}

// ── Anti-clip: una voz full-scale (amp alto) NO pasa el techo del sello (~0.85, true-peak-safe) ──
TEST_CASE ("pitch: una voz a amplitud alta no clippea (peak <= techo)", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    auto r = runSine (shifter, { 12.0f }, 220.0, 1.5, /*amp*/ 1.0f);
    INFO ("peak = " << r.peak);
    REQUIRE (r.finite);
    REQUIRE (r.peak <= 1.0f);                 // jamás full-scale-clip
    REQUIRE (r.peak <= 0.90f);                // dentro del presupuesto true-peak del sello
}

// ── Multi-voz: 3 voces (tríada +4/+7/+12) sumadas a amplitud alta NO clippean (limiter compartido) ─
TEST_CASE ("pitch: tríada de 3 voces no clippea la suma", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    auto r = runSine (shifter, { 4.0f, 7.0f, 12.0f }, 220.0, 1.5, /*amp*/ 1.0f);
    INFO ("peak (3 voces) = " << r.peak);
    REQUIRE (r.finite);
    REQUIRE (r.peak <= 1.0f);
    REQUIRE (r.peak <= 0.90f);
}

// ── Sin denormals / NaN tras silencio largo (el buffer del grano no debe acumular basura) ────────
// El granular tiene latencia intrínseca de ~2 granos: tras cortar la entrada, los cabezales de lectura
// siguen leyendo el seno ya GRABADO en el ring durante ~2·grano (≈80 ms ≈ 4 bloques) → eso es FLUSH
// esperado, no un bug. Lo que medimos es que, UNA VEZ vaciado el ring, el wet caiga a SILENCIO REAL
// (no zumbido residual ni denormals que se acumulen) y que NUNCA aparezca un NaN/Inf en TODO el tramo.
TEST_CASE ("pitch: silencio largo no produce NaN ni denormals", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    shifter.setVoices (std::array<float,1>{ 12.0f }.data(), 1);
    // primero un poco de seno, luego silencio largo
    double phase = 0.0;
    for (int blk = 0; blk < 40; ++blk) { juce::AudioBuffer<float> b (2, kB); fillSine (b, 220.0, phase); shifter.process (b); }

    const int flushBlocks = (int) std::ceil (3.0 * shifter.latencySamples() / kB) + 2;  // vaciar el ring
    float peakAll = 0.0f, peakSettled = 0.0f; bool finite = true;
    for (int blk = 0; blk < 200; ++blk) {
        juce::AudioBuffer<float> z (2, kB); z.clear();
        shifter.process (z);
        for (int ch = 0; ch < 2; ++ch) { auto* d = z.getReadPointer (ch);
            for (int i = 0; i < kB; ++i) {
                if (! std::isfinite (d[i])) finite = false;
                const float a = std::abs (d[i]);
                peakAll = juce::jmax (peakAll, a);
                if (blk >= flushBlocks) peakSettled = juce::jmax (peakSettled, a);
            } }
    }
    INFO ("silencio: peakAll=" << peakAll << "  peakSettled(tras flush)=" << peakSettled);
    REQUIRE (finite);                          // jamás NaN/Inf en TODO el tramo
    REQUIRE (peakAll <= 0.90f);                // el flush del grano no clippea
    REQUIRE (peakSettled < 1.0e-6f);           // ya vaciado: silencio REAL (sin zumbido ni denormals)
}

// ── De-zipper del ratio: cambiar de voicing en caliente no mete un click (transitorio acotado) ───
// Cambiamos la voz de +12 a +7 a mitad de camino; el pico en el bloque del cambio no debe dispararse.
TEST_CASE ("pitch: cambio de voicing en caliente no clippea (ratio rampeado)", "[pitch]") {
    PitchShifter shifter; shifter.prepare ({ kSr, (juce::uint32) kB, 2 }, 3);
    double phase = 0.0; float peak = 0.0f; bool finite = true;
    shifter.setVoices (std::array<float,1>{ 12.0f }.data(), 1);
    for (int blk = 0; blk < 120; ++blk) {
        if (blk == 60) shifter.setVoices (std::array<float,1>{ 7.0f }.data(), 1);   // cambio de voicing
        juce::AudioBuffer<float> b (2, kB); fillSine (b, 220.0, phase, 0.9f);
        shifter.process (b);
        for (int ch = 0; ch < 2; ++ch) { auto* d = b.getReadPointer (ch);
            for (int i = 0; i < kB; ++i) { if (! std::isfinite (d[i])) finite = false; peak = juce::jmax (peak, std::abs (d[i])); } }
    }
    INFO ("cambio de voicing: peak=" << peak);
    REQUIRE (finite);
    REQUIRE (peak <= 0.90f);                  // sin spike de click al cambiar
}
