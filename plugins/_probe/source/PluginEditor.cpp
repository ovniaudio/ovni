#include "PluginEditor.h"

using namespace ovni::ui;

PluginEditor::PluginEditor (PluginProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("MOV·01")),   // designación tras el wordmark
      meter (p.uiOutPeak, p.uiClip)                                    // meter desacoplado: lee los atomics
{
    mixKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    mixKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 18);
    mixKnob.getProperties().set ("hue", (int) theme::cyan.getARGB());  // hue de familia MOVIMIENTO
    mixKnob.setLookAndFeel (&knobLAF);
    addToCanvas (mixKnob);

    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);
    addToCanvas (meter);

    mixAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.apvts, "mix", mixKnob);

    setBaseSize (560, 400);   // cada plugin define su tamaño de diseño (coords base)
}

PluginEditor::~PluginEditor()
{
    mixKnob.setLookAndFeel (nullptr);   // soltar el L&F antes de que se destruya
}

void PluginEditor::layoutBody (juce::Rectangle<int> body)
{
    auto b = body.reduced (28, 20);
    meter.setBounds (b.removeFromBottom (74));
    b.removeFromBottom (16);
    captionArea = b.removeFromTop (18);
    const int kd = 152;
    knobArea = b.withSizeKeepingCentre (kd, kd + 24);   // +24 para el textbox del valor
    mixKnob.setBounds (knobArea);
}

void PluginEditor::paintBody (juce::Graphics& g)
{
    g.setColour (theme::mut);
    g.setFont (fonts::mono (11.5f));
    g.drawText (juce::String::fromUTF8 ("MIX · SPREAD"), captionArea, juce::Justification::centred);
}
