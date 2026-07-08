#pragma once
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <functional>

namespace pulsar::ui
{
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Posición de cada atractor en el morph (0..1): ORBIT · PENDULUM · LORENZ · RÖSSLER.
inline constexpr float kAttractorPos[4] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };

// Ícono del atractor (0 ORBIT · 1 PENDULUM · 2 LORENZ · 3 RÖSSLER) dentro de r — la "forma de lo que hace".
inline void paintAttractorIcon (juce::Graphics& g, juce::Rectangle<float> r, int idx, juce::Colour col)
{
    g.setColour (col);
    const auto  c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    const float t = juce::jmax (1.2f, s * 0.09f);
    switch (idx)
    {
        case 0:  // ORBIT — elipse (órbita limpia)
            g.drawEllipse (c.x - s * 0.40f, c.y - s * 0.26f, s * 0.80f, s * 0.52f, t);
            break;

        case 1: {  // PENDULUM — pivote + varilla + bob + arco de swing
            g.fillEllipse (c.x - s * 0.05f, c.y - s * 0.34f, s * 0.10f, s * 0.10f);
            g.drawLine (c.x, c.y - s * 0.27f, c.x, c.y + s * 0.10f, t * 0.85f);
            g.fillEllipse (c.x - s * 0.13f, c.y + s * 0.04f, s * 0.26f, s * 0.26f);
            juce::Path arc; arc.addCentredArc (c.x, c.y - s * 0.27f, s * 0.42f, s * 0.42f, 0.0f, -0.8f, 0.8f, true);
            g.strokePath (arc, juce::PathStrokeType (t * 0.6f));
            break; }

        case 2: {  // LORENZ — lemniscata (mariposa, dos lóbulos)
            juce::Path p; const int N = 90;
            for (int i = 0; i <= N; ++i)
            {
                const float a = (float) i / (float) N * juce::MathConstants<float>::twoPi;
                const float ca = std::cos (a), den = 1.0f + ca * ca;
                const float x = c.x + (s * 0.48f) * ca / den;
                const float y = c.y + (s * 0.48f) * std::sin (a) * ca / den;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break; }

        case 3: {  // RÖSSLER — espiral (la cola de cometa)
            juce::Path p; const int N = 76;
            for (int i = 0; i <= N; ++i)
            {
                const float ph = (float) i / (float) N;
                const float a  = ph * juce::MathConstants<float>::twoPi * 1.9f;
                const float rr = s * 0.42f * (0.16f + 0.84f * ph);
                const float x  = c.x + rr * std::sin (a), y = c.y - rr * std::cos (a);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break; }

        default: break;
    }
}

// =================================================================================================
// MorphSelector — el control HÉROE de PULSAR (lo que lo distingue de ÓRBITA). Lee/escribe el param
// SHAPE (0..1 continuo) y muestra los 4 atractores con ÍCONO + nombre + un marcador en la posición
// del morph. Drag = morph CONTINUO (preserva los presets que caen en el medio); click en un ícono =
// SALTA a ese atractor. Sondea el param (12 Hz) para reflejar presets/automatización en silencio.
// Mismo lenguaje visual que el SegControl/bahía: velo de superficie + filo de familia, sin marco duro.
// =================================================================================================
class MorphSelector : public juce::Component, private juce::Timer
{
public:
    MorphSelector (juce::AudioProcessorValueTreeState& state, juce::String shapeParamID, juce::Colour hue)
        : apvts (state), id (std::move (shapeParamID)), familyHue (hue)
    {
        param = apvts.getParameter (id);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
        startTimerHz (12);
    }

    // hover -> bottom-bar contextual (nombre canónico + atractor actual + %)
    std::function<void (const juce::String& name, const juce::String& valueText)> onHover;
    std::function<void()> onHoverExit;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // superficie HUNDIDA con bisel (no sticker plano): fill + sombra interior arriba + filo de familia
        // + highlight inferior. Da grosor de material.
        g.setColour (juce::Colour (0xff0a131f));
        g.fillRoundedRectangle (b, 7.0f);
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.drawLine (b.getX() + 8.0f, b.getY() + 1.0f, b.getRight() - 8.0f, b.getY() + 1.0f, 1.0f);   // sombra sup (hundido)
        g.setColour (familyHue.withAlpha (0.26f));
        g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);                                       // filo de familia
        g.setColour (juce::Colour (0x12bee1ff));
        g.drawLine (b.getX() + 8.0f, b.getBottom() - 1.0f, b.getRight() - 8.0f, b.getBottom() - 1.0f, 1.0f); // highlight inf

        const float pad    = 36.0f;   // inset suficiente para que el PILL de los extremos (Orbit/Rössler) NO se salga
        const float x0     = b.getX() + pad,  x1 = b.getRight() - pad;
        const float trackW = x1 - x0;
        const float iconH  = 26.0f, iconY = b.getY() + 14.0f;
        const float trackY = b.getBottom() - 24.0f;

        const float sh     = shapeNorm();
        const int   active = juce::jlimit (0, 3, juce::roundToInt (sh * 3.0f));
        const juce::String names[4] = { "ORBIT", "PENDULUM", "LORENZ", juce::String::fromUTF8 ("R\xc3\x96SSLER") };

        // PILL de selección activa: el atractor elegido es un OBJETO elevado (fill + glow contenido + filo),
        // no un simple cambio de tinte. Lee al instante cuál está seleccionado (crítica de los críticos).
        {
            const float tx = x0 + kAttractorPos[active] * trackW;
            juce::Rectangle<float> pill (tx - 26.0f, iconY - 7.0f, 52.0f, iconH + 13.0f);
            g.setColour (familyHue.withAlpha (0.13f));
            g.fillRoundedRectangle (pill, 7.0f);
            g.setColour (familyHue.withAlpha (0.45f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 7.0f, 1.0f);
        }

        g.setColour (th::fnt.withAlpha (0.45f));
        g.fillRoundedRectangle (x0, trackY - 1.0f, trackW, 2.0f, 1.0f);

        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.06f));
        for (int i = 0; i < 4; ++i)
        {
            const float tx  = x0 + kAttractorPos[i] * trackW;
            const bool  on  = (i == active);
            const auto  col = on ? familyHue : th::fnt;

            paintAttractorIcon (g, { tx - iconH * 0.5f, iconY, iconH, iconH }, i, col.withAlpha (on ? 1.0f : 0.5f));

            g.setColour (col.withAlpha (on ? 0.9f : 0.45f));
            g.fillRect (tx - 0.5f, trackY - 4.0f, 1.0f, 8.0f);

            const auto just = (i == 0) ? juce::Justification::centredLeft
                            : (i == 3) ? juce::Justification::centredRight
                                       : juce::Justification::centred;
            const float lw = 90.0f;
            const float lx = (i == 0) ? tx - 2.0f
                           : (i == 3) ? tx - lw + 2.0f
                                      : tx - lw * 0.5f;
            g.setColour (on ? th::txt : th::fnt);
            g.drawText (names[i], (int) lx, (int) trackY + 7, (int) lw, 11, just);
        }

        // marcador (glow + núcleo) en la posición del morph
        const float mx = x0 + sh * trackW;
        g.setColour (familyHue.withAlpha (0.22f)); g.fillEllipse (mx - 6.0f, trackY - 6.0f, 12.0f, 12.0f);
        g.setColour (familyHue.withAlpha (0.85f)); g.fillEllipse (mx - 3.0f, trackY - 3.0f, 6.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.95f)); g.fillEllipse (mx - 1.4f, trackY - 1.4f, 2.8f, 2.8f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int icon = iconHit (e.position);          // click sobre ícono => salto al atractor
        setShape (icon >= 0 ? kAttractorPos[icon] : xToShape (e.position.x));
        emitHover();
    }
    void mouseDrag (const juce::MouseEvent& e) override { setShape (xToShape (e.position.x)); emitHover(); }
    void mouseMove (const juce::MouseEvent& e) override { emitHover(); juce::Component::mouseMove (e); }
    void mouseExit (const juce::MouseEvent& e) override { if (onHoverExit) onHoverExit(); juce::Component::mouseExit (e); }

private:
    void timerCallback() override
    {
        const float v = shapeNorm();
        if (std::abs (v - lastShown) > 0.001f) { lastShown = v; repaint(); }
    }
    float shapeNorm() const { return param != nullptr ? param->getValue() : 0.0f; }
    void  setShape (float v01)
    {
        if (param == nullptr) return;
        param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01));
        repaint();
    }
    float xToShape (float x) const
    {
        const float pad = 36.0f, x0 = pad, x1 = (float) getWidth() - pad;
        return juce::jlimit (0.0f, 1.0f, (x - x0) / juce::jmax (1.0f, x1 - x0));
    }
    int iconHit (juce::Point<float> p) const
    {
        if (p.y > 46.0f) return -1;                      // sólo la franja de íconos (arriba)
        const float pad = 36.0f, x0 = pad, trackW = (float) getWidth() - 2.0f * pad;
        for (int i = 0; i < 4; ++i)
            if (std::abs (p.x - (x0 + kAttractorPos[i] * trackW)) < 18.0f) return i;
        return -1;
    }
    void emitHover()
    {
        if (! onHover) return;
        const int active = juce::jlimit (0, 3, juce::roundToInt (shapeNorm() * 3.0f));
        const juce::String names[4] = { "ORBIT", "PENDULUM", "LORENZ", juce::String::fromUTF8 ("R\xc3\x96SSLER") };
        onHover ("SHAPE", names[active] + "  " + juce::String (juce::roundToInt (shapeNorm() * 100.0f)) + "%");
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::Colour familyHue;
    juce::RangedAudioParameter* param = nullptr;
    float lastShown = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MorphSelector)
};
}
