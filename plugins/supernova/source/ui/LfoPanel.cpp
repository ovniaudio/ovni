#include "LfoPanel.h"
#include "params/ParameterIDs.h"
#include "ui/theme.h"          // look::hue (SUPERNOVA fire)
#include "ui-kit/Theme.h"      // ovni::ui::theme (sello palette)
#include "ui-kit/Fonts.h"
#include <cmath>

namespace supernova
{
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;
namespace pid   = params::id;

namespace {
const juce::Colour kHue = look::hue;   // SUPERNOVA incandescent orange

// Curated modulation targets (highest visual impact). value = the APVTS id.
struct Dest { const char* id; const char* label; };
const Dest kDests[] = {
    { pid::HUE, "Hue" }, { pid::SAT, "Saturation" }, { pid::INTENSITY, "Intensity" },
    { pid::PARTICLE_SIZE, "Size" }, { pid::GLOW, "Glow" }, { pid::CHAOS, "Chaos" },
    { pid::ROTATE, "Rotate" }, { pid::ORBIT, "Orbit" }, { pid::SCATTER, "Scatter" },
    { pid::TRAILS, "Trails" }, { pid::LINKS, "Links" }, { pid::DEPTH, "Depth" },
};
// Beat divisions → beatsPerCycle. Cada división trae su TRESILLO (·T, x2/3) y su PUNTILLO (·D, x1.5): en
// todo el relevamiento del nicho (Resolume, VDMX, Magic, Synesthesia, VS 2) no los tiene nadie.
struct Rate { const char* label; float beats; };
constexpr float kT = 2.0f / 3.0f, kD = 1.5f;
const Rate kRates[] = {
    { "1/16",   0.25f }, { "1/16·T",   0.25f * kT }, { "1/16·D",   0.25f * kD },
    { "1/8",    0.5f  }, { "1/8·T",    0.5f  * kT }, { "1/8·D",    0.5f  * kD },
    { "1/4",    1.0f  }, { "1/4·T",    1.0f  * kT }, { "1/4·D",    1.0f  * kD },
    { "1/2",    2.0f  }, { "1/2·T",    2.0f  * kT }, { "1/2·D",    2.0f  * kD },
    { "1 bar",  4.0f  }, { "1 bar·T",  4.0f  * kT }, { "1 bar·D",  4.0f  * kD },
    { "2 bars", 8.0f  }, { "2 bars·T", 8.0f  * kT }, { "2 bars·D", 8.0f  * kD },
    { "4 bars", 16.0f }, { "4 bars·T", 16.0f * kT }, { "4 bars·D", 16.0f * kD },
};
const char* kShapeNames[] = { "Sine", "Triangle", "Saw", "Square", "Ramp Down", "Sample & Hold" };
constexpr int kNumDests = (int) (sizeof (kDests) / sizeof (kDests[0]));
constexpr int kNumRates = (int) (sizeof (kRates) / sizeof (kRates[0]));
constexpr int kHzItemId = kNumRates + 1;   // "Hz": el ciclo lo manda el RELOJ, no el tempo
constexpr float kHzMin = 0.05f, kHzMax = 20.0f;
constexpr int kNumShapes = 6;

// --- layout ---------------------------------------------------------------------------------------
constexpr int kMargin     = 10;
constexpr int kMaxContentW = 1000;   // cap + centre so the cards never sprawl on a huge app window
constexpr int kMaxContentH = 620;   // la card lleva DOS filas de controles (polaridad/fase/retrigger)
constexpr int kHeaderH    = 72;
constexpr int kColHeadH   = 22;
constexpr int kCardGap    = 12;
constexpr int kLeftCellW  = 96;
constexpr int kGap        = 14;
constexpr int kTargetW    = 156;
constexpr int kRateW      = 112;   // entra "2 bars·D"
constexpr int kShapeW     = 216;
constexpr int kShapeH     = 46;
constexpr int kCtrlH      = 30;
// Fila 2 de la card.
constexpr int kRow2H      = 26;
constexpr int kPolarityW  = 56;
constexpr int kPhaseCapW  = 50;
constexpr int kPhaseW     = 230;
constexpr int kRetrigW    = 88;
constexpr int kHzW        = 170;

// ============================================================================ pro look (mirrors BarLnf)
struct PanelLnf final : juce::LookAndFeel_V4
{
    PanelLnf()
    {
        setColour (juce::PopupMenu::backgroundColourId, th::surf2);
        setColour (juce::PopupMenu::textColourId, th::txt);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, kHue.withAlpha (0.22f));
        setColour (juce::PopupMenu::highlightedTextColourId, th::txt);
        setColour (juce::TextButton::textColourOffId, th::txt);
        setColour (juce::TextButton::textColourOnId, kHue);
        setColour (juce::ComboBox::textColourId, th::txt);
        setColour (juce::Slider::textBoxTextColourId, th::txt);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, th::surf2);
        setColour (juce::TooltipWindow::textColourId, th::txt);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        g.setColour (on ? kHue.withAlpha (0.18f) : th::surf2);
        g.fillRoundedRectangle (r, 6.0f);
        if (down || highlighted)
        {
            g.setColour (kHue.withAlpha (down ? 0.26f : 0.10f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (on ? kHue.withAlpha (0.9f) : th::line);
        g.drawRoundedRectangle (r, 6.0f, on ? 1.4f : 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return fonts::body (12.5f); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
        g.setColour (th::surf2);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (th::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        juce::Path p;
        const float cx = (float) w - 15.0f, cy = (float) h * 0.5f;
        p.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (th::mut);
        g.fillPath (p);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return fonts::body (13.0f); }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (th::surf2.brighter (0.15f));
        g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
        g.setColour (kHue.withAlpha (0.8f));
        g.fillRoundedRectangle ((float) x, cy - 1.5f, juce::jmax (0.0f, pos - (float) x), 3.0f, 1.5f);
        g.setColour (kHue);
        g.fillEllipse (pos - 5.5f, cy - 5.5f, 11.0f, 11.0f);
    }
};
} // namespace

// ================================================================================ ShapeStrip
ShapeStrip::ShapeStrip()
{
    setInterceptsMouseClicks (true, false);
}

void ShapeStrip::setSelected (int s)
{
    sel = juce::jlimit (0, kNumShapes - 1, s);
    repaint();
}

int ShapeStrip::cellAt (juce::Point<float> p) const noexcept
{
    if (getWidth() <= 0) return 0;
    return juce::jlimit (0, kNumShapes - 1, (int) (p.x / ((float) getWidth() / kNumShapes)));
}

void ShapeStrip::drawWave (juce::Graphics& g, LfoShape shape, juce::Rectangle<float> r, juce::Colour c)
{
    juce::Path p;
    if (shape == LfoShape::SampleHold)
    {
        // Staircase of a few stable pseudo-random steps → reads as sample & hold.
        constexpr int steps = 5;
        const float sw = r.getWidth() / steps;
        for (int j = 0; j < steps; ++j)
        {
            const float v  = LfoBank::wave (LfoShape::SampleHold, (double) j);   // stable per integer
            const float py = r.getBottom() - v * r.getHeight();
            const float x0 = r.getX() + j * sw, x1 = x0 + sw;
            if (j == 0) p.startNewSubPath (x0, py);
            else        p.lineTo (x0, py);                                        // vertical connector
            p.lineTo (x1, py);
        }
    }
    else
    {
        constexpr int N = 56;
        for (int k = 0; k <= N; ++k)
        {
            const double x01 = juce::jmin (0.9999, (double) k / N);   // avoid the phase-wrap at 1.0
            const float  v   = LfoBank::wave (shape, x01);            // 0..1
            const float  px  = r.getX() + (float) x01 * r.getWidth();
            const float  py  = r.getBottom() - v * r.getHeight();
            if (k == 0) p.startNewSubPath (px, py);
            else        p.lineTo (px, py);
        }
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void ShapeStrip::paint (juce::Graphics& g)
{
    const float cw = (float) getWidth() / kNumShapes;
    for (int i = 0; i < kNumShapes; ++i)
    {
        auto cell = juce::Rectangle<float> (i * cw, 0.0f, cw, (float) getHeight()).reduced (3.0f);
        const bool isSel = (i == sel);
        const bool isHov = (i == hover);

        g.setColour (isSel ? kHue.withAlpha (0.16f) : th::surf2);
        g.fillRoundedRectangle (cell, 6.0f);
        if (isHov && ! isSel)
        {
            g.setColour (kHue.withAlpha (0.08f));
            g.fillRoundedRectangle (cell, 6.0f);
        }
        g.setColour (isSel ? kHue.withAlpha (0.9f) : th::line);
        g.drawRoundedRectangle (cell, 6.0f, isSel ? 1.5f : 1.0f);

        drawWave (g, (LfoShape) i, cell.reduced (7.0f, 8.0f),
                  isSel ? kHue : (isHov ? th::txt : th::mut));
    }
}

void ShapeStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = cellAt (e.position);
    if (h != hover) { hover = h; repaint(); }
}

void ShapeStrip::mouseExit (const juce::MouseEvent&)
{
    if (hover != -1) { hover = -1; repaint(); }
}

void ShapeStrip::mouseDown (const juce::MouseEvent& e)
{
    const int i = cellAt (e.position);
    if (i != sel) { sel = i; repaint(); if (onSelect) onSelect (i); }
    else if (onSelect) onSelect (i);
}

juce::String ShapeStrip::getTooltip()
{
    const int i = (hover >= 0 ? hover : sel);
    return juce::String (kShapeNames[juce::jlimit (0, kNumShapes - 1, i)]);
}

// ================================================================================ LfoPanel
LfoPanel::LfoPanel (LfoBank& b) : bank (b)
{
    lnf = std::make_unique<PanelLnf>();
    setLookAndFeel (lnf.get());

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    closeBtn.setTooltip ("Close (Esc)");
    addAndMakeVisible (closeBtn);

    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        auto& r = rows[(size_t) i];

        // IDs estables por control: la barra/automatización y los tests los encuentran sin depender del orden
        // de los hijos (juce::Component::findChildWithID).
        const juce::String rid = "lfo" + juce::String (i) + ".";

        r.enable.setClickingTogglesState (true);
        r.enable.setTooltip ("Enable this LFO");
        r.enable.setComponentID (rid + "enable");
        addAndMakeVisible (r.enable);

        r.target.setTextWhenNothingSelected (juce::String::fromUTF8 ("\xE2\x80\x94 target \xE2\x80\x94"));
        r.target.setTooltip ("Which world parameter this LFO modulates");
        r.target.addItem (juce::String::fromUTF8 ("\xE2\x80\x94 target \xE2\x80\x94"), 1);
        for (int d = 0; d < kNumDests; ++d) r.target.addItem (kDests[d].label, d + 2);
        r.target.setComponentID (rid + "target");
        addAndMakeVisible (r.target);

        r.rate.setTooltip ("How fast the LFO cycles: locked to the tempo, or free in Hz");
        r.rate.addSectionHeading ("SYNC");
        for (int k = 0; k < kNumRates; ++k) r.rate.addItem (juce::String::fromUTF8 (kRates[k].label), k + 1);
        r.rate.addSeparator();
        r.rate.addSectionHeading ("FREE");
        r.rate.addItem ("Hz", kHzItemId);
        r.rate.setComponentID (rid + "rate");
        addAndMakeVisible (r.rate);

        r.shape.setComponentID (rid + "shape");
        addAndMakeVisible (r.shape);
        r.shape.onSelect = [this, i] (int) { pushRow (i); };

        r.depth.setSliderStyle (juce::Slider::LinearHorizontal);
        r.depth.setRange (0.0, 100.0, 1.0);
        r.depth.setTextValueSuffix (" %");
        r.depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
        r.depth.setTooltip ("Modulation depth");
        r.depth.setComponentID (rid + "depth");
        addAndMakeVisible (r.depth);

        // --- fila 2: polaridad · fase · retrigger (los tres campos que el modelo tenía y la UI no exponía)
        r.polarity.setClickingTogglesState (true);
        r.polarity.setTooltip ("BI: swings both ways around the knob \xc2\xb7 UNI: only adds, from the knob up");
        r.polarity.setComponentID (rid + "polarity");
        addAndMakeVisible (r.polarity);

        r.phase.setSliderStyle (juce::Slider::LinearHorizontal);
        r.phase.setRange (0.0, 360.0, 1.0);
        r.phase.setTextValueSuffix (juce::String::fromUTF8 (" \xc2\xb0"));
        r.phase.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
        r.phase.setTooltip ("Where in the cycle this LFO starts");
        r.phase.setComponentID (rid + "phase");
        addAndMakeVisible (r.phase);

        r.retrig.setTooltip ("Restart the cycle NOW (phase 0 at this beat)");
        r.retrig.setComponentID (rid + "retrig");
        addAndMakeVisible (r.retrig);

        // Frecuencia libre: sólo aparece con RATE = Hz. Escala logarítmica (1 Hz al medio) — abajo del todo
        // son ondas de minutos, arriba del todo es parpadeo.
        r.hz.setSliderStyle (juce::Slider::LinearHorizontal);
        r.hz.setRange ((double) kHzMin, (double) kHzMax, 0.01);
        r.hz.setSkewFactorFromMidPoint (1.0);
        r.hz.setTextValueSuffix (" Hz");
        r.hz.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        r.hz.setTooltip ("Free rate, independent of the tempo");
        r.hz.setComponentID (rid + "hz");
        addChildComponent (r.hz);          // visible sólo en modo Hz (lo decide refreshFromBank/pushRow)

        r.enable.onClick      = [this, i] { pushRow (i); };
        r.target.onChange     = [this, i] { pushRow (i); };
        r.rate.onChange       = [this, i] { pushRow (i); };
        r.depth.onValueChange = [this, i] { pushRow (i); };
        r.polarity.onClick    = [this, i] { pushRow (i); };
        r.phase.onValueChange = [this, i] { pushRow (i); };
        r.hz.onValueChange    = [this, i] { pushRow (i); };
        r.retrig.onClick      = [this, i]
        {
            pushRow (i);                                                   // lo que muestra la fila manda
            bank.retrigger (i, beatPos != nullptr ? beatPos() : 0.0,       // …y el ciclo arranca acá
                               timeSec != nullptr ? timeSec() : 0.0);
            if (onChange) onChange();
        };
    }
    refreshFromBank();
    startTimerHz (30);   // medidor de SALIDA en vivo (repinta SOLO los meters, y solo con el panel visible)
}

LfoPanel::~LfoPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LfoPanel::timerCallback()
{
    if (! isShowing() || beatPos == nullptr) return;
    for (const auto& m : meterRects)
        if (! m.isEmpty()) repaint (m.expanded (2));
}

void LfoPanel::applyEnabledLook (int i)
{
    auto& r = rows[(size_t) i];
    const float a = r.enable.getToggleState() ? 1.0f : 0.42f;
    r.target.setAlpha (a);
    r.rate.setAlpha (a);
    r.shape.setAlpha (a);
    r.depth.setAlpha (a);
    r.polarity.setAlpha (a);
    r.phase.setAlpha (a);
    r.retrig.setAlpha (a);
    r.hz.setAlpha (a);
}

void LfoPanel::refreshFromBank()
{
    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        const auto& sl = bank.slot (i);
        auto& r = rows[(size_t) i];
        r.enable.setToggleState (sl.enabled, juce::dontSendNotification);
        int destId = 1;
        for (int d = 0; d < kNumDests; ++d) if (sl.target == kDests[d].id) { destId = d + 2; break; }
        r.target.setSelectedId (destId, juce::dontSendNotification);
        int rateId = 7;   // "1/4" por defecto
        for (int k = 0; k < kNumRates; ++k)
            if (std::abs (kRates[k].beats - sl.beatsPerCycle) < 0.005f) { rateId = k + 1; break; }
        r.rate.setSelectedId (sl.freeHz ? kHzItemId : rateId, juce::dontSendNotification);
        r.hz.setValue (juce::jlimit ((double) kHzMin, (double) kHzMax, (double) sl.hz), juce::dontSendNotification);
        r.hz.setVisible (sl.freeHz);
        r.shape.setSelected ((int) sl.shape);
        r.depth.setValue (juce::jlimit (0.0, 100.0, sl.depth * 100.0), juce::dontSendNotification);
        r.polarity.setToggleState (sl.bipolar, juce::dontSendNotification);
        r.polarity.setButtonText (sl.bipolar ? "BI" : "UNI");
        r.phase.setValue (juce::jlimit (0.0, 360.0, sl.phaseOffset * 360.0), juce::dontSendNotification);
        applyEnabledLook (i);
    }
}

void LfoPanel::pushRow (int i)
{
    auto& r = rows[(size_t) i];
    auto& sl = bank.slot (i);
    sl.enabled = r.enable.getToggleState();
    const int destId = r.target.getSelectedId();
    sl.target = (destId >= 2 && destId - 2 < kNumDests) ? kDests[destId - 2].id : "";
    const int rateId = r.rate.getSelectedId();
    sl.freeHz = (rateId == kHzItemId);
    if (! sl.freeHz)
        sl.beatsPerCycle = (rateId >= 1 && rateId - 1 < kNumRates) ? kRates[rateId - 1].beats : 1.0f;
    sl.hz = (float) juce::jlimit ((double) kHzMin, (double) kHzMax, r.hz.getValue());
    r.hz.setVisible (sl.freeHz);
    sl.shape = (LfoShape) juce::jlimit (0, kNumShapes - 1, r.shape.selected());
    sl.depth = (float) juce::jlimit (0.0, 1.0, r.depth.getValue() / 100.0);
    sl.bipolar = r.polarity.getToggleState();
    sl.phaseOffset = (float) juce::jlimit (0.0, 1.0, r.phase.getValue() / 360.0);
    r.polarity.setButtonText (sl.bipolar ? "BI" : "UNI");
    applyEnabledLook (i);
    if (onChange) onChange();
}

void LfoPanel::paint (juce::Graphics& g)
{
    g.fillAll (th::bg0);

    // Header — title + subtitle.
    g.setColour (th::txt);
    g.setFont (fonts::display (30.0f));
    g.drawText ("LFOs", contentArea.getX(), contentArea.getY() + 6, 300, 36, juce::Justification::topLeft);
    g.setColour (th::mut);
    g.setFont (fonts::body (12.5f));
    g.drawText (juce::String::fromUTF8 ("Waves \xE2\x80\x94 synced to the beat or free-running in Hz \xE2\x80\x94 "
                                        "ADD motion to their target; knobs stay still, the orange bar is the live output."),
                contentArea.getX(), contentArea.getY() + 42, contentArea.getWidth() - 120, 18,
                juce::Justification::topLeft);

    g.setColour (th::line);
    g.drawHorizontalLine (contentArea.getY() + kHeaderH - 2, (float) contentArea.getX(),
                          (float) contentArea.getRight());

    // Column headers, aligned to row 0's controls.
    g.setColour (th::fnt);
    g.setFont (fonts::mono (10.0f));
    auto head = [&g] (juce::Rectangle<int> rc, const char* t)
    { g.drawText (t, rc, juce::Justification::centredLeft); };
    head (targetHead, "TARGET");
    head (rateHead,   "RATE");
    head (shapeHead,  "SHAPE");
    head (depthHead,  "DEPTH");

    // Cards.
    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        auto card = cardRects[(size_t) i].toFloat();
        const bool on = rows[(size_t) i].enable.getToggleState();

        g.setColour (th::surf);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (on ? kHue.withAlpha (0.55f) : th::line);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        // Enabled accent — a hue bar on the card's left edge.
        if (on)
        {
            g.setColour (kHue);
            g.fillRoundedRectangle (card.getX() + 4.0f, card.getY() + 10.0f, 3.0f, card.getHeight() - 20.0f, 1.5f);
        }

        // "LFO n" label, above the ON pill (fila 1 de la celda izquierda).
        g.setColour (on ? kHue : th::mut);
        g.setFont (fonts::display (17.0f));
        g.drawText ("LFO " + juce::String (i + 1), titleRects[(size_t) i], juce::Justification::centredLeft);

        // Rótulo de la fila 2, al estilo de los encabezados de columna.
        if (! phaseCapRects[(size_t) i].isEmpty())
        {
            g.setColour (th::fnt);
            g.setFont (fonts::mono (10.0f));
            g.drawText ("PHASE", phaseCapRects[(size_t) i], juce::Justification::centredLeft);
        }
    }

    // Medidor de SALIDA en vivo — la PRUEBA de que el LFO modula: barra bipolar desde el centro con
    // valueFor() real (mismo valor que suma el editor al param). Escala completa = ±100% del rango.
    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        const auto m = meterRects[(size_t) i].toFloat();
        if (m.isEmpty()) continue;
        const auto& sl = bank.slot (i);
        g.setColour (th::surf2);
        g.fillRoundedRectangle (m, 2.0f);
        g.setColour (th::line);
        g.fillRect (m.getCentreX() - 0.5f, m.getY(), 1.0f, m.getHeight());   // tick central (0)
        if (sl.enabled && ! sl.target.empty() && beatPos != nullptr)
        {
            const float v    = bank.valueFor (i, beatPos(),                  // [−depth, +depth]
                                              timeSec != nullptr ? timeSec() : 0.0);
            const float norm = juce::jlimit (-1.0f, 1.0f, v);
            const float len  = std::abs (norm) * m.getWidth() * 0.5f;
            const float x0   = norm >= 0.0f ? m.getCentreX() : m.getCentreX() - len;
            g.setColour (kHue.withAlpha (0.92f));
            g.fillRoundedRectangle (x0, m.getY(), juce::jmax (2.0f, len), m.getHeight(), 2.0f);
        }
    }
}

void LfoPanel::resized()
{
    auto full = getLocalBounds();
    const int contentW = juce::jmin (full.getWidth()  - 2 * kMargin, kMaxContentW);
    const int contentH = juce::jmin (full.getHeight() - 2 * kMargin, kMaxContentH);
    contentArea = full.withSizeKeepingCentre (contentW, contentH);

    auto area = contentArea;
    closeBtn.setBounds (area.getRight() - 96, area.getY() + (kHeaderH - 30) / 2, 96, 30);

    area.removeFromTop (kHeaderH);
    auto colHead = area.removeFromTop (kColHeadH);

    // Alto de card y separación: la card lleva DOS filas, así que se le da todo lo que entre (78..128) y el
    // hueco entre cards cede primero. Con la ventana en su mínimo (720×480) sigue entrando sin recortarse.
    const int cardH = juce::jlimit (78, 128,
                                    (area.getHeight() - (LfoBank::kNum - 1) * kCardGap) / LfoBank::kNum);
    const int gapV  = juce::jlimit (4, kCardGap,
                                    (area.getHeight() - LfoBank::kNum * cardH) / (LfoBank::kNum - 1));

    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        if (i) area.removeFromTop (gapV);
        auto card = area.removeFromTop (cardH);
        cardRects[(size_t) i] = card;

        auto inner = card.reduced (16, 8);
        inner.removeFromBottom (8);                     // franja del medidor de salida, al pie de la card
        const int rowH1 = juce::jlimit (28, kShapeH, inner.getHeight() - kRow2H - 6);
        auto r1 = inner.removeFromTop (rowH1);
        auto r2 = inner.removeFromBottom (kRow2H);

        auto& r = rows[(size_t) i];

        // Celda izquierda (ambas filas): "LFO n" arriba (lo dibuja paint), pastilla ON abajo.
        titleRects[(size_t) i] = r1.removeFromLeft (kLeftCellW);
        auto onCell = r2.removeFromLeft (kLeftCellW);
        r.enable.setBounds (onCell.withSizeKeepingCentre (58, juce::jmin (26, onCell.getHeight())));
        r1.removeFromLeft (kGap);
        r2.removeFromLeft (kGap);

        // Fila 1 — TARGET · RATE · SHAPE · DEPTH.
        const int ctrlH = juce::jmin (kCtrlH, r1.getHeight());
        r.target.setBounds (r1.removeFromLeft (kTargetW).withSizeKeepingCentre (kTargetW, ctrlH));
        r1.removeFromLeft (kGap);
        r.rate.setBounds   (r1.removeFromLeft (kRateW).withSizeKeepingCentre (kRateW, ctrlH));
        r1.removeFromLeft (kGap);
        r.shape.setBounds  (r1.removeFromLeft (kShapeW));
        r1.removeFromLeft (kGap);
        r.depth.setBounds  (r1.withSizeKeepingCentre (r1.getWidth(), ctrlH));

        // Fila 2 — BI/UNI · PHASE · RETRIG.
        const int h2 = juce::jmin (kRow2H, r2.getHeight());
        r.polarity.setBounds (r2.removeFromLeft (kPolarityW).withSizeKeepingCentre (kPolarityW, h2));
        r2.removeFromLeft (kGap);
        phaseCapRects[(size_t) i] = r2.removeFromLeft (kPhaseCapW);
        const int phW = juce::jlimit (90, kPhaseW, r2.getWidth() - 2 * kGap - kRetrigW - kHzW);
        r.phase.setBounds (r2.removeFromLeft (phW).withSizeKeepingCentre (phW, h2));
        r2.removeFromLeft (kGap);
        r.retrig.setBounds (r2.removeFromLeft (juce::jmin (kRetrigW, juce::jmax (0, r2.getWidth())))
                              .withSizeKeepingCentre (kRetrigW, h2));
        r2.removeFromLeft (kGap);
        const int hzW = juce::jmin (kHzW, juce::jmax (0, r2.getWidth()));
        r.hz.setBounds (r2.removeFromLeft (hzW).withSizeKeepingCentre (hzW, h2));

        // Medidor de salida: franja fina al pie de la card, del inicio de TARGET al final de DEPTH.
        meterRects[(size_t) i] = { r.target.getX(), card.getBottom() - 10,
                                   r.depth.getRight() - r.target.getX(), 4 };

        if (i == 0)   // capture column-header positions from the first row
        {
            const int y = colHead.getY(), h = colHead.getHeight();
            targetHead = { r.target.getX(), y, r.target.getWidth(), h };
            rateHead   = { r.rate.getX(),   y, r.rate.getWidth(),   h };
            shapeHead  = { r.shape.getX(),  y, r.shape.getWidth(),  h };
            depthHead  = { r.depth.getX(),  y, r.depth.getWidth(),  h };
        }
    }
}
}
