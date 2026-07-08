#pragma once
#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser + fondo + selector S·M·L)
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (slider con FEEL: velocity/shift/reset/tipear/estados)
#include "ui-kit/Controls.h"             // ovni::ui::ToggleButton (IN PHASE)
#include "ui-kit/SyncControl.h"          // ovni::ui::SyncControl (toggle SYNC + RATE/chips de división) — control CORE
#include "ui-kit/OutputMeter.h"          // ovni::ui::OutputMeter (meter desacoplado)
#include "PluginProcessor.h"
#include "ui/AuroraField.h"              // la aurora desplegada (gancho visual)
#include <memory>
#include <vector>

// ========================================================================================================
// AURORA — editor concreto (SPL·01). El chasis (ovni::PluginEditorBase) ya pinta el fondo atmósfera + el
// header browser (marca · power · SAVE · ‹preset› · A/B · selector S·M·L). Acá va el CUERPO: la AURORA
// desplegada (AuroraField — el espectro en posición, gancho visual que domina el área) + la banda SYNC
// (a la IZQUIERDA del cuerpo, con la perilla RATE en FREE y los 4 chips de división en SYNC) + el rail
// inferior (6 knobs verdes SPREAD/TILT/MOTION/MONO SAFE/DUCK/MIX, las macros de la curaduría) + la
// sección de utilidad del chasis a la derecha (meter + IN/OUT + IN PHASE). Hue de familia = VERDE
// (Espectral/STFT, th::green = oklch 80% .14 160).
//
// SYNC = control CORE del sello: engancha el RATE del MOTION (el abanico que se abre/cierra) al tempo.
//
// REGLA DE LAYOUT CRÍTICA (Diccionario OVNI §4): la banda SYNC se ancla DENTRO de `body` (que ya excluyó
// la utilidad con removeFromRight), nunca con coords del rail completo → es IMPOSIBLE que se pise con el
// IN PHASE (que vive en la columna de utilidad, a la derecha). Test de no-solape por bounds.
// ========================================================================================================
namespace aurora
{

// El visualizador y el processor deben hablar del MISMO número de bandas (telemetría 1:1).
static_assert (ui::AuroraField::kBands == AuroraProcessor::kVizBands,
               "AuroraField::kBands debe casar con AuroraProcessor::kVizBands");

class AuroraEditor : public ovni::PluginEditorBase
{
public:
    explicit AuroraEditor (AuroraProcessor& p);
    ~AuroraEditor() override;

    // Test-only [diccionario][aurora]: bounds (coords del canvas; mismo padre) → verificar
    // DETERMINÍSTICAMENTE que nada pisa la columna de utilidad (regresión del bug de NÉBULA).
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    juce::Rectangle<int> dbgMeterBounds()   const { return meter.getBounds(); }
    juce::Rectangle<int> dbgFieldBounds()   const { return field.getBounds(); }
    void dbgPump (int n) noexcept { field.pumpFrames (n); }   // shot real: acumula cortinas headless
    juce::Rectangle<int> dbgUtilUnionBounds() const
    {
        return meter.getBounds().getUnion (in.slider.getBounds())
                                .getUnion (out.slider.getBounds())
                                .getUnion (lowCut.slider.getBounds())
                                .getUnion (hiCut.slider.getBounds())
                                .getUnion (inPhase.getBounds());
    }
    std::vector<juce::Rectangle<int>> dbgRailKnobBounds() const
    {
        return { spread.slider.getBounds(), tilt.slider.getBounds(), motion.slider.getBounds(),
                 monoSafe.slider.getBounds(), duck.slider.getBounds(), mix.slider.getBounds() };
    }
    std::vector<juce::String> dbgRailLabels() const
    {
        return { spread.label.getText(), tilt.label.getText(), motion.label.getText(),
                 monoSafe.label.getText(), duck.label.getText(), mix.label.getText() };
    }

protected:
    void layoutBody (juce::Rectangle<int> body) override;   // ubicar aurora + SYNC + rail + utilidad
    void paintBody  (juce::Graphics& g) override;           // superficies del cuerpo (rail izq · campo · util)

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    AuroraProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;

    struct Knob
    {
        ovni::ui::OvniKnob slider;   // slider del sello con FEEL (velocity/shift/reset/doble-click-tipear/estados)
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob spread, tilt, motion, monoSafe, duck, mix;   // las 6 macros de la curaduría (rail sobre el campo)
    Knob lowCut, hiCut;                               // par de filtros de la SALIDA (columna de utilidad, hue neutro)
    Knob in, out;                                     // gain IN/OUT del chasis (columna de utilidad, hue neutro)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text, juce::Colour hue);

    ui::AuroraField        field;     // LA AURORA desplegada (gancho visual; domina el cuerpo)
    ovni::ui::OutputMeter  meter;     // meter de salida + LED de clip (lee uiOutPeak/uiClip)
    ovni::ui::ToggleButton inPhase;   // IN PHASE (mono-safe) del chasis (columna de utilidad)
    ovni::ui::SyncControl  syncCtl;   // SYNC (core del sello): RATE en FREE + 4 chips de división en SYNC (rail izq)

    // Zonas que PINTA el editor (superficies de rail/campo/util, caps de sección, specBlock,
    // índices de knob). Coords del canvas (mismo padre que los controles).
    juce::Rectangle<int> railLArea, fieldArea, utilArea, specArea, macroArea;
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraEditor)
};

} // namespace aurora
