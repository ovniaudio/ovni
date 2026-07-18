#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

// Imagen de fábrica de SUPERNOVA (RF1: nunca pantalla vacía). Procedural: un núcleo incandescente + un anillo
// de choque sobre un fondo frío azul-violeta — evoca una supernova y da una silueta reconocible cuando las
// partículas la muestrean. M0 usa esto; en release puede reemplazarse por un PNG de marca vía juce_add_binary_data.
namespace supernova
{
inline std::vector<uint8_t> makeFactoryImage (int w = 512, int h = 512)
{
    std::vector<uint8_t> px ((size_t) w * h * 4);
    auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto to8     = [] (float v) { return (uint8_t) (v < 0.0f ? 0.0f : (v > 1.0f ? 255.0f : v * 255.0f)); };

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float u = (x + 0.5f) / w - 0.5f;
            const float v = (y + 0.5f) / h - 0.5f;
            const float r = std::sqrt (u * u + v * v) * 2.0f;         // 0..~1.41

            const float core   = std::exp (-r * r * 7.0f);            // núcleo brillante
            const float ring   = std::exp (-std::pow ((r - 0.55f) / 0.10f, 2.0f)) * 0.7f;  // anillo de choque
            const float streak = 0.14f * std::exp (-r * r * 2.0f)
                                       * (1.0f - std::min (1.0f, std::abs (v) * 6.0f));      // veta ecuatorial
            const float val = clamp01 (core + ring + streak);

            const float cold[3] = { 0.05f, 0.06f, 0.12f };
            const float hot [3] = { 1.00f, 0.62f, 0.30f };
            const float white[3]= { 1.00f, 0.95f, 0.85f };

            float rgb[3];
            for (int c = 0; c < 3; ++c)
            {
                float base = cold[c] + (hot[c] - cold[c]) * val;
                base = base + (white[c] - base) * clamp01 (core * 1.2f);
                rgb[c] = base;
            }

            uint8_t* p = &px[((size_t) y * w + x) * 4];
            p[0] = to8 (rgb[0]);
            p[1] = to8 (rgb[1]);
            p[2] = to8 (rgb[2]);
            p[3] = 255;
        }
    return px;
}

// Imagen de REFERENCIA para los thumbnails del world browser (fix 3). La factory de arriba tiene el núcleo
// QUEMADO (blanco) → todos los mundos salían igual de sobreexpuestos y no se distinguían. Esta versión es
// LANDSCAPE (sin barras negras al rendear al aspecto del tile), con exposición MODERADA (pico ~0.72, no
// blanco) y ESTRUCTURA (sujeto legible + horizonte graduado + banda de color) → cada mundo muestra su FIRMA
// (color de la paleta, forma del glifo, figura 3D, silueta del cutout). NO se usa en el motor: sólo la
// preview del browser (el usuario diseña sobre SU imagen; ésta es el placeholder cuando no cargó ninguna).
inline std::vector<uint8_t> makeThumbnailReference (int w = 320, int h = 200)
{
    std::vector<uint8_t> px ((size_t) w * h * 4);
    auto clamp01 = [] (float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    auto to8     = [] (float v) { v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); return (uint8_t) (v * 255.0f); };
    auto smooth  = [] (float a, float b, float x) { float t = (x - a) / (b - a); t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3.0f - 2.0f * t); };

    // Sujeto: disco cálido descentrado (regla de tercios) — legible para figura/cutout/depth, sin quemarse.
    const float cx = 0.38f, cy = 0.46f, cr = 0.19f;

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float u = (x + 0.5f) / w;          // 0..1
            const float v = (y + 0.5f) / h;          // 0..1 (arriba=0)
            const float du = (u - cx) * (float) w / (float) h;   // corrige aspecto → disco redondo
            const float dv = (v - cy);
            const float rr = std::sqrt (du * du + dv * dv) / cr;  // 0 centro del sujeto

            // Fondo: gradiente diagonal frío (arriba noche → abajo horizonte índigo) + estructura de banda.
            const float grad  = smooth (0.0f, 1.0f, v * 0.7f + u * 0.3f);
            const float band  = 0.08f * std::exp (-std::pow ((v - 0.74f) / 0.06f, 2.0f));   // horizonte tenue
            float bg[3] = { 0.04f + 0.05f * grad + band * 1.1f,
                            0.05f + 0.04f * grad + band * 0.6f,
                            0.11f + 0.09f * grad + band * 0.4f };

            // Sujeto: disco graduado (pico ~0.55, NO blanco) con reborde frío → estructura + color, sin quemar.
            const float core = std::exp (-rr * rr * 2.4f);               // relleno suave, más contenido
            const float halo = 0.28f * std::exp (-std::pow ((rr - 1.0f) / 0.55f, 2.0f)); // borde/atmósfera
            const float warm[3] = { 0.55f, 0.36f, 0.22f };
            const float rim [3] = { 0.42f, 0.50f, 0.72f };               // reborde frío (contraluz)
            float rgb[3];
            for (int c = 0; c < 3; ++c)
            {
                float s = warm[c] * core + rim[c] * halo;
                rgb[c] = clamp01 (bg[c] * (1.0f - core * 0.8f) + s);
            }
            // Acento pequeño arriba-derecha (una "estrella") — punto de brillo puntual (no domina la exposición).
            const float su = (u - 0.80f) * (float) w / (float) h, sv = (v - 0.24f);
            const float star = 0.45f * std::exp (-(su * su + sv * sv) * 1100.0f);
            rgb[0] = clamp01 (rgb[0] + star * 0.9f); rgb[1] = clamp01 (rgb[1] + star * 0.9f); rgb[2] = clamp01 (rgb[2] + star);

            uint8_t* p = &px[((size_t) y * w + x) * 4];
            p[0] = to8 (rgb[0]); p[1] = to8 (rgb[1]); p[2] = to8 (rgb[2]); p[3] = 255;
        }
    return px;
}
}
