#pragma once
#include "Theme.h"
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <memory>

namespace ovni::ui
{
// =================================================================================================
// GLOW additivo del sello (rediseño 2026-06) — reproduce el `globalCompositeOperation = 'lighter'`
// de los mockups (pulsar/halo/dust §draw): el bloom de los visualizadores se ACUMULA capa sobre capa
// y revienta hacia el blanco donde se solapa, en vez de quedar lavado por el alpha-over normal de JUCE.
//
// Cómo: la subclase abre un `Bloom` del tamaño del componente, dibuja todas sus capas de luz con
// addSprite()/addDisc() (que hacen una SUMA saturada por píxel sobre un buffer ARGB premultiplicado
// propio), y al final hace compositeOnto(g). El buffer arranca negro-transparente; cada blit SUMA luz;
// el resultado se vuelca una sola vez sobre el chasis oscuro → mismo look "lighter" del mockup, con un
// único drawImage. CPU acotada: cada blit sólo toca su bounding-box; el buffer se cachea por tamaño.
//
// El sprite base es un disco radial BLANCO premultiplicado (núcleo lleno + cola suave). Se tiñe con el
// hue de familia al sumar (multiplicación del color × alpha del sprite), igual que el viejo blitGlow,
// pero acumulando. Compositor-friendly: cero gradientes por frame.
// =================================================================================================
class Bloom
{
public:
    // Disco radial blanco premultiplicado (perfil bloom: núcleo denso, cola suave). Compartido.
    static const juce::Image& sprite()
    {
        static const juce::Image s = []
        {
            constexpr int S = 160;
            juce::Image img (juce::Image::ARGB, S, S, true);
            const float c = S * 0.5f;
            juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < S; ++y)
                for (int x = 0; x < S; ++x)
                {
                    const float d = juce::jmin (1.0f, std::hypot ((float) x + 0.5f - c,
                                                                  (float) y + 0.5f - c) / c);
                    const float f = 1.0f - d;
                    const float a = f * f * (0.62f + 0.38f * f);   // núcleo lleno, cola suave (bloom real)
                    const auto  v = (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (a * 255.0f));
                    // premultiplicado: blanco con alpha a -> (a,a,a,a)
                    bd.setPixelColour (x, y, juce::Colour (v, v, v).withAlpha (a));
                }
            return img;
        }();
        return s;
    }

    // Prepara el buffer additivo (px-físico) para un componente w×h a la escala dada. Lo limpia a 0.
    void begin (int w, int h, float scale)
    {
        gctx.reset();   // soltar cualquier Graphics del frame anterior (refcount del buffer = 1)
        sc = juce::jmax (1.0f, scale);
        const int pw = juce::jmax (1, juce::roundToInt ((float) w * sc));
        const int ph = juce::jmax (1, juce::roundToInt ((float) h * sc));
        if (buf.isNull() || buf.getWidth() != pw || buf.getHeight() != ph)
            buf = juce::Image (juce::Image::ARGB, pw, ph, true);
        else
            buf.clear (buf.getBounds());
        lw = w; lh = h;
    }

    // Graphics sobre el buffer (coords lógicas) — para TRAZOS finos/núcleos (líneas, elipses) que se dibujan
    // con alpha-over dentro de la misma capa de luz. Al volcar sobre el chasis oscuro leen additivos junto a
    // los discos de addSprite(). Se crea perezosamente; addSprite() lo SUELTA antes de tocar píxeles para que
    // el Graphics y la BitmapData NUNCA coexistan sobre el mismo buffer (evita el copy-on-write que perdía
    // los blits). Sólo válido entre begin() y compositeOnto().
    juce::Graphics& gfx()
    {
        if (gctx == nullptr)
        {
            gctx = std::make_unique<juce::Graphics> (buf);
            gctx->addTransform (juce::AffineTransform::scale (sc));   // dibujo lógico, render físico
        }
        return *gctx;
    }

    // Suma un disco de luz centrado en (cx,cy) lógicos, radios (rx,ry) lógicos, teñido `tint` (su alpha
    // pondera la intensidad). SUMA saturada sobre el buffer → solapamientos revientan hacia el blanco.
    void addSprite (float cx, float cy, float rx, float ry, juce::Colour tint)
    {
        if (rx <= 0.3f || ry <= 0.3f) return;
        const float a = tint.getFloatAlpha();
        if (a <= 0.002f) return;

        gctx.reset();   // soltar el Graphics: su renderer y nuestra BitmapData no deben coexistir (evita COW)
        const auto& sp = sprite();
        juce::Image::BitmapData src (const_cast<juce::Image&> (sp), juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (buf, juce::Image::BitmapData::readWrite);

        // bounding-box físico del blit (recortado al buffer)
        const float pcx = cx * sc, pcy = cy * sc, prx = rx * sc, pry = ry * sc;
        const int x0 = juce::jmax (0, (int) std::floor (pcx - prx));
        const int y0 = juce::jmax (0, (int) std::floor (pcy - pry));
        const int x1 = juce::jmin (dst.width  - 1, (int) std::ceil (pcx + prx));
        const int y1 = juce::jmin (dst.height - 1, (int) std::ceil (pcy + pry));
        if (x1 < x0 || y1 < y0) return;

        const float tr = tint.getFloatRed(), tg = tint.getFloatGreen(), tb = tint.getFloatBlue();
        const float spc = (float) src.width * 0.5f;
        const float invX = (prx > 0.0f) ? (spc / prx) : 0.0f;     // físico-dst -> px-sprite
        const float invY = (pry > 0.0f) ? (spc / pry) : 0.0f;

        for (int y = y0; y <= y1; ++y)
        {
            const float sy = (float) (y - pcy) * invY + spc;
            const int   syi = (int) sy;
            if (syi < 0 || syi >= src.height) continue;
            auto* drow = (juce::uint8*) dst.getLinePointer (y);
            const auto* srow = src.getLinePointer (syi);
            for (int x = x0; x <= x1; ++x)
            {
                const float sx = (float) (x - pcx) * invX + spc;
                const int   sxi = (int) sx;
                if (sxi < 0 || sxi >= src.width) continue;
                // alpha del sprite (premultiplicado blanco => canal alpha = intensidad)
                const juce::uint8 sa = srow[(size_t) sxi * (size_t) src.pixelStride + 3];
                if (sa == 0) continue;
                const float lum = (float) sa * (1.0f / 255.0f) * a;   // intensidad de ESTA capa
                juce::uint8* px = drow + (size_t) x * (size_t) dst.pixelStride;
                // Índices de byte del PixelARGB de JUCE (portable: macOS LE = {B,G,R,A}).
                addSat (px[juce::PixelARGB::indexR], tr * lum);
                addSat (px[juce::PixelARGB::indexG], tg * lum);
                addSat (px[juce::PixelARGB::indexB], tb * lum);
                addSat (px[juce::PixelARGB::indexA], lum);
            }
        }
    }

    void addSprite (float cx, float cy, float r, juce::Colour tint) { addSprite (cx, cy, r, r, tint); }

    // Vuelca el buffer acumulado sobre el Graphics real (alpha-over normal; el chasis es casi negro →
    // el resultado lee como additivo). `g` está en coords lógicas; el buffer está en px-físico.
    void compositeOnto (juce::Graphics& g)
    {
        gctx.reset();   // flush de los trazos pendientes (suelta el Graphics del buffer)
        if (buf.isValid())
            g.drawImageTransformed (buf, juce::AffineTransform::scale (1.0f / sc));
    }

    int   logicalW() const noexcept { return lw; }
    int   logicalH() const noexcept { return lh; }
    float scale()    const noexcept { return sc; }

private:
    static inline void addSat (juce::uint8& dst, float add) noexcept
    {
        const int v = (int) dst + (int) std::lround (add * 255.0f);
        dst = (juce::uint8) (v > 255 ? 255 : v);
    }

    juce::Image buf;
    std::unique_ptr<juce::Graphics> gctx;   // Graphics sobre buf (coords lógicas); vive entre begin/composite
    float sc = 1.0f;
    int   lw = 0, lh = 0;
};
}
