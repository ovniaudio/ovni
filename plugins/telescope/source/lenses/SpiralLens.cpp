#include "lenses/SpiralLens.h"
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
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Do0 = MIDI 12. Todo el mapeo cuelga de esta constante: `turns` se cuenta desde acá, así la parte entera
// ES la octava científica y la fraccionaria ES la clase de nota.
constexpr double kC0Hz = 16.351597831287414;

const char* kChannelNames[Cqt::kNumChannels] = { "L", "R", "M" };
}

const juce::ValueTree& SpiralLens::stateTree() const { return processor.apvts.state; }

SpiralLens::SpiralLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (8);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//======================================================================================== el mapeo
// PURO: sólo depende del bin y de los bins por octava. Con f_min = A0 = 27.5 Hz,
//     turns(k) = log2(27.5 / 16.3516) + k/B = 0.75 + k/B
// y de ahí sale todo: la octava es ⌊turns⌋ y la clase de nota es la parte fraccionaria.
SpiralLens::Position SpiralLens::positionFor (int bin, int binsPerOctave)
{
    Position p;
    const int b = juce::jmax (1, binsPerOctave);
    p.turns = std::log2 (Cqt::kFMinHz / kC0Hz) + (double) bin / (double) b;
    p.octave = (int) std::floor (p.turns);
    p.classFraction = (float) (p.turns - std::floor (p.turns));
    p.angleRad = (float) (kTwoPi * (double) p.classFraction);   // desde las 12, horario
    return p;
}

float SpiralLens::radiusForTurns (double turns) const
{
    const double span = juce::jmax (1.0e-6, turnsMax - turnsMin);
    const double t = juce::jlimit (0.0, 1.0, (turns - turnsMin) / span);
    return rInner + (float) t * (rOuter - rInner);
}

juce::Point<float> SpiralLens::pointForTurns (double turns, float classFraction) const
{
    const float r = radiusForTurns (turns);
    const float a = (float) (kTwoPi * (double) classFraction);
    return { centre.x + r * std::sin (a), centre.y - r * std::cos (a) };   // Do arriba, horario
}

//======================================================================================== geometría
SpiralLens::Zones SpiralLens::zonesFor (int w, int h) const
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

    z.keyText = body.removeFromBottom (16);
    body.removeFromBottom (th::padIn / 2);
    z.plot = body;
    z.keyText = z.keyText.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

void SpiralLens::updateGeometry()
{
    centre = zones.plot.getCentre().toFloat();

    // Se deja lugar afuera para las doce etiquetas de nota: sin ese margen, el Do de la vuelta más grande
    // se comería el borde del panel.
    const float rMax = 0.5f * (float) juce::jmin (zones.plot.getWidth(), zones.plot.getHeight()) - 22.0f;
    rOuter = juce::jmax (10.0f, rMax);
    // El disco interior es la RUEDA DE CROMA y el texto de la tonalidad: no puede ser un punto.
    rInner = juce::jmax (34.0f, rOuter * 0.30f);

    turnsMin = positionFor (0, bpoSeen).turns;
    turnsMax = positionFor (juce::jmax (1, binsSeen) - 1, bpoSeen).turns;
    const double vueltas = juce::jmax (1.0, turnsMax - turnsMin);
    ringStep = (rOuter - rInner) / (float) vueltas;
}

void SpiralLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    updateGeometry();
    Lens::resized();
}

float SpiralLens::levelAt (int bin) const
{
    if (bin < 0 || bin >= (int) dispDb.size()) return 0.0f;
    const float range = (float) juce::jmax (1, lastRangeDb);
    return juce::jlimit (0.0f, 1.0f, (dispDb[(size_t) bin] + range) / range);
}

//======================================================================================== animación
bool SpiralLens::advanceFrame()
{
    const int wantedRange = processor.spectrumSettings().rangeDb();
    if (wantedRange != lastRangeDb) { lastRangeDb = wantedRange; invalidateStatic(); }

    const auto& f = processor.cqt().read();
    const bool fresh = f.frameIndex != lastFrameIndex || f.numBins != binsSeen;

    if (fresh)
    {
        if (f.numBins != binsSeen || f.binsPerOctave != bpoSeen)
        {
            binsSeen = f.numBins;
            bpoSeen  = juce::jmax (1, f.binsPerOctave);
            fMinSeen = f.fMin > 0.0f ? f.fMin : (float) Cqt::kFMinHz;
            targetDb.assign ((size_t) juce::jmax (0, binsSeen), CqtFrame::kFloorDb);
            dispDb.assign   ((size_t) juce::jmax (0, binsSeen), CqtFrame::kFloorDb);
            updateGeometry();
            invalidateStatic();
        }
        latencySeen    = f.lowestBinLatencySec;
        lastFrameIndex = f.frameIndex;

        for (int k = 0; k < binsSeen; ++k) targetDb[(size_t) k] = f.magDb[k];
        for (int c = 0; c < CqtFrame::kNumClasses; ++c) chromaSm[c] = f.chromaSmooth[c];
        keyTonic        = f.keyTonic;
        keyMode         = f.keyMode;
        keyConfidence   = f.keyConfidence;
        keyTimeFraction = f.keyTimeFraction;
    }

    const bool reduced = prefersReducedMotion();
    bool moved = false;
    for (size_t k = 0; k < dispDb.size(); ++k)
    {
        const float target = targetDb[k], before = dispDb[k];
        dispDb[k] = (reduced || target > before) ? target : before + (target - before) * kRelease;
        moved = moved || std::abs (dispDb[k] - before) > 0.01f;
    }
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const float target = chromaSm[c], before = dispChroma[c];
        dispChroma[c] = (reduced || target > before) ? target : before + (target - before) * kRelease;
        moved = moved || std::abs (dispChroma[c] - before) > 0.002f;
    }
    return fresh || moved;
}

//======================================================================================== capa estática
void SpiralLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    updateGeometry();
    lastRangeDb = processor.spectrumSettings().rangeDb();

    // ---- los doce rayos de clase de nota, con su etiqueta afuera ----
    g.setFont (ovni::ui::fonts::label (10.0f));
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const float frac = (float) c / (float) CqtFrame::kNumClasses;
        const float a = (float) (kTwoPi * (double) frac);
        const auto from = juce::Point<float> (centre.x + rInner * std::sin (a), centre.y - rInner * std::cos (a));
        const auto to   = juce::Point<float> (centre.x + rOuter * std::sin (a), centre.y - rOuter * std::cos (a));

        g.setColour (c == 0 ? th::line : th::lineSoft);
        g.drawLine ({ from, to }, c == 0 ? 1.0f : 0.6f);

        const float rl = rOuter + 12.0f;
        const auto lp = juce::Point<float> (centre.x + rl * std::sin (a), centre.y - rl * std::cos (a));
        g.setColour (c == 0 ? th::txt : th::fnt);
        g.drawText (classNameFor (c, strings::languageOf (stateTree())), juce::Rectangle<float> (lp.x - 18.0f, lp.y - 7.0f, 36.0f, 14.0f),
                    juce::Justification::centred, false);
    }

    // ---- una circunferencia por octava, como REGLA DE RADIO ----
    // Ojo con lo que dice y lo que no: la espiral es de Arquímedes, así que una circunferencia la cruza en
    // UN solo punto — el rayo de Do. O sea que el círculo no es "la octava n": es el radio DONDE EMPIEZA
    // la octava n. Por eso el rótulo va justo ahí y no en cualquier ángulo, y va pegado a la izquierda del
    // rayo para no taparle las púas a los Do (que es donde más se miran).
    //
    // 56b — SE LEE SIEMPRE, con la misma cajita del resto. Los rótulos competían con las púas: pegados
    // al rayo del Do, el glow de las púas fuertes (2–4 px además de su grosor, desde el 56) le pasaba
    // por encima a C2…C5 en tamaño M.
    //
    // MOVERLOS NO ALCANZA, y vale anotar por qué: más a la izquierda están las púas del SI, y en la
    // bisectriz Do/Do# están las del DO#. Cerca del centro los doce rayos convergen —a la altura de C1
    // hay 30° entre vecinos y eso son pocos píxeles—, así que NINGÚN ángulo está libre; probado y
    // descartado, con capturas. Lo que sí se puede garantizar es que el rótulo se LEA: la misma caja
    // opaca que ya usan las lecturas de las otras lentes y los rótulos de tiempo del waterfall. Queda
    // una regla de radio con diez marcas, que es lo que es.
    const auto mo = look::metricsFor (getWidth());
    g.setFont (look::tabularFont (9.5f));   // 56: tabular, y un punto más grande — es un rótulo de eje
    for (int oct = (int) std::ceil (turnsMin); oct <= (int) std::floor (turnsMax); ++oct)
    {
        const float r = radiusForTurns ((double) oct);
        g.setColour (th::lineSoft);
        g.drawEllipse (centre.x - r, centre.y - r, 2.0f * r, 2.0f * r, 0.6f);
        g.setColour (th::fnt);
        const juce::String tag = "C" + juce::String (oct);
        const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag)) + 12;
        look::drawReadoutBox (g, { (int) (centre.x - 6.0f) - tw, (int) (centre.y - r - 6.0f), tw, 13 },
                              tag, mo, look::gridMajor);
    }

    // ---- LA ESPIRAL, que es la línea de base de las púas ----
    if (binsSeen > 1)
    {
        juce::Path spiral;
        const double step = 1.0 / 64.0;
        bool first = true;
        for (double t = turnsMin; t <= turnsMax + 1.0e-9; t += step)
        {
            const auto p = pointForTurns (t, (float) (t - std::floor (t)));
            if (first) { spiral.startNewSubPath (p); first = false; } else spiral.lineTo (p);
        }
        g.setColour (th::green.withAlpha (0.18f));
        g.strokePath (spiral, juce::PathStrokeType (1.0f));
    }

    // ---- el disco de la rueda de croma ----
    g.setColour (th::surf.withAlpha (0.75f));
    g.fillEllipse (centre.x - rInner, centre.y - rInner, 2.0f * rInner, 2.0f * rInner);
    g.setColour (th::lineSoft);
    g.drawEllipse (centre.x - rInner, centre.y - rInner, 2.0f * rInner, 2.0f * rInner, 1.0f);
}

//======================================================================================== capa viva
void SpiralLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); updateGeometry(); }

    // ---- LAS PÚAS. Una por bin, desde su vuelta hacia afuera. El piso NO se dibuja: un punto tenue en el
    //      ruido de fondo sería inventar una nota que nadie tocó. ----
    const float maxOut = ringStep * 0.85f;   // nunca invade la vuelta de arriba
    const auto  m = look::metricsFor (getWidth());

    // ===== 56 ===== las púas eran de 0.8 px en el piso y llegaban a 3 px: el concepto se entendía y el
    // DATO no se veía. Ahora arrancan más gruesas y las FUERTES llevan el glow de la lente (uno solo,
    // acumulado en un Path: el glow no puede ser por púa o serían 300 sombras y una mancha).
    //
    // El glow va sólo por encima de kGlowFrom: si brillara todo, no marcaría nada. Es jerarquía, no adorno.
    juce::Path strong;
    for (int k = 0; k < binsSeen; ++k)
    {
        const float t = levelAt (k);
        if (t <= 0.001f) continue;

        const auto pos = positionFor (k, bpoSeen);
        const float r0 = radiusForTurns (pos.turns);
        const float r1 = r0 + t * maxOut;
        const float sa = std::sin (pos.angleRad), ca = std::cos (pos.angleRad);

        if (t >= kGlowFrom)
        {
            strong.startNewSubPath (centre.x + r0 * sa, centre.y - r0 * ca);
            strong.lineTo (centre.x + r1 * sa, centre.y - r1 * ca);
        }
    }
    if (! strong.isEmpty())
        look::glowPath (g, strong, look::dataLine, m.dataW, m.glowRadius);

    for (int k = 0; k < binsSeen; ++k)
    {
        const float t = levelAt (k);
        if (t <= 0.001f) continue;

        const auto pos = positionFor (k, bpoSeen);
        const float r0 = radiusForTurns (pos.turns);
        const float r1 = r0 + t * maxOut;
        const float sa = std::sin (pos.angleRad), ca = std::cos (pos.angleRad);

        // 57b — la púa con GLOW barato: las mismas tres anchuras del resto del instrumento, dibujadas
        // con tres `drawLine` (que ya son antialiaseadas). Sin él, 229 púas del mismo grosor sobre una
        // espiral se leen como una maraña; con él, las fuertes se separan solas de las débiles.
        const float a = juce::jlimit (0.0f, 1.0f, 0.22f + 0.78f * t * t);
        const float w = 1.5f + 3.0f * t;
        const float x0 = centre.x + r0 * sa, y0 = centre.y - r0 * ca;
        const float x1 = centre.x + r1 * sa, y1 = centre.y - r1 * ca;
        // El glow SÓLO en las púas que valen: son 229 y la mayoría están en el piso. Con `t > 0.25` se
        // ilumina lo que se mira (los picos) y las demás siguen siendo un trazo antialiaseado, que es lo
        // que eran. Medido: con las tres pasadas en las 229, la lente pasaba de 0.51 a 2.6 ms.
        if (t > 0.25f)
        {
            g.setColour (look::dataLine.withAlpha (a * 0.14f));
            g.drawLine (x0, y0, x1, y1, w + 3.5f);
        }
        g.setColour (look::dataLine.withAlpha (a));
        g.drawLine (x0, y0, x1, y1, w);
    }

    paintWheel (g);
    paintReadout (g);

    const auto s = processor.cqtSettings();
    paintButton (g, zones.button[ctrlChannel], tr (strings::Key::channel),
                 kChannelNames[juce::jlimit (0, (int) Cqt::kNumChannels - 1, s.channel)],
                 hovered == ctrlChannel);
    paintButton (g, zones.button[ctrlChroma], tr (strings::Key::chroma),
                 juce::String (s.chromaSeconds(), 1) + " s", hovered == ctrlChroma);

    const auto libre = zones.footer.withLeft (zones.button[kNumControls - 1].getRight() + 12);
    if (libre.getWidth() > 120)
    {
        g.setColour (th::fnt);
        g.setFont (ovni::ui::fonts::label (9.0f));
        g.drawText (tr (strings::Key::bassLatency), libre.withTrimmedRight (72), juce::Justification::centredRight, false);
        g.setColour (th::mut);
        g.setFont (ovni::ui::fonts::mono (10.0f));
        g.drawText ("A0 " + juce::String::fromUTF8 ("\xc2\xb7") + " "
                        + juce::String (latencySeen > 0.0f ? latencySeen : 1.241f, 2) + " s",
                    libre, juce::Justification::centredRight, false);
    }

    // La tonalidad completa, con sus DOS números, debajo del dibujo.
    juce::String text;
    if (keyTonic >= 0)
        text = keyLabel (keyTonic, keyMode, strings::languageOf (stateTree()))
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + trLower (strings::Key::confidence) + " "
             + juce::String (keyConfidence, 2) + juce::String::fromUTF8 ("  \xc2\xb7  ")
             + juce::String (juce::roundToInt (100.0f * keyTimeFraction)) + " " + tr (strings::Key::ofTheTime);
    else
        text = tr (strings::Key::noKeyEstimated);

    g.setColour (keyTonic >= 0 ? th::txt : th::mut);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (text, zones.keyText, juce::Justification::centred, false);
}

// LA RUEDA DE CROMA cierra el círculo, literalmente: cada sector está en el MISMO ángulo que las púas de
// su clase de nota, así que la relación entre "qué notas suenan" y "qué clase pesa" se ve sin explicarla.
void SpiralLens::paintWheel (juce::Graphics& g) const
{
    // 56: la rueda se ensancha (0.62 → 0.50 del radio interior). Era un anillo fino en el centro de un
    // dibujo grande y la lectura de "qué clase pesa" se perdía frente a las púas.
    const float rOut = rInner * 0.97f, rIn = rInner * 0.50f;
    const float half = (float) (kTwoPi / (double) CqtFrame::kNumClasses * 0.5);

    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const float a = (float) (kTwoPi * (double) c / (double) CqtFrame::kNumClasses);
        const float v = juce::jlimit (0.0f, 1.0f, dispChroma[c]);

        juce::Path sector;
        sector.addPieSegment (centre.x - rOut, centre.y - rOut, 2.0f * rOut, 2.0f * rOut,
                              a - half, a + half, rIn / rOut);
        // 57b — DEGRADADO RADIAL en cada sector: más denso contra el centro de la rueda, que es donde
        // el ojo busca la tónica. Un sector de alpha plano es una porción de torta; degradado, la rueda
        // se lee como un objeto con centro.
        // Doce sectores: acá el degradado radial SÍ entra en presupuesto (son doce, no doscientos).
        const auto  hue   = (c == keyTonic ? look::caution : look::dataLine);
        const float alpha = 0.12f + 0.78f * v;
        look::radialFill (g, sector, hue, centre, rOut, alpha, alpha * 0.35f);
    }

    // Adentro, la tonalidad y su confianza. Nunca la tonalidad sola.
    const auto middle = juce::Rectangle<float> (centre.x - rIn, centre.y - rIn, 2.0f * rIn, 2.0f * rIn);
    g.setColour (keyTonic >= 0 ? th::txt : th::mut);
    // 56: LA TONALIDAD es la conclusión de esta lente — va en la display del sello y grande, no en una
    // mono del tamaño de una etiqueta de eje.
    g.setFont (ovni::ui::fonts::display (juce::jlimit (14.0f, 30.0f, rIn * 0.62f)));
    g.drawText (keyTonic >= 0 ? keyLabel (keyTonic, keyMode, strings::languageOf (stateTree()))
                          : juce::String::fromUTF8 ("\xe2\x80\x94"),
                middle.withTrimmedBottom (middle.getHeight() * 0.45f), juce::Justification::centredBottom, false);

    if (keyTonic >= 0)
    {
        g.setColour (th::mut);
        g.setFont (look::tabularFont (juce::jlimit (9.0f, 13.0f, rIn * 0.30f)));
        g.drawText (juce::String (keyConfidence, 2),
                    middle.withTrimmedTop (middle.getHeight() * 0.52f), juce::Justification::centredTop, false);
    }
}

//======================================================================================== lectura
// LA OCTAVA SALE DEL RADIO Y LA CLASE DEL ÁNGULO: es exactamente cómo se lee el dibujo, así que la lectura
// no puede decir algo distinto de lo que el ojo ve en ese punto.
SpiralLens::Readout SpiralLens::readoutAt (juce::Point<int> p) const
{
    Readout r;
    if (binsSeen <= 0 || dispDb.empty() || ! zones.plot.contains (p)) return r;

    const float dx = (float) p.x - centre.x, dy = (float) p.y - centre.y;
    const float rad = std::sqrt (dx * dx + dy * dy);
    if (rad < rInner - ringStep * 0.5f || rad > rOuter + ringStep * 0.5f) return r;

    double ang = std::atan2 ((double) dx, (double) -dy);          // 0 arriba, horario
    if (ang < 0.0) ang += kTwoPi;
    const double classFraction = ang / kTwoPi;

    const double span = juce::jmax (1.0e-6, turnsMax - turnsMin);
    const double fromRadius = turnsMin + (double) (rad - rInner) / (double) (rOuter - rInner) * span;
    const double octave = std::round (fromRadius - classFraction);
    const double turns = octave + classFraction;

    const int bin = (int) std::lround ((turns - turnsMin) * (double) bpoSeen);
    if (bin < 0 || bin >= binsSeen) return r;

    r.valid  = true;
    r.bin    = bin;
    r.octave = (int) octave;
    r.freqHz = (double) fMinSeen * std::pow (2.0, (double) bin / (double) bpoSeen);
    r.note   = noteForFrequency (r.freqHz);
    r.db     = targetDb[(size_t) bin];
    return r;
}

void SpiralLens::paintReadout (juce::Graphics& g) const
{
    if (cursor.x < 0) return;
    const auto r = readoutAt (cursor);
    if (! r.valid) return;

    // Un anillo en la púa que se está leyendo: sin él no se sabe cuál de las nueve vueltas contestó.
    const auto pos = positionFor (r.bin, bpoSeen);
    const auto pt = pointForTurns (pos.turns, pos.classFraction);
    g.setColour (th::amber.withAlpha (0.85f));
    g.drawEllipse (pt.x - 4.0f, pt.y - 4.0f, 8.0f, 8.0f, 1.2f);

    const juce::String text = r.note.name + juce::String::fromUTF8 ("  \xc2\xb7  ")
                            + juce::String (r.freqHz, r.freqHz < 100.0 ? 2 : 1) + " Hz"
                            + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (r.db, 1) + " dB";

    g.setFont (ovni::ui::fonts::mono (11.0f));
    const int tw = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16);
    const auto box = readoutBoxFor (zones.plot, cursor.x, tw);

    g.setColour (th::bg1.withAlpha (0.9f));
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (th::green.withAlpha (0.4f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (th::txt);
    g.drawText (text, box, juce::Justification::centred, false);
}

void SpiralLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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
void SpiralLens::cycleControl (int control)
{
    auto s = processor.cqtSettings();
    if (control == ctrlChannel)     s.channel = (s.channel + 1) % (int) Cqt::kNumChannels;
    else if (control == ctrlChroma) s.chromaSecIndex = (s.chromaSecIndex + 1) % Cqt::kNumChromaSecOptions;
    else return;
    processor.setCqtSettings (s);
}

void SpiralLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void SpiralLens::mouseMove (const juce::MouseEvent& e)
{
    int over = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { over = i; break; }

    const auto p = zones.plot.contains (e.getPosition()) ? e.getPosition() : juce::Point<int> (-1, -1);
    if (over != hovered || p != cursor) { hovered = over; cursor = p; repaint(); }
}

void SpiralLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}
}
