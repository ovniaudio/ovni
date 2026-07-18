#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// SPSC lock-free de mensajes MIDI CC crudos (Phase B · MIDI-learn). Productor: audio thread (processAudio).
// Consumidor: editor (message thread) que los resuelve contra el MidiCcMap y escribe el APVTS. Drop-on-full
// (el audio NUNCA bloquea, RNF1). Mismo patrón que MidiTriggerQueue: el mapeo/aplicación NO tocan el audio.
namespace supernova
{
struct MidiCcMsg { int cc = 0; int value = 0; };   // value 0..127

class MidiCcQueue
{
public:
    explicit MidiCcQueue (int capacity = 128)
        : fifo (capacity), buffer ((size_t) capacity) {}

    void reset() { fifo.reset(); }

    void push (const MidiCcMsg& m) noexcept   // audio thread
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) buffer[(size_t) s1] = m;
        fifo.finishedWrite (n1);
    }

    bool pop (MidiCcMsg& out) noexcept        // message thread
    {
        if (fifo.getNumReady() < 1) return false;
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 > 0) out = buffer[(size_t) s1];
        fifo.finishedRead (n1);
        return n1 > 0;
    }

private:
    juce::AbstractFifo fifo;
    std::vector<MidiCcMsg> buffer;
};
}
