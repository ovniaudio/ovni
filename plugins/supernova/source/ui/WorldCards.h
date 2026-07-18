#pragma once
// WorldCards — la FIRMA visual de cada mundo como imagen PROCEDURAL instantánea (pedido de Joaquín:
// "en vez de un render en tiempo real que no carga, una imagen que simule lo que hace, que cargue en
// el momento"). Cero GPU, cero hilos, determinista (seed = índice): 36+ cards en <5ms total.
// Cada card se deriva de los PARAMS del preset (los mismos que applyFactory): motion dibuja el patrón,
// shape el glifo, palette/hue/sat el color, trails/links/glow/kaleido/figure/cutout los rasgos.
#include <juce_graphics/juce_graphics.h>
#include <cstring>
#include <vector>
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace supernova::cards
{
namespace pid = supernova::params::id;

// Param del preset o su default (mismas semánticas que applyFactory: id omitido = default).
inline float cardParam (const ovni::presets::FactoryPreset& p, const char* id)
{
    for (const auto& q : p.params) if (std::strcmp (q.id, id) == 0) return q.value;
    return pid::paramDefault (id);
}

// RNG determinista (xorshift32) — misma card en cada apertura, testeable.
struct CardRng
{
    uint32_t s;
    explicit CardRng (uint32_t seed) : s (seed ? seed : 1u) {}
    float next()               { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 16777216.0f; }
    float range (float a, float b) { return a + (b - a) * next(); }
};

// Duo de color por paleta (LookRamps): bg + tinta + acento. Índice 0 = Original (espacio + incandescente).
struct CardPalette { juce::Colour bg, ink, acc; bool paper; };
inline CardPalette paletteDuo (int idx)
{
    switch (idx)
    {
        case 1:  return { juce::Colour (0xff05060a), juce::Colour (0xffff8a00), juce::Colour (0xfffff06a), false }; // Thermal
        case 2:  return { juce::Colour (0xff0a0406), juce::Colour (0xffff2e63), juce::Colour (0xffffa9c0), false }; // Infrared
        case 3:  return { juce::Colour (0xfff2ead8), juce::Colour (0xff1d2a4a), juce::Colour (0xff44598c), true  }; // Duotone
        case 4:  return { juce::Colour (0xff0d2b4e), juce::Colour (0xffddebff), juce::Colour (0xff7fb4e8), false }; // Cyanotype
        case 5:  return { juce::Colour (0xff0b0c0f), juce::Colour (0xffd9dee6), juce::Colour (0xff8a94a6), false }; // Chrome
        case 6:  return { juce::Colour (0xff140a1e), juce::Colour (0xffff71ce), juce::Colour (0xff01cdfe), false }; // Vaporwave
        case 7:  return { juce::Colour (0xff07040d), juce::Colour (0xffff3af2), juce::Colour (0xff29f1ff), false }; // Neon
        case 8:  return { juce::Colour (0xff0b0303), juce::Colour (0xffff4d1c), juce::Colour (0xffffc24d), false }; // Magma
        case 9:  return { juce::Colour (0xff040a12), juce::Colour (0xff7fd4ff), juce::Colour (0xffc9efff), false }; // Cold Fire
        case 10: return { juce::Colour (0xff0a0514), juce::Colour (0xffb14dff), juce::Colour (0xffe6c9ff), false }; // Ultraviolet
        case 11: return { juce::Colour (0xff120c06), juce::Colour (0xffd9a86a), juce::Colour (0xfff2dcbb), false }; // Sepia
        case 12: return { juce::Colour (0xfff4efe4), juce::Colour (0xffe0442e), juce::Colour (0xff2857a4), true  }; // Riso
        case 13: return { juce::Colour (0xff05100a), juce::Colour (0xff5fd68a), juce::Colour (0xffc4f0d2), false }; // Forest
        case 14: return { juce::Colour (0xff0c0d0f), juce::Colour (0xffaab2bd), juce::Colour (0xffe2e8ee), false }; // Pewter
        case 15: return { juce::Colour (0xff060504), juce::Colour (0xffe8b84b), juce::Colour (0xfffff2c9), false }; // Black Gold
        default: return { juce::Colour (0xff0a0b10), juce::Colour (0xffffa640), juce::Colour (0xffffe0b0), false }; // Original
    }
}

// Un punto de la firma: posición + dirección de flujo (para dash/trails).
struct Pt { float x, y, ang; };

// Renderiza la card de un mundo. w×h típicos 256×132 (el grid recorta crop-to-fill).
inline juce::Image renderWorldCard (const ovni::presets::FactoryPreset& p, int index, int w, int h)
{
    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);
    CardRng rng (0x9E3779B9u * (uint32_t) (index + 1));

    const float intensity = cardParam (p, pid::INTENSITY)     / 100.0f;
    const float chaos     = cardParam (p, pid::CHAOS)         / 100.0f;
    const float psize     = cardParam (p, pid::PARTICLE_SIZE) / 100.0f;
    const float glow      = cardParam (p, pid::GLOW)          / 100.0f;
    const float trails    = cardParam (p, "trails")           / 100.0f;
    const float links     = cardParam (p, "links")            / 100.0f;
    const float density   = cardParam (p, "density")          / 100.0f;
    const float jitterG   = cardParam (p, "jitterGain")       / 100.0f;
    const float scatter   = cardParam (p, "scatter")          / 100.0f;
    const float momentum  = cardParam (p, "momentum")         / 100.0f;
    const float gravity   = cardParam (p, "gravity")          / 100.0f;   // −1..1
    const float sat       = cardParam (p, "sat")              / 100.0f;
    const float hueShift  = cardParam (p, "hue")              / 360.0f;   // −0.5..0.5
    const float hueCycle  = cardParam (p, "hueCycle")         / 100.0f;
    const float cutout    = cardParam (p, "cutout")           / 100.0f;
    const float depth3d   = cardParam (p, "depth")            / 100.0f;
    const float orbit     = cardParam (p, "orbit")            / 100.0f;
    const int   motion    = (int) cardParam (p, "motion");
    const int   shape     = (int) cardParam (p, "shape");
    const int   figure    = (int) cardParam (p, "figure");
    const int   kaleido   = (int) cardParam (p, "kaleido");
    const int   palette   = (int) cardParam (p, pid::PALETTE);

    CardPalette pal = paletteDuo (palette);
    if (palette == 0)   // Original: hue/sat del preset tiñen la tinta incandescente
    {
        pal.ink = pal.ink.withRotatedHue (hueShift).withMultipliedSaturation (juce::jlimit (0.2f, 1.6f, sat * 2.0f));
        pal.acc = pal.acc.withRotatedHue (hueShift);
    }
    const bool paper = pal.paper;
    const float cx = w * 0.5f, cy = h * 0.5f;

    // ---- fondo: gradiente + viñeta (cutout = casi negro, la escultura flota) --------------------
    if (cutout > 0.6f && ! paper)
        g.fillAll (juce::Colour (0xff030308));
    else
    {
        juce::ColourGradient bg (pal.bg.brighter (paper ? 0.06f : 0.06f + 0.20f * intensity), cx, 0.0f,
                                 pal.bg.darker (paper ? 0.02f : 0.35f), cx, (float) h, false);
        g.setGradientFill (bg); g.fillAll();
    }
    if (! paper)
    {
        juce::ColourGradient vig (juce::Colours::transparentBlack, cx, cy,
                                  juce::Colours::black.withAlpha (0.30f), 0.0f, 0.0f, true);
        g.setGradientFill (vig); g.fillRect (0, 0, w, h);
    }

    // ---- puntos de la firma según MOTION ---------------------------------------------------------
    const int mirrors = kaleido <= 0 ? 1 : juce::jlimit (2, 8, 2 * kaleido);        // choice: Off/2/4/6/8
    int n = (int) (12.0f + density * 34.0f);   // densos (Deep Field 85) → campo poblado de verdad
    if (mirrors > 1) n = juce::jmax (8, (int) (n / std::sqrt ((float) mirrors)));
    const float jit = (jitterG * 0.6f + scatter * 0.8f + chaos * 0.6f) * 7.0f;

    std::vector<Pt> pts; pts.reserve ((size_t) (n * mirrors));
    auto push = [&] (float x, float y, float ang)
    {
        x += rng.range (-jit, jit); y += rng.range (-jit, jit);
        ang += rng.range (-chaos, chaos) * 0.9f;
        pts.push_back ({ x, y, ang });
    };
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) juce::jmax (1, n - 1);
        switch (motion)
        {
            case 1: {   // Materia: cúmulo gaussiano que respira
                const float a = rng.range (0.0f, juce::MathConstants<float>::twoPi);
                const float r = (rng.next() + rng.next()) * 0.5f * h * 0.42f;
                push (cx + std::cos (a) * r * 1.35f, cy + std::sin (a) * r, a + juce::MathConstants<float>::halfPi);
                break; }
            case 2: {   // Onda: filas horizontales onduladas
                const int row = i % 3;
                const float x = t * (float) w;
                const float y = h * (0.30f + 0.20f * (float) row) + std::sin (t * 6.3f + (float) row * 1.7f) * h * 0.10f;
                push (x, y, std::cos (t * 6.3f + (float) row * 1.7f) * 0.6f);
                break; }
            case 3: {   // Vórtices: brazos espirales
                const int arm = i % 2;
                const float a = t * 3.6f * juce::MathConstants<float>::pi + (float) arm * juce::MathConstants<float>::pi;
                const float r = 4.0f + t * h * 0.52f;
                push (cx + std::cos (a) * r * 1.25f, cy + std::sin (a) * r, a + juce::MathConstants<float>::halfPi);
                break; }
            case 4: {   // Radial: estallido desde el centro
                const float a = rng.range (0.0f, juce::MathConstants<float>::twoPi);
                const float r = (0.18f + 0.82f * std::sqrt (rng.next())) * h * 0.55f;
                push (cx + std::cos (a) * r * 1.3f, cy + std::sin (a) * r, a);
                break; }
            default: {  // Contornos: 3 curvas fluidas a lo ancho
                const int band = i % 3;
                const float x = t * (float) w;
                const float y = h * (0.26f + 0.24f * (float) band)
                              + std::sin (t * 4.2f + (float) band * 2.3f + (float) index) * h * 0.13f;
                push (x, y, std::cos (t * 4.2f + (float) band * 2.3f + (float) index) * 0.8f);
                break; }
        }
    }
    // gravity inclina el campo (Meteor/Ember caen): corrimiento vertical proporcional a x-fase
    if (std::abs (gravity) > 0.05f)
        for (auto& q : pts) { q.y += gravity * 14.0f * rng.next(); q.ang = juce::MathConstants<float>::halfPi * (gravity > 0 ? 1.0f : -1.0f) * 0.6f + q.ang * 0.4f; }

    // kaleido: replicar rotado alrededor del centro (mandala)
    if (mirrors > 1)
    {
        const size_t base = pts.size();
        for (int m = 1; m < mirrors; ++m)
        {
            const float rot = juce::MathConstants<float>::twoPi * (float) m / (float) mirrors;
            const float cs = std::cos (rot), sn = std::sin (rot);
            for (size_t i = 0; i < base; ++i)
            {
                const float dx = pts[i].x - cx, dy = pts[i].y - cy;
                pts.push_back ({ cx + dx * cs - dy * sn, cy + dx * sn + dy * cs, pts[i].ang + rot });
            }
        }
    }

    // ---- cutout: recorte a una silueta orgánica (la "escultura") --------------------------------
    juce::Graphics::ScopedSaveState maskState (g);
    if (cutout > 0.6f)
    {
        juce::Path blob;
        constexpr int nodes = 9;
        for (int i = 0; i <= nodes; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) (i % nodes) / (float) nodes;
            const float r = h * rng.range (0.34f, 0.50f);
            const float px = cx + std::cos (a) * r * 1.35f, py = cy + std::sin (a) * r;
            if (i == 0) blob.startNewSubPath (px, py); else blob.lineTo (px, py);
        }
        blob.closeSubPath();
        blob = blob.createPathWithRoundedCorners (18.0f);
        g.setColour (pal.ink.withAlpha (0.06f)); g.fillPath (blob);                       // cuerpo tenue de la escultura
        g.setColour (pal.ink.withAlpha (0.30f)); g.strokePath (blob, juce::PathStrokeType (1.4f));
        g.reduceClipRegion (blob);
    }

    // ---- figure/3D: andamiaje débil detrás de las partículas -------------------------------------
    const juce::Colour scaffold = pal.ink.withAlpha (paper ? 0.22f : 0.16f);
    g.setColour (scaffold);
    const float R = h * 0.40f;
    if (figure == 1)      { g.drawEllipse (cx - R, cy - R, R * 2, R * 2, 1.0f);                         // Esfera
                            g.drawEllipse (cx - R, cy - R * 0.45f, R * 2, R * 0.9f, 0.8f); }
    else if (figure == 2) { juce::Path sp; for (int i = 0; i <= 40; ++i) { const float t2 = (float) i / 40.0f;
                            const float a = t2 * 4.5f * juce::MathConstants<float>::pi; const float r = t2 * R;
                            const float px = cx + std::cos (a) * r * 1.2f, py = cy + std::sin (a) * r;
                            if (i == 0) sp.startNewSubPath (px, py); else sp.lineTo (px, py); }
                            g.strokePath (sp, juce::PathStrokeType (0.9f)); }                            // Espiral
    else if (figure == 3) for (int k = 1; k <= 3; ++k) g.drawEllipse (cx - R * k / 3.0f * 1.3f, cy - R * k / 3.0f * 0.7f,
                            R * 2 * k / 3.0f * 1.3f, R * 2 * k / 3.0f * 0.7f, 0.8f);                     // Anillos
    else if (figure == 4) { for (int gx = 1; gx < 6; ++gx) g.drawVerticalLine   ((int) (w * gx / 6.0f), cy - R, cy + R);
                            for (int gy = 1; gy < 4; ++gy) g.drawHorizontalLine ((int) (cy - R + 2 * R * gy / 4.0f), cx - R * 1.4f, cx + R * 1.4f); } // Grilla
    else if (figure == 5) { juce::Path h1, h2; for (int i = 0; i <= 40; ++i) { const float t2 = (float) i / 40.0f;
                            const float px = cx - R * 1.4f + t2 * R * 2.8f; const float py1 = cy + std::sin (t2 * 9.0f) * R * 0.5f;
                            const float py2 = cy - std::sin (t2 * 9.0f) * R * 0.5f;
                            if (i == 0) { h1.startNewSubPath (px, py1); h2.startNewSubPath (px, py2); }
                            else        { h1.lineTo (px, py1);          h2.lineTo (px, py2); } }
                            g.strokePath (h1, juce::PathStrokeType (0.9f)); g.strokePath (h2, juce::PathStrokeType (0.9f)); } // Hélice
    if (depth3d > 0.1f && orbit > 0.05f)   // órbita 3D inclinada + satélite
    {
        const float orx = R * 1.5f, ory = R * (0.45f + 0.2f * (1.0f - depth3d));
        g.setColour (pal.acc.withAlpha (0.22f));
        g.drawEllipse (cx - orx, cy - ory, orx * 2, ory * 2, 1.0f);
        const float sa = rng.range (0.0f, juce::MathConstants<float>::twoPi);
        g.setColour (pal.acc); g.fillEllipse (cx + std::cos (sa) * orx - 2.0f, cy + std::sin (sa) * ory - 2.0f, 4.0f, 4.0f);
    }

    // ---- glow: halos suaves detrás -----------------------------------------------------------------
    if (glow > 0.05f)
        for (size_t i = 0; i < pts.size(); i += 3)
        {
            const float r = 9.0f + glow * 22.0f;
            juce::ColourGradient halo (pal.ink.withAlpha ((paper ? 0.05f : 0.05f) + glow * 0.08f),
                                       pts[i].x, pts[i].y, juce::Colours::transparentBlack,
                                       pts[i].x + r, pts[i].y, true);
            g.setGradientFill (halo);
            g.fillEllipse (pts[i].x - r, pts[i].y - r, r * 2, r * 2);
        }

    // ---- links: red entre vecinos -------------------------------------------------------------------
    if (links > 0.02f)
    {
        g.setColour (pal.ink.withAlpha (0.12f + links * 0.30f));
        for (size_t i = 0; i + 1 < pts.size(); ++i)
            for (size_t j = i + 1; j < juce::jmin (pts.size(), i + 4); ++j)
            {
                const float dx = pts[j].x - pts[i].x, dy = pts[j].y - pts[i].y;
                if (dx * dx + dy * dy < 40.0f * 40.0f)
                    g.drawLine (pts[i].x, pts[i].y, pts[j].x, pts[j].y, 0.7f);
            }
    }

    // ---- trails + glifos ------------------------------------------------------------------------------
    const float ps = 1.6f + psize * 5.2f;
    const float trailLen = trails * (9.0f + momentum * 17.0f);
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const float t = (float) i / (float) (pts.size() > 1 ? pts.size() - 1 : 1);
        juce::Colour ink = pal.ink;
        if (hueCycle > 0.01f) ink = ink.withRotatedHue (hueCycle * 0.9f * (t - 0.5f));   // centrado: no verdea la base
        else if (i % 5 == 4)  ink = pal.acc;                       // acento salpicado
        const float dx = std::cos (pts[i].ang), dy = std::sin (pts[i].ang);

        if (trailLen > 1.5f)                                        // estela que se desvanece
            for (int s = 1; s <= 3; ++s)
            {
                const float f0 = trailLen * (float) (s - 1) / 3.0f, f1 = trailLen * (float) s / 3.0f;
                g.setColour (ink.withAlpha (0.34f * (1.0f - (float) s / 3.6f)));
                g.drawLine (pts[i].x - dx * f0, pts[i].y - dy * f0,
                            pts[i].x - dx * f1, pts[i].y - dy * f1, juce::jmax (0.7f, ps * 0.3f));
            }

        g.setColour (ink.withAlpha (paper ? 0.92f : 0.88f));
        const float x = pts[i].x, y = pts[i].y;
        switch (shape)
        {
            case 1: g.fillEllipse (x - ps * 0.9f, y - ps * 0.9f, ps * 1.8f, ps * 1.8f); break;              // Disc
            case 2: g.drawEllipse (x - ps, y - ps, ps * 2, ps * 2, juce::jmax (0.8f, ps * 0.28f)); break;   // Ring
            case 3: g.drawLine (x - dx * ps * 1.7f, y - dy * ps * 1.7f,
                                x + dx * ps * 1.7f, y + dy * ps * 1.7f, juce::jmax (0.9f, ps * 0.4f)); break; // Dash
            case 4: { juce::Path tr; tr.addTriangle (x, y - ps * 1.2f, x - ps, y + ps * 0.8f, x + ps, y + ps * 0.8f);
                      tr.applyTransform (juce::AffineTransform::rotation (pts[i].ang, x, y)); g.fillPath (tr); break; } // Tri
            case 5: { juce::Path q; q.addRectangle (x - ps * 0.9f, y - ps * 0.9f, ps * 1.8f, ps * 1.8f);
                      q.applyTransform (juce::AffineTransform::rotation (pts[i].ang * 0.5f, x, y)); g.fillPath (q); break; } // Quad
            case 6: { const float sl = ps * 1.9f;                                                            // Spark
                      g.drawLine (x - sl, y, x + sl, y, 0.9f); g.drawLine (x, y - sl, x, y + sl, 0.9f);
                      g.fillEllipse (x - 1.1f, y - 1.1f, 2.2f, 2.2f); break; }
            default: g.fillEllipse (x - ps * 0.55f, y - ps * 0.55f, ps * 1.1f, ps * 1.1f); break;            // Dot
        }
    }

    // ---- grano sutil (atmósfera del sello) ------------------------------------------------------------
    if (! paper)
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        for (int i = 0; i < 90; ++i) { const float x = rng.next() * (float) w, y = rng.next() * (float) h;
                                       g.fillRect (x, y, 1.0f, 1.0f); }
    }
    return img;
}
} // namespace supernova::cards
