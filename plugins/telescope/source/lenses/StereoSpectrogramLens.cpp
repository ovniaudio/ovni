#include "lenses/StereoSpectrogramLens.h"
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
}

const juce::ValueTree& StereoSpectrogramLens::stateTree() const { return processor.apvts.state; }

StereoSpectrogramLens::StereoSpectrogramLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (2);   // corre mientras entre audio; en silencio se para enseguida
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    buildPalette();
}

//======================================================================================== paleta
// EL COLOR ES LA FASE, EL BRILLO ES EL NIVEL. La rampa de coherencia sale entera del tema del sello:
//
//     0 (coherencia −1) = el color de ALERTA        "fuera de fase": esto se cancela al monoficar
//   128 (coherencia  0) = el acento de la FAMILIA   "ancho": los dos canales traen cosas distintas
//   255 (coherencia +1) = el texto (casi blanco)    "mono": los dos canales traen lo mismo
//
// Y todo eso multiplicado por el brillo del nivel: una celda sin energía es NEGRA, no "roja apagada".
// Sin esa multiplicación, el piso de ruido —donde la fase es puro azar— pintaría la pantalla entera de
// colores que no quieren decir nada.
// 57b — QUÉ HACE ACÁ LA PALETA ELEGIDA, que en esta lente no puede hacer lo mismo que en las otras tres.
//
// En SPECTROGRAM, WATERFALL y FIELD el color ES el nivel, así que cambiar de rampa cambia el color. Acá
// no: el color es la FASE (y tiene que seguir siendo bipolar, o la lente deja de decir lo que dice). Lo
// que la rampa elegida aporta es su ENVOLVENTE DE LUMINANCIA como respuesta del eje de NIVEL — que es la
// mitad del problema que Joaquín señaló: con una curva que sube despacio, las celdas de nivel medio
// quedan tan oscuras que no se les puede leer el color, que es justo lo que esta lente existe para
// mostrar. `inferno`, por ejemplo, sube rápido al principio y ahí se ganan los escalones.
//
// La envolvente se fuerza MONÓTONA (máximo corrido) porque acá el brillo ES el nivel: `spectrum` no es
// monótona en luminancia —a propósito, ver Palettes.h— y usarla cruda haría que una celda más fuerte se
// viera más apagada que una más débil.
void StereoSpectrogramLens::buildPalette()
{
    paletteSeen = processor.paletteIndex();
    const auto& ramp = look::palette (look::paletteFromIndex (paletteSeen));

    std::array<float, 256> level {};
    {
        const auto luma = [] (juce::uint32 argb)
        {
            return 0.2126f * (float) ((argb >> 16) & 0xffu) + 0.7152f * (float) ((argb >> 8) & 0xffu)
                 + 0.0722f * (float) (argb & 0xffu);
        };
        float running = luma (ramp[0]);
        const float lo = running;
        for (int i = 0; i < 256; ++i)
        {
            running = juce::jmax (running, luma (ramp[(size_t) i]));
            level[(size_t) i] = running - lo;
        }
        const float hi = juce::jmax (1.0e-3f, level[255]);
        for (auto& v : level) v /= hi;
    }

    for (int c = 0; c < 256; ++c)
    {
        const float t = (float) c / 255.0f;
        const juce::Colour hue = t < 0.5f ? th::red.interpolatedWith (th::green, t / 0.5f)
                                          : th::green.interpolatedWith (th::txt, (t - 0.5f) / 0.5f);
        const int r = hue.getRed(), g = hue.getGreen(), b = hue.getBlue();

        for (int e = 0; e < 256; ++e)
        {
            // El brillo sigue la envolvente de la rampa elegida (ver el bloque de arriba). La de `ovni`
            // es casi la potencia 0.8 que había acá hasta el 57b, así que el default no cambia de aspecto.
            const double k = (double) level[(size_t) e];
            palette[(size_t) (c * 256 + e)] =
                juce::Colour ((juce::uint8) std::lround ((double) r * k),
                              (juce::uint8) std::lround ((double) g * k),
                              (juce::uint8) std::lround ((double) b * k)).withAlpha (1.0f).getARGB();
        }
    }
}

//======================================================================================== geometría
StereoSpectrogramLens::Zones StereoSpectrogramLens::zonesFor (int w, int h) const
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

    z.legend   = body.removeFromBottom (kLegendH);
    z.timeAxis = body.removeFromBottom (kTimeH);
    z.freqAxis = body.removeFromLeft (kAxisW);
    z.plot     = body;
    z.timeAxis = z.timeAxis.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    z.legend   = z.legend.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

void StereoSpectrogramLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    // En píxeles de DISPOSITIVO, igual que la lente 4 (ver lenses/Raster.h).
    // El tamaño en píxeles de dispositivo lo fija la caché en `paintLive` (es el único lugar que ve la
    // escala física): acá se configura con lo que haya, y el primer pintado lo rehace si cambió.
    scroll.configure (processor.stereoSpectrogram(), cache.deviceW(), cache.deviceH(),
                      StereoSpectrogramRing::kRows);
    Lens::resized();
}

float StereoSpectrogramLens::yForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (StereoSpectrogramRing::kMinHz, StereoSpectrogramRing::kMaxHz, hz)
                               / StereoSpectrogramRing::kMinHz)
                   / std::log (StereoSpectrogramRing::kMaxHz / StereoSpectrogramRing::kMinHz);
    return (float) zones.plot.getBottom() - (float) t * (float) zones.plot.getHeight();
}

double StereoSpectrogramLens::visibleSeconds() const
{
    return scroll.visibleSeconds (processor.stereoSpectrogram().columnsPerSecond());
}

//======================================================================================== imagen
// Agrupa las columnas del grupo QUEDÁNDOSE CON LA CELDA MÁS FUERTE de cada fila, y sus dos bytes viajan
// juntos: el color que se ve es el de un momento que existió, no el promedio de varios.
//
// La celda sale EMPAQUETADA como (energía << 8) | coherencia. El empaquetado se escribe a mano, no se
// castea la memoria del anillo a uint16: así el orden de bytes de la máquina no entra en la cuenta. Con la
// energía en el byte alto, comparar dos celdas empaquetadas es comparar primero la energía (y la
// coherencia sólo desempata) — o sea que el `max` del bucle de abajo no necesita rama.
//
// DESEMPATE: con dos celdas de la MISMA energía ahora gana la de mayor coherencia (antes ganaba la
// primera que se hubiera visto). Es una diferencia de un byte en un píxel de un empate exacto; la LECTURA
// del cursor no la toca, porque sale del dato del anillo y no del color pintado.
int StereoSpectrogramLens::columnPick (const StereoSpectrogramRing& ring, long long firstSrc,
                                       juce::uint16* dst) const
{
    constexpr int kRows = StereoSpectrogramRing::kRows;
    juce::uint8 tmp[StereoSpectrogramRing::kCellBytes];
    int found = 0;

    for (int i = 0; i < scroll.columnsPerGroup(); ++i)
    {
        if (! ring.copyColumn (firstSrc + i, tmp)) continue;

        if (found++ == 0)
        {
            for (int r = 0; r < kRows; ++r)                        // la primera manda de arranque
                dst[r] = (juce::uint16) (((juce::uint16) tmp[2 * r + 1] << 8) | (juce::uint16) tmp[2 * r]);
            continue;
        }
        for (int r = 0; r < kRows; ++r)
        {
            const auto v = (juce::uint16) (((juce::uint16) tmp[2 * r + 1] << 8) | (juce::uint16) tmp[2 * r]);
            dst[r] = std::max (dst[r], v);
        }
    }

    // Ninguna columna del grupo sigue en el anillo: celda vacía con la coherencia SIN DEFINIR (128), que
    // no es lo mismo que -1. El relleno va acá y no al principio para no pagarlo en el caso normal, que es
    // el que corre medio millón de veces en un redibujo completo.
    if (found == 0)
        std::fill (dst, dst + kRows, (juce::uint16) StereoBands::coherenceToByte (0.0));   // energía 0

    return found;
}

// Escribe el grupo en sus `pixels` columnas. ARGB directo con alpha 255 (donde el premultiplicado coincide
// con el color plano): setPixelColour cuesta diez veces más y es lo único que se hace medio millón de
// veces acá. La fila se resuelve UNA VEZ por grupo y se copia a las columnas que le tocan: un grupo puede
// ocupar dos píxeles, y resolver dos veces la misma fila era hacer el doble para dibujar lo mismo.
void StereoSpectrogramLens::drawGroupInto (const juce::Image::BitmapData& bd, int firstPx, int pixels,
                                           const juce::uint16* packed) const
{
    const int rows = juce::jmin (bd.height, scroll.mappedRows());
    const int n    = juce::jmin (pixels, bd.width - firstPx);
    if (n <= 0) return;

    for (int y = 0; y < rows; ++y)
    {
        // 56b — EL DEFAULT DE UN GRUPO VACÍO ES 128, NO 0. La celda empaqueta energía en el byte alto y
        // correlación en el bajo, así que `best = 0` es "silencio Y fuera de fase": un grupo de filas sin
        // dato salía pintado del rojo de la contrafase en vez del neutro. 128 es "sin definir", que es lo
        // que el código anterior al 56 dejaba. Y no rompe el máximo: cualquier celda con energía >= 1 vale
        // >= 256, así que gana igual; las que pierden tienen energía 0 y son silencio.
        juce::uint16 best = 128;
        jassert (scroll.rowFrom (y) <= scroll.rowTo (y));   // el grupo puede estar vacío, nunca invertido
        for (int r = scroll.rowFrom (y), e = juce::jmin (scroll.rowTo (y), StereoSpectrogramRing::kRows);
             r < e; ++r)
            best = std::max (best, packed[r]);

        const auto argb = palette[(size_t) ((best & 0xff) * 256 + (best >> 8))];
        auto* line = reinterpret_cast<juce::uint32*> (bd.getLinePointer (y)) + firstPx;
        for (int i = 0; i < n; ++i) line[i] = argb;
    }
}

void StereoSpectrogramLens::updateImage()
{
    const auto& ring = processor.stereoSpectrogram();

    // UNA BitmapData por redibujo, no una por columna. Se crea PEREZOSA porque `refresh` puede recrear la
    // imagen (cambio de tamaño) o correrla con moveImageSection antes de pedir el primer grupo: tomarla
    // antes sería tomarla de una imagen que ya no es.
    std::optional<juce::Image::BitmapData> bd;

    auto& image = cache.image();
    scroll.refresh (ring, image, cache.deviceW(), cache.deviceH(),
                    // El "todavía no llegó nada" es el POZO del sistema visual (Look.h), no un negro
                    // suelto: es el mismo fondo que usa SPECTROGRAM para su columna vacía, así las dos
                    // lentes hermanas arrancan del mismo color. (56)
                    StereoSpectrogramRing::kRows, look::well,
                    [this, &ring, &bd, &image] (int firstPx, int pixels, long long firstSrc)
                    {
                        if (! bd.has_value())
                        {
                            if (! image.isValid()) return;
                            bd.emplace (image, juce::Image::BitmapData::writeOnly);
                            if (bd->pixelStride != 4) { bd.reset(); return; }
                        }
                        if (! bd.has_value()) return;

                        columnPick (ring, firstSrc, packedCol.data());
                        drawGroupInto (*bd, firstPx, pixels, packedCol.data());
                    });
}

//======================================================================================== capa estática
void StereoSpectrogramLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    lastRangeDb = processor.spectrumSettings().rangeDb();

    g.setColour (look::gridMinor);
    g.drawRect (zones.plot.expanded (1), 1);

    // Eje de frecuencia: marcas al costado, nunca encima del sonograma (taparían datos).
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

    // ---- LA LEYENDA. Sin ella el dibujo es bonito y mudo: el color no se deduce solo. ----
    const int lw = juce::jmax (1, zones.legend.getWidth());
    const int barH = 6;
    for (int x = 0; x < lw; ++x)
    {
        const int c = juce::jlimit (0, 255, (int) std::lround (255.0 * (double) x / (double) (lw - 1)));
        g.setColour (juce::Colour (palette[(size_t) (c * 256 + 255)]));
        look::fillSnapped (g, { (float) (zones.legend.getX() + x), (float) (zones.legend.getY()), 1.0f, (float) (barH) });
    }

    g.setFont (ovni::ui::fonts::label (9.0f));
    const auto labels = zones.legend.withTrimmedTop (barH + 1);
    g.setColour (th::red);
    g.drawText (trLower (strings::Key::outOfPhase), labels.withWidth (labels.getWidth() / 3),
                juce::Justification::centredLeft, false);
    g.setColour (th::green);
    g.drawText (trLower (strings::Key::width), labels, juce::Justification::centred, false);
    g.setColour (th::txt);
    g.drawText (trLower (strings::Key::mono), labels.withTrimmedLeft (labels.getWidth() * 2 / 3),
                juce::Justification::centredRight, false);
}

//======================================================================================== capa viva
void StereoSpectrogramLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) zones = zonesFor (getWidth(), getHeight());

    // La escala física se lee acá y sólo acá (ver lenses/Raster.h y la nota gemela de la lente 4).
    if (cache.prepare (look::physicalScale (g), zones.plot.getWidth(), zones.plot.getHeight()))
        scroll.configure (processor.stereoSpectrogram(), cache.deviceW(), cache.deviceH(),
                          StereoSpectrogramRing::kRows);

    updateImage();
    cache.blit (g, zones.plot.getX(), zones.plot.getY());

    // ---- eje de tiempo: el tramo REAL que entra en el ancho, no la historia guardada ----
    const double span = visibleSeconds();
    if (span > 0.0)
    {
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
    }

    if (cursor.x >= 0)
    {
        const auto r = readoutAt (cursor);
        if (r.valid)
        {
            g.setColour (th::txt.withAlpha (0.35f));
            look::fillSnapped (g, { (float) (cursor.x), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });
            look::fillSnapped (g, { (float) (zones.plot.getX()), (float) (cursor.y), (float) (zones.plot.getWidth()), 1.0f });

            const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
            const juce::String text = "-" + juce::String (r.secondsAgo, 2) + " s" + dot
                                    + shortHz (r.freqHz) + " Hz" + dot
                                    + "coh " + juce::String (r.coherence, 2) + dot
                                    + juce::String (r.db, 1) + " dB";
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

    const auto s = processor.spectrumSettings();
    paintButton (g, zones.button[ctrlHistory], tr (strings::Key::history), juce::String (s.historySeconds()) + " s",
                 hovered == ctrlHistory);
    paintButton (g, zones.button[ctrlRange], tr (strings::Key::range), juce::String (s.rangeDb()) + " dB", hovered == ctrlRange);
    paintButton (g, zones.button[ctrlPalette], tr (strings::Key::palette),
                 look::paletteName (look::paletteFromIndex (processor.paletteIndex())), hovered == ctrlPalette);
    paintButton (g, zones.button[ctrlWindow], tr (strings::Key::window),
                 juce::String (processor.bandsWindowSec(), 1) + " s", hovered == ctrlWindow);
}

void StereoSpectrogramLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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
bool StereoSpectrogramLens::advanceFrame()
{
    // REDUCED MOTION no se consulta acá a propósito, igual que en la lente 4: el eje X ES el tiempo.
    if (processor.spectrumSettings().rangeDb() != lastRangeDb) invalidateStatic();
    // 57b — la rampa puede cambiar desde otra lente o al cargar un estado, y los colores viven adentro de
    // la imagen ya dibujada: hay que rehornear la tabla Y rehacerla entera.
    if (processor.paletteIndex() != paletteSeen) { buildPalette(); rebuildOnNextPaint(); return true; }
    return scroll.needsRepaint (processor.stereoSpectrogram().writeIndex());
}

//======================================================================================== lectura
// Los dos números salen del DATO (los bytes del anillo con la misma agrupación con la que se pintó la
// columna), nunca del color del píxel: la paleta tiene 65 536 entradas y muchas repiten color, así que
// buscar "el color más parecido" sería todavía más frágil acá que en la lente 4 (nit del 50).
StereoSpectrogramLens::Readout StereoSpectrogramLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (! zones.plot.contains (p)) return r;

    // De píxel lógico a píxel de dispositivo: el mapeo del desplazador vive en la resolución de la imagen.
    const int col = (int) ((float) (p.x - zones.plot.getX()) * cache.scale());
    const int row = (int) ((float) (p.y - zones.plot.getY()) * cache.scale());
    if (col < 0 || row < 0 || row >= scroll.mappedRows()) return r;

    const long long src = scroll.sourceAt (col);
    if (src < 0) return r;

    // La MISMA agrupación con la que se pintó (celdas empaquetadas: energía en el byte alto), así que la
    // fila que gana acá es exactamente la que decidió el color de ese píxel.
    juce::uint16 cell[StereoSpectrogramRing::kRows];
    if (columnPick (processor.stereoSpectrogram(), src, cell) <= 0) return r;

    juce::uint16 best = 128;    // el mismo "sin definir" que usa el que pinta (ver drawGroupInto)
    jassert (scroll.rowFrom (row) <= scroll.rowTo (row));
    for (int k = scroll.rowFrom (row); k < scroll.rowTo (row) && k < StereoSpectrogramRing::kRows; ++k)
        best = std::max (best, cell[k]);
    const int coh = best & 0xff, energy = best >> 8;

    const double span = visibleSeconds();
    r.valid      = true;
    r.secondsAgo = span * (double) (zones.plot.getRight() - p.x) / (double) zones.plot.getWidth();

    const double t = (double) (zones.plot.getBottom() - p.y) / (double) zones.plot.getHeight();
    r.freqHz = StereoSpectrogramRing::kMinHz
             * std::pow (StereoSpectrogramRing::kMaxHz / StereoSpectrogramRing::kMinHz,
                         juce::jlimit (0.0, 1.0, t));

    r.coherence = StereoBands::byteToCoherence (coh);
    r.db        = StereoBands::byteToDb (juce::jmax (0, energy),
                                         (double) processor.spectrumSettings().rangeDb());
    return r;
}

//======================================================================================== interacción
void StereoSpectrogramLens::cycleControl (int control)
{
    if (control == ctrlPalette)
    {
        // 57b — la misma rampa que las otras tres lentes de nivel. Acá cambia la respuesta del eje de
        // nivel, no el color (ver buildPalette()).
        processor.setPaletteIndex ((processor.paletteIndex() + 1) % look::kNumPalettes);
        buildPalette();
    }
    else if (control == ctrlWindow)
    {
        // La ventana de coherencia es la MISMA que la de BAND CORRELATION: son la misma medición mirada
        // de dos maneras, y dos ventanas distintas darían dos números distintos sin motivo.
        processor.setBandsWindowIndex ((processor.bandsWindowIndex() + 1) % StereoBands::kNumWindowOptions);
    }
    else
    {
        auto s = processor.spectrumSettings();
        if (control == ctrlHistory)
            s.historySecIndex = (s.historySecIndex + 1) % SpectrogramRing::kNumHistoryOptions;
        else if (control == ctrlRange)
            s.rangeDbIndex = (s.rangeDbIndex + 1) % Spectrum::kNumRanges;
        else
            return;
        processor.setSpectrumSettings (s);
        invalidateStatic();   // el rango cambia la escala de brillo, y la leyenda es capa estática
    }

    // Los tres cambian el MAPEO de lo que ya está dibujado: el motor limpia el anillo y acá se rehace todo.
    rebuildOnNextPaint();
    repaint();
}

void StereoSpectrogramLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void StereoSpectrogramLens::mouseMove (const juce::MouseEvent& e)
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

void StereoSpectrogramLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
