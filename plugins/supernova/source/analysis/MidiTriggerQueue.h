#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "midi/MidiMapper.h"   // MidiTriggerEvent (POD)

// SPSC lock-free de eventos de trigger MIDI (explosión / rayo). Productor: audio thread (processAudio).
// Consumidor: render tick (MetalViewComponent, message thread). Descarta si se llena (drop-on-full) — el audio
// NUNCA bloquea (RNF1). Mismo patrón que LockFreeAudioFifo. Los PresetChange NO van por acá (los aplica el
// chasis vía requestFactoryPreset): esta cola es sólo para triggers VISUALES.
namespace supernova
{
class MidiTriggerQueue
{
public:
    explicit MidiTriggerQueue (int capacity = 64)
        : fifo (capacity), buffer ((size_t) capacity) {}

    void reset() { fifo.reset(); }

    // Audio thread: empuja un evento. noexcept, sin locks ni allocations.
    void push (const MidiTriggerEvent& e) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);
        if (size1 > 0) buffer[(size_t) start1] = e;
        fifo.finishedWrite (size1);   // si no hay lugar, se descarta (drop-on-full)
    }

    // Render tick: saca un evento. Devuelve false si la cola está vacía.
    bool pop (MidiTriggerEvent& out) noexcept
    {
        if (fifo.getNumReady() < 1) return false;
        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);
        if (size1 > 0) out = buffer[(size_t) start1];
        fifo.finishedRead (size1);
        return size1 > 0;
    }

private:
    juce::AbstractFifo fifo;
    std::vector<MidiTriggerEvent> buffer;
};
}
