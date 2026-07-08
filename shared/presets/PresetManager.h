#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "presets/PresetTypes.h"

// Cerebro de presets (heredado de ÓRBITA M5): aplica fábrica/user al APVTS, navega, guarda/borra User en
// disco, y marca "modificado" cuando el usuario toca un parámetro tras cargar un preset (compara contra un
// snapshot del preset). Aplicar = reset de TODO a default + set de los que el preset define (vía
// convertTo0to1). Independiente del DSP/UI: sólo necesita el APVTS y la tabla `factoryPresets()` del plugin.
namespace ovni::presets
{
class PresetManager : public juce::ChangeBroadcaster,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Current { juce::String name; Category category = Category::Production; bool isUser = false; bool modified = false; };

    // `pluginName` define la carpeta de User presets:
    //   ~/Library/Application Support/OVNI <pluginName>/User Presets  (NUNCA Audio/Presets = root-owned).
    // `userDirOverride` (tests) sobreescribe esa ruta.
    PresetManager (juce::AudioProcessorValueTreeState& s, juce::String pluginName,
                   juce::File userDirOverride = {});
    ~PresetManager() override;

    void applyFactory (int index);              // reset a default + aplica el preset i
    void applyUserFile (const juce::File& f);   // carga un .preset de disco
    void next();                                // recorre la lista visible (fábrica + user), envuelve
    void prev();
    Current current() const { return cur; }
    int  numFactory() const { return (int) factoryPresets().size(); }

    // User presets (disco)
    juce::File userDir() const;
    void saveUser (const juce::String& name);
    void deleteUser (const juce::File& f);
    juce::Array<juce::File> userPresets() const { return users; }
    void rescanUser();

private:
    void parameterChanged (const juce::String&, float) override;
    void resetToDefaults();
    void snapshot();                            // captura los valores normalizados del preset recién aplicado
    bool differsFromSnapshot() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String pluginName;                     // nombre para la carpeta de User presets
    juce::File   userDirOverride;                // si está seteado, sobreescribe la ruta de User (tests)
    Current cur;
    int   globalIndex = 0;                       // índice en la lista combinada (fábrica + user)
    juce::Array<juce::File>  users;
    juce::Array<float>       presetSnapshot;     // valores del preset cargado -> detectar "modificado"
};
}
