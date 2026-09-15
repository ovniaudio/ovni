#pragma once
#include <vector>
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Spectrum.h"
#include "lenses/Lens.h"
#include "lenses/Raster.h"
#include "lenses/NoteName.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// SPECTRUM — la lente 3, y la que más gente va a abrir. Un analizador de espectro completo:
//
//   arriba      el espectro sobre eje de frecuencia LOG (20 Hz - 20 kHz) con rejilla de décadas y ⅓ de
//               octava, y eje de dB con 0 arriba y el rango elegido (60 / 90 / 120 dB) hacia abajo.
//               Área rellena + línea; en L+R, dos espectros con los dos colores del tema. El peak hold va
//               como línea fina encima. En modo ⅓ de octava o Bark, barras en vez de curva.
//   al pasar    línea vertical + lectura: frecuencia, NOTA con sus cents (A4 = 440) y el dB del bin o de
//               la banda que está debajo del cursor. La nota es lo que convierte "hay algo en 82 Hz" en
//               "es un mi grave" sin salir del plugin.
//   abajo       la tira de controles (patrón de SCOPE, cada botón CICLA su opción): tamaño de FFT,
//               ventana, solape, canal, modo de bandas · slope, promediado, hold, rango.
//
// EL DIBUJO ES POR COLUMNA DE PÍXEL, CON MAX-HOLD. Con orden 15 hay 16 385 bins para ~1 000 píxeles: una
// muestra por píxel se comería 15 de cada 16 picos, y justo los picos son lo que uno mira. Cada columna
// toma el MÁXIMO de los bins que le tocan (y donde sobran píxeles por bin, interpola en dB). Nunca se
// generan más vértices que píxeles — es la misma lección que el osciloscopio de SCOPE.
//
// REDUCED MOTION: sin suavizado entre frames. La curva se dibuja tal cual llega, un cuadro coherente.
// ========================================================================================================
class SpectrumLens : public Lens
{
public:
    // ===== 57c · EL RECTÁNGULO DE LA CACHÉ, para VISUAL[hd] =====
    //
    // La mutación de la auditora sobre el 57b encontró que el test medía el gradiente del PANEL ENTERO, y
    // ahí la rejilla, los textos y los trazos vectoriales —que se dibujan a escala física siempre— tapan
    // lo que hace la caché: con la caché forzada de vuelta a 1×, SPECTRUM seguía dando 3.17. Midiendo
    // sólo adentro de este rectángulo, la razón habla de la caché y de nada más.
    juce::Rectangle<int> cacheAreaForTest() const noexcept { return zones.plot; }
    // 57c — la escala física con la que se horneó la caché. VISUAL[hd] lo verifica además de medir la
    // nitidez: es la comprobación ESTRUCTURAL de la regla de lenses/Raster.h, y la que hace imposible
    // que una mutación de esa clase pase inadvertida.
    float cacheScaleForTest() const noexcept { return curveCache.scale(); }

    explicit SpectrumLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::spectrum]; }
    LensId       id() const override              { return LensId::spectrum; }
    juce::uint32 requiredModules() const override { return kSpectrum; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // ---- lectura de nota ----
    // La DEFINICIÓN vive en lenses/NoteName.h desde el prompt 52 (la piden también CQT y SPIRAL). Acá
    // queda el nombre con el que la llaman el test del 50 y la propia lente: mover la fórmula no puede
    // significar renombrar lo que ya estaba probado.
    using NoteReadout = telescope::NoteReadout;
    static NoteReadout noteFor (double freqHz) { return noteForFrequency (freqHz); }

    // Lo que devuelve la lectura bajo el cursor en la coordenada x (en píxeles del componente). Pública
    // para que el test pueda pedirla sin fabricar eventos de mouse.
    struct Readout { bool valid = false; double freqHz = 0.0; float db = 0.0f; NoteReadout note; };
    Readout readoutAtX (int x) const;

    // Los nueve controles, en el orden en que se dibujan. Público para que el test los cicle sin clicks.
    enum Control { fftSize = 0, window, overlap, channel, bandsMode, slope, average, hold, range,
                   smooth, kNumControls };
    void cycleControl (int control);

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr float  kRelease = 0.35f;   // la curva SUBE de una y baja suave (es un medidor)
    static constexpr int    kScaleW  = 38;      // ancho del canal de etiquetas de dB
    static constexpr int    kAxisH   = 15;      // alto de la tira de frecuencias

    struct Zones
    {
        juce::Rectangle<int> plot, dbScale, freqAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    double freqAtX (int x) const;               // x en coords del componente
    float  xForFreq (double hz) const;

    void rebuildColumns (const SpectrumFrame& f);
    // 57b — EL RASTERIZADOR PROPIO. Arma las curvas a resolución de DISPOSITIVO sobre la caché y la
    // devuelve al plano lógico de un solo blit. Ver el bloque grande de SpectrumLens.cpp.
    void rasteriseCurves (float physScale);
    void buildDeviceCurve (int slot, int deviceW, float physScale, int smoothRadius);
    void paintCurve (juce::Graphics&, int slot, juce::Colour) const;
    void paintBars (juce::Graphics&, int slot, juce::Colour) const;
    void paintReadout (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;
    float yForDb (float db) const;

    TelescopeProcessor& processor;

    // Los settings CACHEADOS. Viven en un ValueTree (11 propiedades con búsqueda por nombre): pedirlos
    // dentro de yForDb() —que corre una vez por columna de píxel— costaba 10 ms de pintado. Se refrescan
    // una vez por frame y una vez por horneado, que es cuando pueden cambiar.
    Spectrum::Settings sets;

    // Estado del último frame leído (no se copia el SpectrumFrame entero: son 260 KB).
    juce::uint32 lastFrameIndex = 0xffffffffu;
    int          fftSizeSeen = 0, numBinsSeen = 0, channelSeen = 0;
    double       srSeen = 48000.0;

    // Una entrada por COLUMNA DE PÍXEL del plot (ver el encabezado).
    std::vector<float> colDb[SpectrumFrame::kMaxSpectra];
    std::vector<float> colHold[SpectrumFrame::kMaxSpectra];
    std::vector<float> dispDb[SpectrumFrame::kMaxSpectra];   // la curva suavizada que se dibuja

    // ===== 57b: el camino de DIBUJO, en píxeles de dispositivo =====
    // `dev*` son las columnas de DISPOSITIVO (interpoladas desde las lógicas, ver buildDeviceCurve): son
    // lo que se DIBUJA. Los números del readout y los tests de bin siguen leyendo `colDb`, que es el DATO.
    raster::Cache      curveCache;
    std::vector<float> devY[SpectrumFrame::kMaxSpectra];      // y de la curva, en px de dispositivo
    std::vector<float> devHoldY[SpectrumFrame::kMaxSpectra];  // ídem del peak hold
    std::vector<float> devScratch, devSlope;                  // buffers del interpolador y del suavizado
    float bandDb  [SpectrumFrame::kMaxSpectra][SpectrumFrame::kNumThird] {};
    float bandHold[SpectrumFrame::kMaxSpectra][SpectrumFrame::kNumThird] {};
    float barkDb  [SpectrumFrame::kMaxSpectra][SpectrumFrame::kNumBark] {};

    Zones zones {};
    int   hovered = -1;      // botón bajo el cursor
    int   cursorX = -1;      // columna bajo el cursor dentro del plot, -1 = afuera
    int   lastRangeDb = 0;   // para rehornear la rejilla cuando cambia la escala

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumLens)
};
}
