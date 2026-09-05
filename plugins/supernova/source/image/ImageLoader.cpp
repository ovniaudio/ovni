#include "image/ImageLoader.h"
#include <cstring>
#include <cstddef>   // std::ptrdiff_t (paso constante del remap por bloques)
#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 #include <CoreGraphics/CoreGraphics.h>
 #include <ImageIO/ImageIO.h>
#endif

namespace supernova
{
namespace
{
// Remap de orientación EXIF (1..8) sobre RGBA row-major, in-place (aloca un buffer nuevo y lo intercambia).
// Deriva source←output por caso. 5..8 transponen (w↔h). Verificado por [exif] con imágenes conocidas.
// POR BLOQUES + PASO CONSTANTE (ronda 4 · R1): las orientaciones que TRASPONEN (5..8) leían una columna de
// la fuente por cada fila de salida — 4 bytes útiles por línea de caché — y resolvían un `switch` por píxel.
// Con una foto de 12 MP eso eran 12 M fallos de caché y el giro tardaba más de un segundo ("si doy vuelta la
// imagen tarda en ponerse en el preview"). Las 8 orientaciones son AFINES: sx = ax·ox + bx·oy + cx (idem sy),
// con coeficientes en {-1,0,1}. Sacando el switch del bucle queda un paso CONSTANTE en la fuente, y
// recorriendo la salida en mosaicos de kRemapTile² el rectángulo fuente de cada mosaico entra en caché.
// El mapeo de píxeles NO cambia: característica de las 8 orientaciones a 130×70 en ImageOrientationTest.
constexpr int kRemapTile = 64;   // 64×64 px × 4 B = 16 KB de destino + 16 KB de fuente

void remapOrientation (LoadedImage& im, int o)
{
    if (o <= 1 || o > 8 || ! im.valid()) return;
    const int w = im.width, h = im.height;
    const bool swap = (o >= 5);
    const int ow = swap ? h : w;
    const int oh = swap ? w : h;

    int ax = 0, bx = 0, cx = 0, ay = 0, by = 0, cy = 0;
    switch (o)
    {
        case 2: ax = -1; cx = w - 1;              by =  1;              break;   // flip H
        case 3: ax = -1; cx = w - 1;              by = -1; cy = h - 1;  break;   // 180
        case 4: ax =  1;                          by = -1; cy = h - 1;  break;   // flip V
        case 5:          bx =  1;                 ay =  1;              break;   // transpose
        case 6:          bx =  1;                 ay = -1; cy = h - 1;  break;   // 90 CW
        case 7:          bx = -1; cx = w - 1;     ay = -1; cy = h - 1;  break;   // transverse
        case 8:          bx = -1; cx = w - 1;     ay =  1;              break;   // 90 CCW
        default: return;
    }
    const std::ptrdiff_t step = ((std::ptrdiff_t) ay * w + ax) * 4;   // avance en la FUENTE por cada ox

    std::vector<uint8_t> out ((size_t) ow * (size_t) oh * 4);
    const uint8_t* src = im.rgba.data();
    for (int ty = 0; ty < oh; ty += kRemapTile)
    {
        const int yEnd = juce::jmin (ty + kRemapTile, oh);
        for (int tx = 0; tx < ow; tx += kRemapTile)
        {
            const int xEnd = juce::jmin (tx + kRemapTile, ow);
            for (int oy = ty; oy < yEnd; ++oy)
            {
                const int sx0 = bx * oy + cx, sy0 = by * oy + cy;
                const uint8_t* s = src + ((std::ptrdiff_t) (ay * tx + sy0) * w + (ax * tx + sx0)) * 4;
                uint8_t* d = out.data() + ((size_t) oy * (size_t) ow + (size_t) tx) * 4;
                for (int ox = tx; ox < xEnd; ++ox, s += step, d += 4)
                {
                    d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
                }
            }
        }
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

#if JUCE_MAC
namespace
{
// Decode vía ImageIO (HEIC/HEIF del iPhone, WebP, TIFF, BMP…): juce::ImageFileFormat sólo reconoce PNG/JPEG/GIF
// (CoreImage se usa DENTRO de esos decoders). Pedimos la "thumbnail" con tope kMaxSide → un HEIC de 48 MP
// baja a ≤4096 px en el propio decoder (RNF7, sin pasar 48 MP por memoria). SIN transform: la orientación
// la aplica orientationOf + applyOrientation, igual que el EXIF del JPEG. RGBA8 recto (alpha = 255).
LoadedImage decodeWithImageIO (const void* data, size_t n, int maxSide)
{
    LoadedImage out;
    CFDataRef cf = CFDataCreateWithBytesNoCopy (kCFAllocatorDefault, (const UInt8*) data, (CFIndex) n, kCFAllocatorNull);
    if (cf == nullptr) return out;
    if (CGImageSourceRef src = CGImageSourceCreateWithData (cf, nullptr))
    {
        CFNumberRef maxN = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &maxSide);
        const void* keys[] = { kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceThumbnailMaxPixelSize,
                               kCGImageSourceCreateThumbnailWithTransform };
        const void* vals[] = { kCFBooleanTrue, maxN, kCFBooleanFalse };
        CFDictionaryRef opts = CFDictionaryCreate (kCFAllocatorDefault, keys, vals, 3,
                                                   &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CGImageRef img = CGImageSourceCreateThumbnailAtIndex (src, 0, opts);
        if (img == nullptr) img = CGImageSourceCreateImageAtIndex (src, 0, nullptr);
        if (img != nullptr)
        {
            const size_t w = CGImageGetWidth (img), h = CGImageGetHeight (img);
            if (w > 0 && h > 0 && w <= 16384 && h <= 16384)
            {
                out.width = (int) w; out.height = (int) h;
                out.rgba.assign (w * h * 4, 0);
                CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
                const CGBitmapInfo info = (CGBitmapInfo) kCGImageAlphaNoneSkipLast | (CGBitmapInfo) kCGBitmapByteOrder32Big;   // R G B X en memoria
                if (CGContextRef ctx = CGBitmapContextCreate (out.rgba.data(), w, h, 8, w * 4, cs, info))
                {
                    CGContextDrawImage (ctx, CGRectMake (0, 0, (CGFloat) w, (CGFloat) h), img);
                    CGContextRelease (ctx);
                    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;   // X → alpha opaco
                }
                else out = {};
                CGColorSpaceRelease (cs);
            }
            CGImageRelease (img);
        }
        if (opts != nullptr) CFRelease (opts);
        if (maxN != nullptr) CFRelease (maxN);
        CFRelease (src);
    }
    CFRelease (cf);
    return out;
}
}
#endif

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

int ImageLoader::orientationOf (const void* data, size_t numBytes) noexcept
{
    const uint8_t* d = (const uint8_t*) data;
    if (d == nullptr || numBytes < 12) return 1;
    if (d[0] == 0xFF && d[1] == 0xD8) return exifOrientation (data, numBytes);   // JPEG: parser propio (testeado)
#if JUCE_MAC
    // HEIC/HEIF del iPhone, TIFF, WebP…: la orientación vive en el contenedor (el `irot` del HEIF no está en
    // APP1) → ImageIO la lee. JUCE decodifica estos formatos vía CoreImage pero NO aplica la orientación
    // (mismo caso que el EXIF del JPEG), así que la aplicamos nosotros con remapOrientation.
    int result = 1;
    if (CFDataRef cf = CFDataCreateWithBytesNoCopy (kCFAllocatorDefault, d, (CFIndex) numBytes, kCFAllocatorNull))
    {
        if (CGImageSourceRef src = CGImageSourceCreateWithData (cf, nullptr))
        {
            if (CFDictionaryRef props = CGImageSourceCopyPropertiesAtIndex (src, 0, nullptr))
            {
                if (auto o = (CFNumberRef) CFDictionaryGetValue (props, kCGImagePropertyOrientation))
                {
                    int v = 1;
                    if (CFNumberGetValue (o, kCFNumberIntType, &v) && v >= 1 && v <= 8) result = v;
                }
                CFRelease (props);
            }
            CFRelease (src);
        }
        CFRelease (cf);
    }
    return result;
#else
    return 1;
#endif
}

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
            applyOrientation (im, orientationOf (mb.getData(), mb.getSize()));
        return im;
    }
    const juce::Image img = juce::ImageFileFormat::loadFrom (file);
    return fromImage (img);   // img nula si no decodifica → LoadedImage inválida
}

LoadedImage ImageLoader::fromEncodedData (const void* data, size_t numBytes, int maxSide)
{
    if (data == nullptr || numBytes == 0)
        return {};
    const juce::Image img = juce::ImageFileFormat::loadFrom (data, numBytes);   // PNG / JPEG / GIF
    if (img.isValid())
        return fromImage (img);
#if JUCE_MAC
    return decodeWithImageIO (data, numBytes, juce::jlimit (16, kMaxSide, maxSide));   // HEIC / HEIF / WebP / TIFF / BMP
#else
    juce::ignoreUnused (maxSide);
    return {};
#endif
}

bool ImageLoader::looksLikeImage (const juce::File& file)
{
    return file.hasFileExtension (imageExtensions());
}

const char* ImageLoader::imageExtensions() noexcept
{
    // SÓLO lo que juce::ImageFileFormat decodifica de verdad. En macOS JUCE decodifica vía CoreImage
    // (JUCE_USE_COREIMAGE_LOADER): también HEIC/HEIF del iPhone, WebP (macOS 11+), TIFF y BMP — las fotos del
    // celular ya no se rechazan en el drop. En otras plataformas, los 3 decoders propios de JUCE. Anunciar
    // interés en algo que no decodifica daría "drop aceptado" y luego un no-op silencioso.
#if JUCE_MAC
    return "png;jpg;jpeg;gif;heic;heif;webp;tif;tiff;bmp";
#else
    return "png;jpg;jpeg;gif";
#endif
}
}
