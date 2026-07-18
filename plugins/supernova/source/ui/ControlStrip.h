#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <unordered_map>
#include "ui-kit/OvniKnob.h"
#include "ui-kit/KnobLookAndFeel.h"

// ControlStrip — la franja de controles de SUPERNOVA, agrupada POR FUNCIÓN (pedido de Joaquín: "que sea
// fácil saber qué knob modifica qué cosa" + "todo en inglés"). Cuatro DOMINIOS sobre UNA cuadrícula (riel +
// 8 columnas idénticas en las 4 filas, steppers alineados en la col 0), cada dominio con su TINTE (nombre de
// grupo, labels y knobs del mismo color), y una barra de AYUDA abajo que describe el control bajo el mouse:
//   MOVEMENT · cómo vive:          MOTION · INTENSITY · SPEED · GRAVITY · PUMP · CHAOS · BREATHE · BLAST
//   MATTER   · de qué está hecho:  SHAPE · DENSITY · SCATTER · SIZE · TRAILS · CUTOUT · LINKS
//   CAMERA   · desde dónde lo ves: FIGURE · ROT Y · ROT X · ORBIT · ROTATE · FORM · DEPTH · KALEIDO
//   COLOR    · la luz:             PALETTE(swatch) · AMOUNT · HUE · SAT · HUE CYC · GLOW ‖ VARIATION · dados
// El orden DENTRO de cada fila es por IMPACTO MEDIDO (tools/supernova-knob-impact.py, 2026-07-12): de más
// a menos lo que el knob modifica la imagen. Excepción de diseño: INTENSITY va primero — con el camino de
// quietud del kernel (INTENSITY 0 = foto congelada) es el fader maestro de vida del sistema.
// HOTKNOBS por dominio: cada riel (MOVEMENT/MATTER/CAMERA/COLOR) tiene su mini-knob de VARIACIÓN bajo el
// nombre — girás el de MOVEMENT y muta SOLO el movimiento; VARIATION (columna del dado) es el MASTER que
// corre todas las filas a la vez (se suman). Determinista, 0 = neutro. El DADO (violeta OVNI, tras el
// divisor): VARIATION (master) + RANDOM (el dado TOTAL: randomiza el mundo entero dentro de rangos sanos —
// nunca pantalla muerta, y "se nota que modifica todo"). MUTATE se retiró (los hotknobs lo reemplazan). El
// botón EXPLODE se retiró de la UI (el kick del audio ya explota; el PARAM 'explode' sigue para automation/
// MIDI) y BLAST —la fuerza de esa explosión— subió a knob. PALETTE muestra el DEGRADÉ real del look (swatch).
// Vive FUERA del área de la vista Metal (que se compone por encima del render JUCE y ocluiría todo).
namespace supernova
{
class ChoiceStepper;

class ControlStrip : public juce::Component
{
public:
    static constexpr int kHeight = 322;   // 4 filas de ~74 + barra de ayuda angosta (sin aire muerto abajo)

    ControlStrip (juce::AudioProcessorValueTreeState& state, juce::Colour familyHue);
    ~ControlStrip() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    // Barra de ayuda: escucha TODOS los hijos (addMouseListener recursivo en el ctor).
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    // UNDO (Phase C): el editor lo cablea a captureUndoState — se llama ANTES de una acción destructiva
    // (RANDOM) para que el usuario pueda deshacerla. RANDOM dejó de ser puerta de una vía.
    std::function<void()> onBeforeEdit;

    // MIDI-LEARN (Phase B UI): click-derecho en un knob → "MIDI Learn" / "Forget CC". El editor las cablea al
    // MidiCcMap del processor. midiCcForParam devuelve el CC asignado (-1 si ninguno) para pintar el menú.
    std::function<void (juce::String paramId)> onMidiLearn, onMidiForget;
    std::function<int  (juce::String paramId)> midiCcForParam;
    void mouseDown (const juce::MouseEvent& e) override;   // captura el click-derecho sobre los knobs

private:
    struct KnobCell
    {
        ovni::ui::OvniKnob slider;
        juce::Label        label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    void setupKnob (KnobCell& cell, const juce::String& paramID, const juce::String& text,
                    const juce::String& hintText, juce::Colour tint);
    void registerHint (juce::Component& c, const juce::String& text);
    void updateHint (juce::Component* under);
    void randomizeWorld();                                            // el dado TOTAL (botón RANDOM)

    // HOTKNOBS por dominio (pedido de campo "que MUEVA los parámetros del grupo"): el hotknob ESCRIBE los
    // params reales de su fila — tomas deterministas alrededor de una BASE capturada al salir de 0; volver
    // a 0 restaura la base (solo los params que nadie más tocó: guard por lastWritten).
    struct DomainTake
    {
        bool captured = false;
        std::vector<std::pair<const char*, float>> base;         // valores al salir de 0
        std::vector<std::pair<const char*, float>> lastWritten;  // lo último que ESTE hotknob escribió
    };
    void applyDomainTake (int domain, float value);               // 0=MOV 1=MAT 2=CAM 3=COL

    juce::AudioProcessorValueTreeState& apvts;
    juce::Colour hue;
    ovni::ui::KnobLookAndFeel knobLaf;
    std::unordered_map<juce::Component*, juce::String> knobParams;   // slider → paramID (MIDI-learn por click-derecho)

    KnobCell intensity, chaos, speed, pump, blast, gravityK, breathe; // MOVEMENT
    KnobCell size, density, scatter, trails, links, cutout;           // MATTER
    KnobCell form, depth, rotX, rotY, orbit, rotate;                  // CAMERA
    KnobCell colorAmt, sat, hueKnob, hueCycle, glow, variation;       // COLOR + dado
    KnobCell hotMov, hotMat, hotCam, hotCol;                          // hotknobs de variación (en los rieles)
    std::array<DomainTake, 4> takes;                                  // estado base/lastWritten por dominio
    bool hotReady = false;                                            // ignora el sync inicial del attachment
    std::unique_ptr<ChoiceStepper> motion, shape, figure, kaleido, palette;
    std::unique_ptr<juce::TextButton> randomBtn;                      // dado TOTAL (randomiza el mundo)

    // Señalética de grupos: rects de fila (los llena resized, los pinta paint) + barra de ayuda.
    struct GroupMeta { const char* name; juce::Colour tint; };
    std::array<juce::Rectangle<int>, 4> rowRects;
    int dadoX = 0;                                                    // divisor del cluster VARIATION/MUTATE
    juce::Label hintBar;
    std::unordered_map<juce::Component*, juce::String> hints;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlStrip)
};
}
