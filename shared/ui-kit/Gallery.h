#pragma once
#include "Theme.h"
#include "Fonts.h"
#include "KnobLookAndFeel.h"
#include "OutputMeter.h"
#include "Controls.h"
#include "Panel.h"
#include "VisualizerBase.h"

namespace ovni::ui
{
// =============================================================================
// GALLERY — superficie "kitchen-sink" del UI-kit. Muestra a la vez Panel + marca/power del header +
// knobs (con hue de familia) + un VisualizerBase real (DemoOrbit) + SegControl + ToggleButton +
// RailSlider + OutputMeter. Sirve para: (1) el snapshot de QA del sello, (2) doc/demo del kit.
// Necesita un APVTS con un AudioParameterChoice (segParamID) y un AudioParameterBool (toggleParamID),
// más dos atomics de pico/clip que alimentan el meter.
// =============================================================================

// --- Demo de VisualizerBase: campo con anillos (estático) + orbe que gira (vivo). --------------
class DemoOrbit : public VisualizerBase
{
public:
    DemoOrbit() : VisualizerBase (30) {}
    void prime (float turn) noexcept { phase = turn; }   // fijar la fase para un snapshot estable

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override
    {
        const float cx = w * 0.5f, cy = h * 0.5f, R = (float) juce::jmin (w, h) * 0.44f;
        juce::ColourGradient well (juce::Colour (0xf60a1626), cx, cy,
                                   juce::Colour (0x00081320), cx, cy - R, true);
        well.addColour (0.55, juce::Colour (0x66081320));
        g.setGradientFill (well);
        g.fillEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float rr = R * (0.40f + 0.28f * (float) i);
            g.setColour (theme::cyan.withAlpha (0.12f - 0.025f * (float) i));
            g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 1.2f);
        }
        g.setColour (theme::fnt);
        g.setFont (fonts::mono (juce::jmax (9.0f, R * 0.058f)));
        g.drawText ("FIELD", 12, 9, 90, 14, juce::Justification::centredLeft);
    }

    void paintLive (juce::Graphics& g) override
    {
        const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
        const float R  = (float) juce::jmin (getWidth(), getHeight()) * 0.44f * 0.82f;
        const float a  = phase * juce::MathConstants<float>::twoPi;
        const float px = cx + R * std::sin (a), py = cy - R * std::cos (a);

        for (int i = 1; i < 20; ++i)                 // estela que se afina y desvanece
        {
            const float aa = a - (float) i * 0.075f, t = (float) i / 20.0f;
            g.setColour (theme::cyan.withAlpha ((1.0f - t) * 0.45f));
            const float s = juce::jmap (t, 0.0f, 1.0f, 3.4f, 0.6f);
            g.fillEllipse (cx + R * std::sin (aa) - s, cy - R * std::cos (aa) - s, s * 2.0f, s * 2.0f);
        }
        g.setColour (theme::cyan.withAlpha (0.40f));
        g.fillEllipse (px - 11.0f, py - 11.0f, 22.0f, 22.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
    }

    bool advanceFrame() override { phase += 0.0045f; if (phase > 1.0f) phase -= 1.0f; return true; }

private:
    float phase = 0.13f;
};

//================================================================================================
class Gallery : public juce::Component
{
public:
    Gallery (juce::AudioProcessorValueTreeState& apvts, std::atomic<float>& peak, std::atomic<float>& clip,
             juce::String segParamID, juce::String toggleParamID)
        : seg (apvts, segParamID, { "CIRCLE", "SPIRAL", "PENDULUM" }),
          toggle (apvts, toggleParamID, "IN PHASE", "MONO-SAFE", theme::cyan),
          meter (peak, clip)
    {
        for (auto* k : { &knobA, &knobB, &knobC })
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setRange (0.0, 1.0, 0.0);
            k->setLookAndFeel (&knobLaf);
            k->setNumDecimalPlacesToDisplay (2);
            k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
            addAndMakeVisible (*k);
        }
        knobA.getProperties().set ("hue", (int) theme::cyan.getARGB());     // Movimiento
        knobB.getProperties().set ("hue", (int) theme::magenta.getARGB());  // Espacio
        knobC.getProperties().set ("hue", (int) theme::green.getARGB());    // Espectral
        knobA.setValue (0.62); knobB.setValue (0.38); knobC.setValue (0.5);

        rail.setSliderStyle (juce::Slider::LinearHorizontal);
        rail.setRange (0.0, 1.0, 0.0);
        rail.setLookAndFeel (&railLaf);
        rail.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        rail.setValue (0.74);
        addAndMakeVisible (rail);

        seg.setHue (theme::cyan);
        addAndMakeVisible (seg);
        addAndMakeVisible (toggle);
        addAndMakeVisible (meter);
        addAndMakeVisible (orbit);
    }

    ~Gallery() override
    {
        for (auto* k : { &knobA, &knobB, &knobC }) k->setLookAndFeel (nullptr);
        rail.setLookAndFeel (nullptr);
    }

    void primeForSnapshot()             // estado fijo (el snapshot no corre los timers)
    {
        orbit.prime (0.13f);
        meter.prime (0.66f, 0.18f);
        meter.setSampleRate (48000.0);
    }

    void paint (juce::Graphics& g) override
    {
        const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
        if (panel.needsRender (getWidth(), getHeight(), scale))
            panel.render (getWidth(), getHeight(), scale);
        panel.paint (g);

        // header: marca PORTAL + wordmark + designación mono + power
        auto h = getLocalBounds().removeFromTop (58).reduced (18, 0);
        paintBrandMark (g, h.removeFromLeft (30).toFloat().reduced (2.0f));
        auto tx = h.removeFromLeft (220);
        g.setColour (theme::txt);  g.setFont (fonts::display (22.0f));
        g.drawText ("OVNI", tx.removeFromTop (34).withTrimmedLeft (8).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (theme::mut);  g.setFont (fonts::mono (10.5f));
        g.drawText (juce::String::fromUTF8 ("UI-KIT · GALLERY"), tx.withTrimmedLeft (8).toNearestInt(), juce::Justification::topLeft);
        paintPowerIcon (g, h.removeFromRight (38).toFloat().withSizeKeepingCentre (38, 30), theme::cyan);
        g.setColour (theme::line);
        g.drawHorizontalLine (58, 0.0f, (float) getWidth());

        // labels de sección
        g.setColour (theme::fnt); g.setFont (fonts::mono (9.5f));
        g.drawText ("SHAPE",   segLabelArea,   juce::Justification::centredLeft);
        g.drawText ("DRY/WET", railLabelArea,  juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (58 + 14);
        auto left = r.removeFromLeft (300).reduced (18, 4);

        auto knobRow = left.removeFromTop (118);
        const int kw = knobRow.getWidth() / 3;
        knobA.setBounds (knobRow.removeFromLeft (kw).reduced (6));
        knobB.setBounds (knobRow.removeFromLeft (kw).reduced (6));
        knobC.setBounds (knobRow.reduced (6));

        left.removeFromTop (10);
        segLabelArea = left.removeFromTop (14);
        seg.setBounds (left.removeFromTop (40));
        left.removeFromTop (12);
        railLabelArea = left.removeFromTop (14);
        rail.setBounds (left.removeFromTop (26).reduced (2, 0));
        left.removeFromTop (12);
        toggle.setBounds (left.removeFromTop (46));

        auto right = r.reduced (18, 4);
        meter.setBounds (right.removeFromBottom (86));
        right.removeFromBottom (10);
        orbit.setBounds (right);   // el visualizador toma el resto (cuadrado-ish)
    }

private:
    Panel panel;
    KnobLookAndFeel knobLaf;
    RailSliderLAF   railLaf;
    juce::Slider knobA, knobB, knobC, rail;
    SegControl   seg;
    ToggleButton toggle;
    OutputMeter  meter;
    DemoOrbit    orbit;
    juce::Rectangle<int> segLabelArea, railLabelArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Gallery)
};
}
