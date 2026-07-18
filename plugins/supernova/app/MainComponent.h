#pragma once
// MainComponent — el cuerpo de la app: hostea el SupernovaEditor (visual + knobs) en APP-MODE
// (sin su HUD propio, arranca inmersivo) con la AppTopBar arriba y el AppAudioEngine alimentando el
// processor. La barra vive en una franja reservada ENCIMA del editor → no pisa la vista Metal.
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AppAudioEngine.h"
#include "AppTopBar.h"

namespace supernova {

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void setAlwaysOnTopHandler (std::function<void (bool)> fn);   // lo cablea el MainWindow
    juce::PropertiesFile& props() noexcept { return *settings; }  // el MainWindow persiste su estado acá

private:
    void toggleAppFullscreen();        // fix 1 · F: kiosk fullscreen de la pantalla actual (barra oculta)

    static constexpr int kBarH = 76;   // 2 filas: audio (fuente/medidor/gain) + medios (📷/CLEAR/preset/SEQ)
    bool appFullscreen = false;        // kiosk: barra oculta + visual full-screen

    std::unique_ptr<juce::PropertiesFile>        settings;
    SupernovaProcessor                           proc;
    std::unique_ptr<AppAudioEngine>              engine;
    std::unique_ptr<juce::AudioProcessorEditor>  editorHolder;
    SupernovaEditor*                             editor = nullptr;   // alias tipado a editorHolder
    std::unique_ptr<AppTopBar>                   bar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace supernova
