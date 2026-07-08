#pragma once
#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser + fondo + selector S·M·L)
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (slider con FEEL: velocity/shift/reset/tipear/estados)
#include "ui-kit/Controls.h"             // ovni::ui::ToggleButton (IN PHASE)
#include "ui-kit/SyncControl.h"          // ovni::ui::SyncControl (toggle SYNC + chips de división)
#include "ui-kit/OutputMeter.h"          // ovni::ui::OutputMeter (meter desacoplado)
#include "PluginProcessor.h"
#include "ui/NebulaCloud.h"              // la nebulosa volumétrica que respira/esculpe (control principal)
#include <memory>
#include <vector>

// ========================================================================================================
// NÉBULA — editor concreto (rediseño mockup nebula-a). El chasis (ovni::PluginEditorBase) ya pinta la
// atmósfera + el header browser (marca · power · SAVE · ‹preset› · A/B · selector S·M·L) + el bisel con
// corner-brackets. Acá va el CUERPO del mockup (grid 130 · 1fr · 116, filas 1fr · 122):
//   · rail IZQUIERDO (130px): BREATH SYNC (SyncControl vertical FREE⇄SYNC + chips 1/2·1 BAR·2 BAR·4 BAR;
//     rateParamID VACÍO = respiración orgánica sin knob) + specBlock decorativo (credenciales del motor FDN).
//   · el CAMPO NEBULAR (NebulaCloud) dominando el centro — ES el control: esculpís Size×Decay arrastrando;
//     telemetría mono en las 4 esquinas + hint "ARRASTRÁ · ↔ SIZE · ↕ DECAY".
//   · columna de UTILIDAD (116px, altura COMPLETA, rebaje del chasis): SALIDA (meter vertical + LED de clip,
//     fill) · FILTRO [LOW | HI] · I/O [IN | OUT] · IN PHASE (vertical, al pie).
//   · rail INFERIOR de macros (122px, cols 1-2 = bajo rail+campo, NO bajo la util): SIZE/DECAY/TONE/BREATH/MIX.
// Hue de familia = magenta (Espacio/Profundidad, FDN).
//
// API del editor base: setBaseSize(w,h) (NO setSize) + addToCanvas(c) (NO addAndMakeVisible) → el resize
// S·M·L sale gratis (todo el contenido vive en el canvas que se escala).
// ========================================================================================================
namespace nebula
{

class NebulaEditor : public ovni::PluginEditorBase
{
public:
    explicit NebulaEditor (NebulaProcessor& p);
    ~NebulaEditor() override;

    // ======== accesores test-only (Diccionario §5: verificación por BOUNDS, no por ojo) ========
    // Coords del canvas (mismo padre) → verificar DETERMINÍSTICAMENTE que las regiones NO se solapan
    // (regresión del bug de layout que pisaba el IN PHASE con el SYNC).
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    juce::Rectangle<int> dbgCloudBounds()   const { return cloud.getBounds(); }
    void dbgPump (int n) noexcept { cloud.pumpFrames (n); }   // shot real: acumula respiración/motas headless
    juce::Rectangle<int> dbgUtilUnionBounds() const
    {
        return meter.getBounds().getUnion (inPhase.getBounds())
                                .getUnion (in.slider.getBounds())
                                .getUnion (out.slider.getBounds())
                                .getUnion (lowCut.slider.getBounds())
                                .getUnion (hiCut.slider.getBounds());
    }
    std::vector<juce::Rectangle<int>> dbgRailKnobBounds() const
    {
        return { size.slider.getBounds(), decay.slider.getBounds(), tone.slider.getBounds(),
                 breath.slider.getBounds(), mix.slider.getBounds() };
    }
    std::vector<juce::String> dbgRailLabels() const
    {
        return { size.label.getText(), decay.label.getText(), tone.label.getText(),
                 breath.label.getText(), mix.label.getText() };
    }

protected:
    void layoutBody (juce::Rectangle<int> body) override;   // ubicar campo + rail de knobs + utilidad
    void paintBody  (juce::Graphics& g) override;           // superficies del cuerpo (rails, util, caps, hint)

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    NebulaProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;

    // Un knob = slider del sello (con FEEL) + label debajo.
    struct Knob
    {
        ovni::ui::OvniKnob slider;   // velocity-drag · Shift=fino · cmd-click=reset · doble-click=tipear · estados
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob size, decay, tone, breath, mix;           // las 5 macros del reverb FDN (rail inferior)
    Knob lowCut, hiCut;                            // par de filtros del WET (columna de utilidad, arriba de IN/OUT)
    Knob in, out;                                  // gain IN/OUT del chasis (sección de utilidad)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text, int textBoxW);

    nebula::ui::NebulaCloud cloud;        // EL CAMPO NEBULAR (control principal: drag = esculpir Size×Decay)
    ovni::ui::OutputMeter   meter;        // meter de salida + LED de clip (lee uiOutPeak/uiClip)
    ovni::ui::ToggleButton  inPhase;      // IN PHASE (mono-safe) del chasis
    ovni::ui::SyncControl   syncCtl;      // SYNC: toggle + chips de división (rateParamID vacío = FREE sin knob)

    // zonas que PINTA el editor (superficies de rail, util, caps, specs, índices de knob).
    juce::Rectangle<int> railLArea, utilArea, specArea;
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaEditor)
};

} // namespace nebula
