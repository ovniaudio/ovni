#pragma once
#include <array>
#include <vector>
#include "analysis/SpectrogramRing.h"
#include "analysis/modules/Spectrum.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"
#include "lenses/Projection2p5.h"
#include "lenses/Raster.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// WATERFALL — la lente 5. El espectrograma EN PROFUNDIDAD: X = frecuencia (log), Y = nivel, Z = tiempo.
// "Ahora" es la línea de adelante y el pasado se va al fondo.
//
// QUÉ AGREGA sobre el sonograma de la lente 4, que muestra los mismos datos. El sonograma pone el nivel en
// el COLOR, y el ojo humano compara colores mal: dos verdes a 6 dB de distancia se ven casi iguales, y un
// pico angosto de 12 dB sobre su entorno pasa desapercibido. Acá el nivel es ALTURA, que es la magnitud que
// el ojo compara mejor que ninguna. A cambio se pierde lo que el sonograma hace mejor: con 120 líneas
// tapándose entre sí, un evento corto puede quedar escondido detrás de uno posterior. Son complementarias,
// y por eso están las dos.
//
// SIN GPU (D-46): proyección oblicua por software, ≤ 120 líneas × 256 puntos. Ver lenses/Projection2p5.h.
//
// ================= LA OCLUSIÓN, y por qué se dibuja del FRENTE HACIA EL FONDO =================
//
// El algoritmo del pintor —de atrás hacia adelante, cada línea rellenando por debajo con el fondo antes de
// trazarse— da el dibujo correcto y es imposible de pagar: el relleno de cada línea cubre desde su curva
// hasta el suelo de su plano, y con 120 líneas sobre un plot de 950×400 eso son decenas de millones de
// píxeles por frame. Medido en el papel: ~28 M píxeles con material normal, contra un presupuesto de
// 4 ms. Ni con memset puro entra.
//
// Acá se dibuja al revés, con un HORIZONTE, y el resultado en pantalla es EL MISMO. La equivalencia se
// apoya en una propiedad de la proyección (que su test verifica): `py` es estrictamente decreciente en z,
// así que el suelo de un plano más lejano está SIEMPRE por encima del suelo de uno más cercano, y como
// ninguna curva baja de su propio suelo, ninguna curva lejana puede caer por debajo del suelo de una
// cercana. Entonces, en una columna de píxel dada:
//
//     el relleno de una línea cercana tapa a una lejana  ⟺  la lejana está por debajo de la CURVA cercana
//
// (el suelo nunca entra en la cuenta). O sea que una línea es visible en esa columna exactamente cuando su
// curva queda por ENCIMA de todas las curvas que tiene delante — que es el test del horizonte. Se recorre
// del frente al fondo, se dibuja sólo lo que pasa el test y se baja el horizonte. Costo: O(líneas × ancho)
// comparaciones y sólo los píxeles que de verdad se ven.
//
// SE DIBUJA A PÍXEL, no con Path: son hasta 120 polilíneas de 256 puntos y el rasterizador con antialias
// cuesta un orden de magnitud más que escribir el ARGB. Es la misma decisión que la lente 4 con su columna.
//
// REDUCED MOTION: la lente SIGUE corriendo, igual que los dos sonogramas. Su eje de profundidad ES el
// tiempo; congelarla no sería menos movimiento, sería dejar de mostrar el dato. Y no hay nada suavizado
// que apagar: cada línea es una columna del anillo tal cual, sin promediar entre frames.
// ========================================================================================================
class WaterfallLens : public Lens
{
public:
    explicit WaterfallLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::waterfall]; }
    LensId       id() const override              { return LensId::waterfall; }
    juce::uint32 requiredModules() const override { return kSpectrum; }
    // El idioma sale del estado del procesador (ver Lens::tr()).
    const juce::ValueTree& stateTree() const override;

    static constexpr int kMaxLines  = 120;   // el tope del spec §4
    static constexpr int kMaxPoints = 256;   // ídem: las 512 filas del anillo decimadas de a 2

    // Las tres inclinaciones del setting `waterfallTilt`. `tilt` es cuánto del alto se lleva la
    // profundidad y `depth` cuánto se encoge la escena al fondo; van de a pares porque una fuga fuerte con
    // el plano casi plano se ve como un error de dibujo, no como perspectiva.
    static constexpr int   kNumTilts = 3;
    static constexpr float kTiltOptions [kNumTilts] = { 0.22f, 0.38f, 0.55f };
    static constexpr float kDepthOptions[kNumTilts] = { 0.10f, 0.20f, 0.32f };

    // ====================================================================================================
    // LA SELECCIÓN DE COLUMNAS, pura y entera para que se pueda testear sin una ventana.
    //
    // Se reparten `wanted` líneas sobre las `available` columnas que el anillo todavía tiene, de la MÁS
    // VIEJA (línea 0) a la más nueva (línea n−1 = la columna que se acaba de escribir). Aritmética entera
    // sobre `writeIndex` y `available`: el mismo anillo da SIEMPRE las mismas columnas, sin depender de
    // relojes ni del orden en que se pintó.
    //
    // Cuando el anillo tiene menos columnas que líneas pedidas se dibujan `available` líneas y no
    // `wanted`: repetir la misma columna dos veces dibujaría un relieve que la señal no tiene.
    // ====================================================================================================
    static int selectColumns (long long writeIndex, int available, int wanted, long long* dst) noexcept;

    // ====================================================================================================
    // ===== 57b: EL COLOR DE UNA LÍNEA, en un solo lugar =====
    //
    // Lo comparten la lente y el pintor bruto de [horizonte], que re-implementa a propósito el ORDEN de
    // dibujo (de atrás hacia adelante, en vez de la oclusión por horizonte) para comparar los dos
    // dibujos píxel a píxel. Lo que ese test afirma es que las DOS maneras pintan lo mismo — no que la
    // fórmula del color sea la correcta —, así que la fórmula puede (y debe) vivir una sola vez: si
    // estuviera copiada, una de las dos copias se iba a quedar vieja y el test se pondría rojo por un
    // motivo que no es el suyo.
    //
    // QUÉ CAMBIÓ EN EL 57b, y por qué. «Waterfall: color más profesional» (Joaquín contra Insight). Antes
    // TODA la línea era de un color —el de su profundidad—, así que la altura decía el nivel y el color
    // no decía nada. Ahora el color de cada PUNTO sale de la rampa elegida según SU nivel: la altura y el
    // color dicen lo mismo, que es lo que hacen Ozone e Insight y lo que permite leer un pico de reojo.
    // Encima va la NIEBLA DE PROFUNDIDAD (las líneas del fondo se mezclan hacia el fondo hasta el 55 %),
    // que es lo que separa las capas sin tener que dibujar sombras.
    // ====================================================================================================
    struct Shading
    {
        look::PaletteId palette = look::PaletteId::ovni;
        float fog   = 0.0f;      // 0 = la línea de adelante, kMaxFog = la del fondo
        bool  front = false;     // la línea de "ahora": lleva un realce chico para encontrarse sin buscarla

        static constexpr float kMaxFog     = 0.55f;
        static constexpr float kFrontLift  = 0.15f;   // cuánto se aclara la línea de adelante
        static constexpr int   kFillRampN  = 5;       // filas bajo la línea que llevan el degradado

        // El brillo del relleno según a cuántos píxeles está de la línea (0 = pegado). Es "el degradado
        // que aclara cerca de la línea" del 57b: el relleno deja de ser una mancha plana y el borde de
        // arriba de cada lámina se lee como un filo.
        static constexpr float kFillLevel[kFillRampN] = { 0.62f, 0.52f, 0.44f, 0.38f, 0.30f };

        // EL RELLENO SE INDEXA POR NIVEL GRUESO (64 escalones en vez de 256). La tabla se arma UNA VEZ
        // POR LÍNEA y hay hasta 120 líneas por frame, así que armar 5 × 256 entradas costaba más que
        // pintarlas: con 64 escalones son 5 × 64. Es invisible — el relleno es la parte oscura y hundida
        // del dibujo, donde un escalón de 1/64 de la rampa no se distingue —, y el TRAZO, que es lo que
        // se mira, sigue con los 256 niveles enteros.
        static constexpr int kFillLevels = 64;
        static constexpr int fillIndex (int level255) noexcept { return (level255 * kFillLevels) >> 8; }

        // Las dos tablas por línea: el trazo (256 niveles) y las cinco filas del relleno (kFillLevels).
        void buildLuts (juce::uint32* lineLut /* 256 */,
                        juce::uint32* fillLut /* kFillRampN × kFillLevels */) const;
    };

    // El suavizado de pantalla de UNA línea: núcleo [1 2 1] sobre los 256 puntos, que sobre este eje
    // (log de frecuencia) son ~1/12 de octava. Es de DIBUJO — el readout y la lectura leen el anillo.
    static void smoothPoints (juce::uint8* pts, int n) noexcept;

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // Fuerza el redibujo completo (resize, cambio de settings, test de presupuesto). Acá TODO frame es un
    // redibujo completo —la escena entera se mueve al entrar una columna—, así que sólo invalida la capa
    // estática; existe para que las lentes se manejen igual desde afuera.
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

    void rebuildOnNextPaint() noexcept { invalidateStatic(); }

    // La lectura bajo el cursor: la línea de ADELANTE (la de "ahora"), que es la única que se puede leer
    // sin ambigüedad — en profundidad, una misma columna de píxel cae sobre varias líneas a la vez.
    // `db` sale del DATO del anillo con la misma decimación con la que se dibujó, no del color del píxel
    // (el nit del revisor del 50: la paleta redondea a 8 bits y mentiría un escalón).
    struct Readout { bool valid = false; double freqHz = 0.0; float db = 0.0f; };
    Readout readoutAt (juce::Point<int> p) const;

    // Segundos que cubren las líneas dibujadas (de la más vieja a "ahora").
    double visibleSeconds() const;
    int    lineCount() const noexcept { return lines; }

    // Los rótulos del eje de TIEMPO: su texto y la caja que los tapa. Uno solo los calcula —el dibujo
    // y el test leen de acá— porque el criterio del test es "bajo la caja no se ve el relleno" y con
    // una copia de la geometría estaría midiendo su propia cuenta, no la de la lente.
    struct TimeLabel { juce::Rectangle<int> box; juce::String text; };
    std::vector<TimeLabel>   timeLabels() const;
    juce::Array<juce::Rectangle<int>> timeLabelBoxes() const;
    juce::Rectangle<int>     plotArea() const noexcept { return zones.plot; }

    enum Control { ctrlLines = 0, ctrlTilt, ctrlHistory, ctrlRange, ctrlPalette, kNumControls };
    void cycleControl (int control);

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr int kAxisW = 44;   // canal de etiquetas de nivel
    static constexpr int kFreqH = 15;   // tira de frecuencia

    struct Zones
    {
        juce::Rectangle<int> plot, levelAxis, freqAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;
    Projection2p5 projectionFor (const Zones&) const;

    void updateImage();
    // El SUELO del escenario. Va en la capa VIVA y no en la estática porque la imagen del plot es opaca y
    // se dibuja encima: lo que la estática pinte adentro del plot queda tapado. Son cuatro líneas.
    void drawStage (juce::Graphics&) const;
    // Las 256 alturas (bytes del anillo) de una columna. Devuelve false si el anillo ya la descartó.
    bool readLine (long long src, juce::uint8* dst256) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;

    // x ∈ [0,1] del plano ↔ frecuencia: las 512 filas del anillo son log de 20 Hz a 20 kHz.
    static double xForFreq (double hz) noexcept;
    static double freqForX (double x01) noexcept;

    TelescopeProcessor& processor;

    // ===== 57c · LA CACHÉ DE PÍXELES, EN raster::Cache =====
    //
    // Era un `juce::Image` más un `float rasterScale` y la lógica de "¿cambió el tamaño o la escala?"
    // copiada a mano, en cinco lentes. Eso es lo que encontró la mutación de la auditora: poniendo la
    // caché de `raster::Cache` de vuelta a 1× —el defecto que el 57b vino a cerrar— `VISUAL[hd]` seguía
    // VERDE, porque de las seis lentes que mide, cinco no pasaban por esa clase. Ahora las seis sí: una
    // mutación de lenses/Raster.h las tumba a todas.
    raster::Cache cache;           // EN PÍXELES DE DISPOSITIVO (ver lenses/Raster.h)
    Projection2p5 proj;
    Zones         zones {};

    std::array<long long, (size_t) kMaxLines> srcIdx {};
    std::array<juce::uint8, (size_t) kMaxPoints> linePts {};
    std::vector<int> horizon;      // por columna de píxel, la y más alta ya pintada
    int              lines = 0;
    long long        lastWrite = -1;
    int paletteSeen = -1;     // 57b — la rampa vista en el último frame (ver Palettes.h)

    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaterfallLens)
};
}
