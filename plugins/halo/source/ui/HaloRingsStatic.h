#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// HALO — capa ESTÁTICA del pozo del halo (mockup halo-a §buildField + overlays).
// Funciones libres puras de dibujo: se hornean UNA vez dentro de VisualizerBase
// (resolución física, Retina-nítido). Nada acá corre por frame.
//
// El campo es un POZO VERTICAL: el eje del halo ASCIENDE. Gradiente alto contraste,
// ambiente magenta del sello, columna de ascenso teñida, polvo estelar baked (LCG
// determinístico), eje vertical con marcas de altura, cruz de la FUENTE donde nacen
// los anillos, + overlays (scanlines, vignette, rim-shadow que hunde el campo en el
// chasis). Mismo nivel que el StellarPadStatic de PULSAR.
// =============================================================================
namespace halo::ui::detail
{

// Geometría compartida con la capa viva: centro, radio del pozo, y la altura de la FUENTE
// (donde nacen los anillos) / el TOPE (donde se desvanecen). En unidades del frame.
inline float haloR (int w, int h) noexcept
{
    return juce::jmax (1.0f, (float) juce::jmax (w, h) * 0.42f);   // pozo amplio (mockup R = min/2 - 14, pero
                                                                   // el funnel sube: usamos jmax para que llene)
}
inline float sourceY (int h, float cy, float R) noexcept { juce::ignoreUnused (h); return cy + R * 0.34f; }   // FUENTE
inline float topY    (int h, float cy, float R) noexcept { juce::ignoreUnused (h); return cy - R * 0.92f; }   // TOPE

// Pozo vertical + ambiente magenta + columna de ascenso + polvo baked + eje/marcas + FUENTE.
inline void paintFieldStatic (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;
    const float R  = haloR (w, h);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // 1) gradiente vertical profundo: el eje del halo asciende (oscuro abajo → violeta-noche arriba).
    {
        juce::ColourGradient vg (juce::Colour (0xff06040c), 0.0f, fh,
                                 juce::Colour (0xff100a1e), 0.0f, 0.0f, false);
        vg.addColour (0.5, juce::Colour (0xff0c0818));
        g.setGradientFill (vg);
        g.fillRect (0, 0, w, h);
    }

    // 2) pozo radial: base oscura en los bordes, MUCHO contraste centro/borde.
    {
        juce::ColourGradient well (juce::Colour (0x0016220f) /* casi transparente al centro */, cx, cy,
                                   juce::Colour (0xd9030205), cx, cy - R * 1.5f, true);
        well.addColour (0.70, juce::Colour (0x6606040c));
        g.setGradientFill (well);
        g.fillRect (0, 0, w, h);
    }

    // 3) columna de ascenso: tinte magenta vertical tenue desde el centro hacia arriba (el eje del funnel).
    {
        juce::ColourGradient col (th::magenta.withAlpha (0.0f),   0.0f, cy + R * 0.2f,
                                  th::magenta.withAlpha (0.0f),   0.0f, cy - R * 1.1f, false);
        col.addColour (0.4, th::magenta.withAlpha (0.035f));
        g.setGradientFill (col);
        g.fillRect (cx - R * 0.55f, 0.0f, R * 1.1f, fh);
    }

    // 4) polvo estelar lejano horneado (~88 puntos) — LCG determinístico (idéntico patrón al mockup §drnd).
    {
        juce::int64 seed = 4242;
        auto rnd = [&seed]() noexcept
        {
            seed = (seed * 16807) % 2147483647;
            return (float) seed / 2147483647.0f;
        };
        const juce::Colour dust (0xffd2c4ec);
        for (int i = 0; i < 88; ++i)
        {
            const float x   = rnd() * fw, y = rnd() * fh;
            const bool  big = rnd() < 0.12f;
            const float a   = (big ? 0.10f : 0.05f) + rnd() * 0.05f;
            const float d   = big ? 1.0f : 0.5f;
            g.setColour (dust.withAlpha (a));
            g.fillEllipse (x - d, y - d, d * 2.0f, d * 2.0f);
        }
    }

    // 5) eje vertical hairline (línea de ascenso del halo) + marcas de altura.
    g.setColour (juce::Colour (0x0fa0c0e0));
    g.drawLine (cx, cy - R * 1.05f, cx, cy + R * 0.55f, 1.0f);
    for (int i = 1; i <= 5; ++i)
    {
        const float y = cy - (R * 0.95f) * (float) i / 5.0f;
        g.drawLine (cx - 4.0f, y, cx + 4.0f, y, 1.0f);
    }

    // 6) FUENTE: cruz en el centro de nacimiento + label mono.
    const float sy = sourceY (h, cy, R);
    g.setColour (th::txt.withAlpha (0.30f));
    g.drawLine (cx - 6.0f, sy, cx + 6.0f, sy, 1.0f);
    g.drawLine (cx, sy - 6.0f, cx, sy + 6.0f, 1.0f);
    g.setColour (th::fnt.withAlpha (0.7f));
    g.setFont (ovni::ui::fonts::mono (8.0f).withExtraKerningFactor (0.12f));
    g.drawText ("SOURCE", juce::Rectangle<int> ((int) cx - 40, (int) (sy + 8.0f), 80, 12),
                juce::Justification::centred);
}

// Overlays del pozo (encima del fondo, debajo de la capa viva): grano baked + scanlines + vignette + rim.
inline void paintFieldOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;

    // scanlines horizontales ultra-sutiles (mockup .scan, multiply efectivo ~0.05).
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // vignette del campo (transparente hasta ~54%, cierra a casi-negro en el borde) — el funnel respira centrado.
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
    inset (r, true,  true,  26.0f, 0.55f);   // arriba (más profundo, mockup ::after)
    inset (r, true,  false, 22.0f, 0.50f);   // abajo
    inset (r, false, true,  22.0f, 0.50f);   // izquierda
    inset (r, false, false, 22.0f, 0.50f);   // derecha
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0.0f, 0.0f, fw, 1.0f);       // filo del rebaje
}

} // namespace halo::ui::detail
