// [supernova][field] — ImageField: el análisis de contenido que mata el "hueco que late en el medio" (feedback
// de Joaquín). Puro (sin GPU): centroide de luminancia, pesos de saliencia, flow tangente a los bordes.
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include "image/ImageField.h"

using supernova::ImageField;

namespace
{
// Imagen sintética W×H negra con un rectángulo blanco [x0,x1)×[y0,y1).
std::vector<uint8_t> rectImage (int W, int H, int x0, int y0, int x1, int y1)
{
    std::vector<uint8_t> px ((size_t) W * H * 4, 0);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            uint8_t* p = &px[((size_t) y * W + x) * 4];
            const bool in = (x >= x0 && x < x1 && y >= y0 && y < y1);
            p[0] = p[1] = p[2] = in ? 255 : 0;
            p[3] = 255;
        }
    return px;
}
}

TEST_CASE ("field: el centroide cae en el SUJETO, no en el centro de la pantalla", "[supernova][field]")
{
    // Cuadrado brillante arriba a la izquierda (centro en ~0.25, 0.25).
    const int W = 64, H = 64;
    auto img = rectImage (W, H, 8, 8, 24, 24);
    const auto f = ImageField::compute (img.data(), W, H, 32, 32);

    REQUIRE (std::abs (f.centroidX - 0.25f) < 0.05f);
    REQUIRE (std::abs (f.centroidY - 0.25f) < 0.05f);
}

TEST_CASE ("field: sin imagen → neutro (centro geométrico, pesos 1, flow 0)", "[supernova][field]")
{
    const auto f = ImageField::compute (nullptr, 0, 0, 16, 16);
    REQUIRE (f.centroidX == 0.5f);
    REQUIRE (f.centroidY == 0.5f);
    REQUIRE (f.weight.size() == 16u * 16u);
    for (float w : f.weight) REQUIRE (w == 1.0f);
    for (float v : f.flowXY) REQUIRE (v == 0.0f);
}

TEST_CASE ("field: los pesos separan sujeto (alto) de fondo (piso, nunca 0)", "[supernova][field]")
{
    const int W = 64, H = 64;
    auto img = rectImage (W, H, 32, 32, 64, 64);   // cuadrante inferior-derecho blanco
    const auto f = ImageField::compute (img.data(), W, H, 32, 32);

    const float wBg  = f.weight[(size_t) 4 * 32 + 4];     // celda en el fondo negro
    const float wSub = f.weight[(size_t) 24 * 32 + 24];   // celda en el sujeto blanco
    REQUIRE (wSub > 0.95f);
    REQUIRE (wBg >= 0.25f);          // piso: el fondo acompaña, no muere
    REQUIRE (wBg < 0.30f);
    REQUIRE (wSub > wBg * 3.0f);     // separación clara
}

TEST_CASE ("field: subject — la silueta estalla HACIA AFUERA (sd con signo + burst = normal exterior)", "[supernova][field]")
{
    // Saliencia sintética: disco brillante centrado en (0.3, 0.3) de radio 0.15 sobre fondo 0.
    const int G = 64;
    std::vector<float> sal ((size_t) G * G, 0.0f);
    for (int y = 0; y < G; ++y)
        for (int x = 0; x < G; ++x)
        {
            const float dx = (x + 0.5f) / G - 0.3f, dy = (y + 0.5f) / G - 0.3f;
            if (dx * dx + dy * dy < 0.15f * 0.15f) sal[(size_t) y * G + x] = 1.0f;
        }
    const auto s = supernova::ImageField::computeSubject (sal, G, G, 0.75f);

    auto at = [&] (float u, float v) { return (size_t) ((int) (v * G)) * G + (int) (u * G); };

    // Centro del disco: adentro (sd < 0), lejos del borde.
    REQUIRE (s.mask[at (0.30f, 0.30f)] == 1);
    REQUIRE (s.signedDist[at (0.30f, 0.30f)] < -0.05f);

    // Punto exterior a la derecha del disco: afuera (sd > 0) y el burst apunta AL ESTE (alejándose).
    const size_t pOut = at (0.55f, 0.30f);
    REQUIRE (s.mask[pOut] == 0);
    REQUIRE (s.signedDist[pOut] > 0.02f);
    REQUIRE (s.burstXY[pOut * 2 + 0] > 0.8f);   // normal exterior ≈ +X

    // Punto interior cerca del borde derecho: adentro y el burst también apunta AL ESTE (estalla hacia afuera).
    const size_t pIn = at (0.42f, 0.30f);
    REQUIRE (s.mask[pIn] == 1);
    REQUIRE (s.burstXY[pIn * 2 + 0] > 0.8f);
}

TEST_CASE ("field: flat-background — un logo sobre fondo liso se recorta por color", "[supernova][field]")
{
    // "Logo": cuadrado rojo [24..40)² sobre fondo blanco 64×64.
    const int W = 64, H = 64;
    std::vector<uint8_t> px ((size_t) W * H * 4, 255);
    for (int y = 24; y < 40; ++y)
        for (int x = 24; x < 40; ++x)
        {
            uint8_t* p = &px[((size_t) y * W + x) * 4];
            p[0] = 220; p[1] = 30; p[2] = 40; p[3] = 255;
        }
    const auto m = supernova::ImageField::maskFromFlatBackground (px.data(), W, H, 32, 32);
    REQUIRE (! m.empty());

    auto at = [&] (float u, float v) { return m[(size_t) ((int) (v * 32)) * 32 + (int) (u * 32)]; };
    REQUIRE (at (0.50f, 0.50f) > 0.9f);   // centro del logo: sujeto
    REQUIRE (at (0.10f, 0.10f) < 0.1f);   // esquina: fondo
    REQUIRE (at (0.85f, 0.50f) < 0.1f);   // lateral: fondo
}

TEST_CASE ("field: flat-background — una imagen con marco NO uniforme devuelve vacío (foto → Vision decide)",
           "[supernova][field]")
{
    // Ruido fuerte en todo el cuadro (marco no uniforme).
    const int W = 64, H = 64;
    std::vector<uint8_t> px ((size_t) W * H * 4);
    uint32_t s = 12345;
    for (auto& b : px) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; b = (uint8_t) s; }
    const auto m = supernova::ImageField::maskFromFlatBackground (px.data(), W, H, 32, 32);
    REQUIRE (m.empty());
}

TEST_CASE ("field: subject — saliencia plana degrada a campo neutro (sin kick de silueta)", "[supernova][field]")
{
    std::vector<float> flat ((size_t) 32 * 32, 0.5f);
    const auto s = supernova::ImageField::computeSubject (flat, 32, 32);
    for (float v : s.burstXY) REQUIRE (v == 0.0f);
    for (float d : s.signedDist) REQUIRE (d == 1.0f);
}

TEST_CASE ("field: el flow es tangente al borde (borde vertical → flujo vertical)", "[supernova][field]")
{
    // Mitad izquierda negra, mitad derecha blanca → borde VERTICAL en x=32 → gradiente horizontal →
    // tangente (flow) VERTICAL. Lejos del borde, flow ≈ 0.
    const int W = 64, H = 64;
    auto img = rectImage (W, H, 32, 0, 64, 64);
    const int G = 32;
    const auto f = ImageField::compute (img.data(), W, H, G, G);

    // Celda sobre el borde (gx≈15-16): |flow.y| fuerte, |flow.x| ≈ 0.
    const size_t onEdge = (((size_t) 16 * G) + 16) * 2;
    REQUIRE (std::abs (f.flowXY[onEdge + 1]) > 0.5f);
    REQUIRE (std::abs (f.flowXY[onEdge + 0]) < 0.01f);

    // Celda lejos del borde: flow ≈ 0.
    const size_t farAway = (((size_t) 16 * G) + 4) * 2;
    REQUIRE (std::abs (f.flowXY[farAway + 0]) < 0.01f);
    REQUIRE (std::abs (f.flowXY[farAway + 1]) < 0.01f);
}

TEST_CASE ("field: depth — la almohada infla el sujeto (centro > borde > plano) y el fondo queda atrás",
           "[supernova][field]")
{
    // Disco blanco centrado sobre fondo negro, máscara = el disco.
    const int W = 64, H = 64, G = 32;
    auto img = rectImage (W, H, 16, 16, 48, 48);   // cuadrado blanco central (sirve igual que un disco)
    std::vector<float> mask ((size_t) G * G, 0.0f);
    for (int y = 8; y < 24; ++y)
        for (int x = 8; x < 24; ++x) mask[(size_t) y * G + x] = 1.0f;

    const auto d = supernova::ImageField::computeDepth (img.data(), W, H, mask, {}, G, G);
    REQUIRE (d.size() == (size_t) G * G);

    const float center = d[(size_t) 16 * G + 16];   // centro del sujeto
    const float rim    = d[(size_t) 9  * G + 16];   // adentro pero cerca de la silueta
    const float back   = d[(size_t) 2  * G + 2];    // fondo
    REQUIRE (center > rim);            // el dome sube hacia el centro
    REQUIRE (rim > 0.5f - 0.06f);      // el sujeto vive alrededor/encima del plano neutro
    REQUIRE (center > 0.62f);          // volumen real, no un relieve tímido
    REQUIRE (back < 0.47f);            // la losa del fondo queda apenas atrás
}

TEST_CASE ("field: depth — sin imagen ni máscara → plano neutro 0.5 (identidad 3D segura)",
           "[supernova][field]")
{
    const int G = 16;
    const auto d = supernova::ImageField::computeDepth (nullptr, 0, 0, {}, {}, G, G);
    for (float v : d) REQUIRE (std::abs (v - 0.5f) < 1e-5f);
}

TEST_CASE ("field: depth — modo foto (sin máscara): lo claro viene adelante", "[supernova][field]")
{
    const int W = 64, H = 64, G = 32;
    auto img = rectImage (W, H, 32, 0, 64, 64);     // mitad derecha blanca
    const auto d = supernova::ImageField::computeDepth (img.data(), W, H, {}, {}, G, G);

    const float bright = d[(size_t) 16 * G + 26];   // celda en la mitad clara (lejos del borde)
    const float dark   = d[(size_t) 16 * G + 5];    // celda en la mitad oscura
    REQUIRE (bright > dark + 0.10f);
}
