#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// AURORA — capa ESTÁTICA del campo boreal (mockup aurora-a §buildBackplate +
// overlays §scan/§vig/§fieldWrap::after). Funciones libres puras de dibujo: se
// hornean UNA vez dentro de VisualizerBase (resolución física, Retina-nítido).
// Nada acá corre por frame.
//
// El "pozo profundo" de AURORA NO es un disco (como el StellarPad de PULSAR) sino
// un CAMPO con HORIZONTE: cielo profundo arriba, una línea de horizonte a y≈0.70
// de donde nacen las cortinas, resplandor verde sobre el horizonte, eje L↔R con
// marcas, polvo estelar LCG determinístico, y el rim-shadow que hunde el campo en
// el chasis. La capa viva (las cortinas) se apila encima de esto.
// =============================================================================
namespace aurora::ui::detail
{

// Altura del horizonte (fracción del alto): de aquí nacen las cortinas. Igual que
// el mockup (horizonY = H*0.70). Compartido con el paintLive de AuroraField.
inline constexpr float kHorizonFrac = 0.70f;
inline constexpr float kEdgePadFrac = 0.04f;   // margen L/R del eje de azimut (frac del ancho)
inline constexpr float kAxisHalfFr  = 0.46f;   // medio-span L↔R (frac del ancho) — = mockup span

inline float horizonY (int h) noexcept { return (float) h * kHorizonFrac; }

// Tinte de la aurora: familia VERDE (Espectral) SIEMPRE. Graves = verde profundo,
// aire = menta/blanco brillante hacia los agudos (el degradé natural de una aurora).
// Réplica de curtainColor() del mockup, anclada a los tokens del sello. Familia fija.
inline juce::Colour curtainColour (float hue, float alpha) noexcept
{
    namespace th = ovni::ui::theme;
    const float t = juce::jlimit (0.0f, 1.0f, hue);
    // verde base (th::green #5ef0a8) → menta-blanca hacia el aire (≈ rgb 196,255,222)
    const juce::Colour bright (0xffc4ffde);
    return th::green.interpolatedWith (bright, t).withAlpha (juce::jlimit (0.0f, 1.0f, alpha));
}

// Backplate del campo: cielo profundo + resplandor verde del horizonte + banda de
// luz horizontal + polvo estelar baked + línea de horizonte + eje central L↔R con
// marcas y rótulos. (= mockup buildBackplate, a tokens del sello.)
inline void paintFieldStatic (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f;
    const float hy = horizonY (h);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // Cielo del campo: degradé vertical, más profundo abajo (#070d10 → #020406).
    {
        juce::ColourGradient sky (juce::Colour (0xff070d10), 0.0f, 0.0f,
                                  juce::Colour (0xff020406), 0.0f, fh, false);
        sky.addColour (0.45, juce::Colour (0xff050a0d));
        g.setGradientFill (sky);
        g.fillRect (0, 0, w, h);
    }

    // Resplandor verde tenue sobre el horizonte (base atmosférica de las cortinas).
    {
        juce::ColourGradient glow (th::green.withAlpha (0.06f), cx, hy,
                                   th::green.withAlpha (0.0f),  cx, hy - fw * 0.62f, true);
        glow.addColour (0.55, th::green.withAlpha (0.016f));
        g.setGradientFill (glow);
        g.fillRect (0, 0, w, h);
    }

    // Banda de luz a lo largo del horizonte (de donde se posan las cortinas).
    {
        const float bandTop = hy - 46.0f;
        juce::ColourGradient band (th::green.withAlpha (0.0f),  0.0f, bandTop,
                                   th::green.withAlpha (0.09f), 0.0f, hy + 8.0f, false);
        band.addColour (0.82, th::green.withAlpha (0.05f));
        g.setGradientFill (band);
        g.fillRect (0.0f, bandTop, fw, 54.0f);
    }

    // Polvo estelar horneado (LCG determinístico, idéntico patrón que el mockup).
    {
        juce::int64 seed = 9123;
        auto rnd = [&seed]() noexcept
        {
            seed = (seed * 16807) % 2147483647;
            return (float) seed / 2147483647.0f;
        };
        const juce::Colour dust (0xffc8e6d7);
        for (int i = 0; i < 110; ++i)
        {
            const float x   = rnd() * fw;
            const float y   = rnd() * hy * 0.96f;
            const bool  big = rnd() < 0.18f;
            const float a   = (big ? 0.08f : 0.05f) + rnd() * 0.06f;
            const float d   = big ? 2.1f : 1.0f;
            g.setColour (dust.withAlpha (a));
            g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
    }

    // Línea de horizonte (el suelo donde aterrizan las cortinas) + su sombra hairline.
    g.setColour (th::green.withAlpha (0.10f));
    g.drawLine (0.0f, hy, fw, hy, 1.0f);
    g.setColour (juce::Colour (0x10a0c0e0));
    g.drawLine (0.0f, hy + 2.0f, fw, hy + 2.0f, 0.7f);

    // Eje central + marcas L↔R hairline (el azimut: centro arriba, las parejas a los lados).
    g.setColour (juce::Colour (0x1aa0c0e0));
    g.drawLine (cx, fh * 0.10f, cx, hy, 1.0f);
    const float span = fw * kAxisHalfFr;
    g.setColour (juce::Colour (0x10a0c0e0));
    for (int i = 1; i <= 4; ++i)
    {
        const float fx = (float) i / 5.0f;
        for (const float x : { cx - fx * span, cx + fx * span })
            g.drawLine (x, hy - 4.0f, x, hy + 4.0f, 1.0f);
    }

    // Rótulos L / CENTRO / R en mono (look "instrumento alien").
    g.setFont (ovni::ui::fonts::mono (8.0f));
    g.setColour (th::fnt.withAlpha (0.85f));
    g.drawText ("L", juce::Rectangle<float> (8.0f, hy + 8.0f, 18.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("R", juce::Rectangle<float> (fw - 26.0f, hy + 8.0f, 18.0f, 12.0f), juce::Justification::centredRight);
    g.setColour (th::fnt.withAlpha (0.7f));
    g.drawText (juce::String::fromUTF8 ("\xc2\xb7 CENTER \xc2\xb7"),
                juce::Rectangle<float> (cx - 50.0f, hy + 8.0f, 100.0f, 12.0f), juce::Justification::centred);
}

// Overlays del campo (encima del fondo, debajo de la capa viva): scanlines +
// vignette lateral/vertical + rim-shadow que hunde el campo en el chasis (= mockup
// .scan / .vig / #fieldWrap::after).
inline void paintFieldOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;

    // Scanlines horizontales ultra-sutiles (mockup .scan, multiply efectivo ~0.04).
    g.setColour (juce::Colours::black.withAlpha (0.04f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // Vignette: oscurece los flancos y el pie (el campo se cierra hacia los bordes).
    {
        const juce::Colour edge (0xff020305);
        juce::ColourGradient vx (edge.withAlpha (0.5f), 0.0f, 0.0f,
                                 edge.withAlpha (0.0f), fw * 0.14f, 0.0f, false);
        g.setGradientFill (vx);
        g.fillRect (0.0f, 0.0f, fw * 0.14f, fh);
        juce::ColourGradient vx2 (edge.withAlpha (0.0f), fw * 0.86f, 0.0f,
                                  edge.withAlpha (0.5f), fw, 0.0f, false);
        g.setGradientFill (vx2);
        g.fillRect (fw * 0.86f, 0.0f, fw * 0.14f, fh);
        juce::ColourGradient vy (edge.withAlpha (0.0f),  0.0f, fh * 0.64f,
                                 edge.withAlpha (0.55f), 0.0f, fh, false);
        g.setGradientFill (vy);
        g.fillRect (0.0f, fh * 0.64f, fw, fh * 0.36f);
    }

    // Rim del rebaje: el campo se HUNDE en el chasis (insets de sombra en los 4 bordes).
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

} // namespace aurora::ui::detail
