#include "image/ImageLoader.h"
#include <cstring>

namespace supernova
{
namespace
{
// Remap de orientación EXIF (1..8) sobre RGBA row-major, in-place (aloca un buffer nuevo y lo intercambia).
// Deriva source←output por caso. 5..8 transponen (w↔h). Verificado por [exif] con imágenes conocidas.
void remapOrientation (LoadedImage& im, int o)
{
    if (o <= 1 || o > 8 || ! im.valid()) return;
    const int w = im.width, h = im.height;
    const bool swap = (o >= 5);
    const int ow = swap ? h : w;
    const int oh = swap ? w : h;
    std::vector<uint8_t> out ((size_t) ow * (size_t) oh * 4);
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox)
        {
            int sx = 0, sy = 0;
            switch (o)
            {
                case 2: sx = w - 1 - ox; sy = oy;         break;   // flip H
                case 3: sx = w - 1 - ox; sy = h - 1 - oy; break;   // 180
                case 4: sx = ox;         sy = h - 1 - oy; break;   // flip V
                case 5: sx = oy;         sy = ox;         break;   // transpose
                case 6: sx = oy;         sy = h - 1 - ox; break;   // 90 CW
                case 7: sx = w - 1 - oy; sy = h - 1 - ox; break;   // transverse
                case 8: sx = w - 1 - oy; sy = ox;         break;   // 90 CCW
                default: break;
            }
            const uint8_t* s = im.rgba.data() + ((size_t) sy * w + sx) * 4;
            uint8_t* dpx = out.data() + ((size_t) oy * ow + ox) * 4;
            dpx[0] = s[0]; dpx[1] = s[1]; dpx[2] = s[2]; dpx[3] = s[3];
        }
    im.rgba = std::move (out);
    im.width = ow;
    im.height = oh;
}

// Parser TIFF mínimo del bloque EXIF (tras "Exif\0\0"): busca el tag Orientation (0x0112) en IFD0.
int parseTiffOrientation (const uint8_t* t, size_t n) noexcept
{
    if (t == nullptr || n < 8) return 1;
    bool le;
    if      (t[0] == 0x49 && t[1] == 0x49) le = true;    // "II" little-endian
    else if (t[0] == 0x4D && t[1] == 0x4D) le = false;   // "MM" big-endian
    else return 1;
    auto u16 = [le] (const uint8_t* p) -> uint32_t
    { return le ? (uint32_t) (p[0] | (p[1] << 8)) : (uint32_t) ((p[0] << 8) | p[1]); };
    auto u32 = [le] (const uint8_t* p) -> uint32_t
    { return le ? (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24))
                : (uint32_t) (((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); };
    if (u16 (t + 2) != 0x002A) return 1;
    const uint32_t ifd = u32 (t + 4);
    if ((size_t) ifd + 2 > n) return 1;
    const uint32_t count = u16 (t + ifd);
    for (uint32_t i = 0; i < count; ++i)
    {
        const size_t e = (size_t) ifd + 2 + (size_t) i * 12;
        if (e + 12 > n) break;
        if (u16 (t + e) == 0x0112)                        // Orientation
        {
            const uint32_t val = u16 (t + e + 8);         // SHORT en los 2 primeros bytes del value field
            return (val >= 1 && val <= 8) ? (int) val : 1;
        }
    }
    return 1;
}
}

int ImageLoader::exifOrientation (const void* dataV, size_t n) noexcept
{
    const uint8_t* d = (const uint8_t*) dataV;
    if (d == nullptr || n < 12) return 1;
    if (d[0] != 0xFF || d[1] != 0xD8) return 1;           // no es JPEG (sin SOI)
    size_t p = 2;
    while (p + 4 <= n)
    {
        if (d[p] != 0xFF) { ++p; continue; }              // resync ante bytes de relleno
        const uint8_t marker = d[p + 1];
        if (marker == 0xD9 || marker == 0xDA) break;       // EOI / inicio del scan: ya no hay APP1
        const size_t segLen = ((size_t) d[p + 2] << 8) | d[p + 3];
        if (segLen < 2) break;
        const size_t segStart = p + 4;
        const size_t segEnd   = p + 2 + segLen;
        if (segEnd > n) break;
        // segLen >= 8 es OBLIGATORIO antes de restar: la longitud del TIFF es segLen-8 en size_t; con
        // segLen en [2,7] eso SUBDESBORDA a ~1e19 y parseTiffOrientation leería megabytes fuera del buffer
        // (OOB confirmado con ASAN). El marcador APP1 con "Exif\0\0" pero longitud enana ya no entra.
        if (marker == 0xE1 && segLen >= 8 && segStart + 6 <= n && std::memcmp (d + segStart, "Exif\0\0", 6) == 0)
            return parseTiffOrientation (d + segStart + 6, segEnd - (segStart + 6));
        p = segEnd;
    }
    return 1;
}

void ImageLoader::applyOrientation (LoadedImage& im, int orientation) { remapOrientation (im, orientation); }

void ImageLoader::rotate90 (LoadedImage& im, int turns)
{
    turns = ((turns % 4) + 4) % 4;
    for (int i = 0; i < turns; ++i) remapOrientation (im, 6);   // 6 = 90° CW
}

int ImageLoader::downsampleFactor (int w, int h, int maxSide) noexcept
{
    const int side = juce::jmax (w, h);
    if (side <= maxSide || maxSide <= 0) return 1;
    // menor factor entero f tal que ceil(side/f) <= maxSide
    int f = (side + maxSide - 1) / maxSide;
    return juce::jmax (1, f);
}

LoadedImage ImageLoader::fromImage (const juce::Image& image)
{
    LoadedImage out;
    if (image.isNull() || image.getWidth() <= 0 || image.getHeight() <= 0)
        return out;

    // RNF7: bajar resolución ANTES de extraer, si el lado mayor supera el máximo.
    juce::Image img = image;
    const int f = downsampleFactor (img.getWidth(), img.getHeight());
    if (f > 1)
    {
        const int nw = juce::jmax (1, img.getWidth()  / f);
        const int nh = juce::jmax (1, img.getHeight() / f);
        img = img.rescaled (nw, nh, juce::Graphics::highResamplingQuality);
    }

    const int w = img.getWidth();
    const int h = img.getHeight();
    out.width  = w;
    out.height = h;
    out.rgba.resize ((size_t) w * (size_t) h * 4);

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y)
    {
        uint8_t* dst = out.rgba.data() + (size_t) y * (size_t) w * 4;
        for (int x = 0; x < w; ++x)
        {
            const juce::Colour c = bd.getPixelColour (x, y);
            dst[x * 4 + 0] = c.getRed();
            dst[x * 4 + 1] = c.getGreen();
            dst[x * 4 + 2] = c.getBlue();
            dst[x * 4 + 3] = c.getAlpha();
        }
    }
    return out;
}

LoadedImage ImageLoader::fromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};
    // Leemos los bytes UNA vez: decodificamos con JUCE y, en paralelo, parseamos el EXIF para enderezar
    // (JUCE no aplica el tag Orientation → las fotos de celular llegan de costado). Fallback: loadFrom(File).
    juce::MemoryBlock mb;
    if (file.loadFileAsData (mb) && mb.getSize() > 0)
    {
        LoadedImage im = fromEncodedData (mb.getData(), mb.getSize());
        if (im.valid())
            applyOrientation (im, exifOrientation (mb.getData(), mb.getSize()));
        return im;
    }
    const juce::Image img = juce::ImageFileFormat::loadFrom (file);
    return fromImage (img);   // img nula si no decodifica → LoadedImage inválida
}

LoadedImage ImageLoader::fromEncodedData (const void* data, size_t numBytes)
{
    if (data == nullptr || numBytes == 0)
        return {};
    const juce::Image img = juce::ImageFileFormat::loadFrom (data, numBytes);
    return fromImage (img);
}

bool ImageLoader::looksLikeImage (const juce::File& file)
{
    // SÓLO los formatos que juce::ImageFileFormat decodifica de fábrica (PNG/JPEG/GIF). Anunciar interés en
    // HEIC/WebP/etc. daría el highlight de "drop aceptado" y luego un no-op silencioso al no poder decodificar.
    return file.hasFileExtension ("png;jpg;jpeg;gif");
}
}
