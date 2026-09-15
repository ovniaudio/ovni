#include "lenses/WaterfallLens.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "lenses/LensReadout.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr double kLabelledHz[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };
constexpr int    kRows = SpectrogramRing::kRows;   // 512

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

const juce::ValueTree& WaterfallLens::stateTree() const { return processor.apvts.state; }

WaterfallLens::WaterfallLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (2);   // corre mientras entren columnas; en silencio se para enseguida
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//======================================================================================== mapeos
double WaterfallLens::xForFreq (double hz) noexcept
{
    const double f = juce::jlimit (SpectrogramRing::kMinHz, SpectrogramRing::kMaxHz, hz);
    return std::log (f / SpectrogramRing::kMinHz) / std::log (SpectrogramRing::kMaxHz / SpectrogramRing::kMinHz);
}

double WaterfallLens::freqForX (double x01) noexcept
{
    return SpectrogramRing::kMinHz * std::pow (SpectrogramRing::kMaxHz / SpectrogramRing::kMinHz,
                                               juce::jlimit (0.0, 1.0, x01));
}

//======================================================================================== columnas
int WaterfallLens::selectColumns (long long writeIndex, int available, int wanted, long long* dst) noexcept
{
    const int n = juce::jlimit (0, juce::jmax (0, wanted), juce::jmax (0, available));
    if (n <= 0 || dst == nullptr) return 0;
    if (n == 1) { dst[0] = writeIndex - 1; return 1; }

    for (int i = 0; i < n; ++i)
    {
        const long long back = (long long) (n - 1 - i) * (long long) (available - 1) / (long long) (n - 1);
        dst[i] = writeIndex - 1 - back;
    }
    return n;
}

//======================================================================================== geometría
WaterfallLens::Zones WaterfallLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (170, (foot.getWidth() - gap * (kNumControls - 1)) / kNumControls);
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.freqAxis  = body.removeFromBottom (kFreqH);
    z.levelAxis = body.removeFromLeft (kAxisW);
    z.plot      = body;
    z.freqAxis  = z.freqAxis.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

Projection2p5 WaterfallLens::projectionFor (const Zones& z) const
{
    const int t = juce::jlimit (0, kNumTilts - 1, processor.waterfallTiltIndex());
    Projection2p5 p;
    p.x0 = (float) z.plot.getX();
    p.y0 = (float) z.plot.getY();
    p.w  = (float) juce::jmax (1, z.plot.getWidth());
    p.h  = (float) juce::jmax (1, z.plot.getHeight());
    p.tilt  = kTiltOptions[t];
    p.depth = kDepthOptions[t];
    return p;
}

void WaterfallLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    proj  = projectionFor (zones);
    Lens::resized();
}

// Los rótulos de tiempo, con su caja. UNA sola cuenta para el dibujo y para el test (ver el .h).
std::vector<WaterfallLens::TimeLabel> WaterfallLens::timeLabels() const
{
    std::vector<TimeLabel> out;
    const double span = visibleSeconds();
    if (! (span > 0.05)) return out;

    const double stepSec = span <= 12.0 ? 2.0 : (span <= 34.0 ? 5.0 : 10.0);
    const auto   font = ovni::ui::fonts::mono (9.0f);
    for (double t = 0.0; t <= span + 1.0e-6; t += stepSec)
    {
        TimeLabel l;
        l.text = t <= 0.0 ? tr (strings::Key::now) : ("-" + juce::String ((int) t) + " s");
        const auto p = proj.project (0.0f, 0.0f, (float) (t / span));
        // +14 y no +10: drawReadoutBox reserva 5 px de margen a CADA lado (box.reduced (5, 0)), así que
        // con +10 el texto ocupaba exactamente el ancho útil y "now" salía cortado en "no".
        const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, l.text)) + 14;
        l.box = { juce::roundToInt (p.x) + 5, juce::roundToInt (p.y) - 7, tw, 14 };
        out.push_back (l);
    }
    return out;
}

juce::Array<juce::Rectangle<int>> WaterfallLens::timeLabelBoxes() const
{
    juce::Array<juce::Rectangle<int>> out;
    for (const auto& l : timeLabels()) out.add (l.box);
    return out;
}

double WaterfallLens::visibleSeconds() const
{
    const auto& ring = processor.spectrogram();
    const int avail = juce::jmin (ring.count(), ring.capacity());
    const double cps = ring.columnsPerSecond();
    return (avail > 1 && cps > 0.0) ? (double) (avail - 1) / cps : 0.0;
}

//======================================================================================== una línea
bool WaterfallLens::readLine (long long src, juce::uint8* dst256) const
{
    juce::uint8 col[kRows];
    if (! processor.spectrogram().copyColumn (src, col)) return false;

    // Las 512 filas del anillo bajan a 256 puntos tomando el MÁXIMO del par: promediar borraría los picos
    // angostos, que en un waterfall son justo lo que se mira.
    for (int j = 0; j < kMaxPoints; ++j)
        dst256[j] = juce::jmax (col[2 * j], col[2 * j + 1]);
    smoothPoints (dst256, kMaxPoints);   // 57b — suavizado de PANTALLA (ver smoothPoints)
    return true;
}

//======================================================================================== 57b · el color
void WaterfallLens::Shading::buildLuts (juce::uint32* lineLut, juce::uint32* fillLut) const
{
    const auto& ramp = look::palette (palette);
    const auto  bg   = th::bg1.withAlpha (1.0f).getARGB();

    // El TRAZO, con los 256 niveles: es lo que se mira.
    for (int v = 0; v < 256; ++v)
    {
        juce::uint32 c = raster::mixArgb (ramp[(size_t) v], bg, fog);
        if (front)
            c = raster::mixArgb (c, look::txtPrimary.withAlpha (1.0f).getARGB(), kFrontLift);
        lineLut[v] = c;
    }

    // El RELLENO, con 64 (ver kFillLevels): el mismo color hundido, aclarándose cerca de la línea.
    for (int i = 0; i < kFillLevels; ++i)
    {
        const int v = juce::jmin (255, (i * 256 + 128) / kFillLevels);
        const juce::uint32 base = raster::mixArgb (ramp[(size_t) v], bg, fog);
        for (int k = 0; k < kFillRampN; ++k)
            fillLut[k * kFillLevels + i] = raster::scaleRgb (base, kFillLevel[k]);
    }
}

// El suavizado de pantalla: [1 2 1] sobre los 256 puntos. Sobre el eje log de esta lente, 256 puntos
// cubren ~9.97 octavas, así que tres puntos son del orden de 1/12 de octava — lo que pidió el 57b, y lo
// que hace que la línea deje de ser un serrucho de bins y pase a leerse como una curva.
void WaterfallLens::smoothPoints (juce::uint8* pts, int n) noexcept
{
    if (n < 3) return;
    juce::uint8 prev = pts[0];
    for (int i = 1; i < n - 1; ++i)
    {
        const int v = ((int) prev + 2 * (int) pts[i] + (int) pts[i + 1] + 2) >> 2;
        prev = pts[i];
        pts[i] = (juce::uint8) v;
    }
}

//======================================================================================== la imagen
void WaterfallLens::updateImage()
{
    const auto& ring = processor.spectrogram();
    // EN PÍXELES DE DISPOSITIVO (ver lenses/Raster.h): en Retina son el doble de los lógicos, y el
    // waterfall es justo la lente donde una línea de 1 px estirada al doble se lee como una escalera.
    if (zones.plot.getWidth() <= 0 || zones.plot.getHeight() <= 0 || ! cache.valid()) return;
    const int w = cache.deviceW();
    const int h = cache.deviceH();
    auto& image = cache.image();


    const auto bgCol = th::bg1.withAlpha (1.0f);
    image.clear (image.getBounds(), bgCol);

    // La proyección de la imagen es LOCAL a ella (origen 0,0) y en la resolución de la imagen: el plot se
    // devuelve al plano lógico al dibujarlo (raster::Cache::blitImage).
    Projection2p5 local = projectionFor (zones);
    local.x0 = 0.0f;
    local.y0 = 0.0f;
    local.w  = (float) w;
    local.h  = (float) h;

    const int avail = juce::jmin (ring.count(), ring.capacity());
    lines = selectColumns (ring.writeIndex(), avail, processor.waterfallLines(), srcIdx.data());
    if (lines <= 0) return;

    horizon.assign ((size_t) w, h);   // nada pintado: el horizonte arranca en el borde de abajo

    const juce::Image::BitmapData bd (image, juce::Image::BitmapData::readWrite);
    if (bd.pixelStride != 4) return;

    // 57b — el color de cada PUNTO sale de la rampa elegida según SU nivel, con niebla de profundidad
    // encima. Ver WaterfallLens::Shading (la fórmula vive ahí porque la comparte el pintor de [horizonte]).
    const auto pal = look::paletteFromIndex (processor.paletteIndex());
    std::array<juce::uint32, 256> lineLut {};
    std::array<juce::uint32, (size_t) Shading::kFillRampN * Shading::kFillLevels> fillLut {};

    // FRENTE → FONDO: ver la nota del header (mismo dibujo que el pintor al revés, sin pagar los rellenos).
    for (int i = lines - 1; i >= 0; --i)
    {
        if (! readLine (srcIdx[(size_t) i], linePts.data())) continue;

        const float z = lines > 1 ? (float) (lines - 1 - i) / (float) (lines - 1) : 0.0f;
        const bool  front = (i == lines - 1);
        // Las dos tablas de ESTA línea (una por profundidad, no una por píxel: de eso vive el presupuesto).
        const Shading sh { pal, Shading::kMaxFog * z, front };
        sh.buildLuts (lineLut.data(), fillLut.data());

        const int xL = juce::jmax (0,     (int) std::floor (local.leftX (z)));
        const int xR = juce::jmin (w - 1, (int) std::ceil  (local.rightX (z)));
        if (xR < xL) continue;

        int prevY = -1;

        // ========================================================================================================
        // ===== 57c · LA GEOMETRÍA SE RESUELVE CADA DOS COLUMNAS CUANDO SOBRA RESOLUCIÓN =====
        //
        // Una línea son **256 puntos** y el plano mide ~1 900 px de dispositivo a escala 2: siete píxeles y
        // medio de pantalla por punto de dato. Todo lo que hay entre dos puntos es interpolación, así que
        // resolver la geometría —`unprojectX`, la interpolación de la línea, `project` y el redondeo— en
        // cada píxel es muestrear una rampa el doble de fino de lo que hace falta.
        //
        // A escala física ≥ 2 se resuelve cada DOS columnas y las dos del par comparten `y`, `top`, `bot` y
        // color; lo único que se recalcula por columna es el relleno, porque el HORIZONTE es por columna y
        // puede diferir entre vecinas. La curva queda cuantizada a 2 px de dispositivo = un píxel lógico.
        //
        // Por qué hacía falta: con la máquina cargada esta lente se iba a 6.15 ms de mediana contra un
        // criterio de 6 y con **k = 1.18**, o sea con el harness diciendo que la máquina estaba sana. La
        // carga patrón del banco no modela lo que hace acá —escribir una pantalla entera por columnas— y
        // un criterio que se cae por el sistema operativo deja de medir el código. A escala 1 no se hace:
        // ahí no sobra nada, y además `[horizonte]` compara byte a byte contra el pintor literal.
        const int colStep = cache.scale() >= 1.5f ? 2 : 1;

        for (int px = xL; px <= xR; px += colStep)
        {
            const float x01 = local.unprojectX ((float) px + 0.5f, z);

            // De x del plano al punto de la línea: el punto j cubre las filas 2j y 2j+1 del anillo, así
            // que su centro está en (2j + 0.5)/511 del eje log.
            const double jf = juce::jlimit (0.0, (double) (kMaxPoints - 1),
                                            ((double) x01 * (double) (kRows - 1) - 0.5) * 0.5);
            const int    j0 = juce::jlimit (0, kMaxPoints - 1, (int) jf);
            const int    j1 = juce::jmin (kMaxPoints - 1, j0 + 1);
            const double fr = jf - (double) j0;
            const double v  = (1.0 - fr) * (double) linePts[(size_t) j0] + fr * (double) linePts[(size_t) j1];

            const int y = (int) std::lround (local.project (x01, (float) (v / 255.0), z).y);

            const int top = juce::jmax (0, juce::jmin (y, prevY < 0 ? y : prevY));
            const int bot = juce::jmin (h - 1, juce::jmax (y, prevY < 0 ? y : prevY) + (front ? 1 : 0));

            // ===== 56: EL RELLENO BAJO LA CURVA =====
            //
            // Antes cada línea era un trazo de 1 px y el conjunto se leía "de alambre": doce curvas
            // flotando, sin volumen y sin decir cuál tapa a cuál. Ahora cada columna se rellena DESDE la
            // curva HASTA el horizonte —o sea hasta lo que ya dibujó la línea de adelante— con el color de
            // su profundidad, y el trazo va encima, más brillante.
            //
            // Es gratis, y por una razón que vale la pena dejar escrita: el horizonte hace que los tramos
            // pintados de una misma columna sean DISJUNTOS (cada línea pinta desde su y hasta donde
            // empieza la de adelante, y después baja el horizonte hasta su y). O sea que entre todas las
            // líneas se pinta, como mucho, la pantalla UNA vez — no una vez por línea.
            //
            // 57b: el color sale de LAS TABLAS por NIVEL, y el relleno se aclara en las primeras filas
            // bajo la línea (`kFillLevel`) — que es lo que le da filo al borde de arriba de cada lámina.
            const int lv = juce::jlimit (0, 255, (int) std::lround (v));
            const juce::uint32 argb = lineLut[(size_t) lv];
            const int fv = Shading::fillIndex (lv);

            for (int q = 0; q < colStep && px + q <= xR; ++q)
            {
                const int X = px + q;
                const int fillTo = juce::jmin (h - 1, horizon[(size_t) X] - 1);
                const int fillFrom = juce::jmax (top, bot + 1);
                for (int yy = fillFrom; yy <= fillTo; ++yy)
                {
                    const int k = juce::jmin (Shading::kFillRampN - 1, yy - fillFrom);
                    *((juce::uint32*) bd.getLinePointer (yy) + X) = fillLut[(size_t) (k * Shading::kFillLevels + fv)];
                }

                const int cut = juce::jmin (bot, fillTo);
                for (int yy = top; yy <= cut; ++yy)
                    *((juce::uint32*) bd.getLinePointer (yy) + X) = argb;

                // `top` y NO `y`: ver la nota de abajo.
                horizon[(size_t) X] = juce::jmin (horizon[(size_t) X], top);
            }

            // `top` y NO `y` (MEDIUM del revisor del 53): lo que esta línea acaba de pintar en esta
            // columna empieza en `top` = min(y, prevY), no en `y`. Con un ESCALÓN —un tono puro cae
            // decenas de dB de un píxel al otro— `prevY` queda muy por encima de `y`; registrar `y` deja
            // el horizonte por debajo de lo pintado y una lámina LEJANA vuelve a pintar sobre las filas
            // [top, y−1] de una cercana. Con `top`, la equivalencia con el pintor es exacta y cada píxel
            // se escribe UNA vez ([horizonte] la verifica píxel a píxel contra el pintor bruto).
            prevY = y;
        }
    }
}

//======================================================================================== el escenario
// El suelo del plano de adelante, el del fondo y los dos costados: dicen de un vistazo hacia dónde se va
// el tiempo, sin tener que leer un rótulo. Van encima de la imagen porque la imagen es opaca.
void WaterfallLens::drawStage (juce::Graphics& g) const
{
    const auto fl = proj.project (0.0f, 0.0f, 0.0f), fr = proj.project (1.0f, 0.0f, 0.0f);
    const auto bl = proj.project (0.0f, 0.0f, 1.0f), br = proj.project (1.0f, 0.0f, 1.0f);

    g.setColour (look::gridMinor);          // 56: jerarquía — el fondo y los costados subdividen…
    g.drawLine (bl.x, bl.y, br.x, br.y, 1.0f);
    g.drawLine (fl.x, fl.y, bl.x, bl.y, 1.0f);
    g.drawLine (fr.x, fr.y, br.x, br.y, 1.0f);
    // 57b — LA GRILLA DEL SUELO: las décadas de frecuencia proyectadas desde el borde de adelante hasta
    // el del fondo. Son las que le dan al suelo una superficie en vez de un borde suelto: sin ellas la
    // fuga de la perspectiva no se lee, y con ellas se ve de un vistazo que el eje de profundidad es
    // tiempo y no otra frecuencia. Van en gridMinor: estructuran, no compiten con el dato.
    g.setColour (look::gridMinor);
    for (const double hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x01 = (float) xForFreq (hz);
        const auto a = proj.project (x01, 0.0f, 0.0f), b = proj.project (x01, 0.0f, 1.0f);
        g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
    }

    g.setColour (look::gridAxis);           // …y el suelo de ADELANTE estructura (es contra el que se
    g.drawLine (fl.x, fl.y, fr.x, fr.y, 1.0f);   // apoya el dato, y el que dice dónde empieza el "ahora")
}

//======================================================================================== capa estática
void WaterfallLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    proj  = projectionFor (zones);

    // ---- eje de FRECUENCIA (log), en el plano de adelante ----
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (const double hz : kLabelledHz)
    {
        const auto p = proj.project ((float) xForFreq (hz), 0.0f, 0.0f);
        const int x = juce::roundToInt (p.x);
        g.setColour (th::line);
        look::fillSnapped (g, { (float) (x), (float) (zones.freqAxis.getY()), 1.0f, (float) (4) });
        g.setColour (th::fnt);
        g.drawText (shortHz (hz), x - 24, zones.freqAxis.getY() + 3, 48, 12,
                    juce::Justification::centred, false);
    }

    // ---- eje de NIVEL, también en el plano de adelante (el rango del módulo Spectrum) ----
    const int range = processor.spectrumSettings().rangeDb();
    const int step  = range >= 120 ? 30 : (range >= 90 ? 20 : 10);
    for (int db = 0; db >= -range; db -= step)
    {
        const float y01 = (float) ((double) (db + range) / (double) range);
        const auto  p   = proj.project (0.0f, y01, 0.0f);
        const int   y   = juce::roundToInt (p.y);
        g.setColour (th::line);
        look::fillSnapped (g, { (float) (zones.levelAxis.getRight() - 5), (float) (y), (float) (5), 1.0f });
        g.setColour (th::fnt);
        g.drawText (juce::String (db), zones.levelAxis.getX(), y - 6, kAxisW - 8, 12,
                    juce::Justification::centredRight, false);
    }
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("dB", zones.levelAxis.getX(), zones.plot.getY() + 2, kAxisW - 8, 12,
                juce::Justification::centredRight, false);
    // "Hz" va en el canal de la IZQUIERDA y no al final del eje: ahí pisaba el rótulo de 20 kHz, que
    // además ya llega justo al borde del plot.
    g.drawText ("Hz", zones.levelAxis.getX(), zones.freqAxis.getY() + 3, kAxisW - 8, 12,
                juce::Justification::centredRight, false);
}

//======================================================================================== capa viva
void WaterfallLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); proj = projectionFor (zones); }

    // La escala física se lee acá (ver lenses/Raster.h). El escenario, los rótulos y la lectura siguen
    // dibujándose en el plano LÓGICO con antialiasing de JUCE: sólo la imagen del dato es de píxeles.
    const float ps = look::physicalScale (g);
    cache.prepare (ps, zones.plot.getWidth(), zones.plot.getHeight());

    updateImage();
    cache.blit (g, zones.plot.getX(), zones.plot.getY());
    drawStage (g);   // encima de la imagen: ver el comentario de drawStage

    // ---- el eje de TIEMPO va en la PROFUNDIDAD, sobre el dibujo (el fondo es opaco: debajo no se vería).
    //
    // ===== 56b ===== CAJA OPACA, no un velo. Con alpha 0.78 el relleno verde se colaba por atrás y los
    // rótulos de la esquina de abajo a la izquierda —-2 s, -4 s, -6 s, los que caen sobre la parte más
    // llena— se leían como agujeros sucios en el dato. Ahora es la misma cajita del resto de las lentes
    // (look::drawReadoutBox): opaca, con su borde, y el dato no se ve por debajo.
    for (const auto& l : timeLabels())
    {
        const auto mw = look::metricsFor (getWidth());
        g.setColour (look::gridMajor);
        look::fillSnapped (g, { (float) (l.box.getX() - 5), (float) (l.box.getY() + 7), (float) (5), 1.0f });
        // La caja de lectura del sello va al 88 % de opacidad, que alcanza sobre un pozo pero no sobre
        // el relleno: acá abajo hay DATO, no fondo. Se pone un piso opaco del color del pozo y encima
        // la caja de siempre, así el rótulo se ve igual que en las otras lentes y no deja pasar nada.
        // El piso va CUADRADO y no redondeado: con esquinas redondeadas el antialiasing deja pasar el
        // relleno justo en las cuatro puntas (36 px medidos), y "casi opaco" es la misma clase de
        // problema que el 0.88 de origen. Las puntas quedan del color del pozo, que contra un tema
        // oscuro se lee como la sombra de la cajita.
        g.setColour (look::well);
        g.fillRect (l.box);
        look::drawReadoutBox (g, l.box, l.text, mw, look::gridMajor);
    }

    // ---- lectura bajo el cursor: la línea de ADELANTE ----
    if (cursor.x >= 0)
    {
        const auto r = readoutAt (cursor);
        if (r.valid)
        {
            g.setColour (th::txt.withAlpha (0.30f));
            look::fillSnapped (g, { (float) (cursor.x), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });

            const juce::String text = shortHz (r.freqHz) + " Hz  \xc2\xb7  " + juce::String (r.db, 1)
                                    + " dB  \xc2\xb7  " + trLower (strings::Key::now);
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
    paintButton (g, zones.button[ctrlLines],   tr (strings::Key::lines), juce::String (processor.waterfallLines()),
                 hovered == ctrlLines);
    paintButton (g, zones.button[ctrlTilt],    tr (strings::Key::tilt),  juce::String (processor.waterfallTiltIndex() + 1) + "/3",
                 hovered == ctrlTilt);
    paintButton (g, zones.button[ctrlHistory], tr (strings::Key::history), juce::String (s.historySeconds()) + " s",
                 hovered == ctrlHistory);
    paintButton (g, zones.button[ctrlPalette], tr (strings::Key::palette),
                 look::paletteName (look::paletteFromIndex (processor.paletteIndex())), hovered == ctrlPalette);
    paintButton (g, zones.button[ctrlRange],   tr (strings::Key::range), juce::String (s.rangeDb()) + " dB",
                 hovered == ctrlRange);

    // El canal que se está mirando, arriba a la derecha: sin esto, "L+R" y "M" dan dibujos distintos de la
    // misma música y nada en pantalla dice cuál se está viendo.
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    g.drawText (tr (strings::Key::channel) + " " + channelLabel (s.channel),
                zones.plot.getRight() - 90, zones.plot.getY() + 2, 88, 12,
                juce::Justification::centredRight, false);
}

void WaterfallLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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
bool WaterfallLens::advanceFrame()
{
    // REDUCED MOTION no se consulta a propósito: el eje de profundidad de esta lente ES el tiempo (misma
    // razón que los dos sonogramas). Y no hay nada suavizado que apagar.
    // 57b — la rampa puede cambiar desde otra lente o al cargar un estado; sin esto, en silencio (con el
    // anillo quieto) el waterfall se quedaría con los colores viejos hasta que volviera a entrar audio.
    const int pal = processor.paletteIndex();
    if (pal != paletteSeen) { paletteSeen = pal; return true; }

    const auto w = processor.spectrogram().writeIndex();
    if (w == lastWrite) return false;
    lastWrite = w;
    return true;
}

//======================================================================================== lectura
WaterfallLens::Readout WaterfallLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (! zones.plot.contains (p) || lines <= 0) return r;

    // Sólo la línea de adelante (z = 0): en profundidad una columna de píxel cae sobre varias líneas y
    // "cuál se está señalando" no tendría una respuesta.
    if ((float) p.x < proj.leftX (0.0f) || (float) p.x > proj.rightX (0.0f)) return r;

    const float x01 = proj.unprojectX ((float) p.x + 0.5f, 0.0f);

    juce::uint8 pts[kMaxPoints];
    if (! readLine (srcIdx[(size_t) (lines - 1)], pts)) return r;   // el anillo ya la descartó

    const int j = juce::jlimit (0, kMaxPoints - 1,
                                (int) std::lround (((double) x01 * (double) (kRows - 1) - 0.5) * 0.5));
    const double range = (double) processor.spectrumSettings().rangeDb();

    r.valid  = true;
    r.freqHz = freqForX ((double) x01);
    r.db     = (float) ((double) pts[j] / 255.0 * range - range);
    return r;
}

//======================================================================================== interacción
void WaterfallLens::cycleControl (int control)
{
    switch (control)
    {
        case ctrlLines:
            processor.setWaterfallLinesIndex ((processor.waterfallLinesIndex() + 1)
                                              % TelescopeProcessor::kNumWaterfallLineOptions);
            break;
        case ctrlTilt:
            processor.setWaterfallTiltIndex ((processor.waterfallTiltIndex() + 1) % kNumTilts);
            break;
        case ctrlPalette:   // 57b — la misma rampa que las otras tres lentes de nivel
            processor.setPaletteIndex ((processor.paletteIndex() + 1) % look::kNumPalettes);
            break;
        case ctrlHistory:
        {
            auto s = processor.spectrumSettings();
            s.historySecIndex = (s.historySecIndex + 1) % SpectrogramRing::kNumHistoryOptions;
            processor.setSpectrumSettings (s);
            break;
        }
        case ctrlRange:
        {
            auto s = processor.spectrumSettings();
            s.rangeDbIndex = (s.rangeDbIndex + 1) % Spectrum::kNumRanges;
            processor.setSpectrumSettings (s);
            break;
        }
        default: return;
    }

    // Cuatro de los cinco cambian los EJES (la inclinación mueve la escena entera; el rango y la historia
    // cambian lo que dicen los rótulos), así que la capa estática se rehornea. La paleta no los cambia,
    // pero rehornear de más una vez por click no le cuesta nada a nadie.
    rebuildOnNextPaint();
    repaint();
}

void WaterfallLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void WaterfallLens::mouseMove (const juce::MouseEvent& e)
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

void WaterfallLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
