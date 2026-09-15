#include "lenses/DynamicsLens.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "analysis/modules/Loudness.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr float kSmoothing = 0.25f;

juce::String fmt1 (float v, bool valid) { return valid ? juce::String (v, 1) : juce::String ("--.-"); }
}

const juce::ValueTree& DynamicsLens::stateTree() const { return processor.apvts.state; }

DynamicsLens::DynamicsLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (90);   // 3 s más de repintado: la línea de tiempo sigue corriendo aunque el número no cambie

    threshold.setKnobLookAndFeel (&knobLaf);
    threshold.getProperties().set ("hue", (int) th::green.getARGB());
    threshold.setRange ((double) Loudness::kMinClipThresholdDbtp, (double) Loudness::kMaxClipThresholdDbtp, 0.1);
    threshold.setTextValueSuffix (" dBTP");
    threshold.setValue ((double) processor.clipThresholdDbtp(), juce::dontSendNotification);
    threshold.onValueChange = [this] { processor.setClipThresholdDbtp ((float) threshold.getValue()); };
    addAndMakeVisible (threshold);

    timelineBuf.reserve ((size_t) kTimelineSeconds);
}

DynamicsLens::~DynamicsLens()
{
    threshold.setLookAndFeel (nullptr);   // el LookAndFeel muere con la lente: hay que soltarlo antes
}

//======================================================================================== geometría
DynamicsLens::Zones DynamicsLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    z.header = body.removeFromTop (juce::jmax (104, h / 5));
    z.footer = body.removeFromBottom (juce::jmax (50, h / 12));
    body.removeFromTop (th::padIn / 2);
    body.removeFromBottom (th::padIn / 2);

    z.clips = body.removeFromBottom (juce::jmax (110, body.getHeight() * 2 / 5));
    body.removeFromBottom (th::padIn / 2);
    z.histogram = body;

    // La barra del PSR vive en la mitad derecha del héroe.
    z.psrBar = z.header.reduced (th::padIn / 2).withTrimmedLeft (z.header.getWidth() * 2 / 5)
                       .withTrimmedRight (z.header.getWidth() / 5).withSizeKeepingCentre (
                           juce::jmax (60, z.header.getWidth() * 2 / 5 - th::padIn), 30);

    // Abajo: contador · knob · línea de tiempo.
    auto clips = z.clips.reduced (th::padIn / 2);
    clips.removeFromTop (16);                                  // el rótulo lo pone la capa estática
    auto left = clips.removeFromLeft (juce::jmax (200, clips.getWidth() / 3));
    z.knob = left.removeFromRight (juce::jmin (86, left.getWidth() / 2))
                 .withSizeKeepingCentre (juce::jmin (86, left.getWidth() / 2), juce::jmin (96, left.getHeight()));
    clips.removeFromLeft (th::padIn / 2);
    z.timeline = clips;

    auto btns = z.footer.withTrimmedLeft (z.footer.getWidth() * 3 / 5);
    const int bw = juce::jmin (110, btns.getWidth() / 2 - 8);
    z.pauseBtn = btns.removeFromRight (bw).reduced (0, 10);
    btns.removeFromRight (10);
    z.resetBtn = btns.removeFromRight (bw).reduced (0, 10);

    return z;
}

void DynamicsLens::resized()
{
    ovni::ui::VisualizerBase::resized();   // invalida la capa estática (rehornea con la nueva escala)
    zones = zonesFor (getWidth(), getHeight());
    threshold.setBounds (zones.knob);
}

//======================================================================================== capa estática
void DynamicsLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);

    const auto surface = [&g] (juce::Rectangle<int> r, float alpha)
    {
        g.setColour (th::surf.withAlpha (alpha));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (look::gridMinor);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    };
    surface (zones.header,    0.75f);
    surface (zones.histogram, 0.55f);
    surface (zones.clips,     0.55f);
    surface (zones.footer,    0.45f);

    // ---- rejilla del histograma: marcas cada 6 LU ----
    const auto hist = zones.histogram.reduced (th::padIn / 2).withTrimmedTop (18).withTrimmedBottom (14);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (int lufs = kBinsFloorLufs; lufs <= 0; lufs += 6)
    {
        const float t = (float) (lufs - kBinsFloorLufs) / (float) (-kBinsFloorLufs);
        const int   x = hist.getX() + juce::roundToInt (t * (float) hist.getWidth());
        g.setColour (look::gridMinor);
        look::fillSnapped (g, { (float) (x), (float) (hist.getY()), 1.0f, (float) (hist.getHeight()) });
        g.setColour (th::fnt);
        g.drawText (juce::String (lufs), x - 14, hist.getBottom() + 1, 28, 12, juce::Justification::centred, false);
    }

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (tr (strings::Key::histogramShortTerm),
                hist.getX(), zones.histogram.getY() + 6, hist.getWidth(), 14, juce::Justification::left, false);
    g.drawText (tr (strings::Key::clips) + juce::String::fromUTF8 (" \xc2\xb7 ")
                    + tr (strings::Key::lastMinutes),
                zones.clips.getX() + th::padIn / 2, zones.clips.getY() + 6,
                zones.clips.getWidth(), 14, juce::Justification::left, false);
}

//======================================================================================== capa viva
void DynamicsLens::paintLive (juce::Graphics& g)
{
    if (zones.header.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); threshold.setBounds (zones.knob); }

    paintHero (g);
    paintHistogram (g);
    paintClips (g);

    paintButton (g, zones.resetBtn, tr (strings::Key::reset), false, hovered == 0);
    paintButton (g, zones.pauseBtn, tr (processor.isAnalysisPaused() ? strings::Key::resume : strings::Key::pause),
                 processor.isAnalysisPaused(), hovered == 1);
}

void DynamicsLens::paintHero (juce::Graphics& g) const
{
    const auto hue = th::green;
    auto head = zones.header.reduced (th::padIn / 2);

    // ---- PSR, número héroe ----
    auto hero = head.removeFromLeft (head.getWidth() * 2 / 5);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("PSR", hero.getX(), hero.getY(), hero.getWidth(), 12, juce::Justification::left, false);

    const auto heroSize = (float) juce::jmin (54, hero.getHeight() - 34);
    const auto psrText  = fmt1 (dispPsr, psrValid);
    g.setColour (psrValid ? th::txt : th::mut);
    g.setFont (ovni::ui::fonts::mono (heroSize));
    const auto textW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), psrText)) + 4;
    const int  textH = juce::roundToInt (heroSize * 1.2f);
    g.drawText (psrText, hero.getX(), hero.getY() + 14, textW, textH, juce::Justification::left, false);

    // La unidad PEGADA al número (como en LOUDNESS): suelta en el medio del panel no se lee como su unidad.
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (juce::jmax (9.0f, heroSize * 0.28f)));
    g.drawText ("dB", hero.getX() + textW + 6, hero.getY() + 14 + textH - 16, 40, 14,
                juce::Justification::left, false);

    // ---- la barra 0…20 dB con la línea de referencia en 8 ----
    const auto bar = zones.psrBar;
    g.setColour (th::bg1);
    g.fillRect (bar);
    g.setColour (look::gridMinor);
    g.drawRect (bar, 1);

    if (psrValid)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (dispPsr - kPsrMin) / (kPsrMax - kPsrMin));
        // ===== 56: ZONAS DE COLOR =====
        // La barra era del mismo verde con 2 dB de PSR que con 18. Ahora el color dice de qué lado de la
        // referencia dinámica del spec §5.8 está: alerta bien apretado, ámbar acercándose, verde con aire.
        // Sigue siendo REFERENCIA y no veredicto — el número no se colorea, se colorea la barra.
        const auto zone = dispPsr < kPsrAlert  ? look::alert
                        : dispPsr < kPsrReference ? look::caution
                                                  : look::dataLine;
        g.setGradientFill (juce::ColourGradient (zone.withAlpha (0.45f), (float) bar.getX(), 0.0f,
                                                 zone.withAlpha (0.92f),
                                                 (float) bar.getX() + t * (float) bar.getWidth(), 0.0f, false));
        const float fw = t * (float) (bar.getWidth() - 2);
        g.fillRect (juce::Rectangle<float> ((float) bar.getX() + 1.0f, (float) bar.getY() + 1.0f, fw,
                                            (float) bar.getHeight() - 2.0f));
        // 57b — EL FILO. El degradado dice "de acá para allá hay"; el filo dice CUÁNTO, que es el número
        // que se lee de reojo mientras se mezcla.
        g.setColour (zone.withAlpha (0.95f));
        g.fillRect (juce::Rectangle<float> ((float) bar.getX() + 1.0f + juce::jmax (0.0f, fw - 2.0f),
                                            (float) bar.getY() + 1.0f, 2.0f, (float) bar.getHeight() - 2.0f));
    }

    // REFERENCIA, no veredicto: la línea del spec §5.8 dibujada y ROTULADA, sin colorear el número.
    const float rt = (kPsrReference - kPsrMin) / (kPsrMax - kPsrMin);
    const int   rx = bar.getX() + juce::roundToInt (rt * (float) bar.getWidth());
    g.setColour (th::amber.withAlpha (0.85f));
    look::fillSnapped (g, { (float) (rx), (float) (bar.getY() - 3), 1.0f, (float) (bar.getHeight() + 6) });
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.setColour (th::amber.withAlpha (0.9f));
    g.drawText (tr (strings::Key::dynamicRefShort) + juce::String (kPsrReference, 0) + " dB", rx - 60, bar.getBottom() + 5, 130, 12,
                juce::Justification::centred, false);

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    g.drawText ("0",  bar.getX(), bar.getY() - 14, 20, 12, juce::Justification::left, false);
    g.drawText ("20", bar.getRight() - 20, bar.getY() - 14, 20, 12, juce::Justification::right, false);

    // ---- PLR, secundario ----
    auto side = zones.header.reduced (th::padIn / 2);
    side = side.withTrimmedLeft (side.getWidth() * 4 / 5);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("PLR", side.getX(), side.getY(), side.getWidth(), 12, juce::Justification::left, false);
    g.setColour (plrValid ? th::txt : th::mut);
    g.setFont (ovni::ui::fonts::mono (24.0f));
    g.drawText (fmt1 (plr, plrValid), side.getX(), side.getY() + 16, side.getWidth(), 30,
                juce::Justification::topLeft, false);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.drawFittedText (tr (strings::Key::plrSinceReset),
                      side.getX(), side.getY() + 48, side.getWidth(), 28, juce::Justification::topLeft, 2);
}

void DynamicsLens::paintHistogram (juce::Graphics& g) const
{
    const auto hist = zones.histogram.reduced (th::padIn / 2).withTrimmedTop (18).withTrimmedBottom (14);
    if (hist.isEmpty() || histMax == 0) return;

    const auto hue = th::green;
    const float barW = (float) hist.getWidth() / (float) kBins;

    for (int i = 0; i < kBins; ++i)
    {
        if (histogram[i] == 0) continue;
        const float t = (float) histogram[i] / (float) histMax;
        const int   h = juce::jmax (1, juce::roundToInt (t * (float) hist.getHeight()));
        const int   x = hist.getX() + juce::roundToInt ((float) i * barW);
        const int   w = juce::jmax (1, juce::roundToInt (barW) - 1);

        // 56: el bin ACTUAL con el acento (es "dónde está el tema ahora") y el resto del histograma con
        // el relleno del sistema visual, que pesa lo justo para leerse como distribución y no competir.
        // 57b — barra antialiaseada con DEGRADADO y tapa brillante (look::dataBar): un histograma de
        // rectángulos planos se lee como una pared; con la tapa, cada barra tiene un filo que ES su valor.
        look::dataBar (g, juce::Rectangle<float> ((float) x, (float) (hist.getBottom() - h),
                                                  (float) w, (float) h),
                       i == currentBin ? look::accent : hue);
    }

    // El bin ACTUAL, marcado aunque todavía no tenga altura: es donde está el tema ahora mismo.
    if (currentBin >= 0)
    {
        const int x = hist.getX() + juce::roundToInt ((float) currentBin * barW);
        const float bw = (float) juce::jmax (1, juce::roundToInt (barW));
        g.setColour (look::accent.withAlpha (0.25f));
        g.fillRect (juce::Rectangle<float> ((float) x - 1.5f, (float) hist.getY() - 6.0f, bw + 3.0f, 6.5f));
        g.setColour (look::accent);
        g.fillRect (juce::Rectangle<float> ((float) x, (float) hist.getY() - 4.0f, bw, 3.0f));
    }
}

void DynamicsLens::paintClips (juce::Graphics& g) const
{
    const auto hue = th::green;
    auto area = zones.clips.reduced (th::padIn / 2);
    area.removeFromTop (16);

    // ---- contador ----
    auto counter = area.removeFromLeft (juce::jmax (200, area.getWidth() / 3) - zones.knob.getWidth() - th::padIn / 2);
    g.setColour (clipEvents > 0 ? th::red : th::txt);
    g.setFont (ovni::ui::fonts::mono ((float) juce::jmin (40, counter.getHeight() - 24)));
    g.drawText (juce::String ((int) clipEvents), counter.getX(), counter.getY(), counter.getWidth(),
                counter.getHeight() - 18, juce::Justification::topLeft, false);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.drawText (tr (strings::Key::eventsAbove) + juce::String (processor.clipThresholdDbtp(), 1)
                    + " dBTP",
                counter.getX(), counter.getBottom() - 16, counter.getWidth(), 14,
                juce::Justification::left, false);

    // ---- línea de tiempo: 10 min, el ahora a la DERECHA (misma convención que la historia de LOUDNESS) ----
    const auto tl = zones.timeline;
    if (tl.isEmpty()) return;

    g.setColour (th::bg1);
    g.fillRect (tl);
    g.setColour (look::gridMinor);
    g.drawRect (tl, 1);

    for (int min = 2; min <= 10; min += 2)
    {
        const int x = tl.getRight() - juce::roundToInt ((float) min / 10.0f * (float) tl.getWidth());
        g.setColour (look::gridMinor);
        look::fillSnapped (g, { (float) (x), (float) (tl.getY() + 1), 1.0f, (float) (tl.getHeight() - 2) });
        g.setColour (th::fnt);
        g.setFont (ovni::ui::fonts::mono (8.5f));
        g.drawText ("-" + juce::String (min), x + 3, tl.getBottom() - 12, 24, 11, juce::Justification::left, false);
    }

    const auto n = (int) timelineBuf.size();
    for (int i = 0; i < n; ++i)
    {
        if (timelineBuf[(size_t) i] == 0u) continue;
        const float age = (float) (n - 1 - i);                        // segundos hacia atrás desde el ahora
        const int   x   = tl.getRight() - juce::roundToInt (age / (float) kTimelineSeconds * (float) tl.getWidth());
        if (x < tl.getX()) continue;
        // 57b — con HALO: un segundo mide menos de un píxel, así que el clip es un trazo de 2 px que en
        // una línea de tiempo de diez minutos se pierde. El halo lo hace encontrable sin agrandar el dato.
        g.setColour (look::alert.withAlpha (0.20f));
        g.fillRect (juce::Rectangle<float> ((float) x - 2.5f, (float) tl.getY() + 1.0f, 5.0f,
                                            (float) tl.getHeight() - 2.0f));
        g.setColour (look::alert.withAlpha (0.95f));
        g.fillRect (juce::Rectangle<float> ((float) x - 1.0f, (float) tl.getY() + 2.0f, 2.0f,
                                            (float) tl.getHeight() - 4.0f));
    }

    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.5f));
    g.drawText (trLower (strings::Key::threshold), zones.knob.getX(), zones.knob.getY() - 13,
                zones.knob.getWidth(), 12, juce::Justification::centred, false);
    juce::ignoreUnused (hue);
}

void DynamicsLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
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
    g.setFont (ovni::ui::fonts::label (11.0f));
    g.drawText (text, area, juce::Justification::centred, false);
}

//======================================================================================== animación
bool DynamicsLens::advanceFrame()
{
    const auto& f = processor.analysis().read();
    latest     = f.loudness;
    psr        = f.psr;
    plr        = f.plr;
    psrValid   = f.psrValid;
    plrValid   = f.plrValid;
    clipEvents = f.clipEvents;

    histMax = 1;
    juce::uint32 histSum = 0;
    for (int i = 0; i < kBins; ++i)
    {
        histogram[i] = f.histogram[i];
        histMax  = juce::jmax (histMax, histogram[i]);
        histSum += histogram[i];   // Σ bins = short-terms medidos: cambia en cuanto entra uno nuevo
    }

    currentBin = (f.loudness.shortTerm > (float) kBinsFloorLufs - 0.5f && f.loudness.shortTerm < 1.0f)
                   ? juce::jlimit (0, kBins - 1, juce::roundToInt (f.loudness.shortTerm) - kBinsFloorLufs)
                   : -1;

    processor.clipHistory().copyLatest (timelineBuf, kTimelineSeconds);

    const float before = dispPsr;
    if (! psrValid)                    dispPsr = 0.0f;
    else if (prefersReducedMotion())   dispPsr = psr;
    else                               dispPsr += (psr - dispPsr) * kSmoothing;

    // La lente dibuja CUATRO cosas y sólo una de ellas es el PSR. Mirar sólo el PSR para decidir si hay
    // que repintar deja un agujero real: con una señal casi periódica el PSR converge a un valor fijo, el
    // chasis pausa el repaint a los `settleHold` frames, y un clip nuevo (o un short-term que cae en otro
    // bin, o un PLR que se mueve) no se vería hasta que el PSR volviera a moverse (LOW del revisor del 49).
    bool changed = std::abs (dispPsr - before) > 1.0e-3f;
    changed |= (clipEvents != lastClipEvents);
    changed |= (histSum    != lastHistSum);
    changed |= (currentBin != lastBin);
    changed |= (std::abs (plr - lastPlr) > 1.0e-3f) || (plrValid != lastPlrValid);

    lastClipEvents = clipEvents;
    lastHistSum    = histSum;
    lastBin        = currentBin;
    lastPlr        = plr;
    lastPlrValid   = plrValid;
    return changed;
}

//======================================================================================== interacción
void DynamicsLens::mouseDown (const juce::MouseEvent& e)
{
    if (zones.resetBtn.contains (e.getPosition()))      processor.resetAnalysis();
    else if (zones.pauseBtn.contains (e.getPosition())) processor.setAnalysisPaused (! processor.isAnalysisPaused());
    else return;
    repaint();
}

void DynamicsLens::mouseMove (const juce::MouseEvent& e)
{
    const int was = hovered;
    hovered = zones.resetBtn.contains (e.getPosition()) ? 0
            : (zones.pauseBtn.contains (e.getPosition()) ? 1 : -1);
    if (hovered != was) repaint();
}

void DynamicsLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}
}
