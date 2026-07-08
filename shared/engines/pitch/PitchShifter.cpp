#include "engines/pitch/PitchShifter.h"
#include <cmath>
#include <algorithm>

namespace ovni::engines {

// =====================================================================================
// Detalle del algoritmo (splice-overlap, por-sample, poly):
//
// Un cabezal de ESCRITURA recorre el ring a 1 muestra/sample. Por cada voz hay DOS cabezales de
// LECTURA separados medio grano (fase φ y φ+0.5). El RETARDO de un cabezal respecto a la escritura es
//   D(φ) = (1 + φ_wrapped) · grain          (base de 1 grano para no pasar la escritura ni ir negativo)
// y la fase avanza por-sample
//   φ += (1 − ratio) / grain
// de modo que D avanza a (1−ratio) por sample: con ratio>1 (subir) D DECRECE → el cabezal recorre la
// onda grabada MÁS RÁPIDO que el tiempo real → sale más agudo (×ratio); con ratio<1 (bajar) D crece y
// sale más grave. Cuando φ envuelve [0,1) el cabezal "salta" un grano; la ventana de Hann lo cruza-funde
// con el otro cabezal (que está a amplitud plena en ese instante) → la discontinuidad queda enmascarada.
//
// ratio = 2^(semitones/12), RAMPEADO por-sample (LinearRamp) → cambiar de voicing no mete clicks.
// La SUMA de las voces se escala (1/√Nvoces, energía) y pasa por el StereoLimiter compartido (anti-clip).
// =====================================================================================

namespace {
// Grano por defecto ~40 ms: compromiso clásico del splice — suficiente para graves (un periodo de ~50 Hz
// entra) y chico para que la latencia (medio grano ≈ 20 ms) y el "doblado temporal" no se vayan de mano.
// HALO pide 90 ms (sidebands más suaves) vía el arg grainMs de prepare(); los demás callers usan 40 ms.
constexpr double kGrainMsDefault = 40.0;
// Tiempo de de-zipper del ratio (ms): suave para matar el zipper sin emborronar el cambio de voicing.
constexpr float  kRatioSmoothMs = 25.0f;
}

void PitchShifter::prepare (const juce::dsp::ProcessSpec& spec, int maxVoices, float grainMs)
{
    sampleRate   = spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0;
    maxBlock     = (int) spec.maximumBlockSize;
    numCh        = std::min<int> (2, (int) spec.numChannels);
    if (numCh < 1) numCh = 1;
    maxVoicesCap = std::clamp (maxVoices, 1, 3);

    const double gMs = (grainMs > 0.0f) ? (double) grainMs : kGrainMsDefault;
    grainSamples = std::max (64, (int) std::round (gMs * 0.001 * sampleRate));

    // Ventana de Hann del largo del grano (w[0]=w[grain-1]≈0, w[grain/2]=1).
    hann.assign ((size_t) grainSamples, 0.0f);
    for (int i = 0; i < grainSamples; ++i)
        hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (grainSamples - 1));

    // Ring: capacidad para 2 granos de retardo + un bloque de holgura.
    ringSize = grainSamples * 2 + maxBlock + 4;
    for (int ch = 0; ch < 2; ++ch) ring[(size_t) ch].assign ((size_t) ringSize, 0.0f);
    writePos = 0;

    for (int ch = 0; ch < 2; ++ch) outScratch[(size_t) ch].assign ((size_t) maxBlock, 0.0f);

    // Estado de voces: ratio neutro (1.0), fases del grano repartidas para que no salten todas juntas.
    for (int v = 0; v < 3; ++v) {
        voices[(size_t) v].ratio.reset (1.0f);
        voices[(size_t) v].ratioTarget = 1.0f;
        voices[(size_t) v].phase  = (double) v / 3.0;   // desfase inicial entre voces
        voices[(size_t) v].active = false;
    }
    activeVoices = 0;

    limiter.prepare (sampleRate);
    reset();
}

void PitchShifter::reset() noexcept
{
    for (int ch = 0; ch < 2; ++ch)
        std::fill (ring[(size_t) ch].begin(), ring[(size_t) ch].end(), 0.0f);
    writePos = 0;
    limiter.reset();
}

void PitchShifter::setVoices (const float* semitones, int count) noexcept
{
    activeVoices = std::clamp (count, 0, maxVoicesCap);
    for (int v = 0; v < 3; ++v) {
        if (v < activeVoices) {
            voices[(size_t) v].ratioTarget = std::pow (2.0f, semitones[v] / 12.0f);
            voices[(size_t) v].active = true;
        } else {
            voices[(size_t) v].active = false;
        }
    }
}

float PitchShifter::readDelayed (int ch, float delaySamples) const noexcept
{
    // Posición fraccional 'delaySamples' detrás de writePos, envuelta en el ring; interpolación lineal.
    float pos = (float) writePos - delaySamples;
    while (pos < 0.0f)            pos += (float) ringSize;
    while (pos >= (float) ringSize) pos -= (float) ringSize;
    const int   i0 = (int) pos;
    const float fr = pos - (float) i0;
    const int   i1 = (i0 + 1 >= ringSize) ? 0 : i0 + 1;
    const auto& r  = ring[(size_t) ch];
    return r[(size_t) i0] + fr * (r[(size_t) i1] - r[(size_t) i0]);
}

void PitchShifter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const int n  = buffer.getNumSamples();
    const int ch = std::min (numCh, buffer.getNumChannels());
    if (grainSamples <= 0 || ringSize <= 0) return;

    // Escala de energía: con varias voces sumadas, 1/√N evita que la suma explote (la pasa el limiter
    // igual, pero gain-stageamos en origen para que el limiter casi no trabaje — ver anti-click §4).
    const float voiceGain = (activeVoices > 0) ? 1.0f / std::sqrt ((float) activeVoices) : 0.0f;

    // Pasos por-sample del ratio de cada voz (de-zipper). LinearRamp recorre current->target en 'n'.
    std::array<float, 3> ratioStep {};
    for (int v = 0; v < activeVoices; ++v)
        ratioStep[(size_t) v] = voices[(size_t) v].ratio.stepTo (voices[(size_t) v].ratioTarget, n);

    // Acumuladores de salida (silencio si no hay voces).
    for (int c = 0; c < ch; ++c)
        std::fill (outScratch[(size_t) c].begin(), outScratch[(size_t) c].begin() + n, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        // 1) Escribir la entrada de este sample en el ring (todos los canales).
        for (int c = 0; c < ch; ++c)
            ring[(size_t) c][(size_t) writePos] = buffer.getSample (c, i);

        // 2) Sumar las voces transpuestas.
        for (int v = 0; v < activeVoices; ++v)
        {
            Voice& vo = voices[(size_t) v];
            const float ratio = vo.ratio.value();

            // Fases de los dos cabezales (φ y φ+0.5), envueltas en [0,1).
            double p0 = vo.phase;            p0 -= std::floor (p0);
            double p1 = vo.phase + 0.5;       p1 -= std::floor (p1);

            // Retardo de cada cabezal: base de 1 grano + φ·grano (∈ [grano, 2·grano)).
            const float d0 = (float) (1.0 + p0) * (float) grainSamples;
            const float d1 = (float) (1.0 + p1) * (float) grainSamples;
            const float w0 = hann[(size_t) std::min (grainSamples - 1, (int) (p0 * grainSamples))];
            const float w1 = hann[(size_t) std::min (grainSamples - 1, (int) (p1 * grainSamples))];

            for (int c = 0; c < ch; ++c)
            {
                const float s = w0 * readDelayed (c, d0) + w1 * readDelayed (c, d1);
                outScratch[(size_t) c][(size_t) i] += s * voiceGain;
            }

            // Avanzar la fase del grano por-sample: φ += (1−ratio)/grano (D avanza a (1−ratio)/sample).
            vo.phase += (double) (1.0f - ratio) / (double) grainSamples;
            // Avanzar el ratio rampeado (anti-zipper).
            vo.ratio.advance (ratioStep[(size_t) v]);
        }

        // 3) Avanzar el cabezal de escritura.
        if (++writePos >= ringSize) writePos = 0;
    }

    // Fijar el ratio de cada voz a su objetivo (continuidad C0 para el próximo bloque).
    for (int v = 0; v < activeVoices; ++v)
        voices[(size_t) v].ratio.reset (voices[(size_t) v].ratioTarget);

    // 4) Copiar el wet al buffer + limiter compartido (anti-clip de la suma).
    for (int c = 0; c < ch; ++c)
        buffer.copyFrom (c, 0, outScratch[(size_t) c].data(), n);
    // Si el buffer trae más canales que los procesados, replicar el canal 0 (mono-fold seguro).
    for (int c = ch; c < buffer.getNumChannels(); ++c)
        buffer.copyFrom (c, 0, outScratch[0].data(), n);

    float* L = buffer.getWritePointer (0);
    float* R = (buffer.getNumChannels() > 1) ? buffer.getWritePointer (1) : L;
    limiter.process (L, R, n);
}

} // namespace ovni::engines
