#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace { constexpr const char* kBypass = "bypass"; constexpr const char* kMix = "mix"; }

PluginProcessor::PluginProcessor()
    : ovni::PluginProcessorBase ("PROBE", createParameterLayout())   // "PROBE" = carpeta de User presets
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { kBypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kMix, 1 }, "Mix",
                    NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    return layout;
}

void PluginProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    engine.prepare (spec);
}

void PluginProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // El único knob abre el SPREAD estéreo del recorrido (0 = centro/mono .. 1 = barrido L↔R completo).
    const float mix = apvts.getRawParameterValue (kMix)->load();
    ovni::engines::MovementParams mp;
    mp.azimuthRad = 0.0f;
    mp.distance01 = 0.5f;
    mp.doppler01  = 0.0f;
    mp.width01    = mix;
    engine.process (buffer, mp);   // espacializa in-place (estéreo out, latencia 0, anti-clip interno)
}

float PluginProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = el limiter del motor contiene → más "push" en el LED de clip del meter.
    return juce::jlimit (0.0f, 1.0f, 1.0f - engine.lastLimiterGain());
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

// ===== Entry point de JUCE (crea la instancia del plugin) =====
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
