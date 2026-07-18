#pragma once
// AppTopBar — la TopBar compartida (source/ui/TopBar.h) + el cluster EXCLUSIVO de la app en la fila 1:
//   SOURCE ▾ (System Audio/mic) · TOP (always-on-top) · MIC/MIDI (setup) · banner de permiso SCK.
// Si System Audio quedó SIN PERMISO (Screen Recording), la fila 1 muestra el banner guiado:
// [⚠ activá SUPERNOVA en Ajustes y reabrí la app] [ABRIR AJUSTES]. La app no reproduce audio: solo analiza.
// Todo lo demás (medidor, IN gain→visGain, ⤢ ⛶ SYPHON, TAP/BPM y la fila entera de MEDIOS) vive en la base —
// idéntico en la app y en el plugin (pedido Joaquín 2026-07-16).
#include "ui/TopBar.h"
#include "AppAudioEngine.h"

namespace supernova {

class AppTopBar final : public TopBar
{
public:
    AppTopBar (AppAudioEngine& engine, SupernovaEditor& editor, SupernovaProcessor& proc);
    ~AppTopBar() override;

    std::function<void (bool)> onAlwaysOnTop;   // lo cablea el MainWindow

private:
    void timerCallback() override;               // base (medidor/BPM/toggles/SEQ) + permiso SCK + retry
    void layoutRow1 (juce::Rectangle<int> row1) override;
    float meterLevel() const override { return engine.meterLevel(); }   // pico crudo pre-gain del engine

    void refreshSources();
    void sourceChanged();
    void openAudioSetup();
    void openScreenRecordingSettings();
    void relaunchApp();                          // relanza el proceso: macOS aplica el permiso recién al reabrir
    void setPermissionUi (bool denied);          // banner ⚠ ↔ medidor+gain

    AppAudioEngine& engine;

    juce::Label      sourceLabel;
    juce::ComboBox   sourceBox;
    juce::Label      permLabel;                  // banner de permiso (oculto por default)
    juce::TextButton permBtn   { "SETTINGS" };
    juce::TextButton reopenBtn { "REOPEN" };
    juce::TextButton topBtn    { "TOP" };
    juce::TextButton setupBtn  { "MIC/MIDI" };

    bool permissionUi = false;
    int  retryTicks = 0;
    juce::Component::SafePointer<juce::DialogWindow> setupDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppTopBar)
};

} // namespace supernova
