#pragma once
#include "Theme.h"
#include "Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace ovni::ui
{
// ================================================================================================
// BottomBar — la franja contextual del sello (doctrina interaction-grammar §"Bottom-bar, no labels por
// knob"). Una tira delgada (theme::barH) abajo del cuerpo que muestra el NOMBRE + VALOR del control bajo
// el cursor → respeta "pocas perillas afuera" y unifica el feel de los 6 plugins. En reposo muestra un
// hint tenue. El hue de familia tiñe el separador y el acento. Sólo dibujo barato (texto + 1 hairline).
// ================================================================================================
class BottomBar : public juce::Component
{
public:
    BottomBar() { setInterceptsMouseClicks (false, false); }   // es lectura: nunca roba clicks

    void setHue (juce::Colour h)        { hue = h; repaint(); }
    void setIdleHint (juce::String s)   { idle = std::move (s); if (name.isEmpty()) repaint(); }
    void setSignature (juce::String s)  { sig = std::move (s); if (name.isEmpty()) repaint(); }   // mono tenue a la derecha en reposo (mockup: "TRANSMISIÓN ESTABLE")

    // El editor lo llama cuando el cursor entra/se mueve sobre un control.
    void show (const juce::String& controlName, const juce::String& valueText)
    {
        if (controlName == name && valueText == value) return;
        name = controlName; value = valueText;
        repaint();
    }
    // El editor lo llama cuando el cursor sale del control (vuelve al hint).
    void clear()
    {
        if (name.isEmpty() && value.isEmpty()) return;
        name.clear(); value.clear();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // hairline superior teñida de familia (ancla la barra al cuerpo, look "instrumento").
        g.setColour (hue.withAlpha (0.22f));
        g.fillRect (b.removeFromTop (1.0f));

        auto row = b.reduced (4.0f, 2.0f);
        const float fh = juce::jlimit (9.0f, 12.0f, row.getHeight() * 0.7f);

        if (name.isEmpty())
        {
            // reposo: hint tenue, en mono, a la izquierda (no grita) + firma a la derecha.
            g.setColour (theme::fnt);
            g.setFont (fonts::mono (fh));
            g.drawText (idle, row.toNearestInt(), juce::Justification::centredLeft);
            if (sig.isNotEmpty())
            {
                g.setColour (theme::fnt.withAlpha (0.55f));
                g.setFont (fonts::mono (fh - 1.0f).withExtraKerningFactor (0.18f));
                g.drawText (sig, row.toNearestInt(), juce::Justification::centredRight);
            }
            return;
        }

        // marcador de familia + NOMBRE (label) a la izquierda · VALOR (mono) a la derecha.
        auto dot = row.removeFromLeft (row.getHeight()).withSizeKeepingCentre (5.0f, 5.0f);
        g.setColour (hue);
        g.fillEllipse (dot);

        g.setColour (theme::txt);
        g.setFont (fonts::label (fh));
        const float nameW = juce::jmin (row.getWidth() * 0.5f, 220.0f);
        g.drawText (name, row.removeFromLeft (nameW).toNearestInt(), juce::Justification::centredLeft);

        g.setColour (hue.brighter (0.10f));
        g.setFont (fonts::mono (fh));
        g.drawText (value, row.toNearestInt(), juce::Justification::centredRight);
    }

private:
    juce::Colour hue = theme::cyan;
    juce::String idle, sig, name, value;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BottomBar)
};
}
