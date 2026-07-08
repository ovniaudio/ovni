#pragma once
#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser + fondo + selector S·M·L)
#include "ui-kit/KnobLookAndFeel.h"      // knob dimensional del sello (hue por-slider)
#include "ui-kit/OvniKnob.h"             // ovni::ui::OvniKnob (slider con FEEL: velocity/shift/reset/tipear/estados)
#include "ui-kit/Controls.h"             // ovni::ui::ToggleButton (IN PHASE / FREEZE)
#include "ui-kit/SyncControl.h"          // ovni::ui::SyncControl (toggle SYNC + chips de división) — control CORE
#include "ui-kit/OutputMeter.h"          // ovni::ui::OutputMeter (meter desacoplado)
#include "PluginProcessor.h"
#include "ui/HaloRings.h"                // el funnel de coronas que orbita (gancho visual)
#include <memory>
#include <vector>

// ========================================================================================================
// HALO — editor concreto (SPC·02, familia Espacio/FDN, hue MAGENTA). Rediseño nativo del mockup halo-a.
// El chasis (ovni::PluginEditorBase) ya pinta la atmósfera multicapa + el header browser (marca · power ·
// SAVE · ‹preset› · A/B · S·M·L) + el bisel con corner-brackets. Acá va el CUERPO del mockup:
//   · rail IZQUIERDO (NÚCLEO): FREEZE prominente arriba + ÓRBITA (SyncControl vertical FREE⇄SYNC, RATE/chips)
//     + specBlock decorativo (credenciales del motor FDN).
//   · el FUNNEL (HaloRings) dominando el centro, con telemetría mono en las 4 esquinas + tag CONGELADO.
//   · columna de UTILIDAD/SALIDA (rebaje del chasis): meter vertical + LED de clip · [LOW CUT | HI CUT] ·
//     [IN | OUT] (hue neutro) · IN PHASE vertical.
//   · rail INFERIOR de macros: 6 knobs magenta MIX · SIZE · DECAY · SHIMMER · TONE · ORBIT.
//
// HONESTIDAD (base técnica §1): NO hay tira SCALE/VOICING — las voces del shimmer son FIJAS (octava+quinta).
// HONESTIDAD motor↔visual: el funnel LEE los atomics reales del DSP (uiShimmer/uiDecay/uiSize/uiTone/uiOrbit/
// uiMix/uiFreeze/uiLoopRms/uiRateNorm). El APVTS queda intacto: sólo cambia la cara y cómo se consume la telemetría.
//
// REGLA DE LAYOUT CRÍTICA (Diccionario OVNI §4): FREEZE y SYNC se anclan DENTRO de `body` (que ya excluyó la
// utilidad con removeFromRight), nunca con coords del rail completo → es IMPOSIBLE que se pisen con el IN PHASE
// (que vive en la columna de utilidad, a la derecha). Test de no-solape por bounds (DiccionarioTest).
// ========================================================================================================
namespace halo
{

class HaloEditor : public ovni::PluginEditorBase
{
public:
    explicit HaloEditor (HaloProcessor& p);
    ~HaloEditor() override;

    // Test-only [diccionario][halo]: bounds (coords del canvas; mismo padre) del FREEZE, del SYNC y del IN PHASE
    // → verificar DETERMINÍSTICAMENTE que NO se solapan (regresión del bug de layout que pisa el IN PHASE).
    juce::Rectangle<int> dbgFreezeBounds()  const { return freeze.getBounds(); }
    juce::Rectangle<int> dbgSyncBounds()    const { return syncCtl.getBounds(); }
    juce::Rectangle<int> dbgInPhaseBounds() const { return inPhase.getBounds(); }
    void dbgPump (int n) noexcept { halo.pumpFrames (n); }   // shot real: acumula órbita de coronas headless

protected:
    void layoutBody (juce::Rectangle<int> body) override;   // ubicar funnel + FREEZE + SYNC + rail + utilidad
    void paintBody  (juce::Graphics& g) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    HaloProcessor& proc;
    ovni::ui::KnobLookAndFeel knobLaf;

    struct Knob
    {
        ovni::ui::OvniKnob slider;   // slider del sello con FEEL (velocity/shift/reset/doble-click-tipear/estados)
        juce::Label        label;
        std::unique_ptr<SliderAttachment> attach;
    };
    Knob mix, size, decay, shimmer, tone, orbit;    // las 6 macros del shimmer espacial (rail inferior)
    Knob lowCut, hiCut;                             // par de filtros del WET (HP + LP), columna de utilidad sobre IN/OUT
    Knob in, out;                                   // gain IN/OUT del chasis (sección de utilidad, hue neutro)

    void setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                    juce::Colour hue, int textBoxW);

    halo::ui::HaloRings    halo;          // EL FUNNEL de coronas (gancho visual; domina el cuerpo)
    ovni::ui::OutputMeter  meter;         // meter de salida + LED de clip (lee uiOutPeak/uiClip)
    ovni::ui::ToggleButton inPhase;       // IN PHASE (mono-safe) del chasis
    ovni::ui::ToggleButton freeze;        // FREEZE (captura la nube) — ligado a "freeze"
    ovni::ui::SyncControl  syncCtl;       // SYNC (core del sello): toggle + RATE/chips (rateParamID = orbitRate)

    // zonas que PINTA el editor (superficies de rail, caps, specs, índices de knob)
    juce::Rectangle<int> railLArea, utilArea, specArea;
    juce::Rectangle<int> filterCapArea, ioCapArea;   // caps "FILTRO"/"I/O" en la columna de utilidad (mockup .utilLab)
    struct KnobIdx { juce::String idx; juce::Point<int> pos; };
    std::vector<KnobIdx> knobIdx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloEditor)
};

} // namespace halo
