#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// DUST — capa ESTÁTICA del CAMPO DE BURBUJAS (mockup dust-a §buildBg + overlays).
// Funciones libres puras de dibujo: se hornean UNA vez dentro de VisualizerBase
// (resolución física, Retina-nítido). Nada acá corre por frame.
//
// El campo es un POZO PROFUNDO ABIERTO (no el pozo circular de PULSAR): un volumen
// vertical de alto contraste (frente claro arriba → fondo negro abajo) bañado por
// el ambiente cian de la familia Movimiento, con polvo baked determinístico (LCG),
// planos de profundidad horizontales y el eje L/R central — el escenario donde
// nacen y flotan los ecos. Termina hundiéndose en el chasis (scanlines + vignette
// + rim-shadow) para leer como un rebaje físico, no un rectángulo pegado.
// =============================================================================
namespace dust::ui::detail
{

// Aire entre el campo [-1,1]² y el borde del componente (espejo de DustField::kFieldInsetPx).
inline constexpr float kFieldInsetPx = 16.0f;

// --- POZO / VOLUMEN: degradé vertical de alto contraste + ambiente cian + nimbo direccional. -----
inline void paintFieldVolume (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;

    // Volumen profundo y abierto: frente claro (arriba) → fondo casi-negro (abajo).
    {
        juce::ColourGradient vol (juce::Colour (0xff0c1320), 0.0f, 0.0f,
                                  juce::Colour (0xff030406), 0.0f, fh, false);
        vol.addColour (0.42, juce::Colour (0xff080d16));
        vol.addColour (0.78, juce::Colour (0xff05080d));
        g.setGradientFill (vol);
        g.fillRect (0, 0, w, h);
    }

    // Ambiente cian que EMANA del frente del campo (radial, centro-arriba).
    {
        const float cx = fw * 0.5f, cy = fh * 0.40f;
        juce::ColourGradient amb (th::cyan.withAlpha (0.065f), cx, cy,
                                  th::cyan.withAlpha (0.0f),   cx, cy - juce::jmax (fw, fh) * 0.66f, true);
        amb.addColour (0.5, th::cyan.withAlpha (0.020f));
        g.setGradientFill (amb);
        g.fillRect (0, 0, w, h);
    }

    // Segundo nimbo de profundidad arriba-izquierda (luz direccional del chasis dentro del campo).
    {
        const float cx = fw * 0.30f, cy = fh * 0.18f;
        juce::ColourGradient nim (juce::Colour (0x0d7ea0c6), cx, cy,
                                  juce::Colour (0x007ea0c6), cx, cy - juce::jmax (fw, fh) * 0.50f, true);
        g.setGradientFill (nim);
        g.fillRect (0, 0, w, h);
    }
}

// --- POLVO baked: ~200 partículas (2 tamaños), atenuadas hacia el fondo (profundidad). LCG fijo. ---
inline void paintFieldDust (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;
    juce::int64 seed = 9173;
    auto rnd = [&seed]() noexcept
    {
        seed = (seed * 16807) % 2147483647;
        return (float) seed / 2147483647.0f;
    };
    const juce::Colour dustCol (0xffbedeff);
    for (int i = 0; i < 200; ++i)
    {
        const float x   = rnd() * fw, y = rnd() * fh;
        const bool  big = rnd() < 0.18f;
        const float dep = 1.0f - (y / fh) * 0.5f;                 // el polvo se apaga hacia abajo (fondo)
        const float al  = ((big ? 0.08f : 0.05f) + rnd() * 0.07f) * dep;
        const float d   = big ? 1.1f : 0.55f;
        g.setColour (dustCol.withAlpha (al));
        g.fillEllipse (x - d, y - d, d * 2.0f, d * 2.0f);
    }
}

// --- ESTRUCTURA: planos de profundidad horizontales (hairline con glow) + eje L/R + marcas L · R. --
inline void paintFieldStructure (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;

    // Planos del volumen: 3 líneas horizontales con un glow cian debajo (más tenues hacia el fondo).
    const float planes[] = { 0.30f, 0.50f, 0.70f };
    for (int i = 0; i < 3; ++i)
    {
        const float y = fh * planes[i];
        g.setColour (th::cyan.withAlpha (0.05f - 0.012f * (float) i));
        g.fillRect (fw * 0.06f, y - 1.2f, fw * 0.88f, 2.4f);      // glow ancho del plano
        g.setColour (juce::Colour (0x12a0c0e0));
        g.fillRect (fw * 0.06f, y - 0.35f, fw * 0.88f, 0.7f);     // hairline nítida
    }

    // Eje central L/R (hairline) + marcas L · R en los costados.
    g.setColour (juce::Colour (0x0fa0c0e0));
    g.fillRect (fw * 0.5f - 0.35f, fh * 0.10f, 0.7f, fh * 0.80f);

    g.setColour (th::fnt.withAlpha (0.75f));
    g.setFont (ovni::ui::fonts::mono (8.0f));
    g.drawText ("L", juce::Rectangle<float> (fw * 0.06f, fh * 0.5f - 6.0f, 14.0f, 12.0f),
                juce::Justification::centredLeft);
    g.drawText ("R", juce::Rectangle<float> (fw * 0.94f - 14.0f, fh * 0.5f - 6.0f, 14.0f, 12.0f),
                juce::Justification::centredRight);
}

// --- OVERLAYS: scanlines + vignette + rim-shadow que HUNDE el campo en el chasis (mockup .scan/.vig). --
inline void paintFieldOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;

    // Scanlines horizontales ultra-sutiles (mockup .scan: multiply ~0.045 efectivo).
    g.setColour (juce::Colours::black.withAlpha (0.045f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // Vignette: transparente en el centro-frente, cierra a casi-negro en los bordes (mockup .vig).
    {
        const juce::Colour edge (0xff020305);
        const float cx = fw * 0.5f, cy = fh * 0.36f;
        juce::ColourGradient vig (edge.withAlpha (0.0f),  cx, cy,
                                  edge.withAlpha (0.64f), cx, cy + juce::jmax (fw, fh) * 0.64f, true);
        vig.addColour (0.52, edge.withAlpha (0.0f));
        vig.addColour (0.86, edge.withAlpha (0.34f));
        g.setGradientFill (vig);
        g.fillRect (0, 0, w, h);
    }

    // Rim del rebaje: el campo se HUNDE en el chasis (insets de sombra en los 4 bordes, arriba más profundo).
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
    inset (r, true,  true,  24.0f, 0.55f);   // arriba (filo del rebaje, más profundo)
    inset (r, true,  false, 22.0f, 0.45f);   // abajo
    inset (r, false, true,  22.0f, 0.45f);   // izquierda
    inset (r, false, false, 22.0f, 0.45f);   // derecha
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (0.0f, 0.0f, fw, 1.0f);       // filo superior del rebaje
}

// Capa estática completa (orden: volumen → polvo → estructura → overlays).
inline void paintFieldStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    paintFieldVolume    (g, w, h);
    paintFieldDust      (g, w, h);
    paintFieldStructure (g, w, h);
    paintFieldOverlays  (g, w, h);
}

} // namespace dust::ui::detail
