#pragma once
#include <array>
#include <vector>
#include "analysis/FieldFrame.h"
#include "analysis/modules/Field.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"
#include "lenses/Strings.h"
#include "lenses/Projection2p5.h"
#include "lenses/Raster.h"

namespace telescope
{
class TelescopeProcessor;

// ========================================================================================================
// FIELD — la lente 11. La energía repartida por DIRECCIÓN DE PANEO × FRECUENCIA, con estela temporal, en
// 2.5D por software (D-46, ver lenses/Projection2p5.h).
//
// ================== LA REGLA DE HONESTIDAD DE ESTA LENTE, que es la razón de que exista así ==================
//
// El eje X es PANEO POR ENERGÍA. Dice cuánta energía tiene cada canal en cada frecuencia, y nada más. NO
// dice dónde estaba la fuente: de una mezcla estéreo terminada la posición real no tiene solución única, y
// es exactamente la trampa en la que cayó el ITD de ORBIT (informe 21 de la auditora, 2026-09-03: el
// retardo interaural implementado valía el 8 % del físico y con el signo invertido, y el plugin lo
// llamaba "posición"). Un analizador del sello no puede repetir ese error en la lente que más invita a
// cometerlo.
//
// En concreto:
//   · el eje se rotula L … C … R, NUNCA −90° … +90° (el spec §5.5 propone lo segundo; no se hace);
//   · debajo del eje va, fijo y sin poder apagarse, "paneo por energía L/R · no localización";
//   · la lectura bajo el cursor da el paneo en PORCENTAJE, no en grados.
//
// LO QUE SÍ MUESTRA, y que ninguna otra lente puede. Los cuatro números de banda ancha de una mezcla con
// dos fuentes duras en canales opuestos son idénticos a los de ruido decorrelacionado (corr ≈ 0,
// width ≈ 1): las dos "suenan igual de anchas" para un correlímetro. Acá una es dos manchas en esquinas
// opuestas y la otra una nube pareja de lado a lado. Esa diferencia es todo el punto.
//
// EL dB DE LA LECTURA ES RELATIVO, y está rotulado así. La celda acumula energía por un promedio
// exponencial, así que su valor absoluto depende de τ y de la tasa de frames; convertirlo a dBFS pediría
// dividir por τ·frames_por_segundo, que es una aproximación válida sólo en régimen. Un número absoluto
// aproximado en un medidor es peor que uno relativo exacto: el nivel absoluto ya lo dan SPECTRUM y
// SPECTROGRAM, que lo miden sin aproximar.
//
// EL PRESUPUESTO DE PUNTOS (§4 del spec): ≤ 4 096 por frame entre la grilla de ahora (96×64 = 6 144
// celdas) y las 8 láminas de estela (8 × 48×32 = 12 288). Se dibujan las que superan un piso relativo, y
// si aun así son más, se quedan LAS MÁS FUERTES (nth_element sobre el valor, sin ordenar todo). Bajar el
// piso hasta que entren habría sido más simple y habría tirado justo los picos en la señal más densa.
//
// Se dibuja a PÍXEL sobre una imagen, no con fillRect por celda: 4 096 rectángulos con antialias y un
// setColour cada uno cuestan un orden de magnitud más que escribir el ARGB (misma decisión que la lente 4).
//
// REDUCED MOTION: se apagan las DOS cosas que se mueven sin ser el dato — la estela (queda sólo la grilla
// de ahora) y el suavizado de la normalización (el brillo pasa a seguir el máximo del frame sin inercia).
// La grilla en sí sigue viva: su contenido es la medición, no una animación.
// ========================================================================================================
class FieldLens : public Lens
{
public:
    explicit FieldLens (TelescopeProcessor& p);

    juce::String name() const override            { return kLensNames[(int) LensId::field]; }
    LensId       id() const override              { return LensId::field; }
    juce::uint32 requiredModules() const override { return kSpectrum | kStereoBands | kField; }

    // Cuánto se ve el plano de adelante donde la celda está en el piso. Sin esto la "superficie" sólo
    // existe donde hay energía y la lente vuelve a leerse vacía, que era el problema original.
    //
    // 56c — de 0.11 a 0.16. Con 0.11 el piso se leía bien con señal densa (`field_wide_M`) pero en tamaño
    // M con una señal angosta quedaba tan al borde de lo visible que la superficie volvía a parecer un
    // recorte flotando (observación del productor sobre el 56b). La plata para subirlo salió del blit por
    // celdas de este mismo prompt: el plano de adelante paga su piso de una sola pasada, así que el número
    // no cuesta nada — lo que costaba era tener margen para tocarlo.
    //
    // 57d — de 0.16 a 0.22. «Darle más brillo» (Joaquín, 14-sep, con el 57c en su DAW): donde el nivel es
    // bajo la superficie sigue siendo casi toda piso, así que el piso es la mitad del brillo que se ve. Es
    // una de las tres perillas del brillo del relieve (las otras dos, abajo) y la que menos cuesta: no
    // agrega ninguna cuenta por muestra. FIELD[brillo] mide el resultado de las tres juntas.
    static constexpr float kSurfaceFloorAlpha = 0.22f;

    // ===== 57d · LA LUZ Y LA CURVA DE COLOR DEL RELIEVE =====
    //
    // lambert = kReliefAmbient + kReliefDiffuse · máx(0, n·L). Hasta el 57c era 0.45 + 0.55: la cara en
    // sombra quedaba al 45 % de su color y la superficie entera se leía oscura, que es lo que Joaquín pidió
    // aclarar. Con 0.60 + 0.40 la sombra sube un 33 % (0.45 → 0.60) y la cara a la luz queda EXACTA (la suma
    // sigue siendo 1): el relieve conserva el contraste de forma, sólo pierde negro.
    static constexpr float kReliefAmbient = 0.60f;
    static constexpr float kReliefDiffuse = 0.40f;
    // El índice de paleta del relieve sale de tv^kReliefGamma: los medios suben (0.25 → 0.33 del recorrido,
    // índice 64 → 84; 0.50 → 0.574, 128 → 146) y los extremos quedan donde estaban (0 → 0, 1 → 1). El ALFA
    // sigue al nivel crudo, así que una celda sin energía no se enciende por esto: sólo cambia de qué color
    // es lo que ya se dibujaba.
    static constexpr float kReliefGamma   = 0.8f;

    // ===== 57b: EL RELIEVE DEL PLANO DE AHORA =====
    //
    // «Field: no se nota lo 3D, todo del mismo color» (Joaquín, 9-sep). Lo del color lo arregla la paleta;
    // lo del 3D, esto: el plano de adelante deja de ser una lámina coloreada y pasa a ser una SUPERFICIE
    // CON ALTURA, iluminada por su propia normal. El nivel ya no se lee sólo por color — se lee por
    // relieve, que es lo que el ojo interpreta como volumen sin que haya que explicárselo.
    //
    // POR QUÉ LAS FILAS SE COMPRIMEN. Si la altura se sumara sobre las filas donde están hoy, un pico en
    // la frecuencia más alta se saldría por el techo del plano y entraría en la franja que es SÓLO de la
    // estela (ver trailOnlyArea()). Entonces las filas se reparten sobre el (1 − kReliefFrac) de abajo del
    // plano y el relieve se lleva el resto: la fila más alta a nivel máximo aterriza EXACTO en el techo y
    // nada se sale de la caja. El eje de frecuencia y la lectura bajo el cursor usan la MISMA cuenta
    // (`baseY01`), así que siguen diciendo la verdad: marcan dónde SE APOYA cada frecuencia.
    static constexpr float kReliefFrac = 0.35f;

    // La y01 del cubo en la que se apoya la fila normalizada `t01` (0 = 20 Hz abajo, 1 = 20 kHz arriba).
    static constexpr float baseY01 (float t01) noexcept { return t01 * (1.0f - kReliefFrac); }
    // El piso del dibujo: por debajo de esto respecto de la celda más fuerte, la celda no se pinta.
    static constexpr float kFloorDbRel = -45.0f;

    // El rótulo que NO se puede apagar (ver la nota de honestidad del encabezado). Desde el 56b sale de
    // Strings.h como todo lo demás: la honestidad no se negocia, pero SÍ se traduce — decirla en un
    // idioma que el usuario no lee es lo mismo que no decirla.
    juce::String honestyLabel() const
    {
        return tr (strings::Key::energyByDirection) + juce::String::fromUTF8 (" \xc2\xb7 ")
             + tr (strings::Key::notLocalisation);
    }

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void rebuildOnNextPaint() noexcept { invalidateStatic(); }

    // La lectura bajo el cursor, sobre el plano de ADELANTE (la grilla de ahora). `panPercent` va de −100
    // (todo L) a +100 (todo R) y `db` es RELATIVO a la celda más fuerte (ver el encabezado).
    struct Readout
    {
        bool   valid = false;
        double panPercent = 0.0, freqHz = 0.0;
        float  db = 0.0f;
        int    row = -1, col = -1;
    };
    Readout readoutAt (juce::Point<int> p) const;

    // La franja del plot a la que SÓLO puede llegar la estela: por encima del techo del plano de adelante.
    // La usa el test de reduced-motion para verificar por píxel que sin estela ahí no se dibuja nada.
    juce::Rectangle<int> trailOnlyArea() const;

    // ===== 57c · la CACHÉ, expuesta para los tests =====
    //
    // `cacheAreaForTest` es el rectángulo LÓGICO que cubre la imagen de píxeles: VISUAL[hd] mide el
    // gradiente sólo ahí adentro (fuera de ese rectángulo viven la caja de alambre, los ejes y los
    // textos, que se dibujan a escala física y taparían la medición de la caché — la mutación de la
    // auditora del 57b encontró justamente eso). Las otras dos dan la lámina SOLA, sin la caja de
    // alambre que `drawStage` pinta encima: es lo que mide FIELD[estela].
    juce::Rectangle<int> cacheAreaForTest() const noexcept { return zones.plot; }
    const juce::Image&   cacheImageForTest() const noexcept { return cache.image(); }
    float                cacheScaleForTest() const noexcept { return cache.scale(); }
    // La franja de ABAJO a la que sólo puede llegar el plano de AHORA: por debajo del suelo del plano de
    // estela más adelantado que puede existir (z = 1/(kTrail+1)). El complemento exacto de trailOnlyArea().
    juce::Rectangle<int> frontOnlyArea() const;
    const juce::ValueTree& stateTree() const override;

    // 56: cuenta CELDAS DE GRILLA con energía dibujadas (la superficie), no puntitos sueltos. El tope de
    // 4096 dejó de aplicar porque el costo dejó de depender de la señal: se dibujan siempre los mismos 9
    // planos, con las mismas 64×96 (frente) y 32×48 (estela) celdas. Ver el encabezado de updateImage().
    int   pointsDrawn() const noexcept { return drawn; }
    // De esas celdas, las del PLANO DE AHORA (layer == 0). Se cuenta aparte porque `pointsDrawn()` lo
    // cumplen las estelas solas: en cff1ec1 el bucle de capas nunca llegaba a 0 y la lente mostraba tres
    // láminas de historia sin el presente, con `drawn` muy por encima de kDenseCells igual.
    int   liveCellsDrawn() const noexcept { return liveCells; }
    // 57b — DOS láminas de estela, no tres. El plano de adelante pasó de lámina plana a superficie con
    // altura y con luz (ver blitRelief) y eso cuesta: la lente se iba a 6.59 ms a escala 2 contra un
    // criterio de 6 y un margen exigido de 4.8. El prompt 57b dice qué recortar cuando no entra —"reducí
    // la altura máxima o el número de estelas dibujadas, no el criterio"— y de las dos, ésta es la que no
    // se nota: la profundidad se sigue leyendo con dos láminas recediendo (era la misma razón por la que
    // en el 56 bajaron de ocho a tres), mientras que bajar la altura sería tirar justo lo que se agregó.
    static constexpr int kMaxTrailPlanes = 1;
    // 57c — cuánto del alpha de la lámina de estela queda tras el suavizado. Ver la llamada a
    // blitSurface en updateImage(): al pasar de mosaico a superficie continua la estela ganó presencia
    // sin que nadie se la diera, y compite con el relieve del presente. 0.75 la devuelve a donde estaba
    // de peso visual, ya sin bordes.
    static constexpr float kTrailAlpha = 0.75f;
    // La COTA ESTRUCTURAL del pintado: el plano de adelante entero más las láminas de estela que se
    // dibujan. No es un tope que haya que aplicar — es todo lo que existe.
    static constexpr int kMaxCells = FieldFrame::kRows * FieldFrame::kDir
                                     + kMaxTrailPlanes * FieldFrame::kTrailRows * FieldFrame::kTrailDir;
    // Con la señal DENSA del banco de pruebas la superficie enciende como mínimo un cuarto del plano de
    // adelante: es lo que hace que el número del presupuesto hable del peor caso y no de una pantalla vacía.
    static constexpr int kDenseCells = FieldFrame::kRows * FieldFrame::kDir / 4;
    int   trailLayersDrawn() const noexcept { return trailLayers; }
    float normalisation() const noexcept { return norm; }

    enum Control { ctrlDecay = 0, ctrlWindow, ctrlPalette, kNumControls };
    void cycleControl (int control);

protected:
    void renderStatic (juce::Graphics&, int width, int height) override;
    void paintLive (juce::Graphics&) override;
    bool advanceFrame() override;

private:
    static constexpr int kAxisW = 44;   // canal de etiquetas de frecuencia
    static constexpr int kDirH  = 28;   // tira de dirección (L … C … R + el rótulo fijo)

    struct Zones
    {
        juce::Rectangle<int> plot, freqAxis, dirAxis, footer;
        juce::Rectangle<int> button[kNumControls];
    };
    Zones zonesFor (int w, int h) const;
    Projection2p5 projectionFor (const Zones&) const;

    void buildPalette();
    void updateImage();
    // La CAJA de alambre y el suelo. Van en la capa VIVA y no en la estática por una razón concreta: la
    // imagen del plot es opaca y se dibuja encima, así que todo lo que la estática pinte adentro del plot
    // queda tapado. Son diez líneas por frame: no se nota.
    void drawStage (juce::Graphics&) const;
    void paintButton (juce::Graphics&, juce::Rectangle<int>, const juce::String& label,
                      const juce::String& value, bool hovered) const;

    // Una celda candidata a dibujarse. `layer` 0 = la grilla de ahora, 1…kTrail = la estela (más grande
    // = más al fondo).
    struct Cell { float v; juce::int16 row, col; juce::int16 layer; };

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

    std::array<juce::uint32, 256> palette {};
    int paletteSeen = -1;          // 57b — la rampa con la que se horneó `palette` (ver Palettes.h)
    std::vector<Cell>  cells;      // reservado una vez: cero asignaciones por frame
    std::vector<float> values;     // ídem, para el nth_element del tope de puntos
    // 56: buffers de la superficie. Miembros (no locales) por lo mismo que los de arriba: se reservan una
    // vez y no se asigna memoria en el camino de pintado.
    mutable std::vector<float> tGrid;
    // 57c — los dos buffers del suavizado de pantalla (ver screenSmoothed). Miembros por lo mismo que
    // todos los de acá: el camino de pintado no asigna memoria.
    mutable std::vector<float> tSmooth, tSmoothTmp;
    mutable std::vector<int>   colIdx;
    mutable std::vector<float> colFrac;
    // 56c — los TRAMOS de píxeles que cubre cada celda en el camino de vecino más cercano (ver
    // blitSurface()). Miembros por lo mismo que los de arriba: cero asignaciones por frame.
    mutable std::vector<int>   colRunFirst, colRunLast, rowRunFirst, rowRunLast;
    // 57b — los buffers del relieve. Miembros por lo mismo que los de arriba: el camino de pintado no
    // asigna memoria. `reliefRun*` son los TRAMOS visibles de cada columna (ver blitRelief): se calculan
    // en una pasada y se pintan en otra, por filas.
    mutable std::vector<float>        reliefPrevCol, reliefCurCol;
    // ARREGLOS PARALELOS y no un arreglo de structs (se probaron los dos): la pasada 2 lee `y` en CADA
    // píxel para ver si el cursor tiene que avanzar, y el color sólo cuando pinta. Con un struct de 16
    // bytes, cada chequeo del cursor arrastra el color a la caché — medido, 4.83 → 4.99 ms.
    mutable std::vector<juce::int32>  reliefRunY;
    mutable std::vector<juce::uint32> reliefRunSrc, reliefRunOverBg;
    mutable std::vector<juce::uint8>  reliefRunAlpha;
    mutable std::vector<juce::int32>  reliefRunCount, reliefCursor;
    // El tramo VIGENTE de cada columna, en arreglos compactos (una entrada por columna): son los que lee
    // la pasada 2 en cada píxel. Ver blitRelief.
    mutable std::vector<juce::int32>  reliefCurY;
    mutable std::vector<juce::uint32> reliefCurPre, reliefCurSrc;
    mutable std::vector<juce::uint32> reliefCurAlpha;

    // `ontoBackground` = no hay nada dibujado debajo (la lámina más lejana, sobre la imagen recién
    // limpiada): el color se premezcla una vez por celda y el bucle interior es un store.
    void blitSurface (const juce::Image::BitmapData& bd, juce::Rectangle<float> rect, const float* t,
                      int rows, int dirs, float fade, float floorAlpha, bool bilinear,
                      bool ontoBackground = false, bool coarse = false) const;
    // 57b — el plano de AHORA: superficie con altura, luz por normal y oclusión por horizonte.
    void blitRelief (const juce::Image::BitmapData& bd, juce::Rectangle<float> rect, const float* t,
                     int rows, int dirs, float fade, bool coarse) const;
    // 57c — la grilla del presente SUAVIZADA PARA LA PANTALLA ([1 2 1]/4 en los dos ejes). Devuelve un
    // puntero a `tSmooth` (o a la entrada, si la grilla es demasiado chica para el núcleo). Ver el
    // bloque grande del .cpp: suaviza cómo se DIBUJA, no lo que se mide.
    const float* screenSmoothed (const std::vector<float>& src, int rows, int dirs) const;

    float norm = 0.0f;             // la normalización del brillo (suavizada salvo con reduced-motion)
    int   drawn = 0, trailLayers = 0, liveCells = 0;
    juce::uint32 lastFrame = 0xffffffffu;

    int   hovered = -1;
    juce::Point<int> cursor { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FieldLens)
};
}
