#pragma once
#include <array>
#include "analysis/modules/StereoBands.h"
#include "lenses/Lens.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// BAND CORRELATION — la lente 9, y el diferencial del producto. Un correlímetro por FRECUENCIA.
//
// Un correlímetro de banda ancha sobre una mezcla con los graves mono y los agudos abiertos da un número
// intermedio que no dice nada: +0.24 no es "un poco fuera de fase", es "hay dos cosas distintas pasando y
// este medidor no puede verlas". Acá se ven las dos.
//
//   arriba      30 barras BIPOLARES de correlación por ⅓ de octava sobre el eje log de 20 Hz a 20 kHz
//               (la misma rejilla de SPECTRUM): el 0 en el MEDIO, hacia arriba en fase, hacia abajo fuera
//               de fase y en el color de alerta del sello. Líneas de referencia rotuladas en +0.5 y 0.
//   abajo       una segunda fila SELECCIONABLE con la misma rejilla: MONO LOSS (0 … −12 dB), WIDTH (0…2)
//               o BALANCE (±12 dB). La que viene puesta es MONO LOSS, que es la que contesta la pregunta
//               que la gente hace de verdad: "si esto se monofica, ¿qué pierdo y dónde?".
//   arriba de   un RESUMEN en texto: la banda más fuera de fase y la de mayor pérdida al monoficar, con
//   todo        su número. Es una referencia, no un veredicto — VERDICT (lente 13) es la que concluye.
//   al pasar    la banda bajo el cursor con sus cuatro números y CUÁNTOS BINS la midieron: una banda de
//               ⅓ de octava en los graves puede tener un solo bin, y eso hay que poder verlo.
//
// LAS BARRAS VAN ENTRE LOS BORDES REALES de cada banda (fc·2^∓1/6), no en anchos iguales: sobre un eje
// logarítmico el ancho de la barra ES el ancho de la banda, y dibujarlas todas iguales sería un gráfico
// que miente sobre su propio eje (la misma regla que las barras de ⅓ de octava de SPECTRUM).
//
// UNA BANDA SIN NINGÚN BIN no se dibuja en cero: se marca distinto. No es que no haya correlación, es que
// a esa resolución de FFT la banda no se puede medir — y decir "cero" sería inventar una medición.
//
// REDUCED MOTION: las barras llegan al valor en un frame, sin suavizado.
// ========================================================================================================
class BandCorrelationLens : public Lens
{
public:
    explicit BandCorrelationLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::bandCorrelation]; }
    LensId       id() const override              { return LensId::bandCorrelation; }
    juce::uint32 requiredModules() const override { return kSpectrum | kStereoBands; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // La fila secundaria. El orden es el que guarda el estado (`bandsRow`): se AGREGA al final.
    enum Row { rowMonoLoss = 0, rowWidth, rowBalance, kNumRows };

    // 56: desde qué pérdida al monoficar la banda se marca en alerta. NO es -3: -3.01 dB es lo que da por
    // construcción cualquier material decorrelacionado (lo exige tests/StereoBandsTest.cpp:263 para ruido
    // independiente), así que alarmarse ahí sería alarmarse con una mezcla ancha normal. -6 dB es donde se
    // pierde el doble de eso: ahí sí hay cancelación.
    static constexpr float kMonoLossAlertDb = -6.0f;
    enum Control { ctrlWindow = 0, ctrlRow, kNumControls };
    void cycleControl (int control);

    // Lo que hay bajo el cursor. Público para que el test lo pida sin fabricar eventos de mouse.
    struct Readout
    {
        bool   valid = false;
        int    band  = -1;
        double centreHz = 0.0;
        int    bins  = 0;      // 0 = a esta resolución la banda no se puede medir
        float  corr = 0.0f, width = 0.0f, balanceDb = 0.0f, monoLossDb = 0.0f;
    };
    Readout readoutAt (juce::Point<int> p) const;

    // El resumen de arriba, como DATO (el texto se arma con esto).
    struct Summary
    {
        bool  valid = false;
        int   worstPhaseBand = -1, worstMonoBand = -1;
        float worstCorr = 0.0f, worstMonoDb = 0.0f;
        int   measured = 0;      // bandas con medición
    };
    Summary      summary() const;
    juce::String summaryText() const;

    // Los topes de la fila secundaria, públicos porque son parte de la definición de la escala.
    static constexpr float kMonoLossFloorDb = -12.0f;
    static constexpr float kWidthCeil       = 2.0f;
    static constexpr float kBalanceSpanDb   = 12.0f;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr float  kRelease = 0.35f;   // suavizado de las barras (sin reduced-motion)
    static constexpr int    kScaleW  = 44;      // canal de etiquetas de escala
    static constexpr int    kAxisH   = 15;      // tira de frecuencias
    static constexpr int    kHeadH   = 22;      // el resumen

    struct Zones
    {
        juce::Rectangle<int> head, corr, row, scaleCorr, scaleRow, freqAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    float xForFreq (double hz) const;
    double freqAtX (int x) const;
    int    bandAtX (int x) const;             // -1 si el cursor no cae en ninguna banda

    // De valor a y dentro de un rectángulo, para cada escala.
    static float yForCorr (float c, juce::Rectangle<int> r);
    float        yForRow (float v, juce::Rectangle<int> r) const;

    void paintBars (juce::Graphics&, juce::Rectangle<int> area, bool isCorr) const;
    void paintReadout (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;

    TelescopeProcessor& processor;

    // El último frame leído (copia de los 30×4, no del AnalysisFrame entero).
    float corr[StereoBands::kNumBands] {}, width[StereoBands::kNumBands] {};
    float balanceDb[StereoBands::kNumBands] {}, monoLossDb[StereoBands::kNumBands] {};
    int   bins[StereoBands::kNumBands] {};
    float windowSec = 0.0f;
    int   measured  = 0;

    // Lo que se DIBUJA (suavizado). Sin reduced-motion las barras llegan de a poco; con él, de una.
    float dispCorr[StereoBands::kNumBands] {}, dispRow[StereoBands::kNumBands] {};

    int   rowMode = rowMonoLoss;
    Zones zones {};
    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandCorrelationLens)
};
}
