#pragma once
#include <memory>
#include "analysis/ReferenceFrame.h"
#include "lenses/Lens.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// TONAL BALANCE — la lente 12. El programa contra UNA REFERENCIA, comparados por el TILT y no por el nivel.
//
//   arriba      las dos curvas de ⅓ de octava NORMALIZADAS (cada una menos su propio LUFS integrado)
//               sobre la misma rejilla logarítmica de SPECTRUM. El programa en el color de la familia,
//               la referencia en el secundario, cada una rotulada con su nombre y su integrado.
//   abajo       el DELTA como barras de ±12 dB, con una banda de referencia de ±3 dB rotulada. Es una
//               REFERENCIA, no un veredicto: acá no dice "está mal", dice cuánto y dónde. Quien concluye
//               es VERDICT (lente 13), y con la regla escrita al lado.
//   al pasar    la banda bajo el cursor con sus tres números (programa, referencia, delta) y cuántos bins
//               la midieron: una banda de ⅓ de octava en los graves puede tener UN bin, y eso hay que
//               poder verlo antes de creerle a la diferencia.
//   abajo del   RESET (reinicia el promedio del programa, no la referencia), CARGAR y QUITAR.
//   todo
//
// SE ARRASTRA UN ARCHIVO ENCIMA y listo. Se acepta CUALQUIER archivo, no sólo los de extensión conocida:
// si no se puede leer, el mensaje lo dice ("formato no reconocido"). Filtrar por extensión haría que un
// AIFF llamado .dat se rechazara en silencio, que es la peor de las dos respuestas.
//
// LA ESCALA ES FIJA (+6 … −42 dB relativos a la loudness, ver kNormBottomDb). Una escala que se
// auto-ajusta hace que dos
// capturas de la misma mezcla no se puedan comparar, que es justo lo que esta lente existe para hacer.
// (Hasta el 56b este comentario decía −54 y la constante valía −42: el que leía el encabezado sacaba
// mal la cuenta de dónde cae una banda en la pantalla.)
//
// LO QUE HAY QUE ENTENDER DEL DELTA, y está dicho en pantalla: las dos curvas están normalizadas a SU
// loudness, así que el delta es de suma (ponderada) cero. Subirle 6 dB a los agudos no se ve como "+6
// arriba y 0 abajo" sino como "+1.7 arriba y −4.3 abajo" (medido en REF[tilt]): son la misma verdad
// contada a igual volumen, que es como se compara una mezcla contra una referencia.
//
// REDUCED MOTION: las curvas y las barras llegan al valor en un frame, sin suavizado.
// ========================================================================================================
class TonalBalanceLens : public Lens,
                         public juce::FileDragAndDropTarget
{
public:
    explicit TonalBalanceLens (TelescopeProcessor& p);
    ~TonalBalanceLens() override;

    juce::String name() const override            { return kLensNames[(int) LensId::tonalBalance]; }
    LensId       id() const override              { return LensId::tonalBalance; }
    juce::uint32 requiredModules() const override { return kSpectrum | kReference; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // ---- drag & drop ----
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    // Los tres botones, en el orden en que se dibujan. Público para que el test los apriete sin clicks.
    enum Control { ctrlReset = 0, ctrlLoad, ctrlClear, kNumControls };
    void pressControl (int control);

    // El texto de estado, como DATO (la lente lo dibuja; el test lo lee).
    juce::String stateText() const;

    // La lectura bajo el cursor. Pública para que el test la pida sin fabricar eventos de mouse.
    struct Readout
    {
        bool   valid = false;
        int    band  = -1;
        double centreHz = 0.0;
        // 57c — FRACCIONARIO: cuánto de la banda cubre la rejilla de la FFT vigente. Menos de 1 = la
        // banda es más angosta que un bin y su valor es la densidad de ese bin.
        float  bins  = 0.0f;
        bool   comparable = false;   // los DOS lados midieron esta banda Y los dos entran en el plot
        // 57c — por lado: si la curva puede dibujar esta banda (medible Y dentro del plot). Lo usa
        // TONAL[piso] para verificar que ningún vértice cae en la fila del piso.
        bool   liveDrawable = false, refDrawable = false;
        float  liveNorm = 0.0f, refNorm = 0.0f, deltaDb = 0.0f;
    };
    Readout readoutAt (juce::Point<int> p) const;
    // La misma lectura, pedida por índice de banda: [tonal] verifica el contenido del readout sin tener
    // que despejar en qué píxel cae cada banda.
    Readout readoutForBand (int band) const;

    // Topes de las escalas, públicos porque son parte de la definición del dibujo.
    // La ventana de las curvas: 48 dB, FIJA. 48 y no 60 porque una curva de ⅓ de octava normalizada a su
    // loudness vive entre unos -5 y unos -40 dB (los graves arriba, el aire abajo): con 60 dB la mitad del
    // panel quedaba vacía y el tilt —que es lo que hay que ver— se aplastaba contra el techo.
    static constexpr float kNormTopDb    =   6.0f;
    static constexpr float kNormBottomDb = -42.0f;
    static constexpr float kDeltaSpanDb  =  12.0f;
    static constexpr float kDeltaRefDb   =   3.0f;   // la banda de referencia rotulada (NO un veredicto)

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr float  kRelease = 0.35f;
    static constexpr int    kScaleW  = 44;
    static constexpr int    kAxisH   = 15;
    static constexpr int    kHeadH   = 34;

    struct Zones
    {
        juce::Rectangle<int> head, curves, delta, scaleCurves, scaleDelta, freqAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    float  xForFreq (double hz) const;
    double freqAtX (int x) const;
    int    bandAtX (int x) const;
    float  yForNorm (float db) const;
    float  yForDelta (float db) const;

    void paintCurve (juce::Graphics&, const float* norm, const bool* has, juce::Colour) const;
    void paintDeltaBars (juce::Graphics&) const;
    void paintHead (juce::Graphics&) const;
    void paintReadout (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label, bool hovered) const;

    TelescopeProcessor& processor;

    // El último frame leído (copia de lo que se dibuja, no del frame entero).
    ReferenceFrame data;
    float          bins[ReferenceFrame::kNumBands] {};   // 57c — fraccionario (ver Readout::bins)

    // Lo que se DIBUJA (suavizado, salvo con reduced-motion).
    float dispLive [ReferenceFrame::kNumBands] {};
    float dispRef  [ReferenceFrame::kNumBands] {};
    float dispDelta[ReferenceFrame::kNumBands] {};
    bool  liveHas  [ReferenceFrame::kNumBands] {};   // DIBUJABLE: medible y dentro del plot (57c)
    bool  refHas   [ReferenceFrame::kNumBands] {};
    bool  comparable[ReferenceFrame::kNumBands] {}; // bandValid del motor ∧ los dos lados dibujables
    bool  primed = false;   // el primer frame entra de una: un medidor no trepa desde el piso al arrancar

    std::unique_ptr<juce::FileChooser> chooser;

    Zones zones {};
    int   hovered = -1;
    bool  dragOver = false;
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TonalBalanceLens)
};
}
