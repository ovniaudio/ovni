#pragma once
#include "template/PluginProcessorBase.h"       // ovni::PluginProcessorBase (chasis del sello, S3)
#include "engines/movement/MovementEngine.h"    // ovni::engines::MovementEngine (motor MOVIMIENTO, S1)

// ========================================================================================================
// PluginProcessor (_probe) — el plugin MÍNIMO de integración. Deriva del chasis del sello y sólo aporta:
//   · su layout (1 knob "mix" + el "bypass" estándar),
//   · su DSP: pasa el audio por el MovementEngine (la pieza de S1),
//   · su editor (header browser de S2/S3 + 1 knob + meter).
// Su única razón de ser: probar que las 4 piezas del monorepo LINKEAN y CORREN juntas.
// ========================================================================================================
class PluginProcessor : public ovni::PluginProcessorBase
{
public:
    PluginProcessor();

    // Layout de parámetros del _probe (IDs: "bypass" + "mix"). Lo pasa el ctor al chasis base.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // devuelve NUESTRO PluginEditor

protected:
    void  prepareEngine (const juce::dsp::ProcessSpec& spec) override;   // prepara el motor (S1)
    void  processAudio  (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;  // el DSP
    float extraClipPush () const override;   // reducción del limiter del motor → LED de clip

private:
    ovni::engines::MovementEngine engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
