#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "FdnReverb.h"
#include <cmath>
#include <vector>

using namespace ovni::engines;

static float rms (const juce::AudioBuffer<float>& b) {
    double s = 0; int N = b.getNumSamples();
    for (int ch = 0; ch < b.getNumChannels(); ++ch) { auto* d = b.getReadPointer(ch); for (int i=0;i<N;++i) s += (double)d[i]*d[i]; }
    return (float) std::sqrt (s / juce::jmax (1, N * b.getNumChannels()));
}

// RMS de un bloque tras un filtro 1-polo (LP o HP a 'fc') aplicado canal-mono al canal 0. Sirve para
// comparar la energía de banda alta vs baja de la cola (no necesitamos exactitud, sólo la tendencia).
static double bandRms (const std::vector<float>& sig, double sr, double fc, bool highpass) {
    const double x = std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sr);  // polo del 1-polo
    const double a0 = 1.0 - x;
    double lp = 0.0, acc = 0.0;
    for (float s : sig) {
        lp = a0 * (double) s + x * lp;                 // low-pass 1-polo
        const double y = highpass ? ((double) s - lp)  // high-pass = entrada - low-pass
                                  : lp;
        acc += y * y;
    }
    const double count = sig.empty() ? 1.0 : (double) sig.size();
    return std::sqrt (acc / count);
}

// Corre 'fdn' con 'p' durante 'numBlocks' bloques de 'blockSize' samples tras un impulso inicial, y
// devuelve la envolvente RMS por bloque del canal 0 (mono-fold de L+R) y el peak absoluto.
struct TailCapture { std::vector<float> rmsPerBlock; std::vector<float> samplesCh0; float peak = 0.0f; };

static TailCapture captureTail (FdnReverb& fdn, FdnParams p, int blockSize, int numBlocks) {
    TailCapture cap;
    cap.rmsPerBlock.reserve ((size_t) numBlocks);
    juce::AudioBuffer<float> impulse (2, blockSize); impulse.clear();
    impulse.setSample (0, 0, 1.0f); impulse.setSample (1, 0, 1.0f);
    fdn.process (impulse, p);
    for (int blk = 0; blk < numBlocks; ++blk) {
        juce::AudioBuffer<float> z (2, blockSize); z.clear();
        fdn.process (z, p);
        cap.rmsPerBlock.push_back (rms (z));
        cap.peak = juce::jmax (cap.peak, z.getMagnitude (0, blockSize), z.getMagnitude (1, blockSize));
        auto* d = z.getReadPointer (0);
        for (int i = 0; i < blockSize; ++i) cap.samplesCh0.push_back (d[i]);
    }
    return cap;
}

TEST_CASE ("fdn: prototipo lossless no diverge ni muere", "[fdn]") {
    FdnReverb fdn;
    fdn.prepare ({ 48000.0, 512, 2 });
    FdnParams p; p.decay01 = 1.0f; p.freeze = true; p.tone01 = 0.0f; p.mix01 = 1.0f;
    // impulso inicial
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f);
    fdn.process (buf, p);
    // dejar correr ~2 s con silencio; la energía de la cola debe mantenerse acotada (ni 0 ni explota)
    float first = 0, last = 0;
    for (int blk = 0; blk < 180; ++blk) {     // ~1.9 s
        juce::AudioBuffer<float> z (2, 512); z.clear();
        fdn.process (z, p);
        const float e = rms (z);
        if (blk == 10) first = e;
        if (blk == 170) last = e;
        REQUIRE (std::isfinite (e));
        REQUIRE (e < 4.0f);                    // no diverge
    }
    REQUIRE (first > 1.0e-4f);                 // hay cola (no murió)
    REQUIRE (last  > 0.2f * first);            // freeze sostiene (no decae fuerte)
}

TEST_CASE ("fdn: anti-clip full-scale", "[fdn]") {
    FdnReverb fdn; fdn.prepare ({ 48000.0, 512, 2 });
    FdnParams p; p.mix01 = 1.0f; p.decay01 = 0.6f;
    float peak = 0;
    for (int blk = 0; blk < 200; ++blk) {
        juce::AudioBuffer<float> b (2, 512);
        for (int ch=0; ch<2; ++ch){ auto* d=b.getWritePointer(ch); for(int i=0;i<512;++i) d[i]=std::sin(juce::MathConstants<float>::twoPi*220.f*(blk*512+i)/48000.f);}
        fdn.process (b, p);
        peak = juce::jmax (peak, b.getMagnitude (0, 512));
    }
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak <= 1.0f);
}

// ── Task 2: T60 real ────────────────────────────────────────────────────────────────────────────
// Con decay01=0.5 medimos el tiempo hasta -60 dB de la envolvente RMS y lo comparamos con el T60
// objetivo de ese decay01 (misma fórmula que el motor expone por FdnReverb::t60ForDecay). ±35%.
TEST_CASE ("fdn: T60 medido ~ objetivo (decay01=0.5)", "[fdn]") {
    constexpr double sr = 48000.0; constexpr int B = 512;
    FdnReverb fdn; fdn.prepare ({ sr, (juce::uint32) B, 2 });
    FdnParams p; p.decay01 = 0.5f; p.tone01 = 0.0f; p.mix01 = 1.0f; p.breath01 = 0.0f; p.size01 = 0.5f;

    const float t60Target = FdnReverb::t60ForDecay (0.5f);            // objetivo (segundos)
    const int   numBlocks = (int) std::ceil (t60Target * 3.0 * sr / B);  // correr ~3·T60 para ver -60 dB
    auto cap = captureTail (fdn, p, B, numBlocks);

    // Pico de la envolvente RMS y primer bloque que cae 60 dB por debajo.
    float peakRms = 0.0f; for (float e : cap.rmsPerBlock) peakRms = juce::jmax (peakRms, e);
    REQUIRE (peakRms > 1.0e-4f);
    const float thresh = peakRms * 0.001f;                            // -60 dB
    int blkAt60 = -1;
    for (int b = 0; b < (int) cap.rmsPerBlock.size(); ++b)
        if (cap.rmsPerBlock[(size_t) b] <= thresh) { blkAt60 = b; break; }
    REQUIRE (blkAt60 > 0);                                            // sí cayó dentro de la ventana

    const double t60Measured = (double) blkAt60 * B / sr;
    INFO ("T60 objetivo=" << t60Target << "s  medido=" << t60Measured << "s");
    REQUIRE (t60Measured > 0.65 * t60Target);                         // dentro de ±35%
    REQUIRE (t60Measured < 1.35 * t60Target);
}

// ── Task 2: Tone oscurece ────────────────────────────────────────────────────────────────────────
// Con tone01 alto la energía HF de la cola DIFUSA debe decaer MÁS RÁPIDO que con tone01 bajo. Medimos
// la relación HF/LF (RMS banda alta / banda baja a ~4 kHz) SOLO en la cola tardía (saltamos los primeros
// ~0.25 s donde viven las early reflections sin filtrar) para aislar el lazo realimentado.
TEST_CASE ("fdn: tone alto oscurece la cola (HF decae antes)", "[fdn]") {
    constexpr double sr = 48000.0; constexpr int B = 512; constexpr int N = 200;  // ~2.1 s de cola
    const int skipBlocks = (int) std::ceil (0.25 * sr / B);   // descarta el tramo de early reflections
    auto hfRatioFor = [&] (float tone01) {
        FdnReverb fdn; fdn.prepare ({ sr, (juce::uint32) B, 2 });
        FdnParams p; p.decay01 = 0.85f; p.tone01 = tone01; p.mix01 = 1.0f; p.breath01 = 0.0f; p.size01 = 0.5f;
        auto cap = captureTail (fdn, p, B, N);
        std::vector<float> diffuse (cap.samplesCh0.begin() + (long) skipBlocks * B, cap.samplesCh0.end());
        const double hf = bandRms (diffuse, sr, 4000.0, /*highpass*/ true);
        const double lf = bandRms (diffuse, sr, 4000.0, /*highpass*/ false);
        return hf / juce::jmax (1.0e-12, lf);
    };
    const double ratioDark   = hfRatioFor (0.9f);   // cola oscura
    const double ratioBright  = hfRatioFor (0.1f);   // cola brillante
    INFO ("HF/LF (cola difusa)  tone=0.1 -> " << ratioBright << "   tone=0.9 -> " << ratioDark);
    REQUIRE (ratioDark < ratioBright);              // tone alto = menos agudos en la cola
}

// ── Task 3: Freeze 5 s ───────────────────────────────────────────────────────────────────────────
// decay01=1.0 + freeze=true: la cola sostiene ~5 s con energía finita y estable (ni 0 ni explota).
TEST_CASE ("fdn: freeze sostiene 5 s estable", "[fdn]") {
    constexpr double sr = 48000.0; constexpr int B = 512;
    const int numBlocks = (int) std::ceil (5.0 * sr / B);   // ~5 s
    FdnReverb fdn; fdn.prepare ({ sr, (juce::uint32) B, 2 });
    FdnParams p; p.decay01 = 1.0f; p.freeze = true; p.tone01 = 0.0f; p.mix01 = 1.0f; p.breath01 = 0.0f;
    auto cap = captureTail (fdn, p, B, numBlocks);

    const float early = cap.rmsPerBlock[(size_t) (numBlocks / 10)];   // ~0.5 s
    const float late  = cap.rmsPerBlock[(size_t) (numBlocks - 2)];    // ~5 s
    INFO ("freeze: early=" << early << "  late=" << late << "  peak=" << cap.peak);
    REQUIRE (std::isfinite (cap.peak));
    REQUIRE (cap.peak <= 1.0f);                       // no diverge ni clippea
    REQUIRE (early > 1.0e-4f);                         // hay cola
    REQUIRE (late  > 0.25f * early);                  // a 5 s sigue sosteniendo (no decae a silencio)
    REQUIRE (late  < 4.0f * early);                   // no crece sin control
}

// ── Task 3: Breath no clippea y respira lento ────────────────────────────────────────────────────
// breath01=1.0 sobre cola sostenida: peak ≤ 1.0, y la energía RMS por bloque varía LENTO (respira)
// sin saltos abruptos bloque-a-bloque (el zipper de la modulación se nota como salto brusco).
TEST_CASE ("fdn: breath alto no clippea y modula lento", "[fdn]") {
    constexpr double sr = 48000.0; constexpr int B = 512;
    const int numBlocks = (int) std::ceil (8.0 * sr / B);   // ~8 s (varios ciclos de respiración lenta)
    FdnReverb fdn; fdn.prepare ({ sr, (juce::uint32) B, 2 });
    FdnParams p; p.decay01 = 1.0f; p.freeze = true; p.breath01 = 1.0f; p.tone01 = 0.2f; p.mix01 = 1.0f; p.size01 = 0.6f;
    auto cap = captureTail (fdn, p, B, numBlocks);

    REQUIRE (std::isfinite (cap.peak));
    REQUIRE (cap.peak <= 1.0f);                        // breath profundo NO clippea

    // La respiración es MUY lenta (≈0.06–0.18 Hz → periodos de 5–16 s). La envolvente RMS por bloque
    // (10.7 ms) tiene además el "beating" natural rápido de los modos de la cola, así que promediamos
    // en ventanas de ~0.5 s para AISLAR la respiración lenta y comprobar que NO da saltos (no zipper).
    const int blocksPerWin = (int) std::ceil (0.5 * sr / B);
    std::vector<float> env;
    for (int b = 0; b < (int) cap.rmsPerBlock.size(); b += blocksPerWin) {
        double acc = 0.0; int cnt = 0;
        for (int j = b; j < b + blocksPerWin && j < (int) cap.rmsPerBlock.size(); ++j) { acc += cap.rmsPerBlock[(size_t) j]; ++cnt; }
        if (cnt > 0) env.push_back ((float) (acc / cnt));
    }
    float maxStep = 0.0f; int counted = 0;
    for (int b = 1; b < (int) env.size(); ++b) {
        const float a = env[(size_t) (b - 1)], c = env[(size_t) b];
        if (a > 1.0e-3f) { maxStep = juce::jmax (maxStep, std::abs (c - a) / a); ++counted; }
    }
    REQUIRE (counted > 10);
    INFO ("breath: peak=" << cap.peak << "  maxStep(frac, env 500ms)=" << maxStep);
    REQUIRE (maxStep < 0.25f);                         // respira suave, sin saltos abruptos (no zipper)
}

// ── Task 2/3: estabilidad peor-caso (Size+breath al tope) ────────────────────────────────────────
// Size=1 + breath=1 + decay alto infla las longitudes al máximo y las modula: la línea de delay debe
// tener capacidad de sobra (no desbordar) y la red mantenerse finita y bajo full-scale, incluso con
// saltos extremos de Size en caliente (cross-fade de longitudes). Cubre el dimensionado del buffer.
TEST_CASE ("fdn: estabilidad con Size+breath extremos", "[fdn]") {
    constexpr double sr = 48000.0; constexpr int B = 512;
    FdnReverb fdn; fdn.prepare ({ sr, (juce::uint32) B, 2 });
    FdnParams p; p.size01 = 1.0f; p.breath01 = 1.0f; p.decay01 = 0.95f; p.tone01 = 0.0f; p.mix01 = 1.0f;
    float peak = 0.0f; bool finite = true;

    auto runSine = [&] (float freq, float amp, int blocks, bool sweepSize) {
        for (int blk = 0; blk < blocks; ++blk) {
            juce::AudioBuffer<float> b (2, B);
            for (int ch = 0; ch < 2; ++ch) { auto* d = b.getWritePointer (ch);
                for (int i = 0; i < B; ++i) d[i] = amp * std::sin (juce::MathConstants<float>::twoPi * freq * (blk * B + i) / (float) sr); }
            if (sweepSize) p.size01 = (blk & 1) ? 0.02f : 1.0f;   // saltos extremos de Size en caliente
            fdn.process (b, p);
            for (int ch = 0; ch < 2; ++ch) { auto* d = b.getReadPointer (ch);
                for (int i = 0; i < B; ++i) { if (! std::isfinite (d[i])) finite = false; peak = juce::jmax (peak, std::abs (d[i])); } }
        }
    };
    runSine (330.0f, 0.9f, 600, /*sweepSize*/ false);   // ~6.4 s sostenido al tope de Size+breath
    runSine (220.0f, 0.5f, 200, /*sweepSize*/ true);    // saltos extremos de Size (cross-fade de longitudes)

    INFO ("stress Size+breath: peak=" << peak);
    REQUIRE (finite);
    REQUIRE (peak <= 1.0f);
}
