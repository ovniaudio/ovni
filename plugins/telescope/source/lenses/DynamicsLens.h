#pragma once
#include <vector>
#include "analysis/History.h"
#include "lenses/Lens.h"
#include "ui-kit/KnobLookAndFeel.h"
#include "ui-kit/OvniKnob.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// DYNAMICS — la lente 2. No mide cuán FUERTE está la mezcla (eso es LOUDNESS): mide cuánto MARGEN le
// queda, que es la pregunta que uno se hace antes de mandar a masterizar.
//
//   héroe    PSR en vivo (TP de los últimos 3 s - short-term) sobre una escala 0…20 dB con la línea de
//            referencia en 8 dB rotulada. Es REFERENCIA, no veredicto: la lente no dice "está aplastado",
//            dibuja dónde cae el número respecto de un umbral citado. Quien concluye es VERDICT (lente 13).
//            Al lado, el PLR desde el reset (TP máx - integrado, AES TD1004).
//   centro   HISTOGRAMA de short-term: 61 barras de 1 LU (-60…0), marcas cada 6, el bin actual resaltado.
//            Un tema comprimido es un pico angosto; uno con dinámica, una montaña ancha.
//   abajo    CLIPS: el contador, el knob de umbral (-3…0 dBTP) y la línea de tiempo de 10 minutos con una
//            marca por segundo en el que hubo eventos. Cambiar el umbral REINICIA el conteo (ver README).
//
// RESET y PAUSE son los mismos botones (y la misma acción) que LOUDNESS: el análisis es uno solo.
// ========================================================================================================
class DynamicsLens : public Lens
{
public:
    // 56: por debajo de este PSR la barra se pone en alerta. 4 dB es donde una mezcla ya está aplastada
    // contra el limitador; entre 4 y la referencia de 8 va en ámbar. La REFERENCIA sigue siendo la del
    // spec §5.8 y el número sigue sin colorearse: lo que se colorea es la barra.
    static constexpr float kPsrAlert = 4.0f;

    explicit DynamicsLens (TelescopeProcessor& p);
    ~DynamicsLens() override;

    juce::String name() const override            { return kLensNames[(int) LensId::dynamics]; }
    LensId       id() const override              { return LensId::dynamics; }
    juce::uint32 requiredModules() const override { return kLoudness; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    // Escala del héroe: 0…20 dB de PSR, con la referencia del spec §5.8 en 8 dB.
    static constexpr float kPsrMin = 0.0f, kPsrMax = 20.0f, kPsrReference = 8.0f;
    // Histograma: 61 bins de 1 LU centrados en -60…0.
    static constexpr int   kBins = 61, kBinsFloorLufs = -60;
    static constexpr int   kTimelineSeconds = 10 * 60;   // los 10 min del ring de ClipHistory

    struct Zones
    {
        juce::Rectangle<int> header, psrBar, histogram, clips, timeline, knob, footer, resetBtn, pauseBtn;
    };
    Zones zonesFor (int w, int h) const;

    void paintHero (juce::Graphics&) const;
    void paintHistogram (juce::Graphics&) const;
    void paintClips (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String&, bool active, bool hovered) const;

    TelescopeProcessor& processor;

    ovni::ui::OvniKnob         threshold;
    ovni::ui::KnobLookAndFeel  knobLaf;

    float        dispPsr = 0.0f;
    AnalysisFrame::Loudness latest {};
    float        psr = 0.0f, plr = 0.0f;
    bool         psrValid = false, plrValid = false;
    juce::uint32 clipEvents = 0;
    juce::uint32 histogram[kBins] {};
    juce::uint32 histMax = 1;
    int          currentBin = -1;

    // Lo que advanceFrame() compara para decidir si hay que repintar (ver la nota ahí): el PSR suavizado
    // no alcanza, porque la lente dibuja además clips, histograma y PLR.
    juce::uint32 lastClipEvents = 0, lastHistSum = 0;
    int          lastBin = -1;
    float        lastPlr = 0.0f;
    bool         lastPlrValid = false;

    std::vector<juce::uint32> timelineBuf;
    Zones zones {};
    int   hovered = -1;   // 0 = RESET, 1 = PAUSE

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicsLens)
};
}
