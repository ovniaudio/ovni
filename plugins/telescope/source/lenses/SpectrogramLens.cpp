#include "lenses/SpectrogramLens.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "lenses/LensReadout.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr double kLabelledHz[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };

juce::String shortHz (double hz)
{
    if (hz >= 1000.0) return juce::String ((int) std::lround (hz / 1000.0)) + "k";
    return juce::String ((int) std::lround (hz));
}

const char* channelLabel (int c)
{
    switch (c)
    {
        case Spectrum::left:  return "L";
        case Spectrum::right: return "R";
        case Spectrum::mid:   return "M";
        case Spectrum::side:  return "S";
        default:              return "L+R";
    }
}
}

const juce::ValueTree& SpectrogramLens::stateTree() const { return processor.apvts.state; }

SpectrogramLens::SpectrogramLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (2);   // el sonograma corre solo mientras entre audio; en silencio se para enseguida
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    buildPalette();
}

//======================================================================================== paleta
// 57b — LA RAMPA LA ELIGE EL USUARIO, y es la misma para las cuatro lentes de nivel (ver
// lenses/Palettes.h y `TelescopeProcessor::kPalette`). Hasta el 57 había una sola, del tema, y el
// diagnóstico de Joaquín contra Insight fue exactamente ese: «todo el mismo color». Una rampa de un solo
// tono sólo puede codificar el nivel con el brillo, y el ojo distingue muchos menos escalones de brillo
// que de tono.
//
// Se copia a un `std::array` propio en vez de leer `look::palette()` en el bucle de dibujo: el pintado
// indexa esta tabla una vez por píxel y una indirección más por píxel se nota en el presupuesto.
void SpectrogramLens::buildPalette()
{
    paletteSeen = processor.paletteIndex();
    palette = look::palette (look::paletteFromIndex (paletteSeen));
}

//======================================================================================== geometría
SpectrogramLens::Zones SpectrogramLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (200, (foot.getWidth() - gap * (kNumControls - 1)) / kNumControls);
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.timeAxis = body.removeFromBottom (kTimeH);
    z.freqAxis = body.removeFromLeft (kAxisW);
    z.plot     = body;
    z.timeAxis = z.timeAxis.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

void SpectrogramLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    // El mapeo se arma en píxeles de DISPOSITIVO: es el tamaño real de la imagen. Con una escala física
    // de 2 eso duplica la resolución temporal (cada columna del anillo puede quedarse con su propio píxel)
    // además de la espacial — el sonograma deja de tirar la mitad de lo que midió.
    // El tamaño en píxeles de dispositivo lo fija la caché en `paintLive` (es el único lugar que ve la
    // escala física): acá se configura con lo que haya, y el primer pintado lo rehace si cambió.
    scroll.configure (processor.spectrogram(), cache.deviceW(), cache.deviceH(), SpectrogramRing::kRows);
    Lens::resized();
}

float SpectrogramLens::yForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (SpectrogramRing::kMinHz, SpectrogramRing::kMaxHz, hz)
                               / SpectrogramRing::kMinHz)
                   / std::log (SpectrogramRing::kMaxHz / SpectrogramRing::kMinHz);
    return (float) zones.plot.getBottom() - (float) t * (float) zones.plot.getHeight();
}

//======================================================================================== mapeo
// Lo que entra en el ancho del plot lo calcula el desplazador compartido: los grupos que se reparten
// sobre él, por las columnas que agrupa cada uno, divididos por la tasa de columnas.
double SpectrogramLens::visibleSeconds() const
{
    return scroll.visibleSeconds (processor.spectrogram().columnsPerSecond());
}

//======================================================================================== imagen
int SpectrogramLens::columnMax (const SpectrogramRing& ring, long long firstSrc, juce::uint8* dst) const
{
    std::fill (dst, dst + SpectrogramRing::kRows, (juce::uint8) 0);
    juce::uint8 tmp[SpectrogramRing::kRows];
    int found = 0;
    for (int i = 0; i < scroll.columnsPerGroup(); ++i)
    {
        if (! ring.copyColumn (firstSrc + i, tmp)) continue;
        ++found;
        for (int r = 0; r < SpectrogramRing::kRows; ++r) dst[r] = juce::jmax (dst[r], tmp[r]);
    }
    return found;
}

// Escribe UNA columna de píxeles. Se escribe el ARGB directo: la imagen la creamos nosotros como ARGB con
// alpha 255 (donde el premultiplicado coincide con el color plano), así que no hace falta pasar por
// setPixelColour, que cuesta diez veces más y es lo único que se hace medio millón de veces acá.
void SpectrogramLens::drawGroupInto (const juce::Image::BitmapData& bd, int firstPx, int pixels,
                                     const juce::uint8* column) const
{
    const int rows = juce::jmin (bd.height, scroll.mappedRows());
    const int n    = juce::jmin (pixels, bd.width - firstPx);
    if (n <= 0) return;

    for (int y = 0; y < rows; ++y)
    {
        int v = 0;
        for (int r = scroll.rowFrom (y), e = juce::jmin (scroll.rowTo (y), SpectrogramRing::kRows); r < e; ++r)
            v = juce::jmax (v, (int) column[r]);

        const auto argb = palette[(size_t) v];
        auto* line = reinterpret_cast<juce::uint32*> (bd.getLinePointer (y)) + firstPx;
        for (int i = 0; i < n; ++i) line[i] = argb;
    }
}

void SpectrogramLens::updateImage()
{
    const auto& ring = processor.spectrogram();

    // UNA BitmapData por redibujo, no una por columna. Perezosa porque `refresh` puede recrear la imagen
    // (cambio de tamaño) o correrla con moveImageSection antes de pedir el primer grupo.
    std::optional<juce::Image::BitmapData> bd;

    auto& image = cache.image();
    scroll.refresh (ring, image, cache.deviceW(), cache.deviceH(), SpectrogramRing::kRows,
                    juce::Colour (palette[0]),
                    [this, &ring, &bd, &image] (int firstPx, int pixels, long long firstSrc)
                    {
                        if (! bd.has_value())
                        {
                            if (! image.isValid()) return;
                            bd.emplace (image, juce::Image::BitmapData::writeOnly);
                            if (bd->pixelStride != 4) { bd.reset(); return; }
                        }
                        if (! bd.has_value()) return;

                        columnMax (ring, firstSrc, scratchCol.data());
                        drawGroupInto (*bd, firstPx, pixels, scratchCol.data());
                    });
}

//======================================================================================== capa estática
void SpectrogramLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);

    g.setColour (look::gridMinor);
    g.drawRect (zones.plot.expanded (1), 1);

    // Eje de frecuencia: la rejilla no va ENCIMA del sonograma (taparía datos), va como marcas al costado.
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (const double hz : kLabelledHz)
    {
        const int y = juce::roundToInt (yForFreq (hz));
        g.setColour (look::gridMajor);
        look::fillSnapped (g, { (float) (zones.freqAxis.getRight() - 5), (float) (y), (float) (5), 1.0f });
        g.setColour (th::fnt);
        g.drawText (shortHz (hz), zones.freqAxis.getX(), y - 6, kAxisW - 8, 12,
                    juce::Justification::centredRight, false);
    }
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("Hz", zones.freqAxis.getX(), zones.plot.getY() + 2, kAxisW - 8, 12,
                juce::Justification::centredRight, false);
}

//======================================================================================== capa viva
void SpectrogramLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) zones = zonesFor (getWidth(), getHeight());

    // LA ESCALA FÍSICA SE LEE ACÁ, que es el único lugar que la sabe (ver lenses/Raster.h). Si cambió —la
    // ventana se movió a otro monitor— el mapeo y la imagen se rehacen enteros: son de otra resolución.
    if (cache.prepare (look::physicalScale (g), zones.plot.getWidth(), zones.plot.getHeight()))
        scroll.configure (processor.spectrogram(), cache.deviceW(), cache.deviceH(), SpectrogramRing::kRows);

    updateImage();
    cache.blit (g, zones.plot.getX(), zones.plot.getY());

    // 57b — UNA REJILLA SUTIL SOBRE EL SONOGRAMA. El plot era un rectángulo de color sin ninguna
    // referencia adentro: para saber a qué altura estaba una franja había que llevar el ojo hasta el eje
    // de la izquierda y volver. Las décadas dibujadas encima, al 12 % (lo justo para verse sobre un mapa
    // de calor y no competir con él), hacen que la frecuencia se lea sin salir del dato.
    {
        const auto plot = zones.plot.toFloat();
        g.setColour (look::gridMinor.withMultipliedAlpha (0.8f));
        for (const double hz : { 100.0, 1000.0, 10000.0 })
        {
            const float y = yForFreq (hz);
            if (y <= plot.getY() || y >= plot.getBottom()) continue;
            look::fillSnapped (g, { plot.getX(), y, plot.getWidth(), 1.0f });
        }
    }

    // ---- eje de tiempo: el tramo REAL que entra en el ancho, no la historia guardada ----
    const double span = visibleSeconds();
    const double step = span <= 12.0 ? 2.0 : (span <= 34.0 ? 5.0 : 10.0);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (double t = 0.0; t <= span + 1.0e-6; t += step)
    {
        const int x = zones.plot.getRight() - juce::roundToInt (t / span * (double) zones.plot.getWidth());
        if (x < zones.plot.getX()) break;
        g.setColour (look::gridMajor);
        look::fillSnapped (g, { (float) (x), (float) (zones.timeAxis.getY()), 1.0f, (float) (4) });
        g.setColour (th::fnt);
        g.drawText (t <= 0.0 ? tr (strings::Key::now) : ("-" + juce::String ((int) t) + " s"),
                    x - 24, zones.timeAxis.getY() + 3, 48, 12, juce::Justification::centred, false);
    }

    if (cursor.x >= 0)
    {
        const auto r = readoutAt (cursor);
        if (r.valid)
        {
            g.setColour (th::txt.withAlpha (0.35f));
            look::fillSnapped (g, { (float) (cursor.x), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });
            look::fillSnapped (g, { (float) (zones.plot.getX()), (float) (cursor.y), (float) (zones.plot.getWidth()), 1.0f });

            const juce::String text = "-" + juce::String (r.secondsAgo, 2) + " s  \xc2\xb7  "
                                    + shortHz (r.freqHz) + " Hz  \xc2\xb7  " + juce::String (r.db, 1) + " dB";
            g.setFont (ovni::ui::fonts::mono (11.0f));
            const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16;
            const auto box = readoutBoxFor (zones.plot, cursor.x, tw);   // ver LensReadout.h
            g.setColour (th::bg1.withAlpha (0.9f));
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
            g.setColour (th::green.withAlpha (0.4f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
            g.setColour (th::txt);
            g.drawText (text, box, juce::Justification::centred, false);
        }
    }

    const auto s = processor.spectrumSettings();
    paintButton (g, zones.button[history], tr (strings::Key::history), juce::String (s.historySeconds()) + " s", hovered == history);
    paintButton (g, zones.button[range],   tr (strings::Key::range),   juce::String (s.rangeDb()) + " dB",    hovered == range);
    paintButton (g, zones.button[channel], tr (strings::Key::channel), channelLabel (s.channel),             hovered == channel);
    paintButton (g, zones.button[ctrlPalette], tr (strings::Key::palette),
                 look::paletteName (look::paletteFromIndex (processor.paletteIndex())), hovered == ctrlPalette);
}

void SpectrogramLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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
    g.drawText (label, inner.removeFromLeft (inner.getWidth() / 2), juce::Justification::centredLeft, false);
    g.setColour (hue);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (value, inner, juce::Justification::centredRight, false);
}

//======================================================================================== animación
bool SpectrogramLens::advanceFrame()
{
    // 57b — el sonograma guarda los colores YA resueltos en la imagen, así que cambiar de rampa obliga a
    // rehacerla entera: no alcanza con rehornear la tabla.
    if (processor.paletteIndex() != paletteSeen)
    {
        buildPalette();
        rebuildOnNextPaint();
        return true;
    }

    // REDUCED MOTION no se consulta acá a propósito: el eje X de esta lente ES el tiempo. Congelarla no
    // sería "menos movimiento", sería dejar de mostrar el dato. Lo que se apaga en las otras lentes son
    // estelas y suavizados; acá no hay ninguno de los dos.
    return scroll.needsRepaint (processor.spectrogram().writeIndex());
}

//======================================================================================== lectura
// EL dB SALE DEL DATO, NO DEL COLOR. La versión anterior leía el píxel y buscaba en la paleta el color
// más parecido. Suena razonable ("lo que el usuario ve") y es frágil: la paleta redondea a 8 bits y tiene
// DIEZ PARES de índices consecutivos con el mismo ARGB, así que en esos diez niveles la búsqueda devolvía
// siempre el más bajo del par y la lectura mentía un escalón entero (rango/255 = 0.353 dB con rango 90).
// Y si algún día la paleta deja de ser monótona, el error deja de ser un escalón.
//
// Acá se relee el byte del anillo con LA MISMA agrupación con la que se pintó la columna (mismo grupo de
// grupo de columnas, mismo máximo por fila de píxel), y se mapea con la fórmula del dato. Si el
// anillo ya descartó esa columna —el único caso posible: el lector atrasado más de la historia entera—
// no hay número que dar y la lectura se declara inválida en vez de inventar uno.
SpectrogramLens::Readout SpectrogramLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (! zones.plot.contains (p)) return r;

    // El mouse llega en píxeles LÓGICOS y el mapeo vive en píxeles de dispositivo: se convierte acá y en
    // un solo lugar. Sin esto la lectura señalaría la mitad izquierda del sonograma en una Retina.
    const int col = (int) ((float) (p.x - zones.plot.getX()) * cache.scale());
    const int row = (int) ((float) (p.y - zones.plot.getY()) * cache.scale());
    if (col < 0 || row < 0 || row >= scroll.mappedRows()) return r;

    const long long src = scroll.sourceAt (col);
    if (src < 0) return r;                       // columna todavía sin pintar

    juce::uint8 cell[SpectrogramRing::kRows];
    if (columnMax (processor.spectrogram(), src, cell) <= 0) return r;

    int v = 0;
    for (int k = scroll.rowFrom (row); k < scroll.rowTo (row) && k < SpectrogramRing::kRows; ++k)
        v = juce::jmax (v, (int) cell[k]);

    const double span = visibleSeconds();
    r.valid = true;
    r.secondsAgo = span * (double) (zones.plot.getRight() - p.x) / (double) zones.plot.getWidth();

    const double t = (double) (zones.plot.getBottom() - p.y) / (double) zones.plot.getHeight();
    r.freqHz = SpectrogramRing::kMinHz * std::pow (SpectrogramRing::kMaxHz / SpectrogramRing::kMinHz,
                                                   juce::jlimit (0.0, 1.0, t));

    const double range = (double) processor.spectrumSettings().rangeDb();
    r.db = (float) ((double) v / 255.0 * range - range);
    return r;
}

//======================================================================================== interacción
void SpectrogramLens::cycleControl (int control)
{
    auto s = processor.spectrumSettings();
    switch (control)
    {
        case history: s.historySecIndex = (s.historySecIndex + 1) % SpectrogramRing::kNumHistoryOptions; break;
        case range:   s.rangeDbIndex    = (s.rangeDbIndex + 1) % Spectrum::kNumRanges; break;
        case channel: s.channel         = (s.channel + 1) % Spectrum::kNumChannels; break;
        // La paleta NO es un setting del espectro: vive suelta en el estado y la comparten cuatro lentes.
        case ctrlPalette:
            processor.setPaletteIndex ((processor.paletteIndex() + 1) % look::kNumPalettes);
            buildPalette();
            rebuildOnNextPaint();
            repaint();
            return;
        default: return;
    }
    processor.setSpectrumSettings (s);
    // Los tres cambian el MAPEO de lo que ya está dibujado: el motor limpia el ring y acá se rehace todo.
    rebuildOnNextPaint();
    repaint();
}

void SpectrogramLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void SpectrogramLens::mouseMove (const juce::MouseEvent& e)
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

void SpectrogramLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
