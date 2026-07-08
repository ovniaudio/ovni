#pragma once
#include "Theme.h"
#include "Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>   // APVTS (binding por string, desacoplado del processor)
#include <functional>

namespace ovni::ui
{
//================================================================================================
// MARCA del sello (PORTAL): órbita inclinada con gradiente cian→violeta + núcleo blanco + satélite.
// El glyph que aparece en el wordmark del header de todos los plugins.
inline void paintBrandMark (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());

    juce::Path orb;
    orb.addEllipse (c.x - s * 0.42f, c.y - s * 0.18f, s * 0.84f, s * 0.36f);
    juce::Path ring;
    juce::PathStrokeType (s * 0.095f).createStrokedPath (ring, orb);
    ring.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-22.0f), c.x, c.y));
    juce::ColourGradient gg (theme::cyan, c.x - s * 0.4f, c.y - s * 0.4f,
                             juce::Colour (0xffc084fc), c.x + s * 0.4f, c.y + s * 0.4f, false);
    g.setGradientFill (gg);
    g.fillPath (ring);

    g.setColour (juce::Colours::white);
    g.fillEllipse (c.x - s * 0.135f, c.y - s * 0.135f, s * 0.27f, s * 0.27f);
    g.setColour (theme::cyan);
    g.fillEllipse (c.x + s * 0.27f, c.y - s * 0.33f, s * 0.15f, s * 0.15f);
}

//================================================================================================
// Ícono de POWER (el bypass del header, M5). El EDITOR decide el color y lo pasa (convención:
// cian = activo / rojo = bypasseado). Dibuja recuadro hairline + glyph power en `col`. Si el editor
// quiere tinte de fondo (estado bypasseado) lo pinta él antes de llamar.
// Firma acordada con S3 (PluginEditorBase la llama así) — ver docs/CONTRACT-CHANGES.md.
inline void paintPowerIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
{
    g.setColour (theme::line); g.drawRoundedRectangle (area, 5.0f, 1.0f);

    const auto  c  = area.getCentre();
    const float rr = juce::jmax (4.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.20f);
    juce::Path pw;
    pw.addCentredArc (c.x, c.y + 1.0f, rr, rr, 0.0f,
                      juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
    g.setColour (col);
    g.strokePath (pw, juce::PathStrokeType (1.7f));
    g.drawLine (c.x, c.y - rr + 1.0f, c.x, c.y + 1.5f, 1.7f);   // línea vertical del símbolo power
}

//================================================================================================
// Control SEGMENTADO ligado a un AudioParameterChoice por string (refleja automatización; click setea).
// Texto (labels) o íconos (vía customDraw). Hue de familia configurable (setHue).
class SegControl : public juce::Component, private juce::Timer
{
public:
    using DrawSeg = std::function<void (juce::Graphics&, juce::Rectangle<float>, int idx, bool on)>;

    SegControl (juce::AudioProcessorValueTreeState& state, juce::String paramID, juce::StringArray segLabels,
                DrawSeg customDraw = {}, int iconCount = 0)
        : apvts (state), id (std::move (paramID)), labels (std::move (segLabels)), draw (std::move (customDraw))
    {
        count = labels.isEmpty() ? juce::jmax (1, iconCount) : labels.size();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        refresh();
        startTimerHz (12);
    }

    void setHue (juce::Colour h) { hue = h; repaint(); }

    // Distribuye los segmentos en una GRILLA de n filas (mockup pulsar-a: chips de división 2×2 en el
    // rail vertical). Default 1 = fila única (comportamiento histórico, no toca a nadie).
    void setRows (int n) { rows = juce::jmax (1, n); repaint(); }

    int segAt (juce::Point<float> p) const
    {
        const int cols = numCols();
        const int col  = juce::jlimit (0, cols - 1, (int) (p.x / ((float) getWidth()  / (float) cols)));
        const int row  = juce::jlimit (0, rows - 1, (int) (p.y / ((float) getHeight() / (float) rows)));
        return juce::jlimit (0, count - 1, row * cols + col);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int seg = segAt (e.position);
        if (auto* pr = apvts.getParameter (id))
            pr->setValueNotifyingHost (count > 1 ? (float) seg / (float) (count - 1) : 0.0f);
        selected = seg;
        pressed  = true;
        repaint();
    }
    void mouseUp   (const juce::MouseEvent&)        override { pressed = false; repaint(); }
    void mouseExit (const juce::MouseEvent&)        override { hovered = -1; repaint(); }
    void mouseMove (const juce::MouseEvent& e)      override { setHovered (segAt (e.position)); }
    void mouseEnter(const juce::MouseEvent& e)      override { setHovered (segAt (e.position)); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // CONTENEDOR suave (cliente: "que no parezcan botones"): sin marco, sin vidrio, sin sheen.
        // A lo sumo un pozo APENAS más oscuro para que el grupo lea como zona, sin pastilla que lo encierre.
        g.setColour (juce::Colour (0x33060709));
        g.fillRoundedRectangle (b, 6.0f);

        // fuente uniforme GRANDE (sin divisores internos no hay línea que pisar)
        const int   cols  = numCols();
        const float cellH = b.getHeight() / (float) rows;
        const float fontH = juce::jlimit (9.0f, 14.0f, cellH * 0.42f);

        for (int i = 0; i < count; ++i)
        {
            // límites enteros = tiles parejos sin gaps, texto centrado exacto
            const int col = i % cols, row = i / cols;
            const int x0 = juce::roundToInt (b.getWidth()  * (float) col       / (float) cols);
            const int x1 = juce::roundToInt (b.getWidth()  * (float) (col + 1) / (float) cols);
            const int y0 = juce::roundToInt (b.getHeight() * (float) row       / (float) rows);
            const int y1 = juce::roundToInt (b.getHeight() * (float) (row + 1) / (float) rows);
            auto cell = juce::Rectangle<float> (b.getX() + (float) x0, b.getY() + (float) y0,
                                                (float) (x1 - x0), (float) (y1 - y0));
            const bool on = (i == selected);

            // hover (no seleccionado): SUSURRO de tinte bajo el cursor, sin borde → "está vivo", no botón.
            if (! on && i == hovered)
            {
                g.setColour (hue.withAlpha (pressed ? 0.10f : 0.06f));
                g.fillRoundedRectangle (cell.reduced (2.5f), 5.0f);
            }

            if (on)
                paintSelected (g, cell.reduced (2.5f), i == hovered);

            if (draw)
                draw (g, cell, i, on);
            else
            {
                if (on)   // texto del activo: leve glow del hue + filo brillante (lee "encendido" por el TEXTO)
                {
                    g.setFont (fonts::mono (fontH));
                    g.setColour (hue.withAlpha (0.30f));
                    g.drawText (labels[i], cell.translated (0.0f, 0.6f).toNearestInt(), juce::Justification::centred);
                    g.setColour (textOnColour());
                    g.drawText (labels[i], cell.toNearestInt(), juce::Justification::centred);
                }
                else
                {
                    g.setColour (i == hovered ? theme::mut : theme::fnt);
                    g.setFont (fonts::mono (fontH));
                    g.drawText (labels[i], cell.toNearestInt(), juce::Justification::centred);
                }
            }
        }
        // sin marco exterior: el texto respira, el grupo no se encierra en una caja.
    }

private:
    // Texto del segmento ACTIVO: filo claro teñido del hue (mockup color:var(--cyan) → casi-blanco saturado).
    juce::Colour textOnColour() const noexcept
    {
        return juce::Colours::white.interpolatedWith (hue, 0.35f).brighter (0.30f);
    }

    // Pinta el tile SELECCIONADO SUAVE (cliente: "que no parezcan botones"): SIN borde, SIN inner-glow,
    // SIN sheen, SIN vidrio marcado. El estado se lee por un TINTE de muy baja alpha + un glow tenue +
    // (en el caller) el brillo del TEXTO. Minimal estilo Ableton / Teenage Engineering, no pastilla FabFilter.
    void paintSelected (juce::Graphics& g, juce::Rectangle<float> sel, bool hov)
    {
        constexpr float rad = 5.0f;
        // 1) glow EXTERIOR muy tenue del hue — halo difuso que insinúa el activo sin dibujar un marco.
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (hue.withAlpha (0.022f + (hov ? 0.012f : 0.0f)));
            g.fillRoundedRectangle (sel.expanded ((float) k * 1.8f), rad + (float) k * 1.4f);
        }
        // 2) tinte plano de baja alpha (sin gradiente glassy ni filo): el realce es la atmósfera, no una caja.
        g.setColour (hue.withAlpha (hov ? (pressed ? 0.18f : 0.15f) : 0.12f));
        g.fillRoundedRectangle (sel, rad);
    }

    void timerCallback() override { refresh(); }
    void refresh()
    {
        const int s = juce::jlimit (0, count - 1, (int) std::round (apvts.getRawParameterValue (id)->load()));
        if (s != selected) { selected = s; repaint(); }
    }
    void setHovered (int seg) { if (seg != hovered) { hovered = seg; repaint(); } }
    int  numCols() const { return juce::jmax (1, (count + rows - 1) / rows); }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::StringArray labels;
    DrawSeg draw;
    juce::Colour hue = theme::cyan;
    int count = 1, selected = 0, rows = 1;
    int  hovered = -1;        // segmento bajo el cursor (-1 = ninguno)
    bool pressed = false;     // feedback de press
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegControl)
};

//================================================================================================
// Botón TOGGLE iluminado (LED + main/sub label), ligado a un parámetro Bool por string. Genérico:
// ÓRBITA lo usa como "IN PHASE / MONO-SAFE"; otros plugins le pasan su texto + hue de familia.
class ToggleButton : public juce::Component, private juce::Timer
{
public:
    ToggleButton (juce::AudioProcessorValueTreeState& state, juce::String paramID,
                  juce::String mainTxt, juce::String subTxt, juce::Colour familyHue = theme::cyan)
        : apvts (state), id (std::move (paramID)), mainText (std::move (mainTxt)),
          subText (std::move (subTxt)), hue (familyHue)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        on = apvts.getRawParameterValue (id)->load() >= 0.5f;
        startTimerHz (12);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        if (auto* pr = apvts.getParameter (id))
        {
            on = ! on;
            pr->setValueNotifyingHost (on ? 1.0f : 0.0f);
        }
        pressed = true;
        repaint();
    }
    void mouseUp   (const juce::MouseEvent&) override { pressed = false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { hovered = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hovered = false; pressed = false; repaint(); }
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (0.5f);
        // estado base + realce de hover/press en el borde de familia (feedback designed, sutil).
        const float hoverBoost = hovered ? (pressed ? theme::state::pressGlow : theme::state::hoverGlow) : 0.0f;

        // ---- modo VERTICAL (mockup pulsar-a §phaseT): símbolo arriba + main + sub apilados ------
        // Se activa solo cuando la celda es más alta que ancha (columna de utilidad angosta).
        if (b.getHeight() > b.getWidth())
        {
            if (hovered)
            {
                g.setColour (hue.withAlpha (hoverBoost * 0.5f));
                g.fillRoundedRectangle (b, 4.0f);
            }
            auto col = b.reduced (2.0f, 4.0f);
            auto symCell = col.removeFromTop (col.getHeight() * 0.46f);
            auto sym = symCell.withSizeKeepingCentre (22.0f, 22.0f);
            if (on)
            {
                // suave: halo difuso del hue + símbolo brillante; SIN anillo duro (eso lo hacía "botón").
                g.setColour (hue.withAlpha (0.16f + hoverBoost)); g.fillEllipse (sym.expanded (4.0f));
                g.setColour (hue.withAlpha (0.10f + hoverBoost)); g.fillEllipse (sym.expanded (1.0f));
                g.setColour (hue);
            }
            else
            {
                g.setColour (theme::fnt);
            }
            g.setFont (fonts::mono (12.0f));
            g.drawText (juce::String::fromUTF8 ("\xc3\xb8"), sym.toNearestInt(), juce::Justification::centred);

            g.setColour (on ? juce::Colour (0xffeaffff) : theme::mut);
            g.setFont (fonts::mono (9.0f));
            g.drawText (mainText, col.removeFromTop (col.getHeight() * 0.55f).toNearestInt(),
                        juce::Justification::centred);
            g.setColour (on ? hue : theme::fnt);
            g.setFont (fonts::mono (7.0f));
            g.drawText (subText, col.toNearestInt(), juce::Justification::centred);
            return;
        }

        if (on)
        {
            // ENCENDIDO SUAVE: SIN vidrio, SIN inner-glow, SIN borde, SIN sheen. Glow tenue + tinte plano
            // de baja alpha; el estado lo cantan el LED encendido y el brillo del texto (no una pastilla).
            g.setColour (hue.withAlpha (0.05f + hoverBoost * 0.3f));
            g.fillRoundedRectangle (b.expanded (2.0f), 7.0f);
            g.setColour (hue.withAlpha (0.12f + hoverBoost * 0.3f));
            g.fillRoundedRectangle (b, 6.0f);
        }
        else
        {
            // APAGADO: pozo APENAS más oscuro, sin marco. En hover, sólo un susurro de tinte (sin borde).
            g.setColour (juce::Colour (0x33060709));
            g.fillRoundedRectangle (b, 6.0f);
            if (hovered)
            {
                g.setColour (hue.withAlpha (0.06f + hoverBoost)); g.fillRoundedRectangle (b, 6.0f);
            }
        }
        auto led = juce::Rectangle<float> (b.getX() + 13.0f, b.getCentreY() - 5.5f, 11.0f, 11.0f);
        if (on)
        {
            g.setColour (hue); g.fillEllipse (led);
            g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (led.reduced (3.2f));
        }
        else { g.setColour (juce::Colour (0xff1b2430)); g.fillEllipse (led); }

        auto tx = b.withTrimmedLeft (34.0f).reduced (0.0f, 4.0f);
        g.setColour (on ? juce::Colour (0xffeaffff) : theme::mut);
        g.setFont (fonts::mono (10.5f));
        g.drawText (mainText, tx.removeFromTop (tx.getHeight() * 0.56f).toNearestInt(), juce::Justification::bottomLeft);
        g.setColour (on ? hue : theme::fnt);
        g.setFont (fonts::mono (7.5f));
        g.drawText (subText, tx.toNearestInt(), juce::Justification::topLeft);
    }
private:
    void timerCallback() override
    {
        const bool s = apvts.getRawParameterValue (id)->load() >= 0.5f;
        if (s != on) { on = s; repaint(); }
    }
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id, mainText, subText;
    juce::Colour hue;
    bool on = false;
    bool hovered = false, pressed = false;   // estados de feel (hover/press)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleButton)
};

//================================================================================================
// LookAndFeel del slider de riel (Dry/Wet, etc.): track inset + fill de familia con glow + handle.
// Hue por slider.getProperties()["hue"] (default cian), igual que KnobLookAndFeel.
struct RailSliderLAF : juce::LookAndFeel_V4
{
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const juce::Colour hue { (juce::uint32) (int) s.getProperties()
                                    .getWithDefault ("hue", (int) theme::cyan.getARGB()) };
        const juce::Colour hueD = hue.darker (0.35f);

        auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 4.0f, (float) w, 8.0f);
        g.setColour (juce::Colour (0xff0b0e13)); g.fillRoundedRectangle (track, 3.0f);
        g.setColour (theme::line);               g.drawRoundedRectangle (track, 3.0f, 1.0f);

        const float fillW = juce::jmax (0.0f, pos - (float) x);
        if (fillW > 0.5f)
        {
            juce::ColourGradient gg (hueD, track.getX(), 0.0f, hue, track.getRight(), 0.0f, false);
            g.setGradientFill (gg);
            g.saveState();
            g.reduceClipRegion (track.withWidth (fillW).getSmallestIntegerContainer());
            g.fillRoundedRectangle (track, 3.0f);
            g.restoreState();
        }
        auto hd = juce::Rectangle<float> (pos - 5.5f, track.getCentreY() - 9.0f, 11.0f, 18.0f);
        juce::ColourGradient hg (juce::Colour (0xff3a4250), hd.getCentreX(), hd.getY(),
                                 juce::Colour (0xff1b1e25), hd.getCentreX(), hd.getBottom(), false);
        g.setGradientFill (hg); g.fillRoundedRectangle (hd, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.20f)); g.drawRoundedRectangle (hd.reduced (0.5f), 3.0f, 1.0f);
    }
};
}
