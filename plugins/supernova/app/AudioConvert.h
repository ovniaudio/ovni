#pragma once
// Helpers PUROS de audio para la app: de-interleave Float32 y pico para el medidor.
// El .mm de captura (ScreenCaptureKit) extrae punteros crudos del AudioBufferList y delega la
// aritmética acá, así se testea sin Cocoa.
#include <juce_audio_basics/juce_audio_basics.h>

namespace supernova {

// Interleaved Float32 (frame-major: c0f0 c1f0 c0f1 c1f1 ...) → AudioBuffer por-canal.
// dst debe venir con >= numCh canales y >= numFrames samples; copia el mínimo común.
inline void deinterleave (const float* interleaved, int numCh, int numFrames, juce::AudioBuffer<float>& dst) noexcept
{
    const int chN = juce::jmin (numCh, dst.getNumChannels());
    const int fN  = juce::jmin (numFrames, dst.getNumSamples());
    for (int c = 0; c < chN; ++c)
    {
        auto* d = dst.getWritePointer (c);
        for (int f = 0; f < fN; ++f)
            d[f] = interleaved[f * numCh + c];
    }
}

inline float bufferPeak (const juce::AudioBuffer<float>& b) noexcept
{
    float pk = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        pk = juce::jmax (pk, b.getMagnitude (c, 0, b.getNumSamples()));
    return pk;
}

} // namespace supernova
