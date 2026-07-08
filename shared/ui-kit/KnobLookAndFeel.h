#pragma once
#include "Theme.h"
#include "Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace ovni::ui
{
// Knob dimensional (la pieza anti-IA del sello): cuerpo metálico (radial-gradient con luz arriba +
// sombra + bisel) + anillo de ticks, todo HORNEADO a juce::Image (cacheado por tamaño físico =
// crisp en Retina). El arco de valor con glow + el puntero se dibujan EN VIVO (barato).
// Hue por-slider vía slider.getProperties()["hue"] (= color de FAMILIA del plugin):
//   slider.getProperties().set ("hue", (int) theme::cyan.getARGB());   // o magenta / green / amber
class KnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KnobLookAndFeel() = default;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;

    // el valor (textbox del slider) en mono del sello
    juce::Font getLabelFont (juce::Label& label) override
    {
        return fonts::mono (juce::jmax (9.0f, (float) label.getHeight() * 0.74f));
    }

private:
    const juce::Image& bodyFor (int physDiameter);     // cuerpo + ticks cacheado (por px físicos)
    std::map<int, juce::Image> bodyCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KnobLookAndFeel)
};
}
