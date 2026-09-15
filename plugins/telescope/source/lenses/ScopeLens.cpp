#include "lenses/ScopeLens.h"
#include "PluginProcessor.h"
#include "lenses/Look.h"
#include "lenses/Strings.h"
#include "analysis/modules/Stereo.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr float kCorrSmoothing = 0.30f;
const float     kInvSqrt2      = 1.0f / std::sqrt (2.0f);

juce::String fmt2 (float v) { return juce::String (v, 2); }
}

ScopeLens::ScopeLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (30);   // 1 s más de repintado tras el último cambio: que la estela alcance a apagarse
}

//======================================================================================== 56: settings
// Estos tres NO pasan por el processor: son settings de VISTA (no cambian una sola cuenta del motor), y
// viven donde vive el resto del estado, que es el ValueTree del APVTS. Mismo criterio que las líneas y la
// inclinación de WATERFALL.
const juce::ValueTree& ScopeLens::stateTree() const { return processor.apvts.state; }

ScopeLens::Mode ScopeLens::scopeMode() const
{
    // Sin la propiedad (preset viejo) el modo sale del `scopePolar` de siempre: un preset guardado antes
    // de que existiera el hemisferio abre en la vista en la que se guardó, no en una nueva.
    const int fallback = processor.scopePolar() ? (int) Mode::polar : (int) Mode::lissajous;
    const int v = (int) stateTree().getProperty (kScopeModeProperty, fallback);
    return (Mode) juce::jlimit (0, 2, v);
}

void ScopeLens::setScopeMode (Mode m)
{
    processor.apvts.state.setProperty (kScopeModeProperty, (int) m, nullptr);
    // Se mantiene `scopePolar` coherente: es el que sigue leyendo el resto del plugin (y los presets ya
    // guardados). El hemisferio no es polar, así que cuenta como "no polar".
    processor.setScopePolar (m == Mode::polar);
    invalidateStatic();
    repaint();
}

ScopeLens::HemiScale ScopeLens::hemiScale() const
{
    const int v = (int) stateTree().getProperty (kHemiScaleProperty, (int) HemiScale::relative);
    return (HemiScale) juce::jlimit (0, 1, v);
}

void ScopeLens::setHemiScale (HemiScale sc)
{
    processor.apvts.state.setProperty (kHemiScaleProperty, (int) sc, nullptr);
    invalidateStatic();   // los arcos y sus rótulos son capa estática y cambian de significado
    repaint();
}

// La energía del hop que está FUERA DE FASE: los pares (l, r) con l·r < 0 sobre el total. Se mide sobre
// las muestras y no sobre la envolvente porque la envolvente es un máximo por dirección —dice dónde hubo
// algo, no cuánto—, y acá la pregunta es cuánta energía se pierde al monoficar.
float ScopeLens::outOfPhasePercent() const noexcept
{
    double total = 0.0, out = 0.0;
    for (int i = 0; i < scope.xyCount; ++i)
    {
        const double l = (double) scope.xyL[(size_t) i], r = (double) scope.xyR[(size_t) i];
        const double e = l * l + r * r;
        total += e;
        if (l * r < 0.0) out += e;
    }
    return total > 1.0e-12 ? (float) (100.0 * out / total) : 0.0f;
}

int ScopeLens::hemiDecayIndex() const
{
    const int v = (int) stateTree().getProperty (kHemiDecayProperty, kDefaultHemiDecayIndex);
    return juce::jlimit (0, kNumHemiDecayOptions - 1, v);
}

void ScopeLens::setHemiDecayIndex (int i)
{
    processor.apvts.state.setProperty (kHemiDecayProperty,
                                       juce::jlimit (0, kNumHemiDecayOptions - 1, i), nullptr);
    repaint();
}

//======================================================================================== geometría
ScopeLens::Zones ScopeLens::zonesFor (int w, int h) const
{
    Zones z;
    const auto m = look::metricsFor (w);
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    z.footer = body.removeFromBottom (juce::jmax (44, h / 13));
    body.removeFromBottom (th::padIn / 2);

    if (scopeMode() == Mode::hemisphere)
    {
        // ===== 56 ===== un semicírculo es 2:1, así que el panel izquierdo se ENSANCHA y se come el ancho
        // que en los otros modos usa la columna de números. El correlímetro pasa a una tira VERTICAL a su
        // derecha (como en la referencia que mandó Joaquín), y el osciloscopio queda igual.
        const int corrW = juce::jlimit (54, 84, w / 12);
        const int plotW = juce::jmax (220, (body.getWidth() - corrW - th::padIn) * 5 / 9);
        z.gonio = body.removeFromLeft (plotW);
        body.removeFromLeft (m.gap > 0.0f ? (int) m.gap : 8);
        z.centre = body.removeFromLeft (corrW);
        body.removeFromLeft (th::padIn);
        z.osc = body;
        // 57b — LOS CUATRO NÚMEROS TAMBIÉN EN ESTE MODO. Hasta el 57 ANCHO / BALANCE / PÉRDIDA MONO sólo
        // se dibujaban en Lissajous y polar: en hemisferio la columna que los lleva la ocupa el
        // correlímetro vertical y los tres se perdían. Eso es lo que Joaquín vio como "sin valor en la
        // captura" — no estaban vacíos, no estaban.
        //
        // Van ARRIBA DEL SEMICÍRCULO, que es espacio que de todos modos sobra: el semicírculo es 2:1 y su
        // radio lo limita el ANCHO del panel, así que la franja de arriba quedaba vacía. Un número que
        // llena un hueco es mejor que un hueco.
        z.readouts = z.gonio.reduced (14, 10).removeFromTop (juce::jmax (30, z.gonio.getHeight() / 8));
    }
    else
    {
        // El goniómetro es CUADRADO (un Lissajous estirado miente sobre el ancho) y manda el ancho de la zona.
        const int side = juce::jmin (body.getHeight(), juce::jmax (160, w * 2 / 5));
        const auto gonioCol = body.removeFromLeft (side);
        z.gonio = juce::Rectangle<int> (side, side).withCentre (gonioCol.getCentre());
        body.removeFromLeft (th::padIn);

        z.centre = body.removeFromLeft (juce::jmax (150, body.getWidth() * 2 / 5));
        body.removeFromLeft (th::padIn);
        z.osc = body;
    }

    // Pie: modo · trigger · (decaimiento del hemisferio) · las tres ventanas.
    auto foot = z.footer.reduced (th::padIn / 2, 6);
    const int bw = juce::jmin (108, foot.getWidth() / 6);
    z.polarBtn   = foot.removeFromLeft (bw);
    foot.removeFromLeft (8);
    z.triggerBtn = foot.removeFromLeft (bw);

    const int ww = juce::jmin (72, foot.getWidth() / 3 - 6);
    for (int i = kWindowOptions - 1; i >= 0; --i)
    {
        z.windowBtn[i] = foot.removeFromRight (ww);
        foot.removeFromRight (6);
    }

    if (scopeMode() == Mode::hemisphere)
    {
        foot.removeFromLeft (8);
        z.decayBtn = foot.removeFromLeft (juce::jmin (bw, juce::jmax (0, foot.getWidth())));
        foot.removeFromLeft (8);
        z.scaleBtn = foot.removeFromLeft (juce::jmin (bw, juce::jmax (0, foot.getWidth())));
    }
    return z;
}

//======================================================================================== capa estática
void ScopeLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    const auto hue = th::green;
    const auto m   = look::metricsFor (width);   // 56

    const auto surface = [&g] (juce::Rectangle<int> r, float alpha)
    {
        g.setColour (th::surf.withAlpha (alpha));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (look::gridMinor);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    };
    surface (zones.gonio,  0.75f);
    surface (zones.centre, 0.55f);
    surface (zones.osc,    0.55f);
    surface (zones.footer, 0.45f);

    if (scopeMode() == Mode::hemisphere)
    {
        // ===== 56 ===== la retícula del hemisferio: la BASE (L—R), el arco exterior de 0 dB, los arcos de
        // -20 y -40, y las diagonales de ±45°. Todo con la jerarquía de Look.h: la base y el arco de 0 dB
        // pesan (son los que se leen), los intermedios subdividen. Y todo snappeado a píxel físico.
        const auto plot = hemiPlotArea (zones).toFloat();
        const auto geo = hemiGeometry (plot);
        const float cx = geo.cx, baseY = geo.baseY, rad = geo.rad;

        const auto arc = [&] (float t, juce::Colour col, float thick)
        {
            juce::Path p;
            p.addCentredArc (cx, baseY, rad * t, rad * t, 0.0f,
                             -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (thick));
        };

        // 57b — LOS ARCOS SON RELATIVOS A LA DIRECCIÓN MÁS FUERTE, así que sus radios son FIJOS y siguen
        // siendo capa estática: −6 dB a medio radio, −12 a un cuarto, −18 a un octavo (o los mismos tres
        // sobre el piso de −24 si la escala está en dB). El arco exterior es el 0 relativo y estructura.
        const auto scNow = hemiScale();
        for (const float db : { -18.0f, -12.0f, -6.0f })
            arc (hemiRadiusFrac (db, 0.0f, scNow), look::gridMinor, 1.0f);
        arc (1.0f, look::gridMajor, 1.0f);

        // Diagonales de ±45° (o sea: a mitad de camino entre un canal solo y el centro).
        for (const float deg : { 45.0f, 135.0f })
        {
            const auto a = deg * juce::MathConstants<float>::pi / 180.0f;
            g.setColour (look::gridMinor);
            g.drawLine (cx - std::cos (a) * rad * 0.10f, baseY - std::sin (a) * rad * 0.10f,
                        cx - std::cos (a) * rad,          baseY - std::sin (a) * rad, 1.0f);
        }
        // El eje MONO: vertical, y pesa — es la referencia contra la que se mira todo lo demás.
        look::vLine (g, cx, baseY - rad, baseY, look::gridAxis);
        // La BASE L—R. Lo que cae por debajo está fuera de fase: la línea tiene que ser inconfundible.
        look::hLine (g, cx - rad, cx + rad, baseY, look::gridAxis);

        g.setFont (look::labelFont (m.textSmall));
        const struct { strings::Key k; float dx, dy; bool hi; } marks[] = {
            { strings::Key::mid,   0.0f,  -1.06f, true  },
            { strings::Key::left, -1.06f,  0.0f,  false },
            { strings::Key::right, 1.06f,  0.0f,  false },
        };
        for (const auto& mk : marks)
        {
            g.setColour (mk.hi ? look::dataLine.withAlpha (0.85f) : look::txtSecondary);
            g.drawText (tr (mk.k), juce::roundToInt (cx + mk.dx * rad - 10.0f),
                        juce::roundToInt (baseY + mk.dy * rad - 7.0f), 20, 14,
                        juce::Justification::centred, false);
        }

        // Los rótulos de dB sobre el eje mono, tabulares para que no bailen al cambiar de tamaño. Son
        // RELATIVOS a la dirección más fuerte (por eso el 0 va sin signo y los otros con −).
        g.setFont (look::tabularFont (m.textMicro));
        g.setColour (look::txtTertiary);
        // El 0 NO se rotula: el arco exterior ya lo es, y el rótulo caía justo encima de la M.
        for (const float db : { -6.0f, -12.0f, -18.0f })
            g.drawText (juce::String ((int) db), juce::roundToInt (cx + 4.0f),
                        juce::roundToInt (baseY - rad * hemiRadiusFrac (db, 0.0f, scNow) - 11.0f), 34, 12,
                        juce::Justification::left, false);

        g.setColour (look::txtTertiary);
        g.setFont (look::labelFont (m.textMicro));
        g.drawText (tr (strings::Key::hemiLegend),
                    (int) plot.getX(), (int) plot.getBottom() - 13, (int) plot.getWidth(), 12,
                    juce::Justification::centred, false);
        g.setColour (look::txtSecondary);
        g.setFont (look::labelFont (m.textSmall));
        g.drawText (tr (strings::Key::hemisphere), zones.gonio.getX() + 10, zones.gonio.getY() + 6,
                    zones.gonio.getWidth() - 20, 14, juce::Justification::left, false);
        g.drawText (tr (strings::Key::correlation), zones.centre.getX(), zones.centre.getY() + 6,
                    zones.centre.getWidth(), 14, juce::Justification::centred, false);
        return;
    }

    // ---- retícula del goniómetro: círculos, ejes y etiquetas L / R / M / S ----
    const auto gr = zones.gonio.reduced (10).toFloat();
    const auto c  = gr.getCentre();
    const float radius = juce::jmin (gr.getWidth(), gr.getHeight()) * 0.5f;

    g.setColour (look::gridMinor);
    for (const float f : { 0.25f, 0.5f, 0.75f })
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f * f, radius * 2.0f * f).withCentre (c), 1.0f);
    g.setColour (look::gridMajor);
    g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 1.0f);

    // Ejes: vertical = M (mono), horizontal = S (lado), diagonales = L y R.
    g.setColour (look::gridMinor);
    g.drawLine (c.x, c.y - radius, c.x, c.y + radius, 1.0f);
    g.drawLine (c.x - radius, c.y, c.x + radius, c.y, 1.0f);
    const float d = radius * kInvSqrt2;
    g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 1.0f);
    g.drawLine (c.x + d, c.y - d, c.x - d, c.y + d, 1.0f);

    g.setFont (ovni::ui::fonts::label (10.0f));
    // 57b — SE FUE EL RÓTULO DEL LADO. Estaba solo a la derecha (el del lado negativo, a la izquierda,
    // no se rotulaba), así que el eje se leía como si hubiera un canal con ese nombre en un solo lado.
    // Que el eje horizontal sea el lado es cierto y no hace falta decirlo: lo que el ojo necesita en un
    // goniómetro es M, L y R. (Lo marcó Joaquín el 9-sep mirando el polar.)
    const struct { const char* t; float dx, dy; } marks[] = {
        { "M", 0.0f, -1.0f }, { "L", -0.72f, -0.72f }, { "R", 0.72f, -0.72f }
    };
    for (const auto& m : marks)
    {
        g.setColour (m.t[0] == 'M' ? hue.withAlpha (0.8f) : th::fnt);
        g.drawText (m.t, juce::roundToInt (c.x + m.dx * (radius + 9.0f) - 8.0f),
                    juce::roundToInt (c.y + m.dy * (radius + 9.0f) - 7.0f), 16, 14,
                    juce::Justification::centred, false);
    }

    // ---- retícula del osciloscopio: cero y ±0.5 ----
    const auto os = zones.osc.reduced (th::padIn / 2);
    g.setColour (look::gridMajor);
    look::fillSnapped (g, { (float) (os.getX()), (float) (os.getCentreY()), (float) (os.getWidth()), 1.0f });
    g.setColour (look::gridMinor);
    for (const float f : { -0.5f, 0.5f })
        look::fillSnapped (g, { (float) (os.getX()), (float) (os.getCentreY() + juce::roundToInt (f * (float) os.getHeight() * 0.5f)), (float) (os.getWidth()), 1.0f });

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (tr (strings::Key::oscilloscope) + "  40 ms", os.getX(), os.getY() + 2, os.getWidth(), 14,
                juce::Justification::left, false);
    g.drawText (tr (strings::Key::correlation), zones.centre.getX() + 10, zones.centre.getY() + 8,
                zones.centre.getWidth() - 20, 14, juce::Justification::left, false);
}

//======================================================================================== capa viva
void ScopeLens::paintLive (juce::Graphics& g)
{
    if (zones.gonio.isEmpty()) zones = zonesFor (getWidth(), getHeight());

    // La escala física se lee acá, que es el único lugar que la ve (ver lenses/Raster.h).
    // La caché de la estela se dimensiona ACÁ, que es el único lugar que sabe la escala física.
    trail.prepare (look::physicalScale (g), trailArea().getWidth(), trailArea().getHeight());

    // 56: si el modo cambió por fuera del botón (cargar un preset, la comparación A/B), la capa estática
    // sigue siendo la del modo anterior. Se rehornea acá, que es el único lugar que ve los dos estados.
    if (scopeMode() != lastMode)
    {
        lastMode = scopeMode();
        zones = zonesFor (getWidth(), getHeight());
        invalidateStatic();
    }

    const auto mode = scopeMode();

    if (mode == Mode::hemisphere)
    {
        paintHemisphere (g);
        paintCorrelationVertical (g, zones.centre);
        paintStereoNumbers (g, zones.readouts);
    }
    else
    {
        paintGonio (g);
        paintCorrelation (g, zones.centre);
    }
    paintOsc (g, zones.osc);

    paintButton (g, zones.polarBtn,
                 tr (mode == Mode::hemisphere ? strings::Key::hemisphere
                                              : (mode == Mode::polar ? strings::Key::polar
                                                                     : strings::Key::lissajous)),
                 mode != Mode::lissajous, hovered == 0);
    paintButton (g, zones.triggerBtn,
                 tr (processor.scopeTrigger() ? strings::Key::trigger : strings::Key::free),
                 processor.scopeTrigger(), hovered == 1);

    if (mode == Mode::hemisphere && ! zones.decayBtn.isEmpty())
        paintButton (g, zones.decayBtn,
                     juce::String ((int) hemiDecayDbPerSec()) + " dB/s", true, hovered == 5);

    if (mode == Mode::hemisphere && ! zones.scaleBtn.isEmpty())
        paintButton (g, zones.scaleBtn, hemiScale() == HemiScale::decibel ? "dB" : "LIN",
                     hemiScale() == HemiScale::decibel, hovered == 6);

    const int wnd = processor.stereoWindowMs();
    for (int i = 0; i < kWindowOptions; ++i)
    {
        const int ms = Stereo::kWindowMsOptions[i];
        paintButton (g, zones.windowBtn[i],
                     ms >= 1000 ? juce::String ("1 s") : (juce::String (ms) + " ms"),
                     ms == wnd, hovered == 2 + i);
    }
}

// ========================================================================================================
// ===== 56: EL HEMISFERIO =====
//
// Tres capas, de atrás hacia adelante, y en ese orden por un motivo: cada una responde una pregunta más
// fina que la anterior, así que la más gruesa no puede taparle el lugar a la más fina.
//
//   1 · la ENVOLVENTE RELLENA — "cuánta energía hay en cada dirección". Es la forma que se lee de lejos.
//   2 · el CONTORNO brillante con glow — el borde de esa forma, que es donde el ojo mide.
//   3 · la NUBE instantánea — las muestras del hop, tenues: el detalle de AHORA sobre la memoria.
//
// Lo que cae por debajo de la base (fuera de fase) se dibuja hacia ABAJO y en el color de alerta. No es
// decoración: es la única parte del dibujo que predice lo que se PIERDE al monoficar.
// La geometría del hemisferio, en UN solo lugar. La usan la capa estática (la retícula) y la viva (el
// dato): si cada una calculara su centro, bastaría un pixel de diferencia para que la envolvente flotara
// sobre su propia rejilla — y ese es exactamente el defecto que se ve y no se sabe nombrar.
//
// El dibujo entero (el semicírculo de arriba MÁS la zona de fuera de fase de abajo) ocupa un círculo de
// radio `rad`, así que se INSCRIBE ese círculo en el área y se centra. Primera versión: el semicírculo se
// apoyaba a 0.68 del alto y abajo quedaba una franja fija — en el panel alto de este layout eso dejaba un
// tercio de la lente vacío, que en la captura se lee como "la lente no llega", justo lo que había que
// arreglar. Un círculo inscrito usa todo lo que hay y no depende del alto que le toque al panel.
//
// Los 16 px de aire son para los rótulos L / M / R, que van FUERA del arco para no pisar el dato.
// El área de dibujo del semicírculo: el panel menos la franja de números de arriba (57b). En un solo
// lugar porque la usan la capa estática (la retícula) y la viva (el dato).
juce::Rectangle<int> ScopeLens::hemiPlotArea (const Zones& z) noexcept
{
    auto r = z.gonio.reduced (10);
    if (! z.readouts.isEmpty())
        r = r.withTop (juce::jmax (r.getY(), z.readouts.getBottom() + 6));
    return r;
}

ScopeLens::HemiGeometry ScopeLens::hemiGeometry (juce::Rectangle<float> plot) noexcept
{
    // 57b — LA BASE VA AL PIE DEL PANEL, no al medio. El panel ES el semicírculo: no hay nada debajo de
    // la base porque lo que está fuera de fase ya no se dibuja para abajo (se pliega, ver paintHemisphere).
    // Con la base al medio se gastaba la mitad de abajo en una zona que casi siempre está vacía, y el
    // semicírculo de arriba —que es TODO el dato— quedaba con la mitad del radio que podía tener.
    HemiGeometry g;
    g.cx    = plot.getCentreX();
    g.baseY = plot.getBottom() - 18.0f;                       // aire para los rótulos L y R
    g.rad   = juce::jmax (0.0f, juce::jmin (plot.getWidth() * 0.5f - 18.0f,
                                            plot.getHeight() - 30.0f));
    return g;
}

// dB → fracción del radio, con la escala vigente. Es la cuenta que comparten la retícula estática y el
// dibujo vivo: si cada una tuviera la suya, la envolvente flotaría sobre sus propios arcos.
float ScopeLens::hemiRadiusFrac (float db, float peakDb, HemiScale sc) noexcept
{
    if (sc == HemiScale::decibel)
        return juce::jlimit (0.0f, 1.0f, (db - peakDb - kHemiRelFloorDb) / -kHemiRelFloorDb);
    // RELATIVA: amplitud respecto de la dirección más fuerte. −6 dB → medio radio, −12 → un cuarto.
    return juce::jlimit (0.0f, 1.0f, std::pow (10.0f, (db - peakDb) / 20.0f));
}

void ScopeLens::paintHemisphere (juce::Graphics& g)
{
    const auto plot = hemiPlotArea (zones).toFloat();
    if (plot.isEmpty()) return;

    const auto  m = look::metricsFor (getWidth());
    const auto  geo = hemiGeometry (plot);
    const float cx = geo.cx, baseY = geo.baseY, rad = geo.rad;
    if (rad <= 4.0f) return;

    const auto  sc = hemiScale();
    const float peak = juce::jmax (ScopeFrame::kHemiFloorDb + 1.0f, hemi.peakDb);
    const auto  radiusOf = [&] (float db) { return rad * hemiRadiusFrac (db, peak, sc); };

    // θ (0° = sólo L, 90° = mono, 180° = sólo R) → punto de pantalla, con la base al pie.
    const auto pointAt = [&] (float deg, float r)
    {
        const auto a = deg * juce::MathConstants<float>::pi / 180.0f;
        return juce::Point<float> (cx - std::cos (a) * r, baseY - std::sin (a) * r);
    };

    // ========================================================================================================
    // ===== 57b: LO QUE ESTÁ FUERA DE FASE SE PLIEGA SOBRE LA BASE =====
    //
    // Antes se dibujaba hacia ABAJO, en un semicírculo inferior propio. Eso costaba la mitad del panel
    // para mostrar una zona que en una mezcla sana está casi vacía, y —peor— hacía que el dato de arriba
    // tuviera la mitad del radio disponible. Insight no lo hace y tiene razón.
    //
    // Ahora el hemisferio inferior se PLIEGA sobre la base como se pliega una hoja: θ' = 360° − θ. Eso
    // manda cada dirección fuera de fase al lugar de pantalla que le corresponde POR PANEO (θ = 190°, que
    // es R con la fase dada vuelta, cae en 170°, al lado de R; θ = 270°, que es L = −R, cae en 90°, al
    // medio), y se pinta en el color de alerta encima del lóbulo en fase. LA INFORMACIÓN NO SE PIERDE:
    // cambia de lugar, y además se imprime como número ("fuera de fase N %", ver outOfPhasePercent()).
    //
    // ========================================================================================================
    // ===== 57c: UN RAYO POR GRADO — POLAR LEVEL =====
    //
    // «El polar level de Insight es más fino; el polar sample está igual al de ellos, es sólo cuando
    // ponemos polar level» (Joaquín, 12-sep). Tenía razón y el motivo estaba a la vista en el código: el
    // motor publica 360 bins de UN grado (ver ScopeFrame.h) y la lente los pasaba por un MÁXIMO MÓVIL
    // circular de ±2° antes de dibujar. Un máximo móvil no suaviza: convierte cada púa en una MESETA
    // PLANA de 5°, y eso es la escalera que se ve en la foto — una escalera con el escalón del ancho del
    // filtro, no del dato.
    //
    // Ahora se dibuja lo que el motor mide: 181 rayos (0° … 180°), cada uno una CUÑA de 1° con sus dos
    // aristas, desde el origen. Con material mono todo cae en el bin 90 y sale una aguja; con material
    // ancho sale el peine fino que dibuja Insight. La cuña se recorta a [0°, 180°]: medio grado por
    // debajo de la base sería dibujo por debajo de la base, que es lo que el plegado vino a eliminar.
    //
    // Y son DOS CAPAS, que es lo que hace legible un perfil que ya no está amesetado:
    //   · PROMEDIO, relleno — la envolvente promediada EN EL TIEMPO (τ = 0.3 s, en energía, por bin; ver
    //     HemisphereView). No se promedia en ÁNGULO: promediar en ángulo es inventar anchura;
    //   · PICO, contorno fino con glow — la retención con decaimiento de siempre (12/24/48 dB/s), sobre
    //     las puntas de los 181 rayos. Es el borde contra el que se mide.
    // ========================================================================================================
    float outProfile[kHemiRays];
    float* const holdProfile = drawnPeak;   // se guardan: son los rayos que [hemis] verifica
    float* const avgProfile  = drawnAvg;
    foldProfile (hemi.env, holdProfile, false);
    foldProfile (hemi.avg, avgProfile,  false);
    foldProfile (hemi.env, outProfile,  true);

    // Los 181 rayos como camino. `closed` lo cierra contra el origen (el relleno); abierto, la polilínea
    // pasa por las puntas y es el contorno.
    const auto buildRays = [&] (const float* profile, bool closed)
    {
        juce::Path p;
        for (int d = 0; d < kHemiRays; ++d)
        {
            const float a0 = juce::jmax (0.0f,   (float) d - 0.5f);
            const float a1 = juce::jmin (180.0f, (float) d + 0.5f);
            const float r  = radiusOf (profile[d]);
            if (d == 0)
            {
                if (closed) p.startNewSubPath (pointAt (a0, 0.0f));
                else        p.startNewSubPath (pointAt (a0, r));
            }
            p.lineTo (pointAt (a0, r));
            p.lineTo (pointAt (a1, r));
        }
        if (closed) { p.lineTo (pointAt (180.0f, 0.0f)); p.closeSubPath(); }
        return p;
    };

    // ===== NADA SE DIBUJA POR DEBAJO DE LA BASE, Y SE RECORTA PARA QUE NO PUEDA =====
    //
    // El plegado (θ' = 360° − θ) manda todo el dato al semicírculo de arriba, así que por construcción
    // ningún vértice cae por debajo de la base. Pero un vértice no es un píxel: el trazo tiene ancho y el
    // glow tiene radio, y los dos florecen alrededor de los puntos que están EN la base (los rayos de 0°
    // y 180°). En el 57c eso dejó 5 píxeles verdes dos filas por debajo — poco, y exactamente el tipo de
    // "poco" que convierte un contrato en una aproximación. El recorte lo hace imposible en vez de
    // improbable; lo verifica HEMIS[plegado].
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) std::floor (plot.getX()), (int) std::floor (plot.getY()),
                                              (int) std::ceil (plot.getWidth()),
                                              juce::jmax (0, juce::roundToInt (baseY)
                                                                 - (int) std::floor (plot.getY()) + 1)));

    // ---- 1 · el PROMEDIO, relleno con degradado radial: más denso contra el origen, que es donde la
    //         energía se concentra y donde el ojo busca el centro de la mezcla ----
    {
        juce::ColourGradient grad (look::dataLine.withAlpha (0.42f), cx, baseY,
                                   look::dataLine.withAlpha (0.06f), cx, baseY - rad, true);
        g.setGradientFill (grad);
        g.fillPath (buildRays (avgProfile, true));
    }

    // ---- 2 · el PICO retenido, como contorno con glow. Es el borde contra el que se mide ----
    const auto holdPath = buildRays (holdProfile, false);
    look::glowPath (g, holdPath, look::dataLine, m.dataW, m.glowRadius);   // EL glow de la lente
    g.setColour (look::dataLine);
    g.strokePath (holdPath, juce::PathStrokeType (m.dataW));

    // ---- 3 · lo FUERA DE FASE, plegado. Tenue de base y encendido en proporción a lo que hay que perder
    //         de verdad: pintar de rojo lleno una mezcla con correlación +0.99 enseña a desconfiar ----
    const float outPct = outOfPhasePercent();
    const float severity = juce::jlimit (0.0f, 1.0f, juce::jmax (monoLossDb / -6.0f, outPct / 25.0f));
    {
        const auto outPath = buildRays (outProfile, true);
        g.setColour (look::alert.withAlpha (0.08f + 0.22f * severity));
        g.fillPath (outPath);
        g.setColour (look::alert.withAlpha (0.35f + 0.55f * severity));
        g.strokePath (outPath, juce::PathStrokeType (m.dataW * 0.8f));
    }

    // ---- 4 · LAS MUESTRAS POLARES (el "Polar Sample" de Insight): los últimos pares L/R, un píxel cada
    //         uno, adentro del lóbulo. Es el detalle de AHORA sobre la forma, y lo que deja ver si una
    //         dirección está llena o es un borde con el medio vacío ----
    constexpr int kPolarSamples = 1500;
    const int first = juce::jmax (0, scope.xyCount - kPolarSamples);
    g.setColour (look::txtPrimary.withAlpha (0.35f));
    for (int i = first; i < scope.xyCount; ++i)
    {
        const double l = (double) scope.xyL[(size_t) i];
        const double r = (double) scope.xyR[(size_t) i];
        const double e = l * l + r * r;
        if (e <= 1.0e-12) continue;

        const auto psi = std::atan2 (r - l, r + l);
        auto deg = (float) std::fmod (90.0 + 2.0 * psi * 180.0 / juce::MathConstants<double>::pi, 360.0);
        if (deg < 0.0f) deg += 360.0f;
        if (deg > 180.0f) deg = 360.0f - deg;      // plegado, igual que la envolvente

        const auto pt = pointAt (deg, radiusOf ((float) (10.0 * std::log10 (e))));
        g.fillRect (pt.x - 0.5f, pt.y - 0.5f, 1.0f, 1.0f);
    }

    g.restoreState();   // fin del recorte por encima de la base

    // ---- el pico y el porcentaje fuera de fase, rotulados. Una escala sin número es un dibujo bonito, y
    //      un plegado sin número escondería lo que se acaba de mover de lugar ----
    g.setColour (look::txtTertiary);
    g.setFont (look::tabularFont (m.textMicro));
    g.drawText (juce::String (hemi.peakDb, 1) + " dB", (int) plot.getX(), (int) plot.getY() + 2,
                (int) plot.getWidth() - 4, 12, juce::Justification::right, false);

}

// El correlímetro VERTICAL del modo hemisferio: +1 arriba, -1 abajo, la mitad negativa marcada en alerta.
// Vertical y no horizontal porque queda pegado al semicírculo y comparte con él la lectura de "arriba =
// en fase", que es la misma metáfora dos veces y no dos metáforas distintas.
void ScopeLens::paintCorrelationVertical (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area.reduced (6);
    if (r.getHeight() < 60) return;
    const auto m = look::metricsFor (getWidth());

    r.removeFromTop (22);                       // el rótulo lo puso la capa estática
    auto value = r.removeFromBottom (26);
    auto bar = r.reduced (juce::jmax (0, (r.getWidth() - 22) / 2), 4);

    g.setColour (look::well);
    g.fillRect (bar);
    // ===== 56b ===== LA MITAD DE ABAJO, PROPORCIONAL. Estaba a alpha 0.16 fija: con correlación +0.99
    // y nada que perder al monoficar, media barra igual teñida de alerta. Dos cosas malas a la vez —
    // enseña a desconfiar del medidor (si siempre hay rojo, el rojo no dice nada) y compite con el
    // lóbulo inferior del hemisferio, que está justo al lado y ESE sí es proporcional desde el 56.
    // Ahora las dos usan la misma cuenta: monoLossDb / -6 (0 dB → nada, -6 dB o peor → lleno), que es
    // la pérdida MEDIDA al sumar a mono. La zona sigue dibujándose siempre (una que aparece y
    // desaparece haría dudar de si el plugin la está mirando), pero con el énfasis que corresponde.
    const float severity = juce::jlimit (0.0f, 1.0f, monoLossDb / -6.0f);
    g.setColour (look::alert.withAlpha (0.04f + 0.18f * severity));   // la mitad de abajo: fuera de fase
    g.fillRect (bar.withTop (bar.getCentreY()));
    g.setColour (look::gridMinor);
    g.drawRect (bar, 1);
    look::hLine (g, (float) bar.getX(), (float) bar.getRight(), (float) bar.getCentreY(), look::gridMajor);

    const float t = juce::jlimit (0.0f, 1.0f, (dispCorr + 1.0f) * 0.5f);
    const int   y = bar.getBottom() - juce::roundToInt (t * (float) bar.getHeight());
    g.setColour (dispCorr < 0.0f ? look::alert : look::dataLine);
    g.fillRect (bar.getX() - 2, y - 1, bar.getWidth() + 4, 3);

    g.setColour (look::txtTertiary);
    g.setFont (look::tabularFont (m.textMicro));
    g.drawText ("+1", area.getX(), bar.getY() - 12, area.getWidth(), 12, juce::Justification::centred, false);
    g.drawText ("0",  area.getX(), bar.getCentreY() - 6, area.getWidth(), 12, juce::Justification::centred, false);
    g.drawText ("-1", area.getX(), bar.getBottom(), area.getWidth(), 12, juce::Justification::centred, false);

    g.setColour (dispCorr < 0.0f ? look::alert : look::txtPrimary);
    g.setFont (look::tabularFont (m.textNumber));
    g.drawText (fmt2 (dispCorr), value, juce::Justification::centred, false);
}

// El goniómetro con su capa de fósforo. La capa se ATENÚA (no se borra) y encima van los puntos del hop:
// eso da la estela sin acumular nada — un punto viejo se apaga solo en ~8 frames.
void ScopeLens::paintGonio (juce::Graphics& g)
{
    const auto area = zones.gonio.reduced (10);
    if (area.isEmpty()) return;

    // La estela vive en píxeles de DISPOSITIVO: `plotPoints` ya trabaja sobre el tamaño de la imagen, así
    // que a escala 2 la nube tiene cuatro veces más resolución sin tocar una sola cuenta del dibujo.
    if (! trail.valid()) return;

    if (prefersReducedMotion())
    {
        trail.image().clear (trail.image().getBounds());   // sin estela: sólo el hop actual, cuadro coherente
        plotPoints (trail.image(), area, 0.95f);
    }
    else
    {
        decayTrail();
        plotPoints (trail.image(), area, 0.85f);
    }

    trail.blit (g, area.getX(), area.getY());

    // La escala, SIEMPRE a la vista: el anillo exterior vale este pico. Sin este rótulo la auto-escala
    // sería un gráfico bonito sin unidades.
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    const auto peakDb = 20.0f * std::log10 (juce::jmax (1.0e-6f, gonioPeak));
    g.drawText (processor.scopePolar()
                    ? tr (strings::Key::polarLegend)
                    : trLower (strings::Key::lissajous) + juce::String::fromUTF8 (" \xc2\xb7 ") + trLower (strings::Key::peakEdge) + " " + juce::String (peakDb, 1) + " dBFS",
                area.getX(), area.getBottom() - 12, area.getWidth(), 12, juce::Justification::centred, false);
}

void ScopeLens::decayTrail()
{
    const juce::Image::BitmapData bd (trail.image(), juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bd.height; ++y)
    {
        auto* line = bd.getLinePointer (y);
        for (int x = 0; x < bd.width; ++x)
        {
            auto* px = line + x * bd.pixelStride;
            for (int i = 0; i < bd.pixelStride; ++i)
                // 56: 184 → 170. La estela era gruesa y empastaba la nube: con un decaimiento un punto
                // más rápido (≈ 0.66 por frame, unos 6 frames de vida) el fósforo se lee FINO y la forma
                // del estéreo —que es lo que el goniómetro tiene que mostrar— vuelve a distinguirse.
                px[i] = (juce::uint8) ((int) px[i] * 170 / 256);   // ≈ kTrailDecay, en enteros
        }
    }
}

// Escribe los puntos del hop DIRECTO en los píxeles: 2 048 escrituras es barato, 2 048 fillRect no.
void ScopeLens::plotPoints (juce::Image& img, juce::Rectangle<int> area, float alpha) const
{
    if (scope.xyCount <= 0) return;

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    const float cx = (float) bd.width  * 0.5f;
    const float cy = (float) bd.height * 0.5f;
    const float radius = juce::jmin (cx, cy);
    const auto  hue = th::green;
    const bool  polar = processor.scopePolar();

    juce::ignoreUnused (area);

    for (int i = 0; i < scope.xyCount; ++i)
    {
        const float l = scope.xyL[(size_t) i];
        const float r = scope.xyR[(size_t) i];

        // Rotación de 45°: mono (L=R) al eje VERTICAL, sólo-L y sólo-R a las diagonales.
        float u = (r - l) * kInvSqrt2;      // lado
        float v = (r + l) * kInvSqrt2;      // medio (arriba positivo)

        if (polar)
        {
            // Mismo ÁNGULO, radio en dB: el material bajo deja de colapsar en un punto en el centro.
            const float mag = std::sqrt (u * u + v * v);
            if (mag <= 1.0e-7f) continue;
            const float db  = 20.0f * std::log10 (mag);
            const float t   = juce::jlimit (0.0f, 1.0f, (db - kOscFloorDb) / -kOscFloorDb);
            u = u / mag * t;
            v = v / mag * t;
        }
        else
        {
            // Lineal, AUTO-ESCALADO al pico del hop (ver el comentario de gonioPeak en el header): el
            // anillo exterior es ese pico. El factor 1/√2 mantiene la geometría — un mono al pico toca
            // el borde y un canal solo llega a 0.707 del radio, como en cualquier vectorscopio.
            const float k = kInvSqrt2 / gonioPeak;
            u *= k;
            v *= k;
        }

        const int px = juce::roundToInt (cx + u * radius);
        const int py = juce::roundToInt (cy - v * radius);
        if (px < 0 || py < 0 || px >= bd.width || py >= bd.height) continue;

        // Acumulativo dentro del frame (donde se cruzan muchas muestras, brilla más) pero acotado a 255.
        const auto prev = bd.getPixelColour (px, py);
        const float a   = juce::jmin (1.0f, prev.getFloatAlpha() + alpha * 0.42f);   // 56: punto más fino
        bd.setPixelColour (px, py, hue.withAlpha (a));
    }
}

void ScopeLens::paintCorrelation (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area.reduced (10);
    r.removeFromTop (18);   // el rótulo lo puso la capa estática

    const auto hue = th::green;

    // ---- el número, grande: es la lectura que se mira de reojo ----
    auto head = r.removeFromTop (juce::jmax (46, r.getHeight() / 4));
    g.setColour (dispCorr < 0.0f ? th::red : th::txt);
    g.setFont (ovni::ui::fonts::mono ((float) juce::jmin (40, head.getHeight() - 6)));
    g.drawText (fmt2 (dispCorr), head, juce::Justification::centredLeft, false);

    // ---- la barra -1…+1, con la mitad negativa marcada ----
    auto bar = r.removeFromTop (26);
    g.setColour (th::bg1);
    g.fillRect (bar);
    g.setColour (th::red.withAlpha (0.16f));                       // zona de alerta: fuera de fase
    g.fillRect (bar.withWidth (bar.getWidth() / 2));
    g.setColour (look::gridMinor);
    g.drawRect (bar, 1);
    g.setColour (look::gridMajor);
    look::fillSnapped (g, { (float) (bar.getCentreX()), (float) (bar.getY()), 1.0f, (float) (bar.getHeight()) });   // el cero

    const float t = juce::jlimit (0.0f, 1.0f, (dispCorr + 1.0f) * 0.5f);
    const int   x = bar.getX() + juce::roundToInt (t * (float) bar.getWidth());
    g.setColour (dispCorr < 0.0f ? th::red : hue);
    g.fillRect (x - 1, bar.getY() + 1, 3, bar.getHeight() - 2);

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    g.drawText ("-1", bar.getX(), bar.getBottom() + 1, 20, 12, juce::Justification::left, false);
    g.drawText ("0",  bar.getCentreX() - 10, bar.getBottom() + 1, 20, 12, juce::Justification::centred, false);
    g.drawText ("+1", bar.getRight() - 20, bar.getBottom() + 1, 20, 12, juce::Justification::right, false);
    r.removeFromTop (16);

    // ---- lectura fina ----
    // Alto ACOTADO: en el panel alto de tamaño L, repartir el sobrante entre las filas las separa tanto
    // que dejan de leerse como un grupo. Se agrupan arriba y el aire queda abajo.
    const int rowH = juce::jlimit (22, 34, r.getHeight() / 4);
    paintReadout (g, r.removeFromTop (rowH), tr (strings::Key::width), fmt2 (width), th::txt);
    paintReadout (g, r.removeFromTop (rowH), tr (strings::Key::balance),
                  juce::String (balanceDb, 1) + " dB", std::abs (balanceDb) > 3.0f ? th::amber : th::txt);
    paintReadout (g, r.removeFromTop (rowH), tr (strings::Key::monoLoss),
                  juce::String (monoLossDb, 1) + " dB", monoLossDb < -3.0f ? th::red : th::txt);

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.drawText (trLower (strings::Key::window) + " " + juce::String (windowSec, 2) + " s",
                r.getX(), r.getY(), r.getWidth(), 14, juce::Justification::left, false);
}

// ===== 57b: LOS TRES NÚMEROS DEL ESTÉREO, EN UNA FILA =====
//
// Los mismos que la columna de Lissajous/polar (ANCHO, BALANCE, PÉRDIDA MONO), para el modo hemisferio,
// que no los tenía. Se dibujan SIEMPRE: con silencio dicen el texto de "sin señal" en vez de quedar en
// blanco — un número que desaparece se lee como "el plugin no está midiendo".
void ScopeLens::paintStereoNumbers (juce::Graphics& g, juce::Rectangle<int> area) const
{
    if (area.getWidth() < 120 || area.getHeight() < 18) return;
    const auto m = look::metricsFor (getWidth());
    const bool quiet = scope.xyCount <= 0 || gonioPeak <= kGonioMinPeak + 1.0e-6f;

    // El cuarto es el PORCENTAJE FUERA DE FASE, que es el número que el plegado del semicírculo obliga a
    // decir: lo que antes se leía como "mirá cuánto hay abajo de la base" ahora se lee como una cifra.
    const float outPct = outOfPhasePercent();
    constexpr int kCells = 4;
    const int cell = area.getWidth() / kCells;
    const struct { strings::Key k; juce::String v; juce::Colour tint; } cells[kCells] = {
        { strings::Key::width,      fmt2 (width),                             look::txtPrimary },
        { strings::Key::balance,    juce::String (balanceDb, 1) + " dB",
          std::abs (balanceDb) > 3.0f ? look::caution : look::txtPrimary },
        { strings::Key::monoLoss,   juce::String (monoLossDb, 1) + " dB",
          monoLossDb < -3.0f ? look::alert : look::txtPrimary },
        { strings::Key::outOfPhase, juce::String (outPct, outPct < 10.0f ? 1 : 0) + " %",
          outPct > 5.0f ? look::alert : look::txtPrimary },
    };

    for (int i = 0; i < kCells; ++i)
    {
        const auto r = area.withX (area.getX() + i * cell).withWidth (cell);
        g.setColour (look::txtTertiary);
        g.setFont (look::labelFont (m.textMicro));
        // En mayúscula como los otros tres: en la tabla `outOfPhase` está en caja baja porque también se
        // usa como frase suelta, y acá es un rótulo de columna.
        g.drawText (tr (cells[i].k).toUpperCase(), r.getX(), r.getY(), r.getWidth(), r.getHeight() / 2,
                    juce::Justification::centredLeft, false);
        g.setColour (quiet ? look::txtTertiary : cells[i].tint);
        g.setFont (quiet ? look::labelFont (m.textMicro) : look::tabularFont (m.textNumber));
        g.drawText (quiet ? tr (strings::Key::noSignal) : cells[i].v,
                    r.getX(), r.getY() + r.getHeight() / 2, r.getWidth(), r.getHeight() / 2,
                    juce::Justification::centredLeft, false);
    }
}

void ScopeLens::paintReadout (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                              const juce::String& value, juce::Colour tint) const
{
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.drawText (label, area.getX(), area.getY(), area.getWidth() / 2, area.getHeight(),
                juce::Justification::centredLeft, false);
    g.setColour (tint);
    g.setFont (ovni::ui::fonts::mono (13.0f));
    g.drawText (value, area.getX() + area.getWidth() / 2, area.getY(), area.getWidth() / 2, area.getHeight(),
                juce::Justification::centredRight, false);
}

void ScopeLens::paintOsc (juce::Graphics& g, juce::Rectangle<int> area) const
{
    const auto r = area.reduced (th::padIn / 2);
    if (scope.oscCount <= 1 || r.isEmpty()) return;

    const bool useTrigger = processor.scopeTrigger() && scope.trigger >= 0;
    const int  start = useTrigger ? scope.trigger : 0;
    const int  n     = scope.oscCount - start;
    if (n <= 1) return;

    const float midY = (float) r.getCentreY();
    const float halfH = (float) r.getHeight() * 0.46f;
    const auto  yOf = [&] (float v) { return midY - juce::jlimit (-1.0f, 1.0f, v) * halfH; };

    // ENVOLVENTE min/max por COLUMNA DE PÍXEL, no una muestra por vértice.
    //
    // A 48 k un hop trae 1 920 muestras para ~330 px de ancho: dibujar los 1 920 vértices costaba 7.4 ms
    // (medido, era TODO el presupuesto de la lente) y no se veía mejor — seis muestras caen en el mismo
    // píxel y el rasterizador las pinta igual. Con min/max por columna el trazo baja a ~660 vértices y
    // los picos NO se pierden, que es justo lo que una decimación "una de cada seis" sí perdería.
    const int columns = juce::jmin (n, juce::jmax (2, r.getWidth()));
    // 57b — EL TRAZO DEJA DE SER UNA ESCALERA. Cada columna dibujaba un `fillRect` de ENTEROS entre el
    // mínimo y el máximo de su tramo: sin antialiasing, en las partes empinadas de la onda el trazo sube
    // en escalones de un píxel entero, y en Retina se ven al doble. Ahora los tramos son rectángulos de
    // coma flotante (que JUCE sí antialiasea) y el trazo de M —el que se lee— lleva el glow barato de
    // tres anchuras, las mismas que usan SPECTRUM y las curvas de TONAL BALANCE.
    const auto trace = [&] (const float* src, juce::Colour colour, float thickness, bool glow)
    {
        for (int c = 0; c < columns; ++c)
        {
            const int i0 = (int) ((long long) c * (long long) n / (long long) columns);
            const int i1 = juce::jmax (i0 + 1, (int) ((long long) (c + 1) * (long long) n / (long long) columns));
            float lo = src[(size_t) (start + i0)], hi = lo;
            for (int i = i0 + 1; i < i1 && i < n; ++i)
            {
                const float v = src[(size_t) (start + i)];
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
            const float x0 = (float) r.getX() + (float) c * (float) r.getWidth() / (float) columns;
            const float x1 = (float) r.getX() + (float) (c + 1) * (float) r.getWidth() / (float) columns;
            const float yTop = yOf (hi), yBot = yOf (lo);
            const float w = juce::jmax (1.0f, x1 - x0);
            const float h = juce::jmax (thickness, yBot - yTop);

            if (glow)
            {
                g.setColour (colour.withMultipliedAlpha (0.12f));
                g.fillRect (juce::Rectangle<float> (x0 - 1.5f, yTop - 2.0f, w + 3.0f, h + 4.0f));
                g.setColour (colour.withMultipliedAlpha (0.30f));
                g.fillRect (juce::Rectangle<float> (x0 - 0.5f, yTop - 0.8f, w + 1.0f, h + 1.6f));
            }
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (x0, yTop, w, h));
        }
    };

    trace (scope.oscL, look::txtSecondary.withAlpha (0.40f), 1.0f, false);
    trace (scope.oscR, look::reference.withAlpha (0.30f),    1.0f, false);
    trace (scope.oscM, look::dataLine,                       1.6f, true);

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    const auto oscTag = tr (strings::Key::mid) + juce::String::fromUTF8 (" \xc2\xb7 ") + tr (strings::Key::left)
                        + juce::String::fromUTF8 (" \xc2\xb7 ") + tr (strings::Key::right)
                        + "   " + tr (useTrigger ? strings::Key::trigger : strings::Key::free);
    g.drawText (oscTag,
                r.getX(), r.getBottom() - 13, r.getWidth(), 12, juce::Justification::left, false);
}

void ScopeLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
                             bool active, bool hovered_) const
{
    const auto hue = th::green;
    const auto r = area.toFloat();

    g.setColour (active ? hue.withAlpha (0.18f) : th::surf2);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hue.withAlpha (active ? 0.9f : (hovered_ ? 0.55f : 0.28f)));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    if (hovered_)
    {
        g.setColour (hue.withAlpha (th::state::hoverGlow));
        g.fillRoundedRectangle (r, 3.0f);
    }

    g.setColour (active ? hue : th::txt);
    g.setFont (ovni::ui::fonts::label (10.5f));
    g.drawText (text, area, juce::Justification::centred, false);
}

//======================================================================================== animación
bool ScopeLens::advanceFrame()
{
    const auto& f = processor.analysis().read();
    corr       = f.corr;
    width      = f.width;
    balanceDb  = f.balanceDb;
    monoLossDb = f.monoLossDb;
    windowSec  = f.stereoWindowSec;

    const auto& s = processor.scope().read();
    const bool  fresh = s.timeSeconds != lastFrameTime;
    if (fresh) { scope = s; lastFrameTime = s.timeSeconds; }

    // Pico del hop para la auto-escala: sube de una (no perderse un transitorio) y baja despacio (que la
    // nube no lata con cada golpe). Es el comportamiento de cualquier medidor de picos con hold.
    float hopPeak = 0.0f;
    for (int i = 0; i < scope.xyCount; ++i)
        hopPeak = juce::jmax (hopPeak, juce::jmax (std::abs (scope.xyL[(size_t) i]),
                                                   std::abs (scope.xyR[(size_t) i])));
    hopPeak = juce::jmax (hopPeak, kGonioMinPeak);
    // Con reduced-motion la escala TAMBIÉN salta: una escala que se desliza sola durante segundos es
    // movimiento, aunque sea movimiento útil. El cuadro tiene que quedar quieto y coherente.
    gonioPeak = (prefersReducedMotion() || hopPeak > gonioPeak)
                  ? hopPeak
                  : gonioPeak + (hopPeak - gonioPeak) * 0.08f;

    // ===== 56 ===== la envolvente del hemisferio. Se actualiza SIEMPRE (aunque el modo no esté a la
    // vista) por 30 muestras de 360 floats por segundo: es barato, y así cambiar de modo no muestra medio
    // segundo de pantalla vacía mientras la memoria se vuelve a llenar.
    //
    // Con reduced-motion NO hay memoria: la envolvente es el hop y nada más, un cuadro quieto.
    hemi.update (scope, hemiDecayDbPerSec(), kHemiDecayFps, ! prefersReducedMotion());

    const float before = dispCorr;
    dispCorr = prefersReducedMotion() ? corr : dispCorr + (corr - dispCorr) * kCorrSmoothing;

    // Sin reduced-motion la estela sigue viva un rato después del último hop: hay que seguir repintando.
    return fresh || std::abs (dispCorr - before) > 1.0e-3f || ! prefersReducedMotion();
}

//======================================================================================== interacción
void ScopeLens::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    // 56: el botón de modo CICLA los tres. Un solo control para una sola decisión ("cómo miro el
    // estéreo"), en vez de dos interruptores que pueden quedar en un estado sin sentido.
    if (zones.polarBtn.contains (p))
        setScopeMode ((Mode) (((int) scopeMode() + 1) % 3));
    else if (zones.triggerBtn.contains (p)) processor.setScopeTrigger (! processor.scopeTrigger());
    else if (scopeMode() == Mode::hemisphere && ! zones.decayBtn.isEmpty() && zones.decayBtn.contains (p))
        setHemiDecayIndex ((hemiDecayIndex() + 1) % kNumHemiDecayOptions);
    else if (scopeMode() == Mode::hemisphere && ! zones.scaleBtn.isEmpty() && zones.scaleBtn.contains (p))
        setHemiScale (hemiScale() == HemiScale::relative ? HemiScale::decibel : HemiScale::relative);
    else
    {
        for (int i = 0; i < kWindowOptions; ++i)
            if (zones.windowBtn[i].contains (p)) { processor.setStereoWindowMs (Stereo::kWindowMsOptions[i]); break; }
    }
    repaint();
}

void ScopeLens::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int was = hovered;
    hovered = zones.polarBtn.contains (p) ? 0 : (zones.triggerBtn.contains (p) ? 1 : -1);
    if (hovered < 0 && ! zones.decayBtn.isEmpty() && zones.decayBtn.contains (p)) hovered = 5;   // 56
    if (hovered < 0 && ! zones.scaleBtn.isEmpty() && zones.scaleBtn.contains (p)) hovered = 6;   // 57b
    if (hovered < 0)
        for (int i = 0; i < kWindowOptions; ++i)
            if (zones.windowBtn[i].contains (p)) { hovered = 2 + i; break; }
    if (hovered != was) repaint();
}

void ScopeLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}
}
