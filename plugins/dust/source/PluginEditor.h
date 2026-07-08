#pragma once

#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser + fondo + selector S·M·L)
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (FEEL: velocity/shift/reset/tipear/estados)
#include "ui-kit/Controls.h"             // ovni::ui::ToggleButton (IN PHASE)
#include "ui-kit/OutputMeter.h"          // ovni::ui::OutputMeter (meter + LED de clip)
#include "ui-kit/SyncControl.h"          // ovni::ui::SyncControl (control CORE: FREE⇄SYNC + chips)
#include "PluginProcessor.h"
#include "ui/DustField.h"                // el campo de burbujas (gancho visual + superficie editable)
#include <memory>
#include <vector>

// ========================================================================================================
// DUST — editor concreto (MOV·03, familia Movimiento = cian). El chasis (ovni::PluginEditorBase) ya pinta
// el fondo atmósfera + el header browser (marca · power · SAVE · ‹preset› · A/B · S·M·L). Acá va el CUERPO:
// el CAMPO DE BURBUJAS (DustField, ≈60% — gancho visual Y primera superficie EDITABLE del sello: el drag
// mueve el ORIGIN) + el rail inferior (SyncControl RATE con chips 1/16·1/8·1/4·1/2 + DENSIDAD/SPREAD/VIDA/
// MIX) + la columna de utilidad a la derecha (meter · DUCK · IN/OUT · IN PHASE — orden curaduría).
//
// REGLA DE LAYOUT CRÍTICA (Diccionario OVNI §4): el rail se quita del body PRIMERO y la utilidad DESPUÉS →
// regiones disjuntas por construcción; el SYNC vive en el rail, lejísimos del IN PHASE. Test por bounds.
// HONESTIDAD (curaduría): sin TONE/DAMP, sin órbita del ORIGIN — no re-exponer lo cortado.
// ========================================================================================================
namespace dust
{

class DustEditor : public ovni::PluginEditorBase
{
public:
    explicit DustEditor (DustProcessor& p);
    ~DustEditor() override;

    // Test-only [diccionario][dust]: bounds en coords del canvas (mismo padre) → verificación
    // DETERMINÍSTICA de no-solape (SYNC vs IN PHASE · DUCK dentro de la columna · rail vs utilidad).
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    juce::Rectangle<int> dbgMeterBounds()   const { return meter.getBounds(); }
    juce::Rectangle<int> dbgFieldBounds()   const { return field.getBounds(); }
    void dbgPump (int n) noexcept { field.pumpFrames (n); }   // shot real: acumula burbujas headless
    juce::Rectangle<int> dbgDuckBounds()    const { return knobUnion (duck); }
    juce::Rectangle<int> dbgInBounds()      const { return knobUnion (in); }
    juce::Rectangle<int> dbgOutBounds()     const { return knobUnion (out); }
    juce::Rectangle<int> dbgRailBounds()    const;   // unión SYNC + DENSIDAD/SPREAD/VIDA/MIX
    juce::Rectangle<int> dbgMacrosBounds()  const;   // unión SOLO de los 4 macros (rail inferior, bajo el campo)
    juce::Rectangle<int> dbgUtilBounds()    const;   // unión meter + DUCK + IN/OUT + IN PHASE

    ui::DustField& dbgFieldRef() noexcept { return field; }   // test del mapeo de drag (1:1)

protected:
    void layoutBody (juce::Rectangle<int> body) override;
    void paintBody (juce::Graphics& g) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    DustProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;

    // Un knob = slider del sello (con FEEL) + label legible debajo.
    struct Knob
    {
        ovni::ui::OvniKnob slider;   // velocity-drag · Shift=fino · cmd-click=reset · doble-click=tipear
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob density, spread, vida, mix;   // rail inferior (RATE vive en el SyncControl)
    Knob duck;                         // columna de utilidad, ARRIBA de IN/OUT (curaduría)
    Knob lowCut, hiCut;                // par de filtros de la SALIDA (columna de utilidad, bajo el meter)
    Knob in, out;                      // gain IN/OUT del chasis (utilidad)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text);
    static juce::Rectangle<int> knobUnion (const Knob& k)
    {
        return k.slider.getBounds().getUnion (k.label.getBounds());
    }

    ui::DustField          field;     // EL CAMPO (gancho + superficie de control: drag = ORIGIN)
    ovni::ui::OutputMeter  meter;     // meter de salida + LED de clip (lee uiOutPeak/uiClip)
    ovni::ui::ToggleButton inPhase;   // IN PHASE (mono-safe) del chasis
    ovni::ui::SyncControl  syncCtl;   // RATE: knob (FREE) ⇄ chips 1/16·1/8·1/4·1/2 (SYNC)

    // Superficies que PINTA el editor (rail elevado izq · macros abajo · util rebaje · caps · índices).
    juce::Rectangle<int> railLArea, macrosArea, utilArea, specArea;
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustEditor)
};

} // namespace dust
