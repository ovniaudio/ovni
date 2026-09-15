#pragma once
#include <array>
#include <vector>
#include "analysis/SpectrogramRing.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/StereoBands.h"
#include "lenses/Lens.h"
#include "lenses/Raster.h"
#include "lenses/SpectrogramScroll.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// STEREO SPECTROGRAM — la lente 10. El mismo sonograma de la 4 (frecuencia log en Y, tiempo en X, la
// historia elegida hasta "ahora" a la derecha) con el COLOR cambiado de significado:
//
//     el color  es la FASE (coherencia): rojo = fuera de fase · verde = ancho · blanco = mono
//     el brillo es el NIVEL: por debajo del piso del rango, negro
//
// Un sonograma normal contesta "qué hay y cuándo". Éste contesta "qué de todo eso se te va a caer cuando
// alguien lo escuche en un parlante mono", y CUÁNDO exactamente. Es la lente 9 (BAND CORRELATION) con el
// tiempo puesto: donde aquella promedia una ventana, ésta muestra la película.
//
// LA CELDA QUE DECIDE EL PÍXEL ES LA MÁS FUERTE que cubre. Cuando varias columnas del anillo o varias
// filas caen en el mismo píxel, gana la de más energía y sus DOS números viajan juntos. Promediar
// coherencias de celdas con niveles muy distintos dejaría que un bin vacío —donde la fase es ruido puro—
// decidiera el color de un pixel que en realidad tiene una sola cosa adentro.
//
// El desplazamiento incremental, el Bresenham y el mapeo de filas son los MISMOS de la lente 4
// (lenses/SpectrogramScroll.h): las dos dibujan la misma grilla y lo único que cambia es el color.
//
// REDUCED MOTION: sigue corriendo, igual que el sonograma de nivel. Su eje X es el tiempo; congelarlo no
// sería menos movimiento, sería dejar de mostrar el dato.
// ========================================================================================================
class StereoSpectrogramLens : public Lens
{
public:
    explicit StereoSpectrogramLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::stereoSpectrogram]; }
    LensId       id() const override              { return LensId::stereoSpectrogram; }
    juce::uint32 requiredModules() const override { return kSpectrum | kStereoBands; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

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
    float cacheScaleForTest() const noexcept { return cache.scale(); }

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Fuerza el redibujo COMPLETO en el próximo pintado (resize, cambio de settings, y el test de
    // presupuesto, que tiene que medir el peor caso y no sólo el desplazamiento de un píxel).
    void rebuildOnNextPaint() noexcept { scroll.invalidate(); }

    // La lectura bajo el cursor. Los dos números salen del DATO (los bytes del anillo con la misma
    // agrupación con la que se pintó), nunca del color del píxel: ver la lente 4 y el nit del 50.
    struct Readout
    {
        bool   valid = false;
        double secondsAgo = 0.0, freqHz = 0.0;
        float  coherence = 0.0f, db = 0.0f;
    };
    Readout readoutAt (juce::Point<int> p) const;

    enum Control { ctrlHistory = 0, ctrlRange, ctrlWindow, ctrlPalette, kNumControls };
    void cycleControl (int control);

    double visibleSeconds() const;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr int kAxisW   = 40;   // canal de etiquetas de frecuencia
    static constexpr int kTimeH   = 15;   // tira de tiempo
    static constexpr int kLegendH = 16;   // la leyenda de color

    struct Zones
    {
        juce::Rectangle<int> plot, freqAxis, timeAxis, legend, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    void buildPalette();
    void updateImage();
    // Pinta el grupo YA RESUELTO en sus `pixels` columnas de la imagen. La BitmapData viene de afuera y
    // vive UN redibujo entero: construir una por columna de píxel costaba ~950 construcciones por
    // redibujo completo, y era pura ceremonia (MEDIUM de la auditora del 51).
    void drawGroupInto (const juce::Image::BitmapData& bd, int firstPx, int pixels,
                        const juce::uint16* packed) const;
    // Agrupa las columnas del grupo quedándose con la celda MÁS FUERTE de cada fila, y la deja EMPAQUETADA
    // como (energía << 8) | coherencia — el empaquetado es EXPLÍCITO, no un cast a uint16 sobre los bytes
    // del anillo: así el orden de bytes de la máquina no entra en la cuenta. Con la energía en el byte
    // alto, un `max` de uint16 ES "la celda de más energía" y no necesita rama, que es lo que abarata el
    // bucle que corre medio millón de veces por redibujo. Devuelve cuántas columnas encontró: 0 = el
    // anillo ya las descartó, y entonces no hay dato que leer.
    int  columnPick (const StereoSpectrogramRing&, long long firstSrc, juce::uint16* dst) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;
    float yForFreq (double hz) const;

    TelescopeProcessor& processor;

    // ===== 57c · LA CACHÉ DE PÍXELES, EN raster::Cache =====
    //
    // Era un `juce::Image` más un `float rasterScale` y la lógica de "¿cambió el tamaño o la escala?"
    // copiada a mano, en cinco lentes. Eso es lo que encontró la mutación de la auditora: poniendo la
    // caché de `raster::Cache` de vuelta a 1× —el defecto que el 57b vino a cerrar— `VISUAL[hd]` seguía
    // VERDE, porque de las seis lentes que mide, cinco no pasaban por esa clase. Ahora las seis sí: una
    // mutación de lenses/Raster.h las tumba a todas.
    raster::Cache cache;   // el sonograma ya pintado (ver lenses/Raster.h)
    SpectrogramScroll scroll;   // el mapeo y el desplazamiento, compartidos con la lente 4

    // TABLA 2D de 256×256: [coherencia][energía] → ARGB. El color de la fase multiplicado por el brillo
    // del nivel. Se arma una vez (256 KB) porque la alternativa es hacer tres multiplicaciones por cada
    // uno de los ~500 000 píxeles de un redibujo completo.
    std::array<juce::uint32, 256u * 256u> palette {};
    // 57b — la rampa con la que se horneó la tabla. Acá NO cambia el color (el color es la FASE y sigue
    // siendo bipolar): cambia la respuesta del EJE DE NIVEL. Ver buildPalette().
    int paletteSeen = -1;

    // La columna del grupo, empaquetada (ver columnPick). Una entrada por fila del anillo.
    std::array<juce::uint16, (size_t) StereoSpectrogramRing::kRows> packedCol {};

    Zones zones {};
    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };
    int   lastRangeDb = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StereoSpectrogramLens)
};
}
