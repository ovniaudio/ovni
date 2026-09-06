#pragma once
// AppTopBar — la TopBar compartida (source/ui/TopBar.h) + el cluster EXCLUSIVO de la app en la fila 1:
//   SOURCE ▾ (System Audio/mic) · TOP (always-on-top) · MIC/MIDI (setup) · aviso de permiso.
// Si System Audio todavía no puede escuchar, la fila 1 muestra el aviso (SourcePermissionNotice) en sus dos
// estados: pedirlo (ALLOW → recién ahí sale el cartel de macOS) o ir a Ajustes (OPEN/REOPEN). D-33.
// La app no reproduce audio: solo analiza.
// Todo lo demás (medidor, IN gain→visGain, ⤢ ⛶ SYPHON, TAP/BPM y la fila entera de MEDIOS) vive en la base —
// idéntico en la app y en el plugin (pedido Joaquín 2026-07-16).
#include "ui/TopBar.h"
#include "AppAudioEngine.h"
#include "ui/SourcePermissionNotice.h"

namespace supernova {

class AppTopBar final : public TopBar
{
public:
    AppTopBar (AppAudioEngine& engine, SupernovaEditor& editor, SupernovaProcessor& proc);
    ~AppTopBar() override;

    std::function<void (bool)> onAlwaysOnTop;   // lo cablea el MainWindow

private:
    void timerCallback() override;               // base (medidor/BPM/toggles/SEQ) + gate de permiso
    void layoutRow1 (juce::Rectangle<int> row1) override;
    float meterLevel() const override { return engine.meterLevel(); }   // pico crudo pre-gain del engine

    void refreshSources();
    void sourceChanged();
    void openAudioSetup();
    void openPrivacySettings();                  // el panel EXACTO del permiso que pide el backend en uso
    void relaunchApp();                          // relanza ESTA copia (nunca por bundle id) — ver AppRelaunch
    void refreshPermissionUi();                  // aviso ↔ medidor+gain, según el gate

    AppAudioEngine& engine;

    juce::Label      sourceLabel;
    juce::ComboBox   sourceBox;
    SourcePermissionNotice notice;               // los tres modos del aviso (oculto por default)
    SourceNoticeModel      noticeModel;          // decide el modo: `requesting` corto no muestra nada,
                                                 // `requesting` largo = el cartel del sistema está abierto
    juce::TextButton topBtn    { "TOP" };
    juce::TextButton setupBtn  { "MIC/MIDI" };

    bool permissionUi = false;
    juce::Component::SafePointer<juce::DialogWindow> setupDialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppTopBar)
};

} // namespace supernova
