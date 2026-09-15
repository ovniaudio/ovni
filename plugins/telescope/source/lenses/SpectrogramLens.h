#pragma once
#include <array>
#include <vector>
#include "analysis/SpectrogramRing.h"
#include "analysis/modules/Spectrum.h"
#include "lenses/Lens.h"
#include "lenses/Raster.h"
#include "lenses/SpectrogramScroll.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// SPECTROGRAM — la lente 4. El sonograma: frecuencia en Y (log, 20 Hz - 20 kHz), tiempo en X (la historia
// elegida hasta "ahora" a la derecha) y el nivel en el COLOR.
//
// SE PINTAN SÓLO LAS COLUMNAS NUEVAS. La imagen se corre a la izquierda con moveImageSection y las
// columnas nuevas entran por la derecha. Redibujar los ~500 000 píxeles en cada frame costaría todo el
// presupuesto para volver a dibujar lo mismo corrido un pixel.
//
// PALETA de 256 entradas armada desde el Theme del sello: piso = fondo, medio = acento de la familia,
// tope = brillo. Ni un color a mano — si el sello cambia de hue, el espectrograma cambia con él.
//
// EL MAPEO TIEMPO → PÍXEL se hace en dos pasos y siempre en píxeles ENTEROS: primero se agrupan columnas
// del ring (tomando el máximo), y después los grupos se reparten sobre el ancho con un acumulador de
// Bresenham, de a 1 o 2 píxeles. Así la historia elegida entra JUSTA en el plot —ni sobra panel negro ni
// se cae tiempo por el borde— y el desplazamiento incremental sigue siendo de píxeles enteros, sin la
// vibración de medio pixel que daría un mapeo fraccionario. El eje de tiempo rotula lo que realmente se ve.
//
// REDUCED MOTION: el espectrograma SIGUE corriendo. No es decoración que se pueda congelar — el movimiento
// ES el dato (el eje X es el tiempo). Lo que se apaga en otras lentes son estelas y suavizados; acá no hay
// ninguno de los dos. Está dicho en el README.
// ========================================================================================================
class SpectrogramLens : public Lens
{
public:
    explicit SpectrogramLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::spectrogram]; }
    LensId       id() const override              { return LensId::spectrogram; }
    juce::uint32 requiredModules() const override { return kSpectrum; }
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

    // Fuerza el redibujo COMPLETO en el próximo pintado (lo usa el resize, el cambio de settings y el test
    // de presupuesto, que tiene que medir el peor caso y no sólo el desplazamiento de un pixel).
    void rebuildOnNextPaint() noexcept { scroll.invalidate(); }

    // La lectura bajo el cursor. `db` sale del DATO (el byte del anillo con la misma agrupación con la
    // que se pintó la columna), NO del color del píxel: la paleta redondea a 8 bits y tiene diez pares de
    // entradas idénticas, así que buscar "el color más parecido" mentía un escalón entero en esos niveles
    // (nit del revisor del 50). `valid` es false si la columna que hay bajo el cursor ya no está en el
    // anillo: sin dato no se inventa un número.
    struct Readout { bool valid = false; double secondsAgo = 0.0, freqHz = 0.0; float db = 0.0f; };
    Readout readoutAt (juce::Point<int> p) const;

    enum Control { history = 0, range, channel, ctrlPalette, kNumControls };
    void cycleControl (int control);

    // Segundos que entran en el ancho del plot con el mapeo entero vigente (lo rotula el eje de tiempo).
    double visibleSeconds() const;

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr int kAxisW = 40;    // canal de etiquetas de frecuencia
    static constexpr int kTimeH = 15;    // tira de tiempo

    struct Zones
    {
        juce::Rectangle<int> plot, freqAxis, timeAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;

    void buildPalette();
    void updateImage();                   // desplaza y pinta lo nuevo (o rehace todo si hace falta)
    // Pinta el grupo YA AGRUPADO en sus `pixels` columnas. La BitmapData viene de afuera y vive UN
    // redibujo entero: construir una por columna de píxel eran ~950 construcciones por redibujo completo,
    // y no dibujaban nada (misma corrección que en la lente 10, MEDIUM de la auditora del 51).
    void drawGroupInto (const juce::Image::BitmapData& bd, int firstPx, int pixels,
                        const juce::uint8* column) const;
    // Agrupa `columnsPerGroup` columnas del anillo tomando el máximo. Devuelve cuántas encontró: 0
    // significa que el anillo ya las descartó (el lector se atrasó), y no hay dato que leer.
    int  columnMax (const SpectrogramRing&, long long firstSrc, juce::uint8* dst) const;
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
    // El mapeo tiempo→píxel y fila→píxel, el Bresenham y el registro de fuentes: compartido con la lente
    // 10, que dibuja exactamente lo mismo con otro color (ver lenses/SpectrogramScroll.h).
    SpectrogramScroll scroll;

    std::array<juce::uint32, 256> palette {};
    // 57b — la rampa elegida con la que se horneó `palette`. Si el setting cambia hay que rehacer la
    // tabla Y el sonograma entero: los píxeles ya dibujados llevan los colores viejos adentro.
    int paletteSeen = -1;
    std::array<juce::uint8, (size_t) SpectrogramRing::kRows> scratchCol {};

    Zones zones {};
    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramLens)
};
}
