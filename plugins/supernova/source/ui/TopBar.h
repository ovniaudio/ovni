#pragma once
// TopBar — la barra PRO de SUPERNOVA en DOS filas (look del sello: ui-kit Theme/Fonts, hue fuego),
// COMPARTIDA por la app y el plugin (pedido Joaquín 2026-07-16: "el plugin igual a la app").
//   Fila 1 · AUDIO:  INPUT · medidor · IN gain                ⤢ ⛶ SYPHON · BPM TAP
//   Fila 2 · MEDIOS: CLEAR LOAD ⟳ EXPORT LFO PRESETS    ◂ MUNDO ▸ ⊞    SEQ n/N · 8s − + ▸
// La app (AppTopBar) reemplaza el cluster izquierdo de la fila 1 por su selector de fuente (SOURCE /
// permiso SCK / MIC-MIDI / TOP); el plugin no lo necesita: el DAW ES la fuente (el insert alimenta el
// análisis). El fader IN escribe el param visGain (sensibilidad visual, solo análisis — RNF1 intacto).
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace supernova {

class TopBar : public juce::Component,
               protected juce::Timer
{
public:
    static constexpr int kHeight = 76;   // 2 filas — el MISMO alto que reserva la app (MainComponent::kBarH)

    TopBar (SupernovaEditor& editor, SupernovaProcessor& proc);
    ~TopBar() override;

    void resized() final;
    void paint (juce::Graphics&) override;
    void setDragHover (bool on);         // feedback del drag&drop del editor (tinte hue sobre la barra)

protected:
    // Look del sello para la barra: botones redondeados sobre surf2 + hairline, acento hue al toggle.
    struct BarLnf final : juce::LookAndFeel_V4
    {
        BarLnf();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                               juce::Slider::SliderStyle, juce::Slider&) override;
    };

    void timerCallback() override;                        // poll 30Hz común; la app lo EXTIENDE (permiso SCK)
    void visibilityChanged() override;                    // el timer corre solo con la barra visible

    virtual void layoutRow1 (juce::Rectangle<int> row1);  // default = plugin; la app la reemplaza entera
    virtual float meterLevel() const;                     // plugin: uiOutPeak del chasis · app: engine

    void layoutRow1Right (juce::Rectangle<int>& row1);    // cluster común derecho: ⤢ ⛶ SYPHON · BPM · TAP
    void layoutRow2 (juce::Rectangle<int> row2);          // fila de MEDIOS (idéntica app/plugin)

    void stepPreset (int delta);
    void showPresetsMenu();                               // "Save current…" + lista de presets de usuario
    void savePresetDialog();
    void showExportMenu();                                // export a video: formatos + duración + sonido
    void stepSeqSeconds (double delta);
    void refreshSeqAndPreset();

    SupernovaEditor&    editor;
    SupernovaProcessor& proc;
    BarLnf lnf;

    // --- fila 1 · audio ---
    juce::Label      inputLabel;                          // "INPUT" (plugin: el DAW es la fuente) — la app lo oculta
    juce::Label      gainLabel;
    juce::Slider     gain;                                // → param visGain (sensibilidad visual, app y plugin)
    std::unique_ptr<juce::SliderParameterAttachment> gainAttach;
    juce::TextButton immersiveBtn  { juce::String::fromUTF8 ("\xE2\xA4\xA2") };  // ⤢
    juce::TextButton fullscreenBtn { juce::String::fromUTF8 ("\xE2\x9B\xB6") };  // ⛶
    juce::TextButton syphonBtn     { "SYPHON" };
    juce::TextButton tapBtn        { "TAP" };      // tap-tempo (BeatClock); dentro del DAW manda el host
    juce::Label      bpmLabel;                     // BPM en vivo (host en el DAW / tap en standalone)
    juce::TextButton exportBtn     { "EXPORT" };   // export a MP4 (1080p/4K/1:1/9:16) → menú de formatos

    // --- fila 2 · medios ---
    juce::TextButton clearBtn      { "CLEAR" };
    juce::TextButton imageBtn      { "LOAD" };
    juce::TextButton rotateBtn     { juce::String::fromUTF8 ("\xE2\x9F\xB3") };  // ⟳
    juce::TextButton presetPrevBtn { juce::String::fromUTF8 ("\xE2\x97\x82") };  // ◂
    juce::TextButton presetNextBtn { juce::String::fromUTF8 ("\xE2\x96\xB8") };  // ▸
    juce::TextButton worldsBtn     { juce::String::fromUTF8 ("\xE2\x8A\x9E") };  // ⊞ grilla de mundos
    juce::TextButton lfoBtn        { "LFO" };                                    // panel de LFOs sync
    juce::TextButton presetsBtn    { "PRESETS" };                                // presets de usuario
    juce::Label      presetLabel;
    juce::Label      seqLabel;
    juce::TextButton seqMinusBtn { "-" }, seqPlusBtn { "+" };
    juce::TextButton seqPlayBtn  { juce::String::fromUTF8 ("\xE2\x96\xB8") };

    juce::Rectangle<int> meterRect;
    float meterSmoothed = 0.0f;
    bool  exportWithSound = true;   // el export muxea los últimos ~12s de audio vivo (toggle en el menú)
    bool  dragHover = false;        // el editor lo prende al arrastrar media sobre el visual

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace supernova
