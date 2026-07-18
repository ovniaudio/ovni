#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// SPSC lock-free — el audio thread empuja samples mono (mezcla de la entrada) y el AnalysisThread los consume.
// Generaliza el patrón de plugins/dust/source/ui/BubbleEvents.h (juce::AbstractFifo). Descarta si se llena
// (el audio NUNCA bloquea): perder algún sample de análisis es inocuo (RNF1: el audio de la sesión no se toca).
namespace supernova
{
class LockFreeAudioFifo
{
public:
    explicit LockFreeAudioFifo (int capacity = 1 << 15)   // 32768 samples ≈ 0.68 s @48k
        : fifo (capacity), buffer ((size_t) capacity, 0.0f) {}

    void reset() { fifo.reset(); }

    // Audio thread: empuja n samples mono. noexcept, sin locks ni allocations.
    void push (const float* mono, int n) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (n, start1, size1, start2, size2);
        if (size1 > 0) std::copy (mono, mono + size1, buffer.begin() + start1);
        if (size2 > 0) std::copy (mono + size1, mono + size1 + size2, buffer.begin() + start2);
        fifo.finishedWrite (size1 + size2);   // si no entró todo, se descarta el resto (drop-on-full)
    }

    // Worker thread: saca hasta maxN samples. Devuelve cuántos sacó.
    int pop (float* dst, int maxN) noexcept
    {
        const int ready = juce::jmin (maxN, fifo.getNumReady());
        int start1, size1, start2, size2;
        fifo.prepareToRead (ready, start1, size1, start2, size2);
        if (size1 > 0) std::copy (buffer.begin() + start1, buffer.begin() + start1 + size1, dst);
        if (size2 > 0) std::copy (buffer.begin() + start2, buffer.begin() + start2 + size2, dst + size1);
        fifo.finishedRead (size1 + size2);
        return size1 + size2;
    }

    int numReady() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> buffer;
};
}
