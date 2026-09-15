#include "lenses/BandCorrelationLens.h"
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

constexpr double kSixthDown = 0.8908987181403393;   // 2^(-1/6)
constexpr double kSixthUp   = 1.1224620483093730;   // 2^(+1/6)

constexpr double kLabelledHz[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };

juce::String shortHz (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        return juce::String (k, k < 10.0 && std::abs (k - std::round (k)) > 0.01 ? 1 : 0) + "k";
    }
    return juce::String (hz, hz < 100.0 && std::abs (hz - std::round (hz)) > 0.01 ? 1 : 0);
}

juce::String fmt2 (float v) { return juce::String (v > 0.0f ? "+" : "") + juce::String (v, 2); }

juce::String rowLabel (const BandCorrelationLens& lens, int r)
{
    switch (r)
    {
        case BandCorrelationLens::rowWidth:   return lens.tr (strings::Key::width);
        case BandCorrelationLens::rowBalance: return lens.tr (strings::Key::balance);
        default:                              return lens.tr (strings::Key::monoLoss);
    }
}

// El rótulo CORTO de la escala lateral: el canal mide 44 px y "MONO LOSS" no entra (se veía "MONO L").
// El nombre completo vive en el botón, que es donde se elige.
juce::String rowShortLabel (const BandCorrelationLens& lens, int r)
{
    switch (r)
    {
        case BandCorrelationLens::rowWidth:   return lens.tr (strings::Key::width);
        case BandCorrelationLens::rowBalance: return "BAL dB";
        default:                              return "MONO dB";
    }
}
}

const juce::ValueTree& BandCorrelationLens::stateTree() const { return processor.apvts.state; }

BandCorrelationLens::BandCorrelationLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (30);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    rowMode = juce::jlimit (0, (int) kNumRows - 1, processor.bandsRow());
    std::fill (std::begin (bins), std::end (bins), 0);
}

//======================================================================================== geometría
BandCorrelationLens::Zones BandCorrelationLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (220, (foot.getWidth() - gap * (kNumControls - 1)) / kNumControls);
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.head = body.removeFromTop (kHeadH);
    body.removeFromTop (6);

    auto axis = body.removeFromBottom (kAxisH);

    // La fila de correlación se lleva más alto que la secundaria: es la que se mira primero.
    const int corrH = juce::jmax (60, (int) std::lround (0.58 * (double) body.getHeight()));
    auto top = body.removeFromTop (corrH);
    body.removeFromTop (th::padIn / 2);

    z.scaleCorr = top.removeFromLeft (kScaleW);
    z.corr      = top;
    z.scaleRow  = body.removeFromLeft (kScaleW);
    z.row       = body;

    z.freqAxis = axis.withLeft (z.corr.getX()).withRight (z.corr.getRight());
    return z;
}

void BandCorrelationLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    Lens::resized();
}

//======================================================================================== ejes
float BandCorrelationLens::xForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return (float) zones.corr.getX() + (float) t * (float) zones.corr.getWidth();
}

double BandCorrelationLens::freqAtX (int x) const
{
    if (zones.corr.getWidth() <= 1) return kMinHz;
    const double t = juce::jlimit (0.0, 1.0, (double) (x - zones.corr.getX()) / (double) zones.corr.getWidth());
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

int BandCorrelationLens::bandAtX (int x) const
{
    const double hz = freqAtX (x);
    for (int b = 0; b < StereoBands::kNumBands; ++b)
        if (hz >= kThirdOctaveHz[b] * kSixthDown && hz < kThirdOctaveHz[b] * kSixthUp) return b;
    return -1;
}

float BandCorrelationLens::yForCorr (float c, juce::Rectangle<int> r)
{
    const float t = juce::jlimit (0.0f, 1.0f, (1.0f - juce::jlimit (-1.0f, 1.0f, c)) * 0.5f);
    return (float) r.getY() + t * (float) r.getHeight();
}

// Las tres escalas de la fila secundaria. Los topes son VISUALES (clamp), no del dato: el número exacto
// sigue estando en la lectura al pasar el cursor, que es donde se va a mirar cuando importe.
float BandCorrelationLens::yForRow (float v, juce::Rectangle<int> r) const
{
    float t = 0.0f;   // 0 = arriba
    if (rowMode == rowWidth)        t = 1.0f - juce::jlimit (0.0f, 1.0f, v / kWidthCeil);
    else if (rowMode == rowBalance) t = juce::jlimit (0.0f, 1.0f, (kBalanceSpanDb - v) / (2.0f * kBalanceSpanDb));
    else                            t = juce::jlimit (0.0f, 1.0f, -v / -kMonoLossFloorDb);   // 0 arriba, -12 abajo
    return (float) r.getY() + t * (float) r.getHeight();
}

//======================================================================================== capa estática
void BandCorrelationLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);

    const auto panel = [&] (juce::Rectangle<int> r)
    {
        g.setColour (th::surf.withAlpha (0.65f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (look::gridMinor);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    };
    panel (zones.corr);
    panel (zones.row);

    // ---- rejilla de frecuencia: los 30 centros de ⅓ de octava, las décadas más marcadas ----
    for (const double hz : kThirdOctaveHz)
    {
        if (hz < kMinHz || hz > kMaxHz) continue;
        const bool decade = std::abs (std::log10 (hz) - std::round (std::log10 (hz))) < 1.0e-6;
        g.setColour (decade ? th::line : th::lineSoft);
        const int x = juce::roundToInt (xForFreq (hz));
        look::fillSnapped (g, { (float) (x), (float) (zones.corr.getY()), 1.0f, (float) (zones.corr.getHeight()) });
        look::fillSnapped (g, { (float) (x), (float) (zones.row.getY()), 1.0f, (float) (zones.row.getHeight()) });
    }

    g.setFont (ovni::ui::fonts::mono (9.0f));
    g.setColour (th::fnt);
    for (const double hz : kLabelledHz)
        g.drawText (shortHz (hz), juce::roundToInt (xForFreq (hz)) - 20, zones.freqAxis.getY() + 1, 40, 12,
                    juce::Justification::centred, false);

    // ---- escala de correlación: el 0 fuerte, ±0.5 rotuladas (REFERENCIA, no veredicto) ----
    const struct { float v; bool strong; } corrTicks[] = {
        { 1.0f, false }, { 0.5f, true }, { 0.0f, true }, { -0.5f, true }, { -1.0f, false }
    };
    for (const auto& t : corrTicks)
    {
        const int y = juce::roundToInt (yForCorr (t.v, zones.corr));
        g.setColour (t.v == 0.0f ? th::line : (t.strong ? th::lineSoft : th::lineSoft.withAlpha (0.5f)));
        look::fillSnapped (g, { (float) (zones.corr.getX()), (float) (y), (float) (zones.corr.getWidth()), 1.0f });
        g.setColour (t.v < 0.0f ? th::red.withAlpha (0.8f) : th::fnt);
        g.drawText (fmt2 (t.v), zones.scaleCorr.getX(), y - 6, kScaleW - 8, 12,
                    juce::Justification::centredRight, false);
    }

    // La mitad negativa del panel de correlación, teñida: es la zona que importa de un vistazo.
    g.setColour (th::red.withAlpha (0.07f));
    g.fillRect (zones.corr.withTop (juce::roundToInt (yForCorr (0.0f, zones.corr))));

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("CORR", zones.scaleCorr.getX(), zones.corr.getY() + 2, kScaleW - 8, 12,
                juce::Justification::centredRight, false);

    // ---- escala de la fila secundaria ----
    float ticks[3] = { 0.0f, 0.0f, 0.0f };
    int   nTicks = 3;
    if (rowMode == rowWidth)        { ticks[0] = kWidthCeil; ticks[1] = 1.0f; ticks[2] = 0.0f; }
    else if (rowMode == rowBalance) { ticks[0] = kBalanceSpanDb; ticks[1] = 0.0f; ticks[2] = -kBalanceSpanDb; }
    else                            { ticks[0] = 0.0f; ticks[1] = 0.5f * kMonoLossFloorDb; ticks[2] = kMonoLossFloorDb; }

    for (int i = 0; i < nTicks; ++i)
    {
        const int y = juce::roundToInt (yForRow (ticks[i], zones.row));
        g.setColour (i == 1 && rowMode == rowBalance ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (zones.row.getX()), (float) (y), (float) (zones.row.getWidth()), 1.0f });
        g.setColour (th::fnt);
        g.setFont (ovni::ui::fonts::mono (9.0f));
        g.drawText (rowMode == rowWidth ? juce::String (ticks[i], 1) : juce::String ((int) ticks[i]),
                    zones.scaleRow.getX(), y - 6, kScaleW - 8, 12, juce::Justification::centredRight, false);
    }

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (rowShortLabel (*this, rowMode), zones.scaleRow.getX(), zones.row.getY() + 2, kScaleW - 8, 12,
                juce::Justification::centredRight, false);
}

//======================================================================================== datos
bool BandCorrelationLens::advanceFrame()
{
    const auto wantedRow = juce::jlimit (0, (int) kNumRows - 1, processor.bandsRow());
    if (wantedRow != rowMode) { rowMode = wantedRow; invalidateStatic(); }

    const auto& f = processor.analysis().read();

    // Los bins por banda salen de la geometría de la FFT vigente: es lo que le permite a la lectura decir
    // "esta banda la midieron 2 bins" en vez de dar un número sin contexto.
    const auto& sp = processor.spectrum().read();
    const double binHz = sp.binHz();
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        if (binHz <= 0.0) { bins[b] = 0; continue; }
        const int k0 = juce::jmax (0, (int) std::ceil (kThirdOctaveHz[b] * kSixthDown / binHz));
        const int k1 = juce::jmin (sp.numBins, (int) std::ceil (kThirdOctaveHz[b] * kSixthUp / binHz));
        bins[b] = juce::jmax (0, k1 - k0);
    }

    measured  = f.bandsValid;
    windowSec = f.bandsWindowSec;

    const bool reduced = prefersReducedMotion();
    bool moved = false;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        corr[b]       = f.bandCorr[b];
        width[b]      = f.bandWidth[b];
        balanceDb[b]  = f.bandBalanceDb[b];
        monoLossDb[b] = f.bandMonoLossDb[b];

        const float target = rowMode == rowWidth ? width[b] : (rowMode == rowBalance ? balanceDb[b] : monoLossDb[b]);
        const float beforeC = dispCorr[b], beforeR = dispRow[b];
        dispCorr[b] = reduced ? corr[b]  : beforeC + (corr[b] - beforeC) * kRelease;
        dispRow[b]  = reduced ? target   : beforeR + (target - beforeR) * kRelease;
        moved = moved || std::abs (dispCorr[b] - beforeC) > 0.001f || std::abs (dispRow[b] - beforeR) > 0.001f;
    }
    return moved;
}

//======================================================================================== capa viva
void BandCorrelationLens::paintLive (juce::Graphics& g)
{
    if (zones.corr.isEmpty()) zones = zonesFor (getWidth(), getHeight());

    // ---- el resumen ----
    // COPIA de la zona, no la zona: `removeFromRight` MUTA el rectángulo, y hacerlo sobre el miembro
    // achicaría la cabecera un poco más en cada pintado (lo cazó el test de reduced-motion, que compara
    // el cuadro 1 con el 40 y los vio distintos sin que hubiera animación).
    auto head = zones.head;
    const auto side = head.removeFromRight (juce::jmin (220, head.getWidth() / 3));

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (trLower (strings::Key::window) + " " + juce::String (windowSec, 2) + " s  \xc2\xb7  "
                    + juce::String (measured) + " " + trLower (strings::Key::measuredBands),
                side, juce::Justification::centredRight, false);

    const auto s = summary();
    g.setFont (ovni::ui::fonts::mono (12.0f));
    g.setColour (s.valid && s.worstCorr < 0.0f ? th::red : th::txt);
    g.drawText (summaryText(), head, juce::Justification::centredLeft, false);

    paintBars (g, zones.corr, true);
    paintBars (g, zones.row, false);
    paintReadout (g);

    paintButton (g, zones.button[ctrlWindow], tr (strings::Key::window),
                 juce::String (processor.bandsWindowSec(), 1) + " s", hovered == ctrlWindow);
    paintButton (g, zones.button[ctrlRow], tr (strings::Key::row), rowLabel (*this, rowMode), hovered == ctrlRow);
}

// Las barras van entre los bordes REALES de la banda (ver el encabezado del header).
void BandCorrelationLens::paintBars (juce::Graphics& g, juce::Rectangle<int> area, bool isCorr) const
{
    g.saveState();
    g.reduceClipRegion (area);

    // La línea base de las barras es siempre el CERO de la escala vigente — lo que cambia es dónde cae:
    // arriba en MONO LOSS (0 dB = no se pierde nada), abajo en WIDTH (0 = mono) y al medio en BALANCE.
    const float base = isCorr ? yForCorr (0.0f, area) : yForRow (0.0f, area);

    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        const double lo = kThirdOctaveHz[b] * kSixthDown, hi = kThirdOctaveHz[b] * kSixthUp;
        if (hi <= kMinHz || lo >= kMaxHz) continue;

        const float xa = xForFreq (lo), xb = xForFreq (hi);
        const float x  = xa + 1.0f;
        const float w  = juce::jmax (2.0f, xb - xa - 2.0f);

        // BANDA SIN NINGÚN BIN: no se dibuja en cero, se marca. A esta resolución no hay medición, y
        // dibujar un cero sería decir "no hay correlación", que es otra cosa.
        if (bins[b] == 0)
        {
            g.setColour (th::fnt.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x, base - 1.5f, w, 3.0f));
            continue;
        }

        const float v = isCorr ? dispCorr[b] : dispRow[b];
        const float y = isCorr ? yForCorr (v, area) : yForRow (v, area);
        const float top = juce::jmin (y, base), bot = juce::jmax (y, base);

        // ===== 56: EL UMBRAL DE PÉRDIDA MONO ESTABA EN EL LUGAR EQUIVOCADO =====
        //
        // Estaba en -3 dB, y -3.01 dB es EXACTAMENTE lo que da material decorrelacionado: M = (L+R)/2 de
        // dos señales independientes pierde 3 dB por construcción. Lo dice el propio test del motor
        // (tests/StereoBandsTest.cpp:263, que EXIGE -3.0 ± tolerancia para ruido independiente). O sea que
        // la lente pintaba de rojo la física normal de cualquier mezcla ancha: en la hoja de contacto del
        // 8-sep, BAND CORRELATION con ruido independiente era un panel rojo entero.
        //
        // Un medidor que se alarma con lo normal enseña a no mirarlo. El umbral pasa a -6 dB, que es donde
        // se pierde el DOBLE de lo que se pierde por estar decorrelacionado — o sea, donde de verdad hay
        // cancelación y no sólo ancho.
        const bool alert = isCorr ? (v < 0.0f)
                                  : (rowMode == rowMonoLoss ? v < kMonoLossAlertDb
                                                            : (rowMode == rowBalance ? std::abs (v) > 3.0f
                                                                                     : false));
        const auto hue = alert ? look::alert : look::dataLine;

        // 57b — DEGRADADO BIPOLAR: la barra se apaga DESDE SU PUNTA hacia el cero, para los dos lados.
        // Plana, una fila de treinta barras se lee como una silueta maciza; degradada, cada barra tiene
        // una punta y el ojo lee el valor y no el bloque.
        g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.46f), 0.0f, y,
                                                 hue.withAlpha (0.06f), 0.0f, base, false));
        g.fillRect (juce::Rectangle<float> (x, top, w, juce::jmax (1.0f, bot - top)));
        g.setColour (hue.withAlpha (0.92f));
        g.fillRect (juce::Rectangle<float> (x, y - 0.75f, w, 1.5f));
    }

    // ===== 57b: EL RESUMEN, COMO CURVA =====
    //
    // Treinta barras sueltas dicen banda por banda; lo que NO dicen es la forma — si la correlación cae
    // hacia los agudos, si hay un pozo en medios. Una curva suavizada sobre las puntas lo dice de un
    // vistazo, y como va encima y con glow no le quita nada a la lectura banda a banda.
    {
        std::vector<float> ys;
        ys.reserve ((size_t) SpectrumFrame::kNumThird);
        std::vector<float> xs;
        xs.reserve ((size_t) SpectrumFrame::kNumThird);
        for (int b = 0; b < SpectrumFrame::kNumThird; ++b)
        {
            if (bins[b] == 0) continue;
            const float v = isCorr ? dispCorr[b] : dispRow[b];
            xs.push_back (xForFreq (kThirdOctaveHz[b]));
            ys.push_back (isCorr ? yForCorr (v, area) : yForRow (v, area));
        }
        if (ys.size() >= 3)
        {
            look::smoothInPlace (ys.data(), (int) ys.size(), 1);
            juce::Path p;
            p.startNewSubPath (xs[0], ys[0]);
            for (size_t i = 1; i < ys.size(); ++i) p.lineTo (xs[i], ys[i]);

            const auto m = look::metricsFor (getWidth());
            look::glowPath (g, p, look::txtPrimary, 1.0f, m.glowRadius * 0.6f);
            g.setColour (look::txtPrimary.withAlpha (0.70f));
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }
    }

    g.restoreState();
}

//======================================================================================== resumen
BandCorrelationLens::Summary BandCorrelationLens::summary() const
{
    Summary s;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        if (bins[b] == 0) continue;
        // Una banda sin energía publica los cuatro números en cero: no cuenta como "medida".
        if (corr[b] == 0.0f && width[b] == 0.0f && monoLossDb[b] == 0.0f && balanceDb[b] == 0.0f) continue;

        ++s.measured;
        if (s.worstPhaseBand < 0 || corr[b] < s.worstCorr)        { s.worstPhaseBand = b; s.worstCorr = corr[b]; }
        if (s.worstMonoBand  < 0 || monoLossDb[b] < s.worstMonoDb) { s.worstMonoBand = b; s.worstMonoDb = monoLossDb[b]; }
    }
    s.valid = s.measured > 0;
    return s;
}

juce::String BandCorrelationLens::summaryText() const
{
    const auto s = summary();
    if (! s.valid) return tr (strings::Key::noSignal);

    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    juce::String t = shortHz (kThirdOctaveHz[s.worstPhaseBand]) + " Hz" + dot + "corr "
                   + fmt2 (s.worstCorr) + dot + trLower (strings::Key::mono) + " "
                   + juce::String (monoLossDb[s.worstPhaseBand], 1) + " dB";

    if (s.worstMonoBand != s.worstPhaseBand)
        t += "     " + tr (strings::Key::mostMonoLoss)
           + shortHz (kThirdOctaveHz[s.worstMonoBand]) + " Hz " + juce::String (s.worstMonoDb, 1) + " dB";
    return t;
}

//======================================================================================== lectura
BandCorrelationLens::Readout BandCorrelationLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (! zones.corr.contains (p) && ! zones.row.contains (p)) return r;

    const int b = bandAtX (p.x);
    if (b < 0) return r;

    r.valid      = true;
    r.band       = b;
    r.centreHz   = kThirdOctaveHz[b];
    r.bins       = bins[b];
    r.corr       = corr[b];
    r.width      = width[b];
    r.balanceDb  = balanceDb[b];
    r.monoLossDb = monoLossDb[b];
    return r;
}

void BandCorrelationLens::paintReadout (juce::Graphics& g) const
{
    if (cursor.x < 0) return;
    const auto r = readoutAt (cursor);
    if (! r.valid) return;

    // La banda entera resaltada: lo que se lee es una BANDA, no una frecuencia.
    const float xa = xForFreq (r.centreHz * kSixthDown), xb = xForFreq (r.centreHz * kSixthUp);
    g.setColour (th::txt.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (xa, (float) zones.corr.getY(), xb - xa, (float) zones.corr.getHeight()));
    g.fillRect (juce::Rectangle<float> (xa, (float) zones.row.getY(), xb - xa, (float) zones.row.getHeight()));

    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    const juce::String text = r.bins == 0
        ? (shortHz (r.centreHz) + " Hz" + dot + tr (strings::Key::noBinsHere))
        : (shortHz (r.centreHz) + " Hz" + dot + "corr " + fmt2 (r.corr) + dot
           + trLower (strings::Key::width) + " " + juce::String (r.width, 2) + dot
           + trLower (strings::Key::balance) + " " + juce::String (r.balanceDb, 1) + " dB" + dot
           + trLower (strings::Key::mono) + " " + juce::String (r.monoLossDb, 1) + " dB" + dot
           + juce::String (r.bins) + (r.bins == 1 ? " bin" : " bins"));

    g.setFont (ovni::ui::fonts::mono (11.0f));
    const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16;
    const auto box = readoutBoxFor (zones.corr, cursor.x, tw);

    g.setColour (th::bg1.withAlpha (0.9f));
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (th::green.withAlpha (0.4f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (th::txt);
    g.drawText (text, box, juce::Justification::centred, false);
}

void BandCorrelationLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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

//======================================================================================== interacción
void BandCorrelationLens::cycleControl (int control)
{
    if (control == ctrlWindow)
    {
        processor.setBandsWindowIndex ((processor.bandsWindowIndex() + 1) % StereoBands::kNumWindowOptions);
    }
    else if (control == ctrlRow)
    {
        rowMode = (rowMode + 1) % kNumRows;
        processor.setBandsRow (rowMode);
        std::fill (std::begin (dispRow), std::end (dispRow), 0.0f);   // otra escala: no se interpola entre las dos
        invalidateStatic();
    }
    else return;

    repaint();
}

void BandCorrelationLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void BandCorrelationLens::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int was = hovered;
    const auto wasCursor = cursor;

    hovered = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (p)) { hovered = i; break; }

    cursor = (zones.corr.contains (p) || zones.row.contains (p)) ? p : juce::Point<int> (-1, -1);
    if (hovered != was || cursor != wasCursor) repaint();
}

void BandCorrelationLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
