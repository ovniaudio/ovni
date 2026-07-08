#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Comparador A/B (heredado de ÓRBITA M5): dos snapshots del estado de parámetros en memoria. toggle()
// guarda el slot activo y carga el otro -> comparás dos sonidos al instante. copyActiveToOther() arranca
// el otro desde el activo (alt+click). No toca disco.
namespace ovni::presets
{
class ABState
{
public:
    explicit ABState (juce::AudioProcessorValueTreeState& s) : apvts (s)
    {
        a = apvts.copyState();
        b = apvts.copyState();
    }

    void toggle();                  // guarda el slot activo, carga el otro
    void copyActiveToOther();       // alt+click: arrancar el otro desde el activo
    char activeSlot() const { return onA ? 'A' : 'B'; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::ValueTree a, b;
    bool onA = true;
};
}
