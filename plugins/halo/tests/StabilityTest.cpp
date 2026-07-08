// Test [stability][halo] — LA puerta difícil (base técnica §6): el lazo de feedback de shimmer (FDN + pitch
// octava+quinta en el lazo) NO debe diverger, NI SIQUIERA con el espacial orbitando (ORBIT=1.0, el caso que
// más riesgo de divergencia tiene — Riesgo R1). Corremos el motor a DECAY alto + SHIMMER alto varios segundos
// y verificamos que la energía de la cola queda ACOTADA (ni explota a infinito/NaN, ni el peak pasa el techo)
// — incluso tras cortar la entrada (la cola sigue circulando con el pitch-up, el caso más peligroso). Se corre
// a 44.1/48/96 kHz (Riesgo R3: el ITD/ILD del pan y los coeficientes del lazo se computan según SR en prepare).
//
// + [gain][halo]: anti-clip real sobre el HaloEngine en el peor caso (todo al máximo + ORBIT máximo).
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/HaloEngine.h"
#include <cmath>
#include <vector>

using namespace halo;
using ovni::engines::TransportInfo;

static float blockRms (const juce::AudioBuffer<float>& b)
{
    double s = 0.0; const int N = b.getNumSamples();
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getReadPointer (ch);
        for (int i = 0; i < N; ++i) s += (double) d[i] * d[i];
    }
    return (float) std::sqrt (s / juce::jmax (1, N * b.getNumChannels()));
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (d[i])) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────────
// LA puerta de estabilidad CON ÓRBITA (Riesgo R1): DECAY alto + SHIMMER alto + ORBIT=1.0, varios segundos, y
// luego silencio para que la cola circule sola con el pitch-up. La energía debe quedar acotada (no diverge) y
// finita en TODO momento — el binaural está FUERA del lazo, así que orbitar NO debe poder hacerlo divergir.
// ─────────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("halo: lazo de shimmer a DECAY alto + ORBIT=1 NO diverge (energia acotada)", "[stability][halo]")
{
    constexpr double sr = 48000.0; constexpr int B = 512;
    HaloEngine eng;
    eng.prepare ({ sr, (juce::uint32) B, 2 });

    HaloParams p;
    p.shimmer01 = 0.95f;   // mucha capa reinyectada
    p.decay01   = 1.0f;    // feedback al máximo (el motor lo clampa a kRegenMax < 1)
    p.size01    = 0.7f;
    p.tone01    = 0.5f;    // EQ del lazo en posición media (debe alcanzar para estabilizar)
    p.orbit01   = 1.0f;    // ÓRBITA PLENA (el caso que más riesgo de divergencia: Riesgo R1)
    p.mix01     = 1.0f;    // wet pleno (medimos la cola)
    TransportInfo tr; tr.isPlaying = true;

    float peak = 0.0f;
    float maxRms = 0.0f;

    // (1) ~3 s EXCITANDO con un seno fuerte: el lazo se carga al máximo.
    for (int blk = 0; blk < (int) std::ceil (3.0 * sr / B); ++blk)
    {
        juce::AudioBuffer<float> buf (2, B);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < B; ++i)
                d[i] = 0.8f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * B + i) / (float) sr);
        }
        eng.process (buf, p, tr);
        REQUIRE (allFinite (buf));
        peak   = juce::jmax (peak, buf.getMagnitude (0, B), buf.getMagnitude (1, B));
        maxRms = juce::jmax (maxRms, blockRms (buf));
    }

    // (2) ~5 s de SILENCIO: la cola circula sola con el pitch-up (el caso peligroso). La energía NO debe
    //     crecer sin control. Medimos la envolvente RMS: comparamos el final contra el arranque del tramo.
    float rmsStartSilence = -1.0f, rmsEndSilence = 0.0f;
    const int silenceBlocks = (int) std::ceil (5.0 * sr / B);
    for (int blk = 0; blk < silenceBlocks; ++blk)
    {
        juce::AudioBuffer<float> z (2, B); z.clear();
        eng.process (z, p, tr);
        REQUIRE (allFinite (z));
        const float e = blockRms (z);
        peak = juce::jmax (peak, z.getMagnitude (0, B), z.getMagnitude (1, B));
        if (blk == 4)                  rmsStartSilence = e;   // ~40 ms dentro del silencio (cola cargada)
        if (blk == silenceBlocks - 2)  rmsEndSilence   = e;   // ~5 s después
    }

    INFO ("peak=" << peak << "  maxRms(excit)=" << maxRms
          << "  rms@start_silence=" << rmsStartSilence << "  rms@end_silence=" << rmsEndSilence);

    // No diverge: peak finito y acotado (el limiter de salida lo mantiene ≤ techo del sello).
    REQUIRE (peak <= 1.0f);
    // La cola NUNCA crece sin control: el final del silencio no debe superar el arranque por un factor grande.
    REQUIRE (rmsStartSilence > 0.0f);
    REQUIRE (rmsEndSilence <= rmsStartSilence * 1.5f);    // acotada (no diverge), pero puede sostener

    // SUSTAIN (carácter shimmer glacial, "casi infinito"): a DECAY máximo la cola NO debe morir a los ~5 s.
    REQUIRE (rmsEndSilence > rmsStartSilence * 0.30f);    // a 5 s sigue sosteniendo (cola larga, no muere)
}

// Estabilidad a 44.1 y 96 kHz (Riesgo R3: el ITD/ILD del pan y los coeficientes del lazo se computan según SR
// en prepare; verificar que a otros SR el pan binaural no rompe ni desestabiliza el lazo).
TEST_CASE ("halo: estable a 44.1 y 96 kHz (ITD/ILD recalculados por SR)", "[stability][halo]")
{
    for (double sr : { 44100.0, 96000.0 })
    {
        const int B = 512;
        HaloEngine eng;
        eng.prepare ({ sr, (juce::uint32) B, 2 });
        HaloParams p;
        p.shimmer01 = 0.9f; p.decay01 = 0.95f; p.size01 = 0.6f; p.tone01 = 0.5f; p.orbit01 = 0.8f; p.mix01 = 1.0f;
        TransportInfo tr; tr.isPlaying = true;

        float peak = 0.0f;
        for (int blk = 0; blk < (int) std::ceil (4.0 * sr / B); ++blk)
        {
            juce::AudioBuffer<float> buf (2, B);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < B; ++i)
                    d[i] = 0.6f * std::sin (juce::MathConstants<float>::twoPi * 196.0f * (float) (blk * B + i) / (float) sr);
            }
            eng.process (buf, p, tr);
            REQUIRE (allFinite (buf));
            peak = juce::jmax (peak, buf.getMagnitude (0, B), buf.getMagnitude (1, B));
        }
        // Cola sola.
        float firstSil = -1.0f, lastSil = 0.0f;
        const int sil = (int) std::ceil (4.0 * sr / B);
        for (int blk = 0; blk < sil; ++blk)
        {
            juce::AudioBuffer<float> z (2, B); z.clear();
            eng.process (z, p, tr);
            REQUIRE (allFinite (z));
            peak = juce::jmax (peak, z.getMagnitude (0, B), z.getMagnitude (1, B));
            const float e = blockRms (z);
            if (blk == 4) firstSil = e;
            if (blk == sil - 2) lastSil = e;
        }
        INFO ("sr=" << sr << "  peak=" << peak << "  firstSil=" << firstSil << "  lastSil=" << lastSil);
        REQUIRE (peak <= 1.0f);
        REQUIRE (firstSil > 0.0f);
        REQUIRE (lastSil <= firstSil * 1.5f);   // acotada a cualquier SR
    }
}

// FREEZE (base técnica §6): feedback=1.0, input→0. Sin input, la nube capturada debe sostener RMS ~constante
// (no morir, no diverger) por varios segundos. El caso de cola infinita controlada.
TEST_CASE ("halo: FREEZE sostiene la nube (RMS ~constante, no diverge)", "[stability][halo]")
{
    constexpr double sr = 48000.0; constexpr int B = 512;
    HaloEngine eng;
    eng.prepare ({ sr, (juce::uint32) B, 2 });
    HaloParams p;
    p.shimmer01 = 0.6f; p.decay01 = 0.8f; p.size01 = 0.6f; p.tone01 = 0.45f; p.orbit01 = 0.4f; p.mix01 = 1.0f;
    TransportInfo tr; tr.isPlaying = true;

    // (1) Cargar la nube ~2 s con input.
    for (int blk = 0; blk < (int) std::ceil (2.0 * sr / B); ++blk)
    {
        juce::AudioBuffer<float> buf (2, B);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < B; ++i)
                d[i] = 0.6f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * B + i) / (float) sr);
        }
        eng.process (buf, p, tr);
    }

    // (2) ACTIVAR FREEZE + cortar el input. La nube debe sostener (no morir, no diverger) ~8 s.
    p.freeze = true;
    float rmsEarly = -1.0f, rmsLate = 0.0f, peak = 0.0f;
    const int hold = (int) std::ceil (8.0 * sr / B);
    for (int blk = 0; blk < hold; ++blk)
    {
        juce::AudioBuffer<float> z (2, B); z.clear();
        eng.process (z, p, tr);
        REQUIRE (allFinite (z));
        peak = juce::jmax (peak, z.getMagnitude (0, B), z.getMagnitude (1, B));
        const float e = blockRms (z);
        if (blk == 40)        rmsEarly = e;   // ~0.4 s tras el freeze (xfade del input ya cerró)
        if (blk == hold - 2)  rmsLate  = e;   // ~8 s después
    }
    INFO ("FREEZE: peak=" << peak << "  rmsEarly=" << rmsEarly << "  rmsLate=" << rmsLate);
    REQUIRE (peak <= 1.0f);
    REQUIRE (rmsEarly > 0.0f);
    REQUIRE (rmsLate <= rmsEarly * 1.5f);     // no diverge
    REQUIRE (rmsLate > rmsEarly * 0.4f);      // sostiene la nube (FREEZE no la deja morir)
}

// Anti-clip real sobre el motor en el peor caso (todo al máximo + ORBIT máximo) con material full-scale.
TEST_CASE ("halo: anti-clip motor full-scale, macros + ORBIT al maximo", "[gain][halo]")
{
    constexpr double sr = 48000.0; constexpr int B = 512;
    HaloEngine eng;
    eng.prepare ({ sr, (juce::uint32) B, 2 });
    HaloParams p;
    p.shimmer01 = 1.0f; p.decay01 = 1.0f; p.size01 = 1.0f; p.tone01 = 0.3f; p.orbit01 = 1.0f; p.mix01 = 1.0f;
    TransportInfo tr; tr.isPlaying = true;

    float peak = 0.0f;
    for (int blk = 0; blk < 500; ++blk)   // ~5.3 s
    {
        juce::AudioBuffer<float> buf (2, B);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < B; ++i)
                d[i] = std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * B + i) / (float) sr);
        }
        eng.process (buf, p, tr);
        peak = juce::jmax (peak, buf.getMagnitude (0, B), buf.getMagnitude (1, B));
    }
    INFO ("gain[halo] motor: peak=" << peak);
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak <= 1.0f);
}

// Energía en una frecuencia puntual por correlación (Goertzel/DFT de 1 bin).
static double binEnergy (const std::vector<float>& sig, double sr, double freq)
{
    double re = 0.0, im = 0.0;
    const double w = juce::MathConstants<double>::twoPi * freq / sr;
    for (size_t i = 0; i < sig.size(); ++i)
    {
        re += (double) sig[i] * std::cos (w * (double) i);
        im += (double) sig[i] * std::sin (w * (double) i);
    }
    const double n = sig.empty() ? 1.0 : (double) sig.size();
    return std::sqrt (re * re + im * im) / n;
}

// El shimmer transpone HACIA ARRIBA (octava+quinta FIJAS): con entrada a 110 Hz, la capa pitched reinyecta
// energía MÁS AGUDA cada vuelta → la cola se LLENA por encima de la fundamental (octavas que trepan). Sin
// shimmer, la cola es sólo el reverb del 110 (energía concentrada en/cerca de 110, poco arriba). Medimos la
// RAZÓN de energía ALTA (>300 Hz, banda ancha) vs la fundamental (110 Hz) en la cola: con shimmer debe ser
// MUCHO mayor (sanity de que el pitch-up del lazo sube material). Razón intra-señal → robusta al limiter.
// ORBIT en 0 para no decorrelar la medición. Banda ancha → robusta a en qué octava exacta cae la energía.
TEST_CASE ("halo: el shimmer sube material (energia HF/fundamental de la cola sube con shimmer)", "[stability][halo]")
{
    constexpr double sr = 48000.0; constexpr int B = 512;
    auto highRatio = [&] (float shimmer) -> double
    {
        HaloEngine eng;
        eng.prepare ({ sr, (juce::uint32) B, 2 });
        HaloParams p; p.shimmer01 = shimmer; p.decay01 = 0.8f; p.size01 = 0.5f; p.tone01 = 0.10f; p.orbit01 = 0.0f; p.mix01 = 1.0f;
        TransportInfo tr; tr.isPlaying = true;

        // Excitar ~3 s (el pitch-up necesita varias vueltas para apilarse; el grano de 90 ms hace cada vuelta
        // más lenta → más tiempo de carga).
        for (int blk = 0; blk < (int) std::ceil (3.0 * sr / B); ++blk)
        {
            juce::AudioBuffer<float> buf (2, B);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < B; ++i)
                    d[i] = 0.6f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) (blk * B + i) / (float) sr);
            }
            eng.process (buf, p, tr);
        }
        // Capturar ~2 s de cola.
        std::vector<float> tail;
        for (int blk = 0; blk < (int) std::ceil (2.0 * sr / B); ++blk)
        {
            juce::AudioBuffer<float> z (2, B); z.clear();
            eng.process (z, p, tr);
            auto* d = z.getReadPointer (0);
            for (int i = 0; i < B; ++i) tail.push_back (d[i]);
        }
        // Energía de la fundamental (110) vs energía ALTA de banda ancha (220..1760, las octavas que trepan).
        const double fund = binEnergy (tail, sr, 110.0);
        double high = 0.0;
        for (double f : { 220.0, 330.0, 440.0, 660.0, 880.0, 1320.0, 1760.0 })
            high += binEnergy (tail, sr, f);
        return high / juce::jmax (1.0e-9, fund);
    };

    const double ratioOn  = highRatio (0.95f);
    const double ratioOff = highRatio (0.0f);
    INFO ("razon HF/fundamental cola:  shimmer=0 -> " << ratioOff << "   shimmer=0.95 -> " << ratioOn);
    REQUIRE (ratioOn > ratioOff * 1.5);   // el shimmer sube material (más energía alta relativa a la fundamental)
}
