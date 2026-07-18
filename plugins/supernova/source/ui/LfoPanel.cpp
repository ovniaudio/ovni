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
// Beat divisions → beatsPerCycle.
struct Rate { const char* label; float beats; };
const Rate kRates[] = {
    { "1/16", 0.25f }, { "1/8", 0.5f }, { "1/4", 1.0f }, { "1/2", 2.0f },
    { "1 bar", 4.0f }, { "2 bars", 8.0f }, { "4 bars", 16.0f },
};
const char* kShapeNames[] = { "Sine", "Triangle", "Saw", "Square", "Ramp Down", "Sample & Hold" };
constexpr int kNumDests = (int) (sizeof (kDests) / sizeof (kDests[0]));
constexpr int kNumRates = (int) (sizeof (kRates) / sizeof (kRates[0]));
constexpr int kNumShapes = 6;

// --- layout ---------------------------------------------------------------------------------------
constexpr int kMargin     = 10;
constexpr int kMaxContentW = 1000;   // cap + centre so the cards never sprawl on a huge app window
constexpr int kMaxContentH = 560;
constexpr int kHeaderH    = 72;
constexpr int kColHeadH   = 22;
constexpr int kCardGap    = 12;
constexpr int kLeftCellW  = 96;
constexpr int kGap        = 14;
constexpr int kTargetW    = 156;
constexpr int kRateW      = 96;
constexpr int kShapeW     = 216;
constexpr int kShapeH     = 46;
constexpr int kCtrlH      = 30;

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

        r.enable.setClickingTogglesState (true);
        r.enable.setTooltip ("Enable this LFO");
        addAndMakeVisible (r.enable);

        r.target.setTextWhenNothingSelected (juce::String::fromUTF8 ("\xE2\x80\x94 target \xE2\x80\x94"));
        r.target.setTooltip ("Which world parameter this LFO modulates");
        r.target.addItem (juce::String::fromUTF8 ("\xE2\x80\x94 target \xE2\x80\x94"), 1);
        for (int d = 0; d < kNumDests; ++d) r.target.addItem (kDests[d].label, d + 2);
        addAndMakeVisible (r.target);

        r.rate.setTooltip ("Beat division — how fast the LFO cycles against the tempo");
        for (int k = 0; k < kNumRates; ++k) r.rate.addItem (kRates[k].label, k + 1);
        addAndMakeVisible (r.rate);

        addAndMakeVisible (r.shape);
        r.shape.onSelect = [this, i] (int) { pushRow (i); };

        r.depth.setSliderStyle (juce::Slider::LinearHorizontal);
        r.depth.setRange (0.0, 100.0, 1.0);
        r.depth.setTextValueSuffix (" %");
        r.depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
        r.depth.setTooltip ("Modulation depth");
        addAndMakeVisible (r.depth);

        r.enable.onClick      = [this, i] { pushRow (i); };
        r.target.onChange     = [this, i] { pushRow (i); };
        r.rate.onChange       = [this, i] { pushRow (i); };
        r.depth.onValueChange = [this, i] { pushRow (i); };
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
        int rateId = 3;   // 1/4 default
        for (int k = 0; k < kNumRates; ++k) if (std::abs (kRates[k].beats - sl.beatsPerCycle) < 0.01f) { rateId = k + 1; break; }
        r.rate.setSelectedId (rateId, juce::dontSendNotification);
        r.shape.setSelected ((int) sl.shape);
        r.depth.setValue (juce::jlimit (0.0, 100.0, sl.depth * 100.0), juce::dontSendNotification);
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
    sl.beatsPerCycle = (rateId >= 1 && rateId - 1 < kNumRates) ? kRates[rateId - 1].beats : 1.0f;
    sl.shape = (LfoShape) juce::jlimit (0, kNumShapes - 1, r.shape.selected());
    sl.depth = (float) juce::jlimit (0.0, 1.0, r.depth.getValue() / 100.0);
    sl.bipolar = true;
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
    g.drawText (juce::String::fromUTF8 ("Waves locked to the beat ADD motion to their target \xE2\x80\x94 "
                                        "knobs stay still; the orange bar is the live output."),
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

        // "LFO n" label, above the ON pill.
        auto leftCell = cardRects[(size_t) i].reduced (16, 0).removeFromLeft (kLeftCellW);
        g.setColour (on ? kHue : th::mut);
        g.setFont (fonts::display (17.0f));
        g.drawText ("LFO " + juce::String (i + 1), leftCell.removeFromTop (leftCell.getHeight() - 34),
                    juce::Justification::centredLeft);
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
            const float v    = bank.valueFor (i, beatPos());                 // [−depth, +depth]
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

    const int avail = area.getHeight() - (LfoBank::kNum - 1) * kCardGap;
    const int cardH = juce::jlimit (64, 112, avail / LfoBank::kNum);

    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        if (i) area.removeFromTop (kCardGap);
        auto card = area.removeFromTop (cardH);
        cardRects[(size_t) i] = card;

        auto row = card.reduced (16, 0);
        row.removeFromLeft (kLeftCellW);                 // "LFO n" + ON pill live here
        auto& r = rows[(size_t) i];

        // ON pill: bottom of the left cell, under the "LFO n" label.
        auto leftCell = card.reduced (16, 0).removeFromLeft (kLeftCellW);
        r.enable.setBounds (leftCell.removeFromBottom (30).withSizeKeepingCentre (58, 26));

        row.removeFromLeft (kGap);
        r.target.setBounds (row.removeFromLeft (kTargetW).withSizeKeepingCentre (kTargetW, kCtrlH));
        row.removeFromLeft (kGap);
        r.rate.setBounds   (row.removeFromLeft (kRateW).withSizeKeepingCentre (kRateW, kCtrlH));
        row.removeFromLeft (kGap);
        r.shape.setBounds  (row.removeFromLeft (kShapeW).withSizeKeepingCentre (kShapeW, kShapeH));
        row.removeFromLeft (kGap);
        r.depth.setBounds  (row.withSizeKeepingCentre (row.getWidth(), kCtrlH));

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
