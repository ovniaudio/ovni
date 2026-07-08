#pragma once
#include "Theme.h"
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <cmath>

namespace ovni::ui
{
// Fondo atmósfera del sello (rediseño 2026-06, mockup §buildAtmo + #plugin::before): luz radial
// direccional desde arriba-izquierda cayendo a negro casi puro, GLOW VOLUMÉTRICO del hue de FAMILIA
// emanando del visualizador (el "color que se respeta" — cian/magenta/verde saturado, no lavado),
// curvas TOPOGRÁFICAS orgánicas teñidas, sweep diagonal de vidrio, vignette perimetral, scanlines y
// GRANO real (tile de ruido horneado). Se hornea UNA vez a una juce::Image a resolución FÍSICA
// (lógico × escala) → nítido en Retina/4K. CPU ~0 por frame.
//
// El editor setea el hue de familia ANTES del primer render (setHue): el tinte se nota de verdad.
// Uso típico desde el editor::paint:
//   panel.setHue (ui::theme::magenta);
//   const float sc = g.getInternalContext().getPhysicalPixelScaleFactor();
//   if (panel.needsRender (getWidth(), getHeight(), sc)) panel.render (getWidth(), getHeight(), sc);
//   panel.paint (g);
class Panel
{
public:
    // Hue de FAMILIA (Movimiento=cian · Espacio=magenta · Espectral=verde · Textura=ámbar). Re-hornea
    // si cambió. Default cian = backward-compatible con plugins que aún no lo seteen.
    void setHue (juce::Colour h)
    {
        if (h.getARGB() != hue.getARGB()) { hue = h; img = juce::Image(); }
    }

    bool needsRender (int w, int h, float scale) const
    {
        return img.isNull() || rw != w || rh != h || std::abs (rs - scale) > 0.01f;
    }

    void render (int w, int h, float scale)
    {
        if (w <= 0 || h <= 0) return;
        rw = w; rh = h; rs = juce::jmax (1.0f, scale);
        const int pw = juce::jmax (1, juce::roundToInt ((float) w * rs));
        const int ph = juce::jmax (1, juce::roundToInt ((float) h * rs));

        img = juce::Image (juce::Image::ARGB, pw, ph, true);
        juce::Graphics g (img);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::scale (rs));   // dibujo en coords lógicas, render físico
            const float fw = (float) w, fh = (float) h;

            // base del chasis
            g.fillAll (juce::Colour (0xff05070b));

            // luz fría DIRECCIONAL entrando desde arriba-izquierda, TEÑIDA hacia el hue de familia (el mockup
            // tiñe la luz volumétrica: cian/magenta/verde). Cae a negro casi puro.
            {
                const auto lit = hue.withMultipliedSaturation (0.78f).withMultipliedBrightness (0.95f);
                juce::ColourGradient light (lit.withAlpha (0.115f), fw * 0.16f, -fh * 0.13f,
                                            lit.withAlpha (0.0f),   fw * 0.16f + fw * 1.23f, -fh * 0.13f, true);
                light.addColour (0.34, lit.withAlpha (0.052f));
                light.addColour (0.68, lit.withAlpha (0.014f));
                g.setGradientFill (light);
                g.fillAll();
            }

            // GLOW VOLUMÉTRICO del hue de FAMILIA emanando del centro-visualizador (mockup #plugin::before):
            // EL color que el cliente pide respetar. Saturado y presente, radial closest-side.
            {
                const float gx = fw * 0.5f, gy = fh * 0.46f;
                const float gr = juce::jmax (fw, fh) * 0.52f;
                const auto fam = hue.withMultipliedSaturation (1.06f);
                juce::ColourGradient glow (fam.withAlpha (0.090f), gx, gy,
                                           fam.withAlpha (0.0f),   gx, gy - gr, true);
                glow.addColour (0.42, fam.withAlpha (0.034f));
                glow.addColour (0.74, fam.withAlpha (0.010f));
                g.setGradientFill (glow);
                g.fillAll();
            }

            // caída a negro casi puro abajo-derecha
            {
                juce::ColourGradient fall (theme::bg0.withAlpha (0.62f), fw * 0.94f, fh * 1.08f,
                                           theme::bg0.withAlpha (0.0f),  fw * 0.94f, fh * 1.08f - fw * 1.07f, true);
                g.setGradientFill (fall);
                g.fillAll();
            }

            // topografía orgánica — anillos cerrados deformados por value-noise, teñidos hacia el hue de familia
            const auto topoCol = theme::txt.interpolatedWith (hue, 0.45f);   // mezcla fría + tinte de familia
            paintTopo (g, topoCol, fw * 0.74f, fh * 0.20f, fw * 0.35f, 6, 0.040f, 0.80f);
            paintTopo (g, topoCol, fw * 0.13f, fh * 0.83f, fw * 0.29f, 5, 0.032f, 0.86f);
            paintTopo (g, topoCol, fw * 0.50f, fh * 0.55f, fw * 0.65f, 7, 0.022f, 0.74f);

            // sweep diagonal de luz — reflejo de vidrio del panel
            {
                juce::ColourGradient sweep (juce::Colour (0x00c3deff), 0.0f, 0.0f,
                                            juce::Colour (0x00c3deff), fw, fh * 0.88f, false);
                sweep.addColour (0.28, juce::Colour (0x00c3deff));
                sweep.addColour (0.45, juce::Colour (0x0ac3deff));
                sweep.addColour (0.52, juce::Colour (0x04c3deff));
                sweep.addColour (0.68, juce::Colour (0x00c3deff));
                g.setGradientFill (sweep);
                g.fillAll();
            }

            // vignette perimetral (cierra a negro en los bordes)
            {
                const juce::Colour edge (0xff020305);
                juce::ColourGradient vig (edge.withAlpha (0.0f),  fw * 0.5f, fh * 0.5f,
                                          edge.withAlpha (0.54f), fw * 0.5f, fh * 0.5f - juce::jmax (fw, fh) * 0.70f, true);
                vig.addColour (0.49, edge.withAlpha (0.0f));        // radio interior limpio (~0.34 del máx)
                vig.addColour (0.70, edge.withAlpha (0.24f));
                g.setGradientFill (vig);
                g.fillAll();
            }

            // scanlines horizontales ultra-sutiles
            g.setColour (juce::Colours::black.withAlpha (0.028f));
            for (float y = 0.0f; y < fh; y += 3.0f)
                g.fillRect (0.0f, y, fw, 1.0f);
        }

        // GRANO real (en píxeles FÍSICOS): tile de ruido claro/oscuro horneado — materia, no banding
        g.setTiledImageFill (grainTile(), 0, 0, 1.0f);
        g.fillRect (0, 0, pw, ph);
    }

    // Blit de la capa cacheada con la transformación inversa (1/escala) -> nítida en Retina.
    void paint (juce::Graphics& g) const
    {
        if (img.isValid())
            g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / rs));
    }

private:
    // value-noise 1D determinístico (lattice fijo) — para las topográficas orgánicas
    static float vnoise (float x) noexcept
    {
        static const auto lattice = []
        {
            std::array<float, 256> t {};
            juce::Random rng (4242);
            for (auto& v : t) v = rng.nextFloat() * 2.0f - 1.0f;
            return t;
        }();
        const int   i = (int) std::floor (x);
        const float f = x - (float) i;
        const float u = f * f * (3.0f - 2.0f * f);
        return lattice[(size_t) (i & 255)] * (1.0f - u) + lattice[(size_t) ((i + 1) & 255)] * u;
    }

    // un sistema de anillos topográficos cerrados, deformados con noise (mockup §topo)
    static void paintTopo (juce::Graphics& g, juce::Colour col, float cx, float cy, float baseR,
                           int rings, float alpha, float squash)
    {
        g.setColour (col.withAlpha (alpha));
        for (int i = 1; i <= rings; ++i)
        {
            const float r0 = baseR * (float) i / (float) rings;
            juce::Path p;
            constexpr int kSegs = 72;
            for (int s = 0; s <= kSegs; ++s)
            {
                const float th = (float) s / (float) kSegs * juce::MathConstants<float>::twoPi;
                const float rr = r0 * (1.0f
                    + 0.24f * vnoise (std::cos (th) * 2.1f + (float) i * 1.73f + cx * 0.011f)
                    + 0.15f * vnoise (std::sin (th) * 3.3f + (float) i * 0.91f + cy * 0.017f));
                const float x = cx + std::cos (th) * rr;
                const float y = cy + std::sin (th) * rr * squash;
                if (s == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
        }
    }

    // tile 128×128 de grano (mitad clarea, mitad oscurece — emula el blend overlay del mockup)
    static const juce::Image& grainTile()
    {
        static const juce::Image tile = []
        {
            constexpr int kSize = 128;
            juce::Image t (juce::Image::ARGB, kSize, kSize, true);
            juce::Random rng (1337);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    const float v = rng.nextFloat();            // 0..1
                    const float a = std::abs (v - 0.5f) * 0.11f; // alpha máx ~0.055
                    t.setPixelAt (x, y, (v >= 0.5f ? juce::Colours::white : juce::Colours::black)
                                            .withAlpha (a));
                }
            return t;
        }();
        return tile;
    }

    juce::Image  img;
    juce::Colour hue = theme::cyan;   // hue de familia (lo setea el editor)
    int   rw = 0, rh = 0;
    float rs = 0.0f;
};
}
