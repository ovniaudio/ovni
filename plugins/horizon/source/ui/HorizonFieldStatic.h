#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// HORIZON — capa ESTÁTICA del campo (mockup horizon-a §buildBackplate + overlays).
// Funciones libres puras de dibujo: se hornean UNA vez dentro de VisualizerBase
// (resolución física, Retina-nítido). Nada acá corre por frame.
//
// El campo es un POZO/HORIZONTE de alto contraste: cielo vertical hacia el horizonte
// de eventos (banda luminosa verde), polvo estelar estático arriba (LCG
// determinístico → snapshot reproducible), la línea de horizonte de la que nacen
// las 24 láminas (espejo arriba/abajo), marcas L↔R y eje central. Los overlays
// (scanlines + vignette + rim-shadow) hunden el campo en el chasis.
// =============================================================================
namespace horizon::ui::detail
{

// Altura del horizonte de eventos (algo bajo el centro — coincide con paintLive).
inline float horizonY (int h) noexcept { return (float) h * 0.52f; }

// Pozo + ambiente verde + banda luminosa + polvo + línea de horizonte + marcas L/R + eje.
inline void paintFieldStatic (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f;
    const float horz = horizonY (h);

    // Cielo del campo: degradé vertical de alto contraste, más oscuro EN el horizonte
    // (mockup: #070e11 → #03070a en el horizonte → #020406 abajo). El reflejo de abajo
    // arranca apenas más claro y cae a negro casi puro.
    {
        juce::ColourGradient sky (juce::Colour (0xff070e11), 0.0f, 0.0f,
                                  juce::Colour (0xff020406), 0.0f, fh, false);
        sky.addColour (0.45, juce::Colour (0xff050a0d));
        sky.addColour (0.52, juce::Colour (0xff03070a));   // el más oscuro, justo en el horizonte
        sky.addColour (0.60, juce::Colour (0xff040a0c));
        g.setGradientFill (sky);
        g.fillRect (0, 0, w, h);
    }

    // Resplandor verde del horizonte de eventos (radial desde el centro-horizonte).
    {
        juce::ColourGradient amb (th::green.withAlpha (0.07f), cx, horz,
                                  th::green.withAlpha (0.0f),  cx, horz - fw * 0.6f, true);
        amb.addColour (0.5, th::green.withAlpha (0.018f));
        g.setGradientFill (amb);
        g.fillRect (0, 0, w, h);
    }

    // Banda luminosa horizontal sobre el horizonte (el "amanecer" del campo).
    {
        const float bandH = 60.0f;
        juce::ColourGradient hb (th::green.withAlpha (0.0f),  0.0f, horz - bandH * 0.5f,
                                 th::green.withAlpha (0.0f),  0.0f, horz + bandH * 0.5f, false);
        hb.addColour (0.5, th::green.brighter (0.25f).withAlpha (0.08f));
        g.setGradientFill (hb);
        g.fillRect (0.0f, horz - bandH * 0.5f, fw, bandH);
    }

    // Polvo estelar estático SÓLO arriba del horizonte (LCG determinístico → reproducible).
    {
        juce::int64 seed = 7321;
        auto rnd = [&seed]() noexcept
        {
            seed = (seed * 16807) % 2147483647;
            return (float) seed / 2147483647.0f;
        };
        const juce::Colour dust (0xffc8e6d7);
        for (int i = 0; i < 90; ++i)
        {
            const float x   = rnd() * fw;
            const float y   = rnd() * horz * 0.92f;
            const bool  big = rnd() < 0.18f;
            const float al  = (big ? 0.07f : 0.04f) + rnd() * 0.05f;
            const float d   = big ? 2.0f : 1.0f;
            g.setColour (dust.withAlpha (al));
            g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
    }

    // Línea de horizonte de eventos: el plano de simetría del que nacen las láminas.
    const float x0 = 0.0f, x1 = fw;
    g.setColour (th::green.withAlpha (0.16f));
    g.drawLine (x0, horz, x1, horz, 1.0f);
    g.setColour (juce::Colour (0x10a0c0e0));
    g.drawLine (x0, horz + 1.5f, x1, horz + 1.5f, 0.7f);

    // Marcas L↔R hairline sobre el horizonte (mockup: 5 pares simétricos).
    g.setColour (juce::Colour (0x12a0c0e0));
    for (int i = 1; i <= 5; ++i)
    {
        const float fx = (float) i / 6.0f;
        for (const float x : { cx - fx * fw * 0.46f, cx + fx * fw * 0.46f })
            g.drawLine (x, horz - 3.0f, x, horz + 3.0f, 1.0f);
    }

    // Eje central tenue (la simetría L/R).
    g.setColour (juce::Colour (0x0da0c0e0));
    g.drawLine (cx, horz - fh * 0.40f, cx, horz + fh * 0.40f, 1.0f);

    // Rótulos L / R / · HORIZONTE DE EVENTOS · (look "instrumento espectral").
    g.setFont (ovni::ui::fonts::mono (8.0f));
    g.setColour (th::fnt.withAlpha (0.85f));
    g.drawText ("L", juce::Rectangle<float> (10.0f, horz - 18.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
    g.drawText ("R", juce::Rectangle<float> (fw - 40.0f, horz - 18.0f, 30.0f, 12.0f), juce::Justification::centredRight);
    g.setColour (th::fnt.withAlpha (0.6f));
    g.drawText (juce::String::fromUTF8 ("\xc2\xb7 EVENT HORIZON \xc2\xb7"),
                juce::Rectangle<float> (cx - 120.0f, horz - 18.0f, 240.0f, 12.0f), juce::Justification::centred);

    // Corner brackets discretos (look "instrumento", como el resto del catálogo).
    const float br = 14.0f, m = 10.0f;
    g.setColour (th::line.withAlpha (0.14f));
    auto bracket = [&] (float x, float y, float dx, float dy)
    {
        g.drawLine (x, y, x + dx, y, 1.0f);
        g.drawLine (x, y, x, y + dy, 1.0f);
    };
    bracket (m, m, br, br);
    bracket (fw - m, m, -br, br);
    bracket (m, fh - m, br, -br);
    bracket (fw - m, fh - m, -br, -br);
}

// Overlays del campo (encima del fondo, debajo de la capa viva): scanlines + vignette
// L/R y arriba/abajo + rim-shadow que HUNDE el campo en el rebaje del chasis (mockup
// #fieldWrap .scan/.vig/::after).
inline void paintFieldOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;

    // scanlines horizontales (mockup .scan: multiply ~0.045 efectivo).
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // viñeta L/R + arriba/abajo (cierra los bordes del campo en negro).
    {
        const juce::Colour edge (0xff020305);
        juce::ColourGradient lr (edge.withAlpha (0.5f), 0.0f, 0.0f,
                                 edge.withAlpha (0.5f), fw,   0.0f, false);
        lr.addColour (0.14, edge.withAlpha (0.0f));
        lr.addColour (0.86, edge.withAlpha (0.0f));
        g.setGradientFill (lr);
        g.fillRect (0.0f, 0.0f, fw, fh);

        juce::ColourGradient ud (edge.withAlpha (0.42f), 0.0f, 0.0f,
                                 edge.withAlpha (0.42f), 0.0f, fh, false);
        ud.addColour (0.24, edge.withAlpha (0.0f));
        ud.addColour (0.76, edge.withAlpha (0.0f));
        g.setGradientFill (ud);
        g.fillRect (0.0f, 0.0f, fw, fh);
    }

    // rim del rebaje: el campo se HUNDE en el chasis (insets de sombra en los 4 bordes).
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
    g.fillRect (0.0f, 0.0f, fw, 1.0f);       // filo superior del rebaje
}

} // namespace horizon::ui::detail
