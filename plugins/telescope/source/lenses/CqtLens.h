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
// CQT — la lente 6: EL ESPECTRO POR NOTAS.
//
// Es el mismo dato que SPECTRUM contestando otra pregunta. SPECTRUM contesta "cuánta energía hay en cada
// frecuencia"; ésta contesta "QUÉ NOTAS ESTÁN SONANDO". El eje X no son hertz repartidos parejo: son los
// 229 bins del constant-Q, dos por semitono, y debajo hay un TECLADO dibujado a escala para que la
// pregunta se conteste mirando, sin traducir nada.
//
//   arriba      una barra por bin sobre el eje de notas, con el eje de dB de SPECTRUM (0 arriba, el rango
//               elegido hacia abajo) y una marca vertical en cada Do (C1 … C8). El peak hold va como línea
//               fina encima, con el mismo decaimiento que SPECTRUM: es la misma perilla.
//   teclado     las teclas blancas y negras de las 114 notas del eje, dibujadas con los colores del tema.
//   al pasar    línea vertical + lectura: nota, octava, cents y el dB del bin de abajo. Los cents salen de
//               la POSICIÓN del cursor, no del bin (que siempre daría 0 o 50): así se lee "A4 +18 ¢".
//   abajo       el CROMAGRAMA: doce barras (Do … Si) con la potencia de cada clase de nota sumada sobre
//               todas las octavas — la barra rellena es el instante, el contorno es la versión suavizada
//               que alimenta la tonalidad — y la TONALIDAD ESTIMADA en texto, siempre con sus dos números:
//
//                   La menor · confianza 0.83 · 91 % del tiempo
//
//               Nunca "La menor" a secas. Es una correlación contra 24 perfiles, no una certeza: una
//               tríada pelada sin bajo puede correlacionar casi igual con dos tonalidades distintas, y el
//               instrumento tiene que dejar ver eso en vez de elegir por el usuario.
//   rótulo      la LATENCIA del bin más grave ("A0 · 1.2 s"). El constant-Q no puede tener resolución de
//               un cuarto de tono en 27.5 Hz sin escuchar un segundo largo; esconderlo sería mentir sobre
//               qué momento del audio está mostrando la parte izquierda del dibujo.
//
// REDUCED MOTION: sin suavizado entre frames. Las barras llegan al valor en un cuadro.
// ========================================================================================================
class CqtLens : public Lens
{
public:
    explicit CqtLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::cqt]; }
    LensId       id() const override              { return LensId::cqt; }
    juce::uint32 requiredModules() const override { return kCqt; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Lo que devuelve la lectura bajo el cursor en la coordenada x (píxeles del componente). Pública para
    // que el test la pida sin fabricar eventos de mouse.
    struct Readout
    {
        bool   valid = false;
        int    bin   = -1;
        double freqHz = 0.0;
        float  db = 0.0f;
        NoteReadout note;
    };
    Readout readoutAtX (int x) const;

    enum Control { ctrlChannel = 0, ctrlChroma, kNumControls };
    void cycleControl (int control);

    // El mapeo del eje, público porque es una definición que se testea sola. La CELDA del bin k está
    // CENTRADA en su frecuencia: xForPosition(k) es donde vive f_k, y la celda va de k−0.5 a k+0.5. Que la
    // barra estuviera corrida media celda respecto de su propia frecuencia era un error de medio cuarto de
    // tono en la lectura del cursor (medido: "A4 +20 ¢" parado en el centro de la barra de A4).
    float  xForPosition (double posInBins) const;
    float  xForBinEdge (int bin) const { return xForPosition ((double) bin - 0.5); }
    double positionAtX (int x) const;
    int    binAtX (int x) const;
    double freqAtX (int x) const;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr float kRelease   = 0.35f;   // la barra SUBE de una y baja suave (es un medidor)
    static constexpr int   kScaleW    = 38;      // canal de etiquetas de dB
    // 56: el teclado sube de 22 a 34 px. A 22 las teclas eran una tira gris sin lectura; a 34 se leen como
    // teclado y cada Do lleva su MARCA DE ACENTO. (El nombre no: se probó y con 114 teclas no entra —
    // ver paintKeyboard(). Los nombres "C1 … C8" viven arriba, en las marcas verticales del plot, que es
    // donde sí hay lugar. Hasta el 56b este comentario y el CHANGELOG decían "el nombre en cada Do".)
    static constexpr int   kKeyboardH = 34;

    struct Zones
    {
        juce::Rectangle<int> plot, dbScale, keyboard, chroma, keyText, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    float yForDb (float db) const;
    void  paintKeyboard (juce::Graphics&) const;
    void  paintReadout (juce::Graphics&) const;
    void  paintChroma (juce::Graphics&) const;
    void  paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                       const juce::String& value, bool hovered) const;

    TelescopeProcessor& processor;

    // Lo último leído del frame (no se copia el CqtFrame entero por frame: la lente sólo necesita esto).
    juce::uint32 lastFrameIndex = 0xffffffffu;
    int    binsSeen = 0;
    float  fMinSeen = (float) Cqt::kFMinHz;
    int    bpoSeen  = Cqt::kBinsPerOctave;
    float  latencySeen = 0.0f;

    std::vector<float> targetDb, dispDb, holdDb;
    float chromaNow[CqtFrame::kNumClasses] {}, chromaSm[CqtFrame::kNumClasses] {};
    float dispChroma[CqtFrame::kNumClasses] {};
    int   keyTonic = -1, keyMode = -1;
    float keyConfidence = 0.0f, keyTimeFraction = 0.0f;

    Zones zones {};
    int   hovered = -1;
    int   cursorX = -1;
    int   lastRangeDb = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CqtLens)
};
}
