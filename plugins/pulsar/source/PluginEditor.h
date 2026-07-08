#pragma once

#include "template/PluginEditorBase.h"
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (slider con FEEL directo + shift/reset/tipear/estados)
#include "ui-kit/Controls.h"             // SegControl · ToggleButton · RailSliderLAF (faders del riel)
#include "ui-kit/OutputMeter.h"
#include "ui-kit/SyncControl.h"
#include "PluginProcessor.h"
#include "ui/StellarPad.h"
#include "ui/MorphSelector.h"          // selector de morph (control HÉROE: ORBIT·PENDULUM·LORENZ·RÖSSLER con íconos)
#include <memory>
#include <vector>

// =============================================================================
// PULSAR — editor concreto (COPIA de la estructura de ÓRBITA). El chasis (ovni::PluginEditorBase)
// ya pinta la atmósfera + header browser + bisel. Acá va el CUERPO, idéntico en orden a ÓRBITA:
//   · el POZO (StellarPad) DOMINA la IZQUIERDA, alto completo, holgado.
//   · BAHÍA a la derecha (superficie elevada), de arriba a abajo:
//       — HÉROE: SHAPE = MorphSelector (ORBIT·PENDULUM·LORENZ·RÖSSLER con ÍCONOS + marcador).
//       — banda de MODO: TEMPO = SyncControl [FREE|SYNC] + RATE knob (FREE) / chips (SYNC).
//       — fila de KNOBS uniformes ALINEADOS: MOTION · SMEAR · WIDTH · LOW CUT · HI CUT.
//       — RIEL inferior: faders MIX · IN · OUT + IN PHASE (mono safe), como ÓRBITA.
//   · tira de METER vertical en el borde derecho (rebaje del chasis) + LED de clip.
// =============================================================================
namespace pulsar
{

class PulsarEditor : public ovni::PluginEditorBase
{
public:
    explicit PulsarEditor (PulsarProcessor& p);
    ~PulsarEditor() override;

    // ======== accesores test-only (Diccionario §5: verificación por BOUNDS, no por ojo) ========
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    juce::Rectangle<int> dbgPadBounds()     const { return pad.getBounds(); }
    void dbgPump (int n) noexcept { pad.pumpFrames (n); }   // shot real: acumula estela/bloom headless
    juce::Rectangle<int> dbgUtilUnionBounds() const
    {
        return meter.getBounds().getUnion (inPhase.getBounds())
                                .getUnion (mixF.slider.getBounds())
                                .getUnion (inF.slider.getBounds())
                                .getUnion (outF.slider.getBounds());
    }
    std::vector<juce::Rectangle<int>> dbgRailKnobBounds() const
    {
        return { motion.slider.getBounds(), smear.slider.getBounds(), width.slider.getBounds(),
                 lowCut.slider.getBounds(), hiCut.slider.getBounds() };
    }
    std::vector<juce::String> dbgRailLabels() const
    {
        return { motion.label.getText(), smear.label.getText(), width.label.getText(),
                 lowCut.label.getText(), hiCut.label.getText() };
    }
    juce::Rectangle<int> dbgMorphBounds() const { return morph.getBounds(); }

protected:
    void layoutBody (juce::Rectangle<int> body) override;
    void paintBody (juce::Graphics& g) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    PulsarProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;   // knob dimensional (rotary)
    ovni::ui::RailSliderLAF   railLaf;   // faders horizontales del riel inferior

    // Un knob = slider rotary del sello (con FEEL) + label debajo.
    struct Knob
    {
        ovni::ui::OvniKnob slider;   // arrastre directo · Shift=fino · cmd-click=reset · doble-click=tipear · estados
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob motion, smear, width, lowCut, hiCut;   // grilla (SHAPE=morph, RATE en TEMPO): 5 macros, MISMO tamaño

    // Un fader = slider horizontal del riel + label de nombre (izq) + valor (der, vivo).
    struct Fader
    {
        juce::Slider slider;                   // LinearHorizontal + railLaf
        juce::Label  nameLabel, valueLabel;
        juce::String fmt;                      // "pct" | "db"
        bool         bipolar = false;
        std::unique_ptr<SliderAttachment> attach;
    };
    Fader mixF, inF, outF;                      // 06 MIX · IN · OUT (riel de salida, estilo ÓRBITA)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                    juce::Colour hue, int textBoxW);
    void setupFader (Fader& f, const juce::String& paramID, const juce::String& name,
                     const juce::String& fmt, bool bipolar);
    juce::String faderValueText (const Fader& f) const;

    pulsar::ui::StellarPad    pad;       // EL POZO (control principal: drag = centro del atractor)
    pulsar::ui::MorphSelector morph;     // HÉROE: SHAPE = morph de atractores con íconos
    ovni::ui::OutputMeter     meter;     // meter vertical + LED de clip
    ovni::ui::ToggleButton    inPhase;   // IN PHASE (mono-safe)
    ovni::ui::SyncControl     syncCtl;   // FREE⇄SYNC + RATE / chips de división (banda de modo)

    // zonas que PINTA el editor (superficie de la bahía, tira de meter, caps, riel, índices de knob)
    juce::Rectangle<int> bayArea, meterArea, morphCapArea, tempoLblArea, macroCapArea, railArea;
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulsarEditor)
};

} // namespace pulsar
