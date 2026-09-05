#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include "midi/MidiMapper.h"   // PhotoCueKind

// SPSC lock-free de CUES DE FOTO por MIDI (ronda 3 · el instrumento en vivo). Productor: audio thread
// (processAudio). Consumidor: el TIMER del editor (message thread) — la PhotoSequence sólo se toca ahí, así
// que el cue no puede viajar por la cola de triggers visuales (esa la drena el render tick). Drop-on-full:
// el audio NUNCA bloquea (RNF1). Mismo patrón que MidiCcQueue / MidiTriggerQueue.
namespace supernova
{
struct MidiCueMsg
{
    PhotoCueKind kind = PhotoCueKind::Tile;
    int index    = -1;    // tile 0-based (sólo kind == Tile)
    int velocity = 0;     // 0..127, tal como llegó
    // beatPos DE LA NOTA (fase del bloque + su offset en samples). El editor ancla con esto la ventana del
    // cue armado: el beatPos del tick que drena la cola llega hasta 33 ms tarde y, si el compás cruzaba en
    // esa ventana, el corte caía un compás entero después del que el VJ estaba anticipando.
    double beatPos = -1.0;
};

class MidiCueQueue
{
public:
    explicit MidiCueQueue (int capacity = 64)
        : fifo (capacity), buffer ((size_t) capacity) {}

    void reset() { fifo.reset(); }

    void push (const MidiCueMsg& m) noexcept   // audio thread
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) buffer[(size_t) s1] = m;
        fifo.finishedWrite (n1);
    }

    bool pop (MidiCueMsg& out) noexcept        // message thread
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
    std::vector<MidiCueMsg> buffer;
};
}
