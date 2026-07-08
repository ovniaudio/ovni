#pragma once

#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>

// =============================================================================
// PULSAR — capa ESTÁTICA del pozo estelar (mockup pulsar-a §buildWell + overlays).
// Funciones libres puras de dibujo: se hornean UNA vez dentro de VisualizerBase
// (resolución física, Retina-nítido). Nada acá corre por frame.
// =============================================================================
namespace pulsar::ui::detail
{

inline float padRadius (int w, int h) noexcept
{
    return juce::jmax (1.0f, (float) juce::jmin (w, h) * 0.5f - 16.0f);
}

// Pozo profundo + ambiente cian + polvo estelar + anillos + marcas + oyente + vignette + rim.
inline void paintWellStatic (juce::Graphics& g, int w, int h)
{
    namespace th = ovni::ui::theme;
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;
    const float R  = padRadius (w, h);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    // pozo: MUCHO contraste centro/borde (#0e1523 → #020304)
    {
        juce::ColourGradient well (juce::Colour (0xff0e1523), cx, cy,
                                   juce::Colour (0xff020304), cx, cy - R * 1.28f, true);
        well.addColour (0.40, juce::Colour (0xff080c14));
        well.addColour (0.75, juce::Colour (0xff04060a));
        g.setGradientFill (well);
        g.fillRect (0, 0, w, h);
    }

    // ambiente cian tenue del fondo del pozo. MUY contenido en el centro (alpha 0.022 + el pico se
    // corre afuera del núcleo): el centro tiene que quedar OSCURO para que la estela/cometa viva lea
    // ARRIBA sin competir con un glow de fondo brillante (bug "movimiento por atrás del fondo").
    {
        juce::ColourGradient amb (th::cyan.withAlpha (0.022f), cx, cy,
                                  th::cyan.withAlpha (0.0f),   cx, cy - R * 0.9f, true);
        amb.addColour (0.35, th::cyan.withAlpha (0.020f));
        amb.addColour (0.7,  th::cyan.withAlpha (0.010f));
        g.setGradientFill (amb);
        g.fillRect (0, 0, w, h);
    }

    // polvo estelar horneado (~130 puntos, 2 tamaños, alpha 0.05–0.15) — LCG determinístico
    {
        juce::int64 seed = 4242;
        auto rnd = [&seed]() noexcept
        {
            seed = (seed * 16807) % 2147483647;
            return (float) seed / 2147483647.0f;
        };
        const juce::Colour dustCol (0xffbedeff);
        for (int i = 0; i < 130; ++i)
        {
            const float a  = rnd() * twoPi;
            const float rr = std::sqrt (rnd()) * R * 1.22f;
            const float x  = cx + std::cos (a) * rr;
            const float y  = cy + std::sin (a) * rr;
            const bool big = rnd() < 0.2f;
            const float al = (big ? 0.08f : 0.05f) + rnd() * 0.07f;
            const float d  = big ? 2.2f : 1.1f;
            g.setColour (dustCol.withAlpha (al));
            g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
    }

    // anillos de distancia: glow cian tenue BAJO la hairline. Subidos (antes 0.045 / 0x1f) para que la
    // PROFUNDIDAD del pozo se LEA (los críticos lo veían "vacío/plano": estaban demasiado tenues).
    for (const float f : { 0.3f, 0.62f, 0.94f })
    {
        const float rr = R * f;
        g.setColour (th::cyan.withAlpha (0.085f));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 3.5f);
        g.setColour (juce::Colour (0x33a0c0e0));
        g.drawEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f, 0.8f);
    }

    // marcas cardinales hairline (12, las mayores cada 3) — subidas un punto para acompañar los anillos
    g.setColour (juce::Colour (0x1ca0c0e0));
    for (int i = 0; i < 12; ++i)
    {
        const float a  = (float) i / 12.0f * twoPi;
        const float r1 = R * ((i % 3 == 0) ? 0.88f : 0.915f);
        g.drawLine (cx + std::cos (a) * R * 0.94f, cy + std::sin (a) * R * 0.94f,
                    cx + std::cos (a) * r1,        cy + std::sin (a) * r1, 1.0f);
    }

    // oyente: cruz + orbe central + label. Orbe contenido (alpha 0.14): un ancla quieta, NO un punto
    // brillante que se coma la cabeza-cometa cuando la órbita cruza el centro.
    g.setColour (th::txt.withAlpha (0.4f));
    g.drawLine (cx - 7.0f, cy, cx + 7.0f, cy, 1.0f);
    g.drawLine (cx, cy - 7.0f, cx, cy + 7.0f, 1.0f);
    g.setColour (th::txt.withAlpha (0.14f));
    g.fillEllipse (cx - 2.6f, cy - 2.6f, 5.2f, 5.2f);
    g.setColour (th::fnt.withAlpha (0.8f));
    g.setFont (ovni::ui::fonts::mono (8.0f));
    g.drawText ("LISTENER", juce::Rectangle<int> ((int) cx - 30, (int) cy + 12, 60, 12),
                juce::Justification::centred);
}

// Overlays del pozo (encima del fondo, debajo de la capa viva): scanlines + vignette + rim-shadow.
inline void paintWellOverlays (juce::Graphics& g, int w, int h)
{
    const float fw = (float) w, fh = (float) h;
    const float cx = fw * 0.5f, cy = fh * 0.5f;

    // scanlines sutiles (mockup .scan: multiply ~0.045 efectivo)
    g.setColour (juce::Colours::black.withAlpha (0.045f));
    for (float y = 0.0f; y < fh; y += 3.0f)
        g.fillRect (0.0f, y, fw, 1.0f);

    // vignette del pad (transparente hasta ~56%, cierra a casi-negro en el borde)
    {
        const juce::Colour edge (0xff020305);
        juce::ColourGradient vig (edge.withAlpha (0.0f),  cx, cy,
                                  edge.withAlpha (0.78f), cx, cy - juce::jmin (fw, fh) * 0.5f, true);
        vig.addColour (0.56, edge.withAlpha (0.0f));
        vig.addColour (0.84, edge.withAlpha (0.42f));
        g.setGradientFill (vig);
        g.fillRect (0, 0, w, h);
    }

    // rim del rebaje: el pozo se HUNDE en el chasis (insets de sombra en los 4 bordes + filo superior)
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

} // namespace pulsar::ui::detail
