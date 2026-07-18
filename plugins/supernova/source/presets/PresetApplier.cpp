#include "presets/PresetApplier.h"
#include "presets/PresetTarget.h"
#include "params/ParameterIDs.h"

namespace supernova
{
PresetApplier::PresetApplier (juce::AudioProcessorValueTreeState& s) : apvts (s)
{
    apvts.addParameterListener (params::id::PRESET, this);
    lastIndex = currentIndex();
}

PresetApplier::~PresetApplier()
{
    apvts.removeParameterListener (params::id::PRESET, this);
    cancelPendingUpdate();
}

int PresetApplier::currentIndex() const noexcept
{
    if (auto* raw = apvts.getRawParameterValue (params::id::PRESET)) return (int) raw->load();
    return 0;
}

void PresetApplier::endStateRestore() noexcept
{
    lastIndex = currentIndex();   // adopta el índice restaurado SIN aplicar (no re-clobber de tweaks)
    suspended.store (false);
}

// RT-safe: la automatización del host puede llegar por el audio thread; triggerAsyncUpdate es thread-safe y la
// escritura real se difiere al message thread.
void PresetApplier::parameterChanged (const juce::String& id, float)
{
    if (id == params::id::PRESET && ! suspended.load()) triggerAsyncUpdate();
}

void PresetApplier::handleAsyncUpdate()
{
    if (suspended.load()) return;
    const int idx = currentIndex();          // índice ACTUAL → la ráfaga reset→0 + corrección→idx se coalesce
    lastIndex = idx;

    const MorphSnapshot target = presetTargetContinuous (apvts, idx);
    for (int i = 0; i < MorphSnapshot::N; ++i)
        if (auto* p = apvts.getParameter (kMorphIds[i]))
            p->setValueNotifyingHost (apvts.getParameterRange (kMorphIds[i]).convertTo0to1 (target.v[i]));

    // Los CHOICES del mundo (motion/shape/kaleido) SALTAN al destino (la identidad no se interpola; el morph
    // desliza los continuos alrededor). Mismo semántico default→overlay que los continuos.
    const PresetChoices choices = presetTargetChoices (idx);
    const auto setChoice = [this] (const char* id, int v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 ((float) v));
    };
    setChoice (params::id::MOTION,  choices.motion);
    setChoice (params::id::SHAPE,   choices.shape);
    setChoice (params::id::KALEIDO, choices.kaleido);
    setChoice (params::id::FIGURE,  choices.figure);
    setChoice (params::id::PALETTE, choices.palette);
}
}
