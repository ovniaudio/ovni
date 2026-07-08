#pragma once
#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser + fondo + selector S·M·L)
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (slider con FEEL)
#include "ui-kit/Controls.h"            // ovni::ui::ToggleButton (FREEZE + IN PHASE)
#include "ui-kit/SyncControl.h"         // ovni::ui::SyncControl (toggle SYNC + RATE/chips) — control CORE
#include "ui-kit/OutputMeter.h"         // ovni::ui::OutputMeter (meter desacoplado)
#include "PluginProcessor.h"
#include "ui/HorizonField.h"            // el espectro congelado que pulsa (gancho visual)
#include <memory>
#include <vector>

// ========================================================================================================
// HORIZON — editor concreto (SPL·02). El chasis (ovni::PluginEditorBase) ya pinta el fondo + el header
// browser (marca · power · SAVE · ‹preset› · A/B · selector S·M·L). Acá va el CUERPO: el ESPECTRO
// CONGELADO que pulsa (HorizonField — gancho visual que domina el área) + el botón FREEZE grande (el
// gesto central) + la banda SYNC (a la IZQUIERDA, con la perilla RATE en FREE y los 6 chips de división
// en SYNC) + el rail inferior (4 knobs verdes WHISPER/SPREAD/DUCK/MIX) + la sección de utilidad del
// chasis a la derecha (meter + IN/OUT + IN PHASE). Hue de familia = VERDE (Espectral/STFT).
//
// CONTROLES DE HORIZON (curaduría 2026-06-10): FREEZE (gesto, toggle) · RATE (+SyncControl) · WHISPER ·
// SPREAD · DUCK · MIX. FREEZE es un GESTO (toggle grande, no perilla) y RATE es el SyncControl → quedan
// 4 perillas en el rail (WHISPER/SPREAD/DUCK/MIX). DUCK va EN EL RAIL entre las 4 macros (decisión
// registrada: la nota del briefing §4.4 «DUCK en utilidad» es de DUST, que tiene 5 macros + DUCK; HORIZON
// tiene 4 macros y DUCK es una de ellas, igual que AURORA lo dejó en el rail entre sus 6).
//
// REGLA DE LAYOUT CRÍTICA (Diccionario OVNI §4): la banda SYNC y el FREEZE se anclan DENTRO de `body`
// (que ya excluyó la utilidad con removeFromRight), nunca con coords del rail completo → es IMPOSIBLE que
// se pisen con el IN PHASE (columna de utilidad, derecha). Test de no-solape por bounds.
// ========================================================================================================
namespace horizon
{

// El visualizador y el processor deben hablar del MISMO número de bandas (telemetría 1:1).
static_assert (ui::HorizonField::kBands == HorizonProcessor::kVizBands,
               "HorizonField::kBands debe casar con HorizonProcessor::kVizBands");

class HorizonEditor : public ovni::PluginEditorBase
{
public:
    explicit HorizonEditor (HorizonProcessor& p);
    ~HorizonEditor() override;

    // Test-only [diccionario][horizon]: bounds (coords del canvas; mismo padre) → verificar
    // DETERMINÍSTICAMENTE que nada pisa la columna de utilidad (regresión del bug de NÉBULA).
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgFreezeBounds()  const { return freezeBtn.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    juce::Rectangle<int> dbgMeterBounds()   const { return meter.getBounds(); }
    juce::Rectangle<int> dbgFieldBounds()   const { return field.getBounds(); }
    void dbgPump (int n) noexcept { field.pumpFrames (n); }   // shot real: acumula latido del espectro headless
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
        return { whisper.slider.getBounds(), spread.slider.getBounds(),
                 duck.slider.getBounds(), mix.slider.getBounds() };
    }
    std::vector<juce::String> dbgRailLabels() const
    {
        return { whisper.label.getText(), spread.label.getText(),
                 duck.label.getText(), mix.label.getText() };
    }

protected:
    void layoutBody (juce::Rectangle<int> body) override;   // ubicar campo + FREEZE + SYNC + rail + utilidad
    void paintBody  (juce::Graphics& g) override;           // superficies del cuerpo (rails/util/caps/specs/índices)

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    HorizonProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;

    struct Knob
    {
        ovni::ui::OvniKnob slider;
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob whisper, spread, duck, mix;   // las 4 macros perilla de la curaduría (rail inferior del campo)
    Knob lowCut, hiCut;                // par de filtros de la SALIDA (columna de utilidad, hue neutro)
    Knob in, out;                      // gain IN/OUT del chasis (columna de utilidad)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                    juce::Colour hue);

    ui::HorizonField       field;      // EL espectro congelado que pulsa (gancho visual; domina el cuerpo)
    ovni::ui::ToggleButton freezeBtn;  // FREEZE — el gesto central (toggle grande, latch automatable)
    ovni::ui::OutputMeter  meter;      // meter de salida + LED de clip (lee uiOutPeak/uiClip)
    ovni::ui::ToggleButton inPhase;    // IN PHASE (mono-safe) del chasis
    ovni::ui::SyncControl  syncCtl;    // SYNC (core del sello): RATE en FREE + 6 chips de división en SYNC

    // Zonas que PINTA el editor (superficies de rail/util, caps, specs, índices de knob — mockup).
    juce::Rectangle<int> railLArea, utilArea, specArea, macroArea;
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonEditor)
};

} // namespace horizon
