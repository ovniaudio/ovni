// [supernova][exif] — orientación de imagen: parser EXIF (JUCE no aplica el tag → fotos de celular de
// costado) + remap de rotación. Puro, sin GPU. Verifica el mapeo de píxeles con imágenes conocidas.
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cstdint>
#include "image/ImageLoader.h"

using supernova::ImageLoader;
using supernova::LoadedImage;

namespace
{
// Imagen 2×3 (w=2,h=3) con R = y*10+x (identifica cada píxel). G=B=0, A=255.
LoadedImage makeTagged()
{
    LoadedImage im;
    im.width = 2; im.height = 3;
    im.rgba.resize (2 * 3 * 4);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 2; ++x)
        {
            uint8_t* p = im.rgba.data() + ((size_t) y * 2 + x) * 4;
            p[0] = (uint8_t) (y * 10 + x); p[1] = 0; p[2] = 0; p[3] = 255;
        }
    return im;
}
uint8_t at (const LoadedImage& im, int x, int y) { return im.rgba[((size_t) y * im.width + x) * 4]; }

// Blob JPEG mínimo con APP1/EXIF Orientation = `orient` (little-endian TIFF).
std::vector<uint8_t> makeExifJpeg (uint8_t orient)
{
    std::vector<uint8_t> b = { 0xFF, 0xD8 };                       // SOI
    const std::vector<uint8_t> tiff = {
        0x49, 0x49, 0x2A, 0x00,                                   // "II", 0x002A
        0x08, 0x00, 0x00, 0x00,                                   // offset a IFD0 = 8
        0x01, 0x00,                                               // count = 1
        0x12, 0x01, 0x03, 0x00,                                   // tag 0x0112, type SHORT
        0x01, 0x00, 0x00, 0x00,                                   // count = 1
        orient, 0x00, 0x00, 0x00,                                 // valor (SHORT en 2 bytes)
        0x00, 0x00, 0x00, 0x00                                    // next IFD = 0
    };
    const size_t app1Len = 2 + 6 + tiff.size();                   // len(2) + "Exif\0\0"(6) + TIFF
    b.push_back (0xFF); b.push_back (0xE1);
    b.push_back ((uint8_t) (app1Len >> 8)); b.push_back ((uint8_t) (app1Len & 0xFF));
    const char* exif = "Exif\0\0";
    for (int i = 0; i < 6; ++i) b.push_back ((uint8_t) exif[i]);
    b.insert (b.end(), tiff.begin(), tiff.end());
    b.push_back (0xFF); b.push_back (0xD9);                       // EOI
    return b;
}
}

TEST_CASE ("exif: parser lee la orientación del APP1 (1..8)", "[supernova][exif]")
{
    for (uint8_t o = 1; o <= 8; ++o)
    {
        const auto blob = makeExifJpeg (o);
        REQUIRE (ImageLoader::exifOrientation (blob.data(), blob.size()) == (int) o);
    }
    // No-JPEG / basura → 1 (normal), nunca crashea.
    const uint8_t junk[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B };
    REQUIRE (ImageLoader::exifOrientation (junk, sizeof junk) == 1);
    REQUIRE (ImageLoader::exifOrientation (nullptr, 0) == 1);
}

TEST_CASE ("exif: APP1 con longitud enana + 'Exif' NO desborda (regresión OOB, review adversarial)",
           "[supernova][exif]")
{
    // El blob EXACTO del hallazgo (ASAN): FF D8 | FF E1 00 02 (segLen=2) | "Exif\0\0" | TIFF basura.
    // segLen-8 subdesbordaba a ~1e19 → lectura ~1MB fuera del buffer. Ahora el guard segLen>=8 lo corta.
    const uint8_t evil[] = {
        0xFF, 0xD8,
        0xFF, 0xE1, 0x00, 0x02,                       // APP1, longitud = 2 (enana)
        0x45, 0x78, 0x69, 0x66, 0x00, 0x00,           // "Exif\0\0"
        0x49, 0x49, 0x2A, 0x00,                       // TIFF "II", 0x002A
        0x00, 0x00, 0x10, 0x00                         // ifd = 0x00100000 (basura)
    };
    REQUIRE (ImageLoader::exifOrientation (evil, sizeof evil) == 1);   // no crash, devuelve normal
}

TEST_CASE ("exif: applyOrientation 6 (90 CW) intercambia w/h y mapea las esquinas", "[supernova][exif]")
{
    auto im = makeTagged();                       // 2×3
    ImageLoader::applyOrientation (im, 6);
    REQUIRE (im.width == 3);                       // transpuesta
    REQUIRE (im.height == 2);
    // Rotar 90 CW: la esquina inferior-izquierda (0,2)=20 va arriba-izquierda.
    REQUIRE (at (im, 0, 0) == 20);
    REQUIRE (at (im, 2, 1) == 1);                 // (1,0)=01 va abajo-derecha
}

TEST_CASE ("exif: orientación 1 y rotate90(...,4) son identidad byte-exacta", "[supernova][exif]")
{
    auto a = makeTagged();
    const auto orig = a.rgba;
    ImageLoader::applyOrientation (a, 1);         // no-op
    REQUIRE (a.rgba == orig);
    REQUIRE (a.width == 2);

    auto b = makeTagged();
    ImageLoader::rotate90 (b, 4);                 // vuelta completa
    REQUIRE (b.width == 2);
    REQUIRE (b.height == 3);
    REQUIRE (b.rgba == orig);
}

TEST_CASE ("exif: rotate90(1) == applyOrientation(6)", "[supernova][exif]")
{
    auto a = makeTagged(); ImageLoader::rotate90 (a, 1);
    auto b = makeTagged(); ImageLoader::applyOrientation (b, 6);
    REQUIRE (a.width == b.width);
    REQUIRE (a.height == b.height);
    REQUIRE (a.rgba == b.rgba);
}

// RONDA 4 · R1 — el remap se hizo POR BLOQUES (girar 12 MP leía una columna por cada fila de salida: 4 bytes
// útiles por línea de caché). Esta característica fija el mapeo EXACTO de las 8 orientaciones a un tamaño que
// NO es múltiplo del bloque (130×70: bordes parciales en las dos direcciones) contra una referencia ingenua,
// para que la optimización no pueda cambiar un solo píxel en silencio.
namespace
{
// Referencia ingenua, píxel por píxel, del contrato de applyOrientation (EXIF 1..8).
LoadedImage remapNaive (const LoadedImage& im, int o)
{
    const int w = im.width, h = im.height;
    const bool swap = (o >= 5);
    LoadedImage out;
    out.width  = swap ? h : w;
    out.height = swap ? w : h;
    out.rgba.resize (im.rgba.size());
    for (int oy = 0; oy < out.height; ++oy)
        for (int ox = 0; ox < out.width; ++ox)
        {
            int sx = 0, sy = 0;
            switch (o)
            {
                case 2: sx = w - 1 - ox; sy = oy;         break;
                case 3: sx = w - 1 - ox; sy = h - 1 - oy; break;
                case 4: sx = ox;         sy = h - 1 - oy; break;
                case 5: sx = oy;         sy = ox;         break;
                case 6: sx = oy;         sy = h - 1 - ox; break;
                case 7: sx = w - 1 - oy; sy = h - 1 - ox; break;
                case 8: sx = w - 1 - oy; sy = ox;         break;
                default: sx = ox;        sy = oy;         break;
            }
            const uint8_t* s = im.rgba.data() + ((size_t) sy * w + sx) * 4;
            uint8_t* d = out.rgba.data() + ((size_t) oy * out.width + ox) * 4;
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    return out;
}

// 130×70 con un valor único por píxel repartido en R/G/B (130*70 = 9100 > 255: hace falta más de un canal).
LoadedImage tagged130x70()
{
    LoadedImage im;
    im.width = 130; im.height = 70;
    im.rgba.resize ((size_t) 130 * 70 * 4);
    for (int y = 0; y < 70; ++y)
        for (int x = 0; x < 130; ++x)
        {
            uint8_t* p = im.rgba.data() + ((size_t) y * 130 + x) * 4;
            const int id = y * 130 + x;
            p[0] = (uint8_t) (id & 0xFF); p[1] = (uint8_t) ((id >> 8) & 0xFF); p[2] = (uint8_t) x; p[3] = 255;
        }
    return im;
}
}

TEST_CASE ("exif: las 8 orientaciones son byte-exactas contra la referencia ingenua a 130×70",
           "[supernova][exif]")
{
    const auto src = tagged130x70();
    for (int o = 1; o <= 8; ++o)
    {
        auto got = src;
        ImageLoader::applyOrientation (got, o);
        const auto want = remapNaive (src, o);
        INFO ("orientación " << o);
        REQUIRE (got.width  == want.width);
        REQUIRE (got.height == want.height);
        REQUIRE (got.rgba   == want.rgba);
    }
}
