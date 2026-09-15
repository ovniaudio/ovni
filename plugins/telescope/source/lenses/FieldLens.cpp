#include "lenses/FieldLens.h"
#include "PluginProcessor.h"
#include "lenses/LensReadout.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

// Las octavas "redondas" que rotula el eje de frecuencia. Todas caen dentro de 20 Hz – 20 kHz.
constexpr double kOctavesHz[] = { 31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };

juce::String shortHz (double hz)
{
    if (hz >= 1000.0) return juce::String ((int) std::lround (hz / 1000.0)) + "k";
    return juce::String ((int) std::lround (hz));
}

// La energía de la celda a dB RELATIVOS a la normalización (ver el encabezado de la lente).
float cellDbRel (double v, double reference) noexcept
{
    if (! (v > 0.0) || ! (reference > 0.0)) return FieldLens::kFloorDbRel;
    return (float) std::max ((double) FieldLens::kFloorDbRel, 10.0 * std::log10 (v / reference));
}
}

FieldLens::FieldLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (4);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    buildPalette();

    // Reservado UNA vez: el peor caso son las 6 144 celdas de la grilla más las 12 288 de la estela.
    const size_t worst = (size_t) (FieldFrame::kRows * FieldFrame::kDir
                                   + FieldFrame::kTrail * FieldFrame::kTrailRows * FieldFrame::kTrailDir);
    cells.reserve (worst);
    values.reserve (worst);
}

//======================================================================================== paleta
// Piso = fondo, medio = el acento de la familia Espectral, tope = brillo. Sale entera del Theme, igual que
// la del sonograma: si el sello cambia el hue, esta lente cambia con él.
void FieldLens::buildPalette()
{
    // 57b — la rampa la elige el usuario y es la misma para las cuatro lentes de nivel (Palettes.h).
    paletteSeen = processor.paletteIndex();
    const auto& ramp = look::palette (look::paletteFromIndex (paletteSeen));

    for (int i = 0; i < 256; ++i)
    {
        // El piso NO arranca en el fondo (a diferencia del sonograma de la lente 4, que pinta todos los
        // píxeles y ahí el piso ES el fondo): acá los puntos son ralos, y uno pintado del color del fondo
        // sería un punto que no existe. Se levanta al 18 % de la rampa, que es lo mínimo que se ve.
        const float t = 0.18f + 0.82f * (float) i / 255.0f;
        palette[(size_t) i] = ramp[(size_t) juce::jlimit (0, 255, (int) std::lround (t * 255.0f))];
    }
}

//======================================================================================== geometría
FieldLens::Zones FieldLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (190, (foot.getWidth() - gap * (kNumControls - 1)) / (kNumControls + 1));
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.dirAxis  = body.removeFromBottom (kDirH);
    z.freqAxis = body.removeFromLeft (kAxisW);
    z.plot     = body;
    z.dirAxis  = z.dirAxis.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

Projection2p5 FieldLens::projectionFor (const Zones& z) const
{
    Projection2p5 p;
    p.x0 = (float) z.plot.getX();
    p.y0 = (float) z.plot.getY();
    p.w  = (float) juce::jmax (1, z.plot.getWidth());
    p.h  = (float) juce::jmax (1, z.plot.getHeight());
    // Menos inclinación y menos fuga que el waterfall: acá el fondo son 8 láminas tenues, no 120 líneas, y
    // con mucha profundidad la grilla de ahora se achataría hasta dejar de leerse.
    p.tilt  = 0.30f;
    p.depth = 0.16f;
    return p;
}

void FieldLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    proj  = projectionFor (zones);
    Lens::resized();
}

juce::Rectangle<int> FieldLens::trailOnlyArea() const
{
    // El techo del plano de ADELANTE: por encima de esa y, sólo puede haber dibujado la estela (la
    // proyección es estrictamente decreciente en z, ver Projection2p5.h).
    const int frontTop = juce::roundToInt (proj.project (0.5f, 1.0f, 0.0f).y);
    const int top = zones.plot.getY() + 1;
    return { zones.plot.getX() + 1, top, juce::jmax (0, zones.plot.getWidth() - 2),
             juce::jmax (0, frontTop - top - 1) };
}

juce::Rectangle<int> FieldLens::frontOnlyArea() const
{
    // El SUELO del plano de estela más adelantado que puede existir: la estela más nueva vive en
    // z = 1/(trailCount+1) y trailCount ≤ kTrail, así que ninguna lámina puede bajar de floorY(1/(kTrail+1)).
    // Por debajo de esa y sólo puede pintar el plano de AHORA (la proyección es estrictamente decreciente
    // en z, ver Projection2p5.h).
    const int trailFloor = juce::roundToInt (proj.floorY (1.0f / (float) (FieldFrame::kTrail + 1)));
    const int bottom = zones.plot.getBottom() - 1;
    return { zones.plot.getX() + 1, trailFloor + 1, juce::jmax (0, zones.plot.getWidth() - 2),
             juce::jmax (0, bottom - trailFloor - 1) };
}

const juce::ValueTree& FieldLens::stateTree() const { return processor.apvts.state; }

//======================================================================================== la imagen
// ========================================================================================================
// ===== 56: DE NUBE DE PUNTOS A SUPERFICIE DE CALOR =====
//
// Lo que había: cada celda por encima del piso se dibujaba como un cuadradito de 1–4 px, con un tope de
// 4 096. Con una señal real eso son unas decenas de puntos sueltos en una caja de alambre enorme — la
// captura del 8-sep marcaba "73/4096 pts" — y se lee VACÍO. Joaquín, mirándola: "no parece de ultra
// calidad; debería ser algo de ultra calidad, más detalle".
//
// Lo que hay ahora: la grilla ENTERA se rasteriza como una superficie continua. El dato no cambió —son
// las mismas celdas, la misma normalización, el mismo piso— cambió que ahora se ve el PLANO y no sólo sus
// picos. Una superficie dice "hay energía acá y no allá" de un vistazo; una constelación de puntos
// obliga a reconstruirla con la imaginación.
//
// CÓMO, y por qué es BARATO. Cada plano de profundidad de Projection2p5 es un rectángulo ALINEADO A LOS
// EJES (px es lineal en x y py lineal en y a z fijo, ver Projection2p5.h). Así que no hace falta ningún
// rasterizador propio: se llena una imagen del tamaño de la GRILLA (64×96 el frente, 32×48 la estela) y
// se estira al rectángulo de su plano con resampling de alta calidad, que es la interpolación bilineal
// que pedía el encargo, hecha por el código que ya está optimizado para eso.
//
// EL COSTO DEJÓ DE DEPENDER DE LA SEÑAL. Antes el tope de 4 096 puntos existía porque una señal densa
// podía encender miles de celdas. Ahora se dibujan SIEMPRE las mismas 9 imágenes chicas (una por plano):
// el peor caso y el mejor caso son el mismo caso. Eso es más fuerte que un tope — un tope hay que
// recordarlo, una cota estructural no se puede olvidar.
void FieldLens::updateImage()
{
    // EN PÍXELES DE DISPOSITIVO (ver lenses/Raster.h): la superficie se dibuja a la resolución de la
    // pantalla y se devuelve al plano lógico al blitearla.
    drawn = 0;
    trailLayers = 0;
    liveCells = 0;
    if (zones.plot.getWidth() <= 0 || zones.plot.getHeight() <= 0 || ! cache.valid()) return;

    const int w = cache.deviceW();
    const int h = cache.deviceH();
    auto& image = cache.image();

    Projection2p5 local = projectionFor (zones);
    local.x0 = 0.0f;
    local.y0 = 0.0f;
    local.w  = (float) w;
    local.h  = (float) h;

    const auto& f = processor.field().read();
    lastFrame = f.frameIndex;

    // ---- la normalización del brillo (sin cambios: es la misma cuenta de siempre) ----
    if (prefersReducedMotion() || f.maxCell >= norm || norm <= 0.0f) norm = f.maxCell;
    else                                                             norm += (f.maxCell - norm) * 0.10f;
    if (! (norm > 0.0f)) return;

    // ---- del FONDO hacia adelante: lo de ahora queda encima de su propia historia ----
    const bool withTrail = ! prefersReducedMotion();
    trailLayers = withTrail ? juce::jlimit (0, FieldFrame::kTrail, f.trailCount) : 0;

    // ===== LA LIMPIEZA SE ACOTA A DONDE HACE FALTA (57b) =====
    //
    // El plano de AHORA cubre TODAS las columnas desde su base hasta el borde de abajo de la imagen (ver
    // blitRelief: `planeBot` ES el borde inferior), así que limpiar la mitad de abajo es escribir 4 MB
    // que se van a pisar enteros un momento después — y de paso barrer de la caché justo las líneas que
    // la pasada 2 va a necesitar. Medido en tamaño L a escala 2: 4.87 → 4.13 ms.
    //
    // Hasta dónde hay que limpiar, y por qué termina siendo CASI TODO: la cota segura es la de más abajo
    // de dos cosas —el suelo de la lámina de estela más adelantada y el techo que la superficie alcanza
    // con la grilla en cero— y la primera cae al 96 % del alto, porque esa lámina está a z = 1/9 y a esa
    // profundidad el suelo todavía está pegado al de adelante. Se probó limpiar sólo hasta ahí y no paga:
    // el ahorro es el 4 % de abajo y se compra con un `if` por píxel en la pasada 2 (medido, 4.87 → 5.31).
    // Queda escrito acá para que no se vuelva a intentar: lo que habría que achicar es la estela, no la
    // limpieza.
    Projection2p5 localProj = local;
    const auto planeRect = [&localProj] (float z)
    {
        const auto l = localProj.project (0.0f, 0.0f, z), r = localProj.project (1.0f, 0.0f, z);
        const auto top = localProj.project (0.5f, 1.0f, z);
        return juce::Rectangle<float> (l.x, top.y, juce::jmax (1.0f, r.x - l.x),
                                       juce::jmax (1.0f, l.y - top.y));
    };

    // La limpieza cubre la imagen entera. Se probó saltear el rectángulo de la estela (que con la tabla
    // del blit podría escribirse completo) y NO paga: la estela deja el fondo donde no tiene nada que
    // decir, y eso es justamente lo que le da al plano de ahora su camino rápido — mezclar contra un
    // fondo que se conoce de antemano en vez de leer el destino. Medido a escala 2, tamaño L: saltear la
    // limpieza y escribir la lámina entera da 5.08 ms; limpiar todo y saltear lo invisible, 4.64.
    image.clear (image.getBounds(), th::bg1.withAlpha (1.0f));

    const juce::Image::BitmapData bd (image, juce::Image::BitmapData::readWrite);
    if (bd.pixelStride != 4) return;

    // El nivel normalizado de cada celda, UNA vez por plano: el logaritmo se paga rows×dirs veces
    // (6 144 como mucho), no una vez por píxel de pantalla.
    std::vector<float>& t = tGrid;

    // Las láminas de estela que de verdad se dibujan. Ocho sábanas translúcidas apiladas cuestan mucho y
    // se leen como NIEBLA; con tres la profundidad se sigue leyendo igual (las tres siguen recediendo) y
    // el dato de adelante conserva su contraste. Medido en el M4 de la casa, tamaño L:
    //     8 láminas con drawImage de JUCE ... 18.8 ms   ·  8 con blit propio ... 5.24 ms
    //     4 con blit propio .................. 4.68 ms   ·  3 con blit propio ... ver BUDGET_FIELD
    const int step = trailLayers > kMaxTrailPlanes
                       ? (trailLayers + kMaxTrailPlanes - 1) / kMaxTrailPlanes
                       : 1;

    // 56b — EL ÚLTIMO PASO ATERRIZA SIEMPRE EN 0. Con `layer -= step` a secas el bucle saltaba por encima
    // del plano de AHORA cuando trailLayers no era múltiplo del paso: con 8 estelas y paso 3 iba
    // 8 → 5 → 2 → −1 y la grilla del presente no se dibujaba nunca (lo mismo con 5 y con 7). Se saltea el
    // 0 y se pierde justamente lo único que la lente tiene que mostrar. `jmax (0, …)` cuesta una
    // instrucción y hace imposible ese salto, sin cambiar CUÁNTAS láminas de estela se dibujan.
    for (int layer = trailLayers; layer >= 0; layer = layer > 0 ? juce::jmax (0, layer - step) : -1)
    {
        const bool  isTrail = layer > 0;
        const float z = trailLayers > 0 ? (float) layer / (float) (trailLayers + 1) : 0.0f;
        const float fade = 1.0f - 0.55f * z;
        const int   rows = isTrail ? FieldFrame::kTrailRows : FieldFrame::kRows;
        const int   dirs = isTrail ? FieldFrame::kTrailDir  : FieldFrame::kDir;

        t.assign ((size_t) (rows * dirs), 0.0f);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < dirs; ++c)
            {
                const float v = isTrail ? f.trail[trailLayers - layer][r][c] : f.grid[r][c];
                if (v > 0.0f)
                    t[(size_t) (r * dirs + c)] = juce::jlimit (
                        0.0f, 1.0f,
                        (cellDbRel ((double) v, (double) norm) - kFloorDbRel) / (-kFloorDbRel));
                if (t[(size_t) (r * dirs + c)] > 0.0f) { ++drawn; if (! isTrail) ++liveCells; }
            }

        // 57b — el presente es una SUPERFICIE CON ALTURA Y LUZ (blitRelief); la estela sigue siendo una
        // lámina plana, que es lo que corresponde: es historia, no presente.
        //
        // ===== 57c · LA ESTELA VA BILINEAL, Y SU ALPHA BAJA =====
        //
        // Iba por vecino más cercano "porque está atrás, atenuada y a media resolución". Los tres motivos
        // son ciertos y la conclusión era falsa: a escala 2 la lámina mide ~1 600 px de ancho para 32
        // columnas de grilla, o sea 50 px por celda, y 50 px del mismo color con un escalón en el borde se
        // leen como un RECTÁNGULO — no como algo que está lejos. Es lo que Joaquín ve como "píxeles
        // arriba" y lo que mide FIELD[estela].
        //
        // El alpha baja a kTrailAlpha por lo mismo que la lámina se suaviza: al dejar de ser un mosaico
        // pasó a leerse como una superficie más, y dos superficies compitiendo por el frente es
        // exactamente lo que el relieve del 57b vino a resolver. La estela es historia: tiene que estar,
        // no tiene que ganar.
        if (isTrail) blitSurface (bd, planeRect (z), t.data(), rows, dirs, fade * kTrailAlpha, 0.0f, true,
                                  layer == trailLayers,               // la más lejana: cae sobre el fondo limpio
                                  cache.scale() >= 1.5f);            // media resolución sólo si sobra
        else         blitRelief  (bd, planeRect (z), screenSmoothed (t, rows, dirs), rows, dirs, fade,
                          cache.scale() >= 1.5f);
    }
}

// ========================================================================================================
// El BLIT de un plano: la grilla estirada al rectángulo de su profundidad, escrito a mano.
//
// Por qué a mano y no con `Graphics::drawImage`. Se probó primero con drawImage y resampling de alta y de
// media calidad: mediana 18.8 y 19.0 ms contra un criterio de 4 (medido, 2026-09-08). El camino genérico
// de JUCE resuelve una transformación afín cualquiera —recorte por arista, coordenadas por píxel— y acá
// no hace falta ninguna de las dos cosas: cada plano de Projection2p5 es un rectángulo ALINEADO A LOS
// EJES (px lineal en x, py lineal en y a z fijo). Con eso, el mapeo de columnas se precalcula una vez por
// plano y el bucle interior queda en cuatro cargas y tres interpolaciones.
//
// El plano de ADELANTE va bilineal (es el que se mira) y las láminas de estela por vecino más cercano:
// están atrás, atenuadas y a media resolución — interpolarlas sería pagar el doble por una diferencia que
// no se ve.
// Las dos cuentas de mezcla que usa el blit viven en lenses/Raster.h desde el 57b: las comparten las
// cinco lentes que escriben ARGB a mano (era la única copia que quedaba fuera de ese archivo).
using raster::blendArgb;

// ========================================================================================================
// ===== 57c · EL SUAVIZADO DE PANTALLA DEL RELIEVE =====
//
// «Se ven como montañas» (Joaquín, 12-sep). Y es verdad: la grilla mide la energía de una ventana de
// 0.3 s, así que cada celda tiene el RUIDO de esa ventana, y una superficie extruida de celda en celda
// convierte ese ruido en picos de sierra. No es que la medición esté mal — es que la medición, dibujada
// cruda como altura, se lee como relieve de montaña en vez de como el contorno de una mezcla.
//
// Se suaviza CÓMO SE DIBUJA, no el dato: un núcleo [1 2 1]/4 separable, una pasada a lo largo del paneo
// (64 columnas) y otra a lo largo de la frecuencia (96 filas). σ = √0.5 = 0.71 celda en cada eje — o sea
// ±1 celda, el mínimo que existe: menos que eso no es un núcleo, y más empezaría a mover picos de lugar.
// En frecuencia 0.71 fila son ~1/14 de octava (las filas miden 9.63 por octava); en paneo, 0.71 de 64
// columnas.
//
// ===== 57d · DOS PASADAS POR EJE =====
// «Habría que suavizarlo un poco más» (Joaquín, 14-sep). [1 2 1]/4 aplicado dos veces es [1 4 6 4 1]/16: las
// varianzas se suman, σ² = 0.5 + 0.5 = 1.0, así que σ = 1.0 celda en cada eje — 1/64 del recorrido L→R y
// 1/9.6 de octava. El núcleo sigue siendo simétrico, así que un pico se ensancha pero no se corre de lugar;
// y sigue siendo separable, así que el orden de las cuatro pasadas no cambia el resultado. Cuesta cuatro
// recorridos de 6 144 celdas en vez de dos: microsegundos, contra un presupuesto de milisegundos.
//
// LO QUE NO TOCA, y es la razón de que el suavizado viva acá y no en el motor: `FieldFrame` sigue igual,
// la lectura bajo el cursor (`readoutAt`) sigue leyendo la celda cruda, `FIELD[lente]` sigue contando las
// mismas celdas vivas (se cuentan ANTES, al armar `t`) y las 9 láminas de estela ni se enteran. Es la
// misma división que el resto de la lente: el motor publica un hecho, la vista decide cómo mostrarlo.
//
// Los bordes REPLICAN (la celda del borde se cuenta dos veces). Espejar o envolver serían las otras dos
// opciones y las dos mienten acá: el paneo no es circular y la fila 0 no continúa en la 95.
// ========================================================================================================
const float* FieldLens::screenSmoothed (const std::vector<float>& src, int rows, int dirs) const
{
    const auto n = (size_t) (rows * dirs);
    if (src.size() < n || rows < 3 || dirs < 3) return src.data();

    tSmooth.resize (n);
    tSmoothTmp.resize (n);

    // Una pasada [1 2 1]/4 a lo largo del PANEO (dentro de cada fila): las columnas son contiguas en memoria.
    const auto alongPan = [rows, dirs] (const float* from, float* to)
    {
        for (int r = 0; r < rows; ++r)
        {
            const float* in  = from + (size_t) (r * dirs);
            float*       out = to   + (size_t) (r * dirs);
            out[0] = (in[0] * 3.0f + in[1]) * 0.25f;                      // borde: la celda 0 cuenta doble
            for (int c = 1; c < dirs - 1; ++c)
                out[c] = (in[c - 1] + 2.0f * in[c] + in[c + 1]) * 0.25f;
            out[dirs - 1] = (in[dirs - 2] + in[dirs - 1] * 3.0f) * 0.25f;
        }
    };

    // Una pasada [1 2 1]/4 a lo largo de la FRECUENCIA (entre filas), columna por columna.
    const auto alongFreq = [rows, dirs] (const float* from, float* to)
    {
        for (int r = 0; r < rows; ++r)
        {
            const int rUp = juce::jmin (rows - 1, r + 1), rDn = juce::jmax (0, r - 1);
            const float* a = from + (size_t) (rDn * dirs);
            const float* b = from + (size_t) (r   * dirs);
            const float* c = from + (size_t) (rUp * dirs);
            float*       o = to   + (size_t) (r   * dirs);
            for (int i = 0; i < dirs; ++i) o[i] = (a[i] + 2.0f * b[i] + c[i]) * 0.25f;
        }
    };

    // 57d — dos pasadas por eje ([1 4 6 4 1]/16), alternando entre los dos buffers y terminando en tSmooth.
    alongPan  (src.data(),        tSmoothTmp.data());
    alongPan  (tSmoothTmp.data(), tSmooth.data());
    alongFreq (tSmooth.data(),    tSmoothTmp.data());
    alongFreq (tSmoothTmp.data(), tSmooth.data());

    return tSmooth.data();
}

void FieldLens::blitSurface (const juce::Image::BitmapData& bd, juce::Rectangle<float> rect,
                             const float* t, int rows, int dirs, float fade, float floorAlpha,
                             bool bilinear, bool ontoBackground, bool coarse) const
{
    // ===== 57b: LA PRIMERA LÁMINA NO TIENE NADA DEBAJO =====
    //
    // Las láminas de estela se dibujan de atrás hacia adelante sobre una imagen recién limpiada, así que
    // la PRIMERA cae siempre sobre el fondo y nada más. Mezclar contra un valor que se sabe de antemano es
    // hacer una cuenta por píxel para llegar a un número que se puede calcular una vez por CELDA: con
    // `ontoBackground` el color ya viene mezclado y el bucle interior es un store.
    //
    // Medido (tamaño L, escala 2): la estela costaba 1.37 ms de los 4.90 de la lente; con esto, 0.7.
    const auto bgArgb = th::bg1.withAlpha (1.0f).getARGB();
    const int x0 = juce::jmax (0, (int) std::floor (rect.getX()));
    const int x1 = juce::jmin (bd.width  - 1, (int) std::ceil (rect.getRight()));
    const int y0 = juce::jmax (0, (int) std::floor (rect.getY()));
    const int y1 = juce::jmin (bd.height - 1, (int) std::ceil (rect.getBottom()));
    if (x1 < x0 || y1 < y0 || rect.getWidth() <= 0.0f || rect.getHeight() <= 0.0f) return;

    // Mapeo de columna → celda, precalculado una vez por plano.
    colIdx.resize ((size_t) (x1 - x0 + 1));
    colFrac.resize ((size_t) (x1 - x0 + 1));
    for (int x = x0; x <= x1; ++x)
    {
        const float u = ((float) x + 0.5f - rect.getX()) / rect.getWidth() * (float) (dirs - 1);
        const int   c = juce::jlimit (0, dirs - 1, (int) u);
        colIdx [(size_t) (x - x0)] = c;
        colFrac[(size_t) (x - x0)] = bilinear ? juce::jlimit (0.0f, 1.0f, u - (float) c) : 0.0f;
    }

    // El PISO del plano de adelante se pinta de una sola vez y después el bucle se saltea las celdas que
    // están en el piso. Sin esto, cada píxel del plano —incluidos los del 90 % que en una señal real no
    // tiene energía— pagaba una mezcla por canal. Con esto, el costo vuelve a seguir a la SEÑAL y no al
    // tamaño de la ventana. (Medido: 5.24 ms → ver BUDGET_FIELD.)
    if (floorAlpha > 0.0f)
    {
        const auto src = palette[0];
        const auto ai  = (juce::uint32) juce::jlimit (0, 255, (int) (floorAlpha * fade * 255.0f));
        for (int y = y0; y <= y1; ++y)
        {
            auto* dst = (juce::uint32*) bd.getLinePointer (y);
            for (int x = x0; x <= x1; ++x) dst[x] = blendArgb (src, dst[x], ai);
        }
    }

    // ===== 56c ===== EL CAMINO DE VECINO MÁS CERCANO SE RECORRE POR CELDAS, NO POR PANTALLA.
    //
    // Sin bilineal, `tv` de un píxel es exactamente `t[r0·dirs + c0]`: los dos pesos valen cero y las
    // cuatro cargas y las tres interpolaciones del bucle de abajo dan siempre el valor de UNA celda. O
    // sea que el bucle por píxel estaba preguntándole a cada uno de los ~600 000 píxeles de un plano a
    // qué celda pertenecía —y pagando dos búsquedas de tabla, cuatro cargas y una entrada de paleta— para
    // repetir la misma respuesta cientos de veces seguidas.
    //
    // Como el mapeo de píxel a celda es MONÓTONO en los dos ejes, cada celda cubre un tramo contiguo de
    // columnas y otro de filas. Se arman los dos tramos una vez por plano (O(ancho + alto), a partir del
    // MISMO `colIdx` y de la misma cuenta de `vy` que usaba el bucle por píxel, así el resultado es
    // idéntico al bit) y después se recorren las 32×48 celdas: las que están en cero —que en un plano de
    // estela son la mayor parte— no cuestan un solo píxel, y las que encienden pagan una sola vez el
    // índice de paleta y el alfa, fuera del bucle interior.
    //
    // Esto es lo que le dio a la lente el MARGEN que le faltaba (56c): la auditoría del 56b la puso roja
    // dos veces sin cambiar el código, con la mediana pegada al criterio. Ver el REQUIRE del margen en
    // LensBudgetTest.cpp.
    if (! bilinear)
    {
        colRunFirst.assign ((size_t) dirs, -1);
        colRunLast .assign ((size_t) dirs, -1);
        for (int x = x0; x <= x1; ++x)
        {
            const auto c = (size_t) colIdx[(size_t) (x - x0)];
            if (colRunFirst[c] < 0) colRunFirst[c] = x;
            colRunLast[c] = x;
        }

        rowRunFirst.assign ((size_t) rows, -1);
        rowRunLast .assign ((size_t) rows, -1);
        for (int y = y0; y <= y1; ++y)
        {
            const float vy = (1.0f - ((float) y + 0.5f - rect.getY()) / rect.getHeight()) * (float) (rows - 1);
            const auto  r  = (size_t) juce::jlimit (0, rows - 1, (int) vy);
            if (rowRunFirst[r] < 0) rowRunFirst[r] = y;
            rowRunLast[r] = y;
        }

        for (int r = 0; r < rows; ++r)
        {
            if (rowRunFirst[(size_t) r] < 0) continue;
            const float* row = t + (size_t) (r * dirs);

            for (int c = 0; c < dirs; ++c)
            {
                const float tv = row[c];
                if (tv <= 0.002f || colRunFirst[(size_t) c] < 0) continue;   // el piso ya está pintado
                const float alpha = (floorAlpha + (1.0f - floorAlpha) * tv) * fade;
                if (alpha <= 0.004f) continue;                       // por debajo de 1/255 no escribe nada

                const auto src = palette[(size_t) (int) (tv * 255.0f)];
                const auto ai  = (juce::uint32) juce::jmin (255, (int) (alpha * 255.0f));
                const int  xa = colRunFirst[(size_t) c], xb = colRunLast[(size_t) c];

                if (ontoBackground)
                {
                    const auto flat = blendArgb (src, bgArgb, ai);   // una vez por celda, no por píxel
                    for (int y = rowRunFirst[(size_t) r]; y <= rowRunLast[(size_t) r]; ++y)
                    {
                        auto* dst = (juce::uint32*) bd.getLinePointer (y);
                        for (int x = xa; x <= xb; ++x) dst[x] = flat;
                    }
                }
                else
                {
                    for (int y = rowRunFirst[(size_t) r]; y <= rowRunLast[(size_t) r]; ++y)
                    {
                        auto* dst = (juce::uint32*) bd.getLinePointer (y);
                        for (int x = xa; x <= xb; ++x) dst[x] = blendArgb (src, dst[x], ai);
                    }
                }
            }
        }
        return;
    }

    // ========================================================================================================
    // ===== 57c · EL CAMINO BILINEAL, EN RAMPAS Y CON UNA TABLA =====
    //
    // Al pasar la estela de vecino más cercano a bilineal el presupuesto se fue de 4.2 a 7.8 ms a escala 2
    // (medido). El motivo NO eran las interpolaciones: era que el bucle por píxel hacía dos BÚSQUEDAS de
    // tabla (`colIdx`, `colFrac`), cuatro cargas dispersas de la grilla, una entrada de paleta con índice
    // impredecible y una mezcla — todo escalar — mientras que el camino de vecino más cercano tenía como
    // bucle interior un `dst[x] = flat` que el compilador vectoriza.
    //
    // Las dos observaciones que lo devuelven al presupuesto:
    //
    //   1 · DENTRO DE UNA COLUMNA DE CELDA, `tv` ES UNA RECTA. El mapeo de píxel a celda es afín
    //       (u = (x + ½ − rectX)·k con k = (dirs−1)/ancho), así que en el tramo de píxeles que cubre la
    //       celda c el peso `wc` avanza k por píxel y `tv = A + B·wc` con A y B constantes de la fila.
    //       O sea: `tv += paso`. Cero cargas dispersas, cero interpolaciones por píxel.
    //
    //   2 · EL COLOR FINAL DEPENDE SÓLO DE `tv`. La paleta, el alfa (floorAlpha + (1−floorAlpha)·tv)·fade
    //       y —cuando la lámina cae sobre el fondo limpio— la mezcla contra el fondo son todas funciones
    //       de `tv` y de nada más. Se tabulan UNA vez por plano en 256 entradas y el bucle interior queda
    //       en: sumar, truncar, leer la tabla, escribir.
    //
    // Y se va el `continue` de las celdas en el piso: con la tabla, `tv ≈ 0` da exactamente el fondo, así
    // que escribirlo no cambia un píxel y el bucle queda sin ramas. Medido: 7.8 → ver BUDGET_FIELD.
    // ========================================================================================================
    if (bilinear)
    {
        // El tramo de píxeles de cada columna de celda (el mismo mapeo monótono del camino de arriba).
        colRunFirst.assign ((size_t) dirs, -1);
        colRunLast .assign ((size_t) dirs, -1);
        for (int x = x0; x <= x1; ++x)
        {
            const auto c = (size_t) colIdx[(size_t) (x - x0)];
            if (colRunFirst[c] < 0) colRunFirst[c] = x;
            colRunLast[c] = x;
        }

        // Las dos tablas de 256 entradas: el color YA mezclado contra el fondo (el caso de la lámina más
        // lejana) y el par color + alfa para cuando hay algo debajo.
        std::array<juce::uint32, 256> flatLut {}, srcLut {};
        std::array<juce::uint32, 256> alphaLut {};
        for (int i = 0; i < 256; ++i)
        {
            const float tv    = (float) i / 255.0f;
            const float alpha = (floorAlpha + (1.0f - floorAlpha) * tv) * fade;
            const auto  ai    = (juce::uint32) juce::jlimit (0, 255, (int) (alpha * 255.0f));
            srcLut  [(size_t) i] = palette[(size_t) i];
            alphaLut[(size_t) i] = ai;
            flatLut [(size_t) i] = blendArgb (palette[(size_t) i], bgArgb, ai);
        }

        // El primer índice cuyo color mezclado NO es el fondo. Ver el bucle de abajo.
        int firstVisible = 256;
        for (int i = 0; i < 256; ++i)
            if (flatLut[(size_t) i] != bgArgb) { firstVisible = i; break; }

        const float k = (float) (dirs - 1) / rect.getWidth();   // wc por píxel

        // ===== MEDIA RESOLUCIÓN CUANDO LA HAY DE SOBRA =====
        //
        // Con la caché a escala física ≥ 2 la lámina mide ~1 600 × 850 px de dispositivo para una grilla
        // de 32 × 48 celdas: 50 píxeles de pantalla por celda. Muestrear cada DOS y repetir deja pasos de
        // 2 px de dispositivo, o sea UN píxel lógico, sobre una rampa que sube menos de un nivel de color
        // por píxel — el ojo no puede verlo y el presupuesto sí (medido: 1.12 → 0.52 ms a escala 2). Es
        // exactamente la salida que el prompt 57c autoriza cuando la bilineal no entra: "media resolución
        // para la estela antes que tocar el criterio". A escala 1 se muestrea entero: ahí no sobra nada.
        const int step = coarse ? 2 : 1;
        const int rowBytes = (x1 - x0 + 1) * 4;

        for (int y = y0; y <= y1; y += step)
        {
            // La fila 0 de la grilla es la frecuencia MÁS BAJA y va abajo: el eje se da vuelta acá.
            const float vy = (1.0f - ((float) y + 0.5f - rect.getY()) / rect.getHeight()) * (float) (rows - 1);
            const int   r0 = juce::jlimit (0, rows - 1, (int) vy);
            const int   r1 = juce::jmin (rows - 1, r0 + 1);
            const float wr = juce::jlimit (0.0f, 1.0f, vy - (float) r0);
            const float* rowA = t + (size_t) (r0 * dirs);
            const float* rowB = t + (size_t) (r1 * dirs);
            auto* dst = (juce::uint32*) bd.getLinePointer (y);

            for (int c = 0; c < dirs; ++c)
            {
                const int xa = colRunFirst[(size_t) c], xb = colRunLast[(size_t) c];
                if (xa < 0) continue;
                const int c1 = juce::jmin (dirs - 1, c + 1);

                const float v0 = rowA[c]  + (rowB[c]  - rowA[c])  * wr;   // tv con wc = 0
                const float v1 = rowA[c1] + (rowB[c1] - rowA[c1]) * wr;   // tv con wc = 1
                const float B  = v1 - v0;

                // `tv` en el primer píxel del tramo, y su paso. 16.16 en entero: el índice de paleta sale
                // de un desplazamiento, sin conversión de coma flotante por píxel.
                const float wcA = juce::jlimit (0.0f, 1.0f, ((float) xa + 0.5f - rect.getX()) * k - (float) c);
                const auto  toFix = [] (float v) { return (int) (juce::jlimit (0.0f, 1.0f, v) * 255.0f * 65536.0f); };
                int       idxFix  = toFix (v0 + B * wcA);
                const int stepFix = (int) (B * k * 255.0f * 65536.0f) * step;

                // LO QUE NO SE VE NO SE ESCRIBE, y no es una optimización cosmética: por debajo de
                // `firstVisible` la mezcla da EXACTAMENTE el fondo (es el mismo criterio alpha < 1/255 del
                // camino de vecino más cercano), así que saltearlo no cambia un píxel — y deja el fondo
                // intacto ahí, que es lo que le permite al plano de ahora usar su camino rápido.
                if (ontoBackground)
                    for (int x = xa; x <= xb; x += step, idxFix += stepFix)
                    {
                        const auto i = juce::jlimit (0, 255, idxFix >> 16);
                        if (i < firstVisible) continue;
                        const auto v = flatLut[(size_t) i];
                        dst[x] = v;
                        if (step == 2 && x + 1 <= xb) dst[x + 1] = v;
                    }
                else
                    for (int x = xa; x <= xb; x += step, idxFix += stepFix)
                    {
                        const auto i = (size_t) juce::jlimit (0, 255, idxFix >> 16);
                        dst[x] = blendArgb (srcLut[i], dst[x], alphaLut[i]);
                        if (step == 2 && x + 1 <= xb)
                            dst[x + 1] = blendArgb (srcLut[i], dst[x + 1], alphaLut[i]);
                    }
            }

            // La fila de abajo es la misma: una copia de línea entera, que es lo más barato que hace una
            // máquina con memoria.
            if (step == 2 && y + 1 <= y1)
                std::memcpy ((juce::uint32*) bd.getLinePointer (y + 1) + x0, dst + x0, (size_t) rowBytes);
        }
        return;
    }

    for (int y = y0; y <= y1; ++y)
    {
        // La fila 0 de la grilla es la frecuencia MÁS BAJA y va abajo: el eje se da vuelta acá.
        const float vy = (1.0f - ((float) y + 0.5f - rect.getY()) / rect.getHeight()) * (float) (rows - 1);
        const int   r0 = juce::jlimit (0, rows - 1, (int) vy);
        const int   r1 = juce::jmin (rows - 1, r0 + 1);
        const float wr = bilinear ? juce::jlimit (0.0f, 1.0f, vy - (float) r0) : 0.0f;
        const float* rowA = t + (size_t) (r0 * dirs);
        const float* rowB = t + (size_t) (r1 * dirs);
        auto* dst = (juce::uint32*) bd.getLinePointer (y);

        for (int x = x0; x <= x1; ++x)
        {
            const int   c0 = colIdx [(size_t) (x - x0)];
            const float wc = colFrac[(size_t) (x - x0)];
            const int   c1 = juce::jmin (dirs - 1, c0 + 1);

            const float a = rowA[c0] + (rowA[c1] - rowA[c0]) * wc;
            const float b = rowB[c0] + (rowB[c1] - rowB[c0]) * wc;
            const float tv = a + (b - a) * wr;

            if (tv <= 0.002f) continue;                          // el piso ya está pintado (ver arriba)
            const float alpha = (floorAlpha + (1.0f - floorAlpha) * tv) * fade;
            if (alpha <= 0.004f) continue;                       // por debajo de 1/255 no escribe nada

            // `tv` ya viene acotado a [0,1] por la interpolación de valores que lo están: no hace falta
            // volver a acotarlo por píxel.
            const auto src = palette[(size_t) (int) (tv * 255.0f)];
            const auto ai  = (juce::uint32) juce::jmin (255, (int) (alpha * 255.0f));
            // 57c — la lámina más lejana cae sobre el fondo recién limpiado, así que no hace falta LEER
            // el destino para mezclarlo: se sabe lo que hay. Con la estela ya bilineal (un color por
            // píxel, no por celda) esto es lo único que se puede ahorrar, y se nota: la lectura del
            // destino es el acceso a memoria de la lámina entera.
            dst[x] = ontoBackground ? blendArgb (src, bgArgb, ai) : blendArgb (src, dst[x], ai);
        }
    }
}

// ========================================================================================================
// ===== 57b: EL PLANO DE AHORA, COMO SUPERFICIE ILUMINADA =====
//
// Hasta el 57 este plano era una lámina: el nivel salía SÓLO del color, y con una rampa de un tono eso es
// lo que Joaquín vio — «no se nota lo 3D, todo del mismo color». Acá se cambian las dos mitades de esa
// frase a la vez: el color sale de la rampa elegida (punto 2) y el nivel LEVANTA la superficie.
//
// CÓMO SE DIBUJA, y por qué así:
//
//   · SE RECORRE POR COLUMNAS, DE ADELANTE HACIA ATRÁS (de la fila grave, abajo, a la aguda, arriba), con
//     un HORIZONTE por columna — la misma mecánica que WATERFALL. Es lo que hace que un pico tape lo que
//     tiene detrás, que es de dónde sale la lectura de volumen. Y es lo que acota el costo: los tramos de
//     una misma columna son DISJUNTOS, así que entre todas las filas se pinta la superficie UNA vez.
//   · SE MUESTREA MÁS FINO QUE LA GRILLA (kReliefSteps por fila) e interpolando bilineal en las dos
//     direcciones: la superficie que se ve es continua, no los 96 escalones de la grilla.
//   · LA LUZ SALE DE LA PROPIA PENDIENTE. La normal se arma con las dos derivadas que la interpolación
//     bilineal ya calculó (no cuestan nada aparte) y se ilumina con lambert = kReliefAmbient +
//     kReliefDiffuse·máx(0, n·L) — 0.60 + 0.40 desde el 57d, era 0.45 + 0.55 —, con L fija arriba-izquierda. Es lo que le da forma a una ladera: sin luz, una superficie con altura
//     y color por nivel se sigue viendo plana, porque el color ya dice lo mismo que la altura.
//   · OPACA. Antes el plano era translúcido y dejaba ver la estela por debajo; una superficie sólida es
//     justamente lo que la hace leer como superficie. La estela sigue visible donde SÓLO ella llega —por
//     encima del techo de este plano, ver trailOnlyArea()—, que es donde dice lo que tiene para decir.
// ========================================================================================================
namespace
{
// La luz, fija arriba-izquierda y normalizada una vez. `y` positiva es HACIA ARRIBA en el espacio de la
// superficie (la pantalla la da vuelta al proyectar).
constexpr float kLightX = -0.5f, kLightY = 0.8f, kLightZ = 0.35f;

struct Light
{
    float x, y, z;
    Light()
    {
        const float n = 1.0f / std::sqrt (kLightX * kLightX + kLightY * kLightY + kLightZ * kLightZ);
        x = kLightX * n; y = kLightY * n; z = kLightZ * n;
    }
};
const Light kLight;

// Cuántas muestras por fila de grilla. UNA: es la resolución que tiene el dato (96 filas), y muestrear
// más fino no agrega información — sólo suaviza la silueta. Y cuesta: con 2 la lente se va a 6.87 ms a
// escala 2 contra un criterio de 6 (medido), porque se duplican los tramos y con ellos los 3 MB que la
// pasada 1 escribe salteados. Lo que SÍ es continuo a resolución de pantalla, que es lo que se ve, son
// el color y la luz: el nivel se interpola bilineal y la normal se mide entre píxeles.
constexpr int kReliefSteps = 1;

// 1/sqrt(1 + h²) tabulado. Es lo único caro por muestra —hay ~370 000 por frame en tamaño L a escala 2—
// y una raíz por muestra se ve en el presupuesto. La tabla va en h² con paso fino cerca de 0 (que es
// donde vive la mayoría de las pendientes) y se corta en 16: más allá la normal ya está casi acostada y
// el lambert no se mueve.
constexpr int   kInvSqrtN = 512;
constexpr float kInvSqrtMax = 16.0f;

struct InvSqrtTable
{
    float v[kInvSqrtN + 1];
    InvSqrtTable()
    {
        for (int i = 0; i <= kInvSqrtN; ++i)
        {
            const float h2 = kInvSqrtMax * (float) i / (float) kInvSqrtN;
            v[i] = 1.0f / std::sqrt (1.0f + h2);
        }
    }
    float at (float h2) const noexcept
    {
        const int i = (int) (h2 * ((float) kInvSqrtN / kInvSqrtMax));
        return v[i < 0 ? 0 : (i > kInvSqrtN ? kInvSqrtN : i)];
    }
};
const InvSqrtTable kInvSqrt;

// 57d — el índice de paleta del relieve con la curva `FieldLens::kReliefGamma` (tv^0.8 · 255), tabulado. Un
// `std::pow` por muestra —~370 000 por frame en tamaño L a escala 2— se vería en el presupuesto; 1 024
// entradas dan pasos de nivel de 1/1023, más finos que los 256 colores de la rampa.
constexpr int kGammaN = 1024;

struct GammaIndexTable
{
    juce::uint8 v[kGammaN];
    GammaIndexTable()
    {
        for (int i = 0; i < kGammaN; ++i)
        {
            const double t = (double) i / (double) (kGammaN - 1);
            v[i] = (juce::uint8) juce::jlimit (0, 255,
                                              (int) (std::pow (t, (double) FieldLens::kReliefGamma) * 255.0));
        }
    }
    size_t at (float tv) const noexcept
    {
        const int i = (int) (tv * (float) (kGammaN - 1) + 0.5f);
        return (size_t) v[i < 0 ? 0 : (i >= kGammaN ? kGammaN - 1 : i)];
    }
};
const GammaIndexTable kGammaIndex;
}

void FieldLens::blitRelief (const juce::Image::BitmapData& bd, juce::Rectangle<float> rect,
                            const float* t, int rows, int dirs, float fade, bool coarse) const
{
    const int x0 = juce::jmax (0, (int) std::floor (rect.getX()));
    const int x1 = juce::jmin (bd.width - 1, (int) std::ceil (rect.getRight()));
    if (x1 < x0 || rect.getWidth() <= 0.0f || rect.getHeight() <= 0.0f || rows < 2 || dirs < 2) return;

    const auto  bgArgb   = th::bg1.withAlpha (1.0f).getARGB();
    const float planeBot = rect.getBottom();
    const float baseSpan = rect.getHeight() * (1.0f - kReliefFrac);   // lo que ocupan las FILAS
    const float reliefPx = rect.getHeight() * kReliefFrac;            // lo que se lleva la ALTURA

    // ========================================================================================================
    // ===== 57c · LA OCLUSIÓN SE RESUELVE CADA DOS COLUMNAS CUANDO SOBRA RESOLUCIÓN =====
    //
    // La grilla tiene 64 columnas de paneo y el plano mide ~1 900 px de dispositivo a escala 2: TREINTA
    // píxeles de pantalla por columna de dato. Todo lo que hay entre dos columnas de grilla es
    // interpolación, así que resolver el horizonte (la pasada 1, que es la cara) en cada píxel es muestrear
    // una rampa treinta veces donde alcanza con quince. A escala física ≥ 2 se resuelve cada DOS columnas
    // y la pasada 2 pinta los dos píxeles del par con el mismo tramo: la silueta queda cuantizada a 2 px
    // de dispositivo, o sea UN píxel lógico — por debajo de lo que el ojo puede separar en una superficie
    // suave, y por debajo de lo que el dato tiene para decir.
    //
    // Es lo que le da a la lente el margen que el harness le exige: medido a escala 2, tamaño L, con la
    // máquina quieta, 4.20 → ver BUDGET_FIELD. El margen importa porque la carga patrón del banco NO
    // modela esta lente (lo dice el bloque del 56c en LensBudgetTest.cpp): con la máquina ocupada FIELD
    // sube más de lo que `k` predice, y sin margen eso es un test que falla por el sistema operativo.
    //
    // A escala 1 no se hace: ahí no sobra nada.
    const int colStep = coarse ? 2 : 1;
    const int nCols = x1 - x0 + 1;
    const int nRes  = (nCols + colStep - 1) / colStep;   // columnas RESUELTAS (las de la pasada 1)

    // Mapeo de columna → celda, una vez por plano (igual que blitSurface), sólo en las resueltas.
    colIdx.resize ((size_t) nRes);
    colFrac.resize ((size_t) nRes);
    for (int i = 0; i < nRes; ++i)
    {
        const float u = ((float) (x0 + i * colStep) + 0.5f - rect.getX()) / rect.getWidth() * (float) (dirs - 1);
        const int   c = juce::jlimit (0, dirs - 2, (int) u);
        colIdx [(size_t) i] = c;
        colFrac[(size_t) i] = juce::jlimit (0.0f, 1.0f, u - (float) c);
    }

    const int   steps = (rows - 1) * kReliefSteps;
    const float rowStep = 1.0f / (float) kReliefSteps;
    const float dyPerStep = baseSpan / (float) steps;   // píxeles de pantalla entre dos muestras

    // ===== LA NORMAL SE MIDE ENTRE PÍXELES, NO ENTRE CELDAS =====
    //
    // Primera versión: la pendiente salía de la diferencia entre las dos celdas que la interpolación ya
    // tenía en la mano. Sale más barato y está MAL: esa diferencia es constante DENTRO de una celda y
    // salta en el borde, así que el sombreado quedaba escalonado en cuadras de ~30 px y la superficie se
    // veía facetada (se vio en la captura de `field_wide_M`). La altura era continua; la LUZ no.
    //
    // Ahora la pendiente se toma contra la muestra de al lado —la columna anterior y el paso anterior—,
    // que ya están calculadas. Son dos restas y da una normal a resolución de pantalla.
    const float kGx = reliefPx / (float) colStep;                   // d(nivel)/d(px) → px/px
    const float kGy = reliefPx / juce::jmax (0.05f, dyPerStep);

    const int bottom = juce::jmin (bd.height - 1, (int) std::ceil (planeBot));
    const int top    = juce::jmax (0, (int) std::floor (rect.getY()));
    if (bottom < top) return;

    // ========================================================================================================
    // DOS PASADAS, Y POR QUÉ: LA MEMORIA MANDA.
    //
    // La oclusión se resuelve por COLUMNA (cada muestra tapa lo que quedó detrás), pero escribir por
    // columna es lo peor que se le puede pedir a una caché: dos píxeles consecutivos de una columna están
    // a `lineStride` bytes, así que CADA píxel toca una línea de caché distinta. Medido en tamaño L a
    // escala 2: 1.39 millones de píxeles escritos así daban 6.3 ms contra un criterio de 6 — y el grueso
    // no eran las cuentas (bajar las muestras a la mitad sólo sacaba 0.5 ms), era el tránsito de memoria.
    //
    // Entonces: la pasada 1 resuelve la oclusión por columna pero NO TOCA LA IMAGEN — anota los TRAMOS
    // visibles (dónde empieza cada uno, de qué color y con qué alfa). La pasada 2 recorre por FILAS, que
    // es como está la imagen en memoria, y pinta 16 píxeles por línea de caché.
    //
    // Los tramos se guardan por PLANO DE `k` (`k * nCols + xi`) y no por columna: se probaron las dos y
    // ésta gana (4.72 contra 4.87 ms). El motivo es que la superficie es continua, así que columnas
    // vecinas van casi siempre por el mismo tramo — y con este orden esos accesos caen juntos.
    // ========================================================================================================
    const int maxRuns = steps + 1;
    // `resize` y no `assign`: los tres arreglos de tramos se escriben enteros antes de leerse (lo que
    // vale de cada columna lo dice `reliefRunCount`), así que ponerlos en cero es 1.7 MB de memset por
    // frame a cambio de nada. Los dos que SÍ hay que limpiar son el conteo y el cursor.
    reliefRunY     .resize ((size_t) (maxRuns * nRes));
    reliefRunSrc   .resize ((size_t) (maxRuns * nRes));
    reliefRunOverBg.resize ((size_t) (maxRuns * nRes));
    reliefRunAlpha .resize ((size_t) (maxRuns * nRes));
    reliefRunCount.assign ((size_t) nRes, 0);
    reliefCursor  .assign ((size_t) nRes, 0);
    reliefPrevCol .assign ((size_t) (steps + 1), 0.0f);
    reliefCurCol  .assign ((size_t) (steps + 1), 0.0f);
    auto& prevCol = reliefPrevCol;
    auto& curCol  = reliefCurCol;
    bool hasPrevCol = false;
    int  surfaceTop = bottom;   // lo más alto que llega la superficie: arriba de eso no hay nada que pintar

    // ---- PASADA 1: la oclusión, columna por columna, sin tocar la imagen ----
    for (int xi = 0; xi < nRes; ++xi)
    {
        const int   c0 = colIdx [(size_t) xi];
        const int   c1 = c0 + 1;
        const float wc = colFrac[(size_t) xi];
        int   horizon = bottom + 1;     // nada visible todavía en esta columna
        int   nRuns = 0;
        float prevTv = 0.0f;
        juce::uint32 lastSrc = 0, lastAi = 0;   // el último tramo anotado, para fusionar (ver abajo)

        for (int s = 0; s <= steps; ++s)
        {
            const float rowPos = (float) s * rowStep;
            const int   r0 = juce::jlimit (0, rows - 2, (int) rowPos);
            const float wr = juce::jlimit (0.0f, 1.0f, rowPos - (float) r0);
            const float* rowA = t + (size_t) (r0 * dirs);
            const float* rowB = t + (size_t) ((r0 + 1) * dirs);

            // Con kReliefSteps = 1 la muestra cae EXACTO sobre la fila (wr = 0 salvo en la última), así
            // que la fila de arriba no se toca: son dos cargas y tres operaciones por muestra —180 000
            // por frame a escala 2— para multiplicar por cero.
            const float a = rowA[c0] + (rowA[c1] - rowA[c0]) * wc;   // nivel en la fila de abajo
            float tv = a;
            if (wr > 0.0f)
            {
                const float b = rowB[c0] + (rowB[c1] - rowB[c0]) * wc;   // …y en la de arriba
                tv = a + (b - a) * wr;
            }
            curCol[(size_t) s] = tv;

            // La y de la superficie: la base de su fila, menos lo que levanta el nivel.
            const float y  = planeBot - rowPos / (float) (rows - 1) * baseSpan - reliefPx * tv;
            const int   yi = juce::jlimit (top, bottom, (int) std::lround (y));
            const float hx = hasPrevCol ? (tv - prevCol[(size_t) s]) * kGx : 0.0f;
            const float hy = s > 0 ? (tv - prevTv) * kGy : 0.0f;
            prevTv = tv;
            if (yi >= horizon) continue;      // ya hay algo más adelante tapándolo
            // 57c — UN TRAMO DE UN PÍXEL DE ALTO NO SE ANOTA. Donde la superficie es suave (que desde el
            // suavizado de pantalla es casi todas partes) dos muestras seguidas caen en filas contiguas:
            // anotar las dos cuesta una escritura de tramo acá y un avance de cursor en la pasada 2 para
            // pintar UN píxel de un color que es casi el del vecino. El horizonte avanza igual, así que
            // la oclusión no cambia; lo que cambia es que ese píxel lo pinta el tramo de abajo.
            if (nRuns > 0 && yi > horizon - 2) { horizon = yi; continue; }

            const float inv = kInvSqrt.at (hx * hx + hy * hy);
            const float lambert = kReliefAmbient
                                + kReliefDiffuse * juce::jmax (0.0f, (-hx * kLight.x - hy * kLight.y + kLight.z) * inv);

            // EL ALFA SIGUE AL NIVEL, como antes del relieve. Una superficie OPACA en todo el plano
            // pinta una pared del color del piso de la rampa sobre el 90 % de la lente —se probó, y es
            // exactamente lo que no queremos—: donde no hay energía tiene que verse el pozo. El piso
            // (`kSurfaceFloorAlpha`) es lo que hace que la superficie EXISTA igual donde la señal no
            // llega, que era el problema original que ese número vino a resolver.
            const float alpha = (kSurfaceFloorAlpha + (1.0f - kSurfaceFloorAlpha) * tv) * fade;

            // 57d — el COLOR sale del nivel con la curva kReliefGamma; el alfa de arriba sigue al nivel crudo.
            const auto src = raster::scaleRgb (palette[kGammaIndex.at (tv)], lambert);
            const auto ai  = (juce::uint32) juce::jlimit (0, 255, (int) (alpha * 255.0f));
            // `overBg` es el mismo tramo YA MEZCLADO contra el fondo. Es el caso de lejos más común —la
            // mayor parte del plano de adelante no tiene estela detrás— y ahí la pasada 2 se ahorra la
            // mezcla entera: compara el destino contra el fondo y escribe. Se calcula una vez por TRAMO
            // (≈186 000) en vez de una vez por píxel (1.39 millones).
            // ===== 57c · DOS TRAMOS DEL MISMO COLOR SON UN TRAMO =====
            //
            // Con la grilla suavizada (ver screenSmoothed) el nivel cambia despacio a lo largo de una
            // columna, así que dos muestras seguidas caen muchas veces en la MISMA entrada de paleta y
            // con el mismo alfa. Anotarlas por separado no cambia un píxel —el color es idéntico— y
            // cuesta dos veces: una escritura de tramo en esta pasada y un avance de cursor en la otra.
            // Se fusionan: el horizonte avanza igual (la oclusión no cambia), el tramo no se anota.
            if (nRuns > 0 && src == lastSrc && ai == lastAi) { horizon = yi; continue; }

            const auto idx = (size_t) (nRuns * nRes + xi);
            reliefRunY     [idx] = yi;
            reliefRunSrc   [idx] = src;
            reliefRunOverBg[idx] = blendArgb (src, bgArgb, ai);
            reliefRunAlpha [idx] = (juce::uint8) ai;
            ++nRuns;
            lastSrc = src;
            lastAi  = ai;
            horizon = yi;
        }

        reliefRunCount[(size_t) xi] = nRuns;
        if (nRuns > 0) surfaceTop = juce::jmin (surfaceTop, (int) reliefRunY[(size_t) ((nRuns - 1) * nRes + xi)]);
        prevCol.swap (curCol);
        hasPrevCol = true;
    }

    // ---- PASADA 2: el pintado, por filas ----
    //
    // Los tramos de una columna están ordenados de abajo hacia arriba y son contiguos, así que barriendo
    // las filas de abajo hacia arriba el cursor de cada columna avanza de a uno y nunca vuelve.
    const juce::int32*  runY  = reliefRunY.data();
    const juce::uint32* runSrc = reliefRunSrc.data();
    const juce::uint32* runPre = reliefRunOverBg.data();
    const juce::uint8*  runA   = reliefRunAlpha.data();
    const juce::int32*  runN   = reliefRunCount.data();
    juce::int32*        cur    = reliefCursor.data();

    // ===== EL TRAMO VIGENTE, EN ARREGLOS COMPACTOS =====
    //
    // La pasada 2 lee, en CADA píxel, dónde empieza el tramo de esa columna y de qué color es. Leerlo del
    // arreglo grande de tramos son cuatro accesos que saltan entre planos de `k` separados por ~8 KB, y a
    // 1.39 millones de píxeles eso es tránsito de memoria, no cuentas. Acá se copia el tramo vigente a
    // cuatro arreglos de UNA entrada por columna (≈ 31 KB entre los cuatro: entran en L1), y se vuelven a
    // escribir sólo cuando el cursor avanza de verdad — que es una vez cada ~7 filas.
    reliefCurY    .assign ((size_t) nRes, bottom + 1);
    reliefCurPre  .assign ((size_t) nRes, 0u);
    reliefCurSrc  .assign ((size_t) nRes, 0u);
    reliefCurAlpha.assign ((size_t) nRes, 0u);
    juce::int32*  curY = reliefCurY.data();
    juce::uint32* curPre = reliefCurPre.data();
    juce::uint32* curSrc = reliefCurSrc.data();
    juce::uint32* curA = reliefCurAlpha.data();
    for (int xi = 0; xi < nRes; ++xi)
        if (runN[xi] > 0)
        {
            curY  [xi] = runY  [xi];
            curPre[xi] = runPre[xi];
            curSrc[xi] = runSrc[xi];
            curA  [xi] = runA  [xi];
        }

    for (int y = bottom; y >= juce::jmax (top, surfaceTop); --y)
    {
        auto* row = (juce::uint32*) bd.getLinePointer (y) + x0;
        for (int xi = 0; xi < nRes; ++xi)
        {
            int k = cur[xi];
            const int n = runN[xi];
            if (k >= n) continue;                       // esta columna ya se terminó
            // El tramo de una columna cubre varias filas seguidas, así que lo habitual es NO avanzar.
            if (y < curY[xi])
            {
                do { ++k; } while (k < n && y < runY[k * nRes + xi]);
                cur[xi] = k;
                if (k >= n) continue;                   // por encima de la silueta: no hay superficie
                const auto idx = (size_t) (k * nRes + xi);
                curY  [xi] = runY  [idx];
                curPre[xi] = runPre[idx];
                curSrc[xi] = runSrc[idx];
                curA  [xi] = runA  [idx];
            }

            // Los `colStep` píxeles del grupo comparten tramo: el cursor se mira UNA vez.
            const auto src = curSrc[xi], pre = curPre[xi], ai = curA[xi];
            for (int q = 0, px = xi * colStep; q < colStep && px < nCols; ++q, ++px)
            {
                auto* p = row + px;
                const auto d = *p;
                *p = (d == bgArgb) ? pre : blendArgb (src, d, ai);
            }
        }
    }
}

//======================================================================================== el escenario
// La caja de alambre NO es decoración. Sin ella, la estela de una fuente QUIETA —que se aleja hacia arriba
// y hacia el centro— se lee como una fuente que se está moviendo. Con las aristas a la vista, la misma
// diagonal se lee como profundidad, que es lo que es.
void FieldLens::drawStage (juce::Graphics& g) const
{
    const auto fl  = proj.project (0.0f, 0.0f, 0.0f), fr  = proj.project (1.0f, 0.0f, 0.0f);
    const auto bl  = proj.project (0.0f, 0.0f, 1.0f), br  = proj.project (1.0f, 0.0f, 1.0f);
    const auto ftl = proj.project (0.0f, 1.0f, 0.0f), ftr = proj.project (1.0f, 1.0f, 0.0f);
    const auto btl = proj.project (0.0f, 1.0f, 1.0f), btr = proj.project (1.0f, 1.0f, 1.0f);

    // 56: la caja usa la JERARQUÍA de Look.h. Antes todas las aristas eran la misma hairline al 7 % y la
    // caja no se leía como caja: el suelo de adelante (el borde contra el que se apoya el dato) y las
    // aristas de fuga pesan, y el fondo subdivide.
    g.setColour (look::gridMinor);
    g.drawLine (bl.x,  bl.y,  br.x,  br.y,  1.0f);   // suelo del fondo
    g.drawLine (btl.x, btl.y, btr.x, btr.y, 1.0f);   // techo del fondo
    g.drawLine (btl.x, btl.y, bl.x,  bl.y,  1.0f);   // verticales del fondo
    g.drawLine (btr.x, btr.y, br.x,  br.y,  1.0f);
    g.drawLine (fl.x,  fl.y,  bl.x,  bl.y,  1.0f);   // aristas de fuga
    g.drawLine (fr.x,  fr.y,  br.x,  br.y,  1.0f);
    g.drawLine (ftl.x, ftl.y, btl.x, btl.y, 1.0f);
    g.drawLine (ftr.x, ftr.y, btr.x, btr.y, 1.0f);

    // El plano de ADELANTE es el que se lee: va más marcado que el resto de la caja.
    g.setColour (ovni::ui::theme::line);
    g.drawLine (fl.x,  fl.y,  fr.x,  fr.y,  1.0f);
    g.drawLine (ftl.x, ftl.y, ftr.x, ftr.y, 1.0f);
    g.drawLine (ftl.x, ftl.y, fl.x,  fl.y,  1.0f);
    g.drawLine (ftr.x, ftr.y, fr.x,  fr.y,  1.0f);

    // La vertical del CENTRO (pan = 0): la referencia con la que se lee todo lo demás.
    const auto c0 = proj.project (0.5f, 0.0f, 0.0f), c1 = proj.project (0.5f, 1.0f, 0.0f);
    g.setColour (ovni::ui::theme::lineSoft);
    g.drawLine (c0.x, c0.y, c1.x, c1.y, 1.0f);
}

//======================================================================================== capa estática
void FieldLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    proj  = projectionFor (zones);

    // ---- eje de FRECUENCIA (log, por octavas), en el plano de adelante ----
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (const double hz : kOctavesHz)
    {
        const double t = std::log10 (hz / FieldFrame::kMinHz) / FieldFrame::kDecades;
        if (t < 0.0 || t > 1.0) continue;
        // 57b — la MISMA cuenta que usa la superficie (`baseY01`): el eje marca dónde SE APOYA cada
        // frecuencia, y el relieve la levanta desde ahí. Sin esto el eje quedaría diciendo otra cosa.
        const auto p = proj.project (0.0f, baseY01 ((float) t), 0.0f);
        const int  y = juce::roundToInt (p.y);
        g.setColour (th::line);
        look::fillSnapped (g, { (float) (zones.freqAxis.getRight() - 5), (float) (y), (float) (5), 1.0f });
        g.setColour (th::fnt);
        g.drawText (shortHz (hz), zones.freqAxis.getX(), y - 6, kAxisW - 8, 12,
                    juce::Justification::centredRight, false);
    }
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("Hz", zones.freqAxis.getX(), zones.plot.getY() + 2, kAxisW - 8, 12,
                juce::Justification::centredRight, false);

    // ---- eje de DIRECCIÓN: L … C … R, con ticks en ±0.5 y NUNCA grados (ver el encabezado) ----
    const struct { float pan; const char* label; } marks[] = {
        { -1.0f, "L" }, { -0.5f, nullptr }, { 0.0f, "C" }, { 0.5f, nullptr }, { 1.0f, "R" }
    };
    g.setFont (ovni::ui::fonts::mono (10.0f));
    for (const auto& m : marks)
    {
        const auto p = proj.project ((m.pan + 1.0f) * 0.5f, 0.0f, 0.0f);
        const int  x = juce::roundToInt (p.x);
        g.setColour (th::line);
        look::fillSnapped (g, { (float) (x), (float) (zones.dirAxis.getY()), 1.0f, (float) (m.label != nullptr ? 5 : 3) });
        if (m.label == nullptr) continue;
        g.setColour (th::mut);
        g.drawText (m.label, x - 20, zones.dirAxis.getY() + 4, 40, 12, juce::Justification::centred, false);
    }

    // EL RÓTULO FIJO. Va debajo del eje que califica —que es donde alguien busca qué significa el eje— y
    // no se puede apagar. Ver la nota de honestidad del encabezado.
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.0f));
    g.drawText (honestyLabel(),
                zones.dirAxis.getX(), zones.dirAxis.getBottom() - 12, zones.dirAxis.getWidth(), 12,
                juce::Justification::centred, false);
}

//======================================================================================== capa viva
void FieldLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); proj = projectionFor (zones); }

    // La escala física se lee acá (ver lenses/Raster.h). La caja de alambre, los ejes y la lectura siguen
    // en el plano LÓGICO: sólo la superficie es de píxeles.
    cache.prepare (look::physicalScale (g), zones.plot.getWidth(), zones.plot.getHeight());

    updateImage();
    cache.blit (g, zones.plot.getX(), zones.plot.getY());
    drawStage (g);   // encima de la imagen: ver el comentario de drawStage

    if (cursor.x >= 0)
    {
        const auto r = readoutAt (cursor);
        if (r.valid)
        {
            g.setColour (th::txt.withAlpha (0.28f));
            look::fillSnapped (g, { (float) (cursor.x), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });

            const juce::String side = r.panPercent < -0.5 ? "L" : (r.panPercent > 0.5 ? "R" : "C");
            const juce::String text = side + " " + juce::String (std::abs (r.panPercent), 0) + " %  \xc2\xb7  "
                                    + shortHz (r.freqHz) + " Hz  \xc2\xb7  " + juce::String (r.db, 1) + " dB rel";
            g.setFont (ovni::ui::fonts::mono (11.0f));
            const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16;
            const auto box = readoutBoxFor (zones.plot, cursor.x, tw);
            g.setColour (th::bg1.withAlpha (0.9f));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (th::green.withAlpha (0.4f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (th::txt);
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }

    paintButton (g, zones.button[ctrlDecay],  tr (strings::Key::decay),
                 juce::String (processor.fieldDecaySec(), 1) + " s", hovered == ctrlDecay);
    paintButton (g, zones.button[ctrlWindow], tr (strings::Key::window),
                 juce::String (processor.bandsWindowSec(), 1) + " s", hovered == ctrlWindow);
    paintButton (g, zones.button[ctrlPalette], tr (strings::Key::palette),
                 look::paletteName (look::paletteFromIndex (processor.paletteIndex())), hovered == ctrlPalette);

    // El estado del dibujo, chico y al costado: cuántos puntos entraron del tope y cuántas láminas de
    // estela hay. Sin esto, "se ven pocos puntos" no se distingue de "hay poca señal".
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    // 56: ya no se cuentan PUNTOS (no hay). Se dice el tamaño de la superficie y cuántas láminas de
    // historia hay detrás, que es lo que de verdad describe lo que se está mirando.
    g.drawText (juce::String (FieldFrame::kDir) + juce::String::fromUTF8 (" \xc3\x97 ")
                    + juce::String (FieldFrame::kRows) + juce::String::fromUTF8 ("  \xc2\xb7  ")
                    + juce::String (trailLayers) + " " + tr (strings::Key::trails),
                zones.footer.getRight() - 180, zones.footer.getY(), 178, zones.footer.getHeight(),
                juce::Justification::centredRight, false);
}

void FieldLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                             const juce::String& value, bool hovered_) const
{
    const auto hue = th::green;
    const auto r = area.toFloat();

    g.setColour (th::surf2);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hue.withAlpha (hovered_ ? 0.55f : 0.24f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    if (hovered_)
    {
        g.setColour (hue.withAlpha (th::state::hoverGlow));
        g.fillRoundedRectangle (r, 3.0f);
    }

    auto inner = area.reduced (8, 0);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.0f));
    g.drawText (label, inner.removeFromLeft (inner.getWidth() * 3 / 5), juce::Justification::centredLeft, false);
    g.setColour (hue);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (value, inner, juce::Justification::centredRight, false);
}

//======================================================================================== animación
bool FieldLens::advanceFrame()
{
    // 57b — si cambió la rampa (desde otra lente, o al cargar un estado) se rehornea la tabla y se
    // repinta: la superficie se dibuja entera cada frame, así que con la tabla nueva ya alcanza.
    if (processor.paletteIndex() != paletteSeen) { buildPalette(); return true; }

    const auto idx = processor.field().read().frameIndex;
    if (idx == lastFrame) return false;
    lastFrame = idx;
    return true;
}

//======================================================================================== lectura
FieldLens::Readout FieldLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (! zones.plot.contains (p) || ! (norm > 0.0f)) return r;

    // Sólo el plano de ADELANTE: en profundidad un píxel cae sobre varias láminas a la vez y "cuál se
    // está señalando" no tendría una respuesta.
    const float frontTop = proj.project (0.5f, 1.0f, 0.0f).y;
    if ((float) p.y < frontTop) return r;

    const float x01 = proj.unprojectX ((float) p.x + 0.5f, 0.0f);
    // 57b — se deshace la compresión de `baseY01`: la lectura sigue al APOYO de la fila, igual que el eje
    // de frecuencia. Sobre un pico alto eso señala la base del pico y no su cresta, que es lo correcto —
    // la cresta está dibujada por encima de su propia frecuencia, no en otra.
    const float y01 = juce::jlimit (0.0f, 1.0f,
                                    (float) ((proj.y0 + proj.h - (float) p.y) / (proj.h * (1.0f - proj.tilt)))
                                        / (1.0f - kReliefFrac));

    const int col = juce::jlimit (0, FieldFrame::kDir - 1,
                                  (int) std::lround ((double) x01 * (double) (FieldFrame::kDir - 1)));
    const int row = juce::jlimit (0, FieldFrame::kRows - 1,
                                  (int) ((double) y01 * (double) FieldFrame::kRows));

    const auto& f = processor.field().read();
    r.valid      = true;
    r.row        = row;
    r.col        = col;
    r.panPercent = 100.0 * (double) FieldFrame::columnPan (col);
    r.freqHz     = FieldFrame::rowFrequency (row);
    r.db         = cellDbRel ((double) f.grid[row][col], (double) norm);
    return r;
}

//======================================================================================== interacción
void FieldLens::cycleControl (int control)
{
    switch (control)
    {
        case ctrlDecay:
            processor.setFieldDecayIndex ((processor.fieldDecayIndex() + 1) % Field::kNumDecayOptions);
            break;
        case ctrlWindow:
            // La ventana del paneo por bin es la MISMA que la de las lentes 9 y 10: es el estéreo por
            // banda mirado de otra manera, y dos ventanas distintas para el mismo número sólo confundirían.
            processor.setBandsWindowIndex ((processor.bandsWindowIndex() + 1) % StereoBands::kNumWindowOptions);
            break;
        case ctrlPalette:   // 57b — la misma rampa que las otras tres lentes de nivel
            processor.setPaletteIndex ((processor.paletteIndex() + 1) % look::kNumPalettes);
            buildPalette();
            break;
        default: return;
    }
    repaint();
}

void FieldLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void FieldLens::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int was = hovered;
    const auto wasCursor = cursor;

    hovered = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (p)) { hovered = i; break; }

    cursor = zones.plot.contains (p) ? p : juce::Point<int> (-1, -1);
    if (hovered != was || cursor != wasCursor) repaint();
}

void FieldLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
