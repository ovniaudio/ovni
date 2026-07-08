#include "presets/PresetManager.h"

namespace ovni::presets
{
PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::String pn, juce::File udo)
    : apvts (s), pluginName (std::move (pn)), userDirOverride (std::move (udo))
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.addParameterListener (wid->paramID, this);
    snapshot();
    rescanUser();
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* wid = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.removeParameterListener (wid->paramID, this);
}

void PresetManager::resetToDefaults()
{
    // getDefaultValue()/setValueNotifyingHost() trabajan en valor NORMALIZADO (0..1) -> sirve para
    // floats, choices y bools por igual.
    for (auto* p : apvts.processor.getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

void PresetManager::snapshot()
{
    presetSnapshot.clearQuick();
    for (auto* p : apvts.processor.getParameters())
        presetSnapshot.add (p->getValue());     // normalizado; getValue() es sincrónico
}

bool PresetManager::differsFromSnapshot() const
{
    auto& params = apvts.processor.getParameters();
    if (presetSnapshot.size() != params.size()) return false;
    for (int i = 0; i < params.size(); ++i)
        if (std::abs (params[i]->getValue() - presetSnapshot[i]) > 1.0e-4f)
            return true;
    return false;
}

void PresetManager::parameterChanged (const juce::String&, float)
{
    const bool m = differsFromSnapshot();        // robusto: compara raw values (sincrónicos)
    if (m != cur.modified) { cur.modified = m; sendChangeMessage(); }
}

void PresetManager::applyFactory (int index)
{
    const auto& all = factoryPresets();
    if (index < 0 || index >= (int) all.size()) return;

    resetToDefaults();
    for (const auto& pp : all[(size_t) index].params)
        if (auto* p = apvts.getParameter (pp.id))
            p->setValueNotifyingHost (p->convertTo0to1 (pp.value));

    cur = { all[(size_t) index].name, all[(size_t) index].category, false, false };
    globalIndex = index;
    snapshot();
    sendChangeMessage();
}

void PresetManager::next()
{
    const int total = numFactory() + users.size();
    if (total == 0) return;
    globalIndex = (globalIndex + 1) % total;
    if (globalIndex < numFactory()) applyFactory (globalIndex);
    else                            applyUserFile (users[globalIndex - numFactory()]);
}

void PresetManager::prev()
{
    const int total = numFactory() + users.size();
    if (total == 0) return;
    globalIndex = (globalIndex - 1 + total) % total;
    if (globalIndex < numFactory()) applyFactory (globalIndex);
    else                            applyUserFile (users[globalIndex - numFactory()]);
}

juce::File PresetManager::userDir() const
{
    if (userDirOverride != juce::File())
        return userDirOverride;
    // OJO: ~/Library/Audio/Presets puede ser root-owned en algunas Macs (no escribible) -> usamos
    // Application Support, que siempre es del usuario. Parametrizado por plugin (sello OVNI).
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("Application Support/OVNI " + pluginName + "/User Presets");
}

void PresetManager::rescanUser()
{
    users.clear();
    auto d = userDir();
    if (d.isDirectory())
        for (auto& f : d.findChildFiles (juce::File::findFiles, false, "*.preset"))
            users.add (f);
}

void PresetManager::applyUserFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    cur = { f.getFileNameWithoutExtension(), Category::Production, true, false };
    snapshot();
    sendChangeMessage();
}

void PresetManager::saveUser (const juce::String& name)
{
    auto d = userDir();
    d.createDirectory();
    if (auto xml = apvts.copyState().createXml())
        d.getChildFile (name + ".preset").replaceWithText (xml->toString());
    rescanUser();
}

void PresetManager::deleteUser (const juce::File& f)
{
    f.deleteFile();
    rescanUser();
}
}
