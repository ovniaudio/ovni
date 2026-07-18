#include "presets/PresetTarget.h"
#include "presets/PresetTypes.h"

namespace supernova
{
namespace
{
// Valor actual de un id en unidades de param (o `fallback` si no existe).
float apvtsValue (juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback)
{
    if (auto* raw = apvts.getRawParameterValue (id)) return raw->load();
    return fallback;
}
// Default de un id en unidades de param.
float apvtsDefault (juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback)
{
    if (auto* p = apvts.getParameter (id))
        return apvts.getParameterRange (id).convertFrom0to1 (p->getDefaultValue());
    return fallback;
}
}

MorphSnapshot snapshotFromApvts (juce::AudioProcessorValueTreeState& apvts)
{
    MorphSnapshot s;
    for (int i = 0; i < MorphSnapshot::N; ++i)
        s.v[i] = apvtsValue (apvts, kMorphIds[i], s.v[i]);
    return s;
}

MorphSnapshot presetTargetContinuous (juce::AudioProcessorValueTreeState& apvts, int presetIndex)
{
    MorphSnapshot s;
    for (int i = 0; i < MorphSnapshot::N; ++i)                 // base = default del param
        s.v[i] = apvtsDefault (apvts, kMorphIds[i], s.v[i]);

    const auto& presets = ovni::presets::factoryPresets();
    if (presetIndex < 0 || presetIndex >= (int) presets.size()) return s;   // fuera de rango → defaults

    for (const auto& pp : presets[(size_t) presetIndex].params)             // overlay del preset
        for (int i = 0; i < MorphSnapshot::N; ++i)
            if (juce::String (pp.id) == kMorphIds[i]) { s.v[i] = pp.value; break; }
    return s;
}

PresetChoices presetTargetChoices (int presetIndex)
{
    PresetChoices c;
    const auto& presets = ovni::presets::factoryPresets();
    if (presetIndex < 0 || presetIndex >= (int) presets.size()) return c;
    for (const auto& pp : presets[(size_t) presetIndex].params)
    {
        if (juce::String (pp.id) == params::id::MOTION)  c.motion  = (int) pp.value;
        if (juce::String (pp.id) == params::id::SHAPE)   c.shape   = (int) pp.value;
        if (juce::String (pp.id) == params::id::KALEIDO) c.kaleido = (int) pp.value;
        if (juce::String (pp.id) == params::id::FIGURE)  c.figure  = (int) pp.value;
        if (juce::String (pp.id) == params::id::PALETTE) c.palette = (int) pp.value;
    }
    return c;
}
}
