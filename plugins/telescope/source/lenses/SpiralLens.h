#pragma once
#include <vector>
#include "analysis/CqtFrame.h"
#include "analysis/modules/Cqt.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"
#include "lenses/NoteName.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// SPIRAL — la lente 7: EL MISMO CONSTANT-Q, ENROLLADO.
//
// La idea es de una sola línea: si una octava es una vuelta, las notas IGUALES quedan en el mismo ángulo.
// Un Do en C2, otro en C4 y otro en C6 dejan de ser tres barras lejanas en un eje largo y pasan a ser tres
// puntos ALINEADOS sobre el mismo radio. Eso es lo que la lente 6 no puede mostrar y ésta sí: la
// estructura de octavas de lo que está sonando, de un vistazo.
//
//     ángulo  = clase de nota   ·  Do arriba, en sentido horario, una vuelta = una octava
//     radio   = octava          ·  A0 adentro, el bin más agudo afuera, lineal por octava (las vueltas
//                                  quedan equidistantes: una octava ocupa lo mismo en el grave que en el
//                                  agudo, que es justo lo que un eje de frecuencias no puede hacer)
//     púa     = magnitud        ·  cada bin sale hacia afuera desde su vuelta, con el brillo y el grosor
//                                  siguiendo el nivel. Por debajo del piso del rango no se dibuja NADA
//                                  (un punto tenue en el ruido de fondo sería inventar una nota).
//
//   centro    la rueda de croma: doce sectores con la intensidad del cromagrama suavizado, y adentro la
//             tonalidad estimada con su confianza. Es el mismo dato que la lente 6 muestra en barras;
//             acá cierra el círculo, literalmente: los sectores están en el MISMO ángulo que las púas.
//   al pasar  nota, octava y dB. La octava sale del RADIO y la clase del ÁNGULO — que es exactamente cómo
//             se lee el dibujo.
//
// REDUCED MOTION: sin decaimiento visual. Las púas siguen el frame tal cual, sin caer suave.
// ========================================================================================================
class SpiralLens : public Lens
{
public:
    // 56: desde qué nivel una púa se considera FUERTE y recibe el glow. Por debajo no brilla nada: un
    // glow en todas las púas no marcaría ninguna.
    static constexpr float kGlowFrom = 0.55f;

    explicit SpiralLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::spiral]; }
    LensId       id() const override              { return LensId::spiral; }
    juce::uint32 requiredModules() const override { return kCqt; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // ---- EL MAPEO, puro y público: es una definición y se testea sola ----
    // `turns` son vueltas contadas desde Do0 (C0 = 16.3516 Hz), así que la parte ENTERA es la octava
    // científica y la FRACCIONARIA es la clase de nota (0 = Do, 0.75 = La). Con f_min = A0 = 27.5 Hz la
    // cuenta sale exacta: log2(27.5 / 16.3516) = 0.75.
    struct Position
    {
        double turns = 0.0;
        int    octave = 0;          // la octava científica (A0 -> 0, A4 -> 4)
        float  classFraction = 0.0f;// 0 = Do, 1/12 = Do#, … 9/12 = La
        float  angleRad = 0.0f;     // desde las 12, en sentido horario
    };
    static Position positionFor (int bin, int binsPerOctave);

    float              radiusForTurns (double turns) const;
    juce::Point<float> pointForTurns (double turns, float classFraction) const;

    struct Readout
    {
        bool  valid = false;
        int   bin = -1;
        int   octave = 0;
        double freqHz = 0.0;
        float db = 0.0f;
        NoteReadout note;
    };
    Readout readoutAt (juce::Point<int> p) const;

    enum Control { ctrlChannel = 0, ctrlChroma, kNumControls };
    void cycleControl (int control);

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr float kRelease = 0.35f;

    struct Zones
    {
        juce::Rectangle<int> plot, keyText, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    void  updateGeometry();
    float levelAt (int bin) const;          // 0…1 sobre el rango de dB vigente
    void  paintWheel (juce::Graphics&) const;
    void  paintReadout (juce::Graphics&) const;
    void  paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                       const juce::String& value, bool hovered) const;

    TelescopeProcessor& processor;

    juce::uint32 lastFrameIndex = 0xffffffffu;
    int    binsSeen = 0, bpoSeen = Cqt::kBinsPerOctave;
    float  fMinSeen = (float) Cqt::kFMinHz;
    float  latencySeen = 0.0f;

    std::vector<float> targetDb, dispDb;
    float chromaSm[CqtFrame::kNumClasses] {}, dispChroma[CqtFrame::kNumClasses] {};
    int   keyTonic = -1, keyMode = -1;
    float keyConfidence = 0.0f, keyTimeFraction = 0.0f;

    // Geometría del enrollado, recalculada al cambiar de tamaño o de cantidad de bins.
    juce::Point<float> centre { 0.0f, 0.0f };
    float  rInner = 0.0f, rOuter = 0.0f, ringStep = 0.0f;
    double turnsMin = 0.75, turnsMax = 10.25;

    Zones zones {};
    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };
    int   lastRangeDb = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpiralLens)
};
}
