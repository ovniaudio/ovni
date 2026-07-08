#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// NÉBULA — capa ESTÁTICA del campo nebular (mockup nebula-a §buildField + overlays).
// Funciones libres puras de dibujo: se hornean UNA vez dentro de VisualizerBase
// (resolución física, Retina-nítido). Nada acá corre por frame.
//
// A diferencia de PULSAR (un POZO de profundidad con un punto que se mueve), NÉBULA
// es un CAMPO: gradiente nebular profundo descentrado arriba, ambiente magenta tenue,
// estrellas de fondo lejanas, anillos de estructura con glow, marcas cardinales, y un
// halo de cuenca que contiene el campo. Encima: scanlines + vignette + rim-shadow que
// HUNDE el campo en el chasis (rebaje, mockup #cloudWrap::after).
// =============================================================================
namespace nebula::ui::detail
{

// Radio del campo nebular (mockup: R = min(W,H)/2 - 14).
inline float fieldRadius (int w, int h) noexcept
{
    return juce::jmax (1.0f, (float) juce::jmin (w, h) * 0.5f - 14.0f);
}

// LCG determinístico (mismo patrón que StellarPadStatic / el buildField del mockup): snapshot reproducible.
struct Lcg
{
    juce::int64 s;
    explicit Lcg (juce::int64 seed) noexcept : s (seed) {}
    float operator()() noexcept
    {
        s = (s * 16807) % 2147483647;
        return (float) s / 2147483647.0f;
    }
};

// Campo nebular profundo + ambiente magenta + estrellas + anillos + marcas + halo de cuenca.
// (mockup §buildField — gradiente alto contraste, descentrado arriba para dar "campo", no "pozo".)
inline void paintFieldStatic (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;
    const float R  = fieldRadius (w, h);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // 1) gradiente nebular profundo (no un pozo: un CAMPO) — foco descentrado arriba (cy*0.86), MUCHO contraste.
    {
        juce::ColourGradient field (juce::Colour (0xff160f22), cx, cy * 0.86f,
                                    juce::Colour (0xff030205), cx, cy + R * 1.5f, true);
        field.addColour (0.34, juce::Colour (0xff0d0918));
        field.addColour (0.72, juce::Colour (0xff06040c));
        g.setGradientFill (field);
        g.fillRect (0, 0, w, h);
    }

    // 2) tinte magenta de fondo, sutil, descentrado arriba-izquierda (ambiente del hue de familia).
    {
        juce::ColourGradient amb (th::magenta.withAlpha (0.05f), cx - R * 0.18f, cy - R * 0.22f,
                                  th::magenta.withAlpha (0.0f),  cx, cy + R * 1.05f, true);
        amb.addColour (0.55, th::magD.withAlpha (0.018f));
        g.setGradientFill (amb);
        g.fillRect (0, 0, w, h);
    }

    // 3) estrellas de fondo lejanas (fuera del disco, muy tenues) — polvo del cosmos.
    {
        Lcg rnd (4242);
        const juce::Colour star (0xffd2c4ec);
        for (int i = 0; i < 90; ++i)
        {
            const float x   = rnd() * fw;
            const float y   = rnd() * fh;
            const bool  big = rnd() < 0.12f;
            const float al  = (big ? 0.10f : 0.05f) + rnd() * 0.05f;
            const float d   = big ? 1.0f : 0.5f;
            g.setColour (star.withAlpha (al));
            g.fillEllipse (x - d, y - d, d * 2.0f, d * 2.0f);
        }
    }

    // 4) anillos de profundidad estáticos: estructura del campo (glow magenta + hairline azulada).
    for (const float f : { 0.34f, 0.66f, 0.98f })
    {
        const float rr = R * f;
        g.setColour (th::magenta.withAlpha (0.038f));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 3.0f);
        g.setColour (juce::Colour (0x15a0c0e0));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.7f);
    }

    // 5) marcas cardinales hairline (12 ticks, las mayores cada 3).
    g.setColour (juce::Colour (0x10a0c0e0));
    for (int i = 0; i < 12; ++i)
    {
        const float a  = (float) i / 12.0f * twoPi;
        const float r1 = R * ((i % 3 == 0) ? 0.93f : 0.955f);
        g.drawLine (cx + std::cos (a) * R * 0.99f, cy + std::sin (a) * R * 0.99f,
                    cx + std::cos (a) * r1,        cy + std::sin (a) * r1, 1.0f);
    }

    // 6) halo de cuenca: contención del campo (hairline + glow tenue), justo afuera del último anillo.
    {
        const float rr = R * 1.04f;
        g.setColour (th::magenta.withAlpha (0.05f));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 3.0f);
        g.setColour (juce::Colour (0x12a0c0e0));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.7f);
    }
}

// Overlays del campo (encima del fondo, debajo de la capa viva): scanlines + vignette + rim-shadow
// que hunde el campo en el chasis (mockup .scan + .vig + #cloudWrap::after).
inline void paintFieldOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;

    // scanlines horizontales sutiles (mockup .scan: multiply ~0.045 efectivo).
    g.setColour (juce::Colours::black.withAlpha (0.045f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // vignette del campo (transparente hasta ~54%, cierra a casi-negro en el borde — mockup .vig).
    {
        const juce::Colour edge (0xff020305);
        juce::ColourGradient vig (edge.withAlpha (0.0f),  cx, cy,
                                  edge.withAlpha (0.76f), cx, cy - juce::jmin (fw, fh) * 0.5f, true);
        vig.addColour (0.54, edge.withAlpha (0.0f));
        vig.addColour (0.84, edge.withAlpha (0.40f));
        g.setGradientFill (vig);
        g.fillRect (0, 0, w, h);
    }

    // rim del rebaje: el campo se HUNDE en el chasis (insets de sombra en los 4 bordes + filo superior).
    auto inset = [&g] (juce::Rectangle<float> r, bool horizontal, bool fromStart, float depth, float alpha)
    {
        juce::ColourGradient sh (juce::Colours::black.withAlpha (alpha),
                                 horizontal ? r.getX() : (fromStart ? r.getX() : r.getRight()),
                                 horizontal ? (fromStart ? r.getY() : r.getBottom()) : r.getY(),
                                 juce::Colours::black.withAlpha (0.0f),
                                 horizontal ? r.getX() : (fromStart ? r.getX() + depth : r.getRight() - depth),
                                 horizontal ? (fromStart ? r.getY() + depth : r.getBottom() - depth) : r.getY(),
                                 false);
        g.setGradientFill (sh);
        if (horizontal)
            g.fillRect (fromStart ? r.withHeight (depth) : r.withTop (r.getBottom() - depth));
        else
            g.fillRect (fromStart ? r.withWidth (depth) : r.withLeft (r.getRight() - depth));
    };
    const juce::Rectangle<float> r (0.0f, 0.0f, fw, fh);
    inset (r, true,  true,  26.0f, 0.55f);   // arriba (más profundo)
    inset (r, true,  false, 22.0f, 0.50f);   // abajo
    inset (r, false, true,  22.0f, 0.50f);   // izquierda
    inset (r, false, false, 22.0f, 0.50f);   // derecha
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, 0.0f, fw, 1.0f);       // filo del rebaje
}

} // namespace nebula::ui::detail
