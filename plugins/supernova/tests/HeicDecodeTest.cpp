// [supernova][heic][media] — fotos del iPhone: HEIC (y WebP/TIFF) se aceptan en el drop y se decodifican con
// la orientación del contenedor (ImageIO), no sólo el EXIF del JPEG. El test CODIFICA un HEIC en el acto con
// CGImageDestination (orientación 6 = 90° CW) y verifica que fromFile lo endereza: dims intercambiadas y el
// mapeo de píxeles correcto. Si el encoder HEIC no está en esta máquina/CI, se saltea (SUCCEED).
#include <catch2/catch_test_macros.hpp>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include "image/ImageLoader.h"
#include "image/MediaThumbs.h"

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 #include <CoreGraphics/CoreGraphics.h>
 #include <ImageIO/ImageIO.h>
#endif

using supernova::ImageLoader;

TEST_CASE ("heic: el drop acepta HEIC/HEIF/WebP/TIFF en macOS", "[supernova][heic][media]")
{
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/foto.png")));
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/foto.JPG")));
#if JUCE_MAC
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/IMG_0001.HEIC")));
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/foto.heif")));
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/foto.webp")));
    REQUIRE (ImageLoader::looksLikeImage (juce::File ("/tmp/foto.tiff")));
#endif
    REQUIRE (! ImageLoader::looksLikeImage (juce::File ("/tmp/cancion.wav")));
    REQUIRE (! ImageLoader::looksLikeImage (juce::File ("/tmp/video.mp4")));
}

#if JUCE_MAC
namespace
{
// Codifica un RGBA w×h a `uti` (public.heic / public.tiff) con la orientación pedida. Vector vacío si el
// encoder no está disponible.
std::vector<uint8_t> encode (const std::vector<uint8_t>& rgba, int w, int h, CFStringRef uti, int orientation)
{
    std::vector<uint8_t> out;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef prov = CGDataProviderCreateWithData (nullptr, rgba.data(), rgba.size(), nullptr);
    CGImageRef img = CGImageCreate ((size_t) w, (size_t) h, 8, 32, (size_t) w * 4, cs,
                                    (CGBitmapInfo) kCGImageAlphaNoneSkipLast, prov, nullptr, false, kCGRenderingIntentDefault);
    CFMutableDataRef data = CFDataCreateMutable (kCFAllocatorDefault, 0);
    if (img != nullptr)
    {
        if (CGImageDestinationRef dest = CGImageDestinationCreateWithData (data, uti, 1, nullptr))
        {
            const int o = orientation;
            CFNumberRef on = CFNumberCreate (kCFAllocatorDefault, kCFNumberIntType, &o);
            const void* keys[]   = { kCGImagePropertyOrientation };
            const void* values[] = { on };
            CFDictionaryRef props = CFDictionaryCreate (kCFAllocatorDefault, keys, values, 1,
                                                        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CGImageDestinationAddImage (dest, img, props);
            if (CGImageDestinationFinalize (dest))
                out.assign (CFDataGetBytePtr (data), CFDataGetBytePtr (data) + CFDataGetLength (data));
            CFRelease (props);
            CFRelease (on);
            CFRelease (dest);
        }
        CGImageRelease (img);
    }
    CFRelease (data);
    CGDataProviderRelease (prov);
    CGColorSpaceRelease (cs);
    return out;
}

// 40×20: mitad izquierda ROJA, mitad derecha AZUL.
std::vector<uint8_t> makeSplit (int w, int h)
{
    std::vector<uint8_t> px ((size_t) w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t* p = px.data() + ((size_t) y * w + x) * 4;
            const bool left = x < w / 2;
            p[0] = left ? 255 : 0; p[1] = 0; p[2] = left ? 0 : 255; p[3] = 255;
        }
    return px;
}

void checkOriented (const juce::File& f, const char* what)
{
    INFO (what);
    const auto li = ImageLoader::fromFile (f);
    REQUIRE (li.valid());
    // Orientación 6 (90° CW): out(ox,oy) = src(sx=oy, sy=h-1-ox) → 20×40, y la mitad de ARRIBA es la roja
    // (columnas izquierdas del original). Sin enderezar quedaría 40×20 con rojo a la izquierda.
    REQUIRE (li.width  == 20);
    REQUIRE (li.height == 40);
    auto at = [&] (int x, int y) { return li.rgba.data() + ((size_t) y * li.width + x) * 4; };
    REQUIRE (at (10, 5)[0]  > 200);   // arriba: rojo
    REQUIRE (at (10, 5)[2]  < 60);
    REQUIRE (at (10, 35)[2] > 200);   // abajo: azul (la compresión HEIC es con pérdida: umbrales laxos)
    REQUIRE (at (10, 35)[0] < 60);

    // La miniatura de la tira toma el MISMO camino (ImageIO + orientación): vertical, dims fuente enderezadas.
    const auto th = supernova::MediaThumbCache::loadThumb (f, 0, false, 116);
    REQUIRE (! th.failed);
    REQUIRE (th.srcW == 20);
    REQUIRE (th.srcH == 40);
    REQUIRE (th.image.getWidth() == 20);
    REQUIRE (th.image.getHeight() == 40);
}
}

TEST_CASE ("heic: un HEIC con orientación 6 se decodifica ENDEREZADO (dims intercambiadas + mapeo correcto)",
           "[supernova][heic][media]")
{
    const auto px = makeSplit (40, 20);
    const auto heic = encode (px, 40, 20, CFSTR ("public.heic"), 6);
    if (heic.empty()) { SUCCEED ("sin encoder HEIC en esta máquina — test saltado"); return; }

    REQUIRE (ImageLoader::orientationOf (heic.data(), heic.size()) == 6);
    juce::File f = juce::File::createTempFile ("heic");
    REQUIRE (f.replaceWithData (heic.data(), heic.size()));
    checkOriented (f, "HEIC");
    f.deleteFile();
}

TEST_CASE ("heic: TIFF con orientación 6 también se endereza (mismo camino ImageIO)", "[supernova][heic][media]")
{
    const auto px = makeSplit (40, 20);
    const auto tiff = encode (px, 40, 20, CFSTR ("public.tiff"), 6);
    REQUIRE (! tiff.empty());   // el encoder TIFF siempre está
    REQUIRE (ImageLoader::orientationOf (tiff.data(), tiff.size()) == 6);
    juce::File f = juce::File::createTempFile ("tiff");
    REQUIRE (f.replaceWithData (tiff.data(), tiff.size()));
    checkOriented (f, "TIFF");
    f.deleteFile();

    // Un PNG sin orientación → 1 (nada cambia para los formatos de siempre).
    juce::Image img (juce::Image::RGB, 8, 4, true);
    juce::MemoryOutputStream mos;
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, mos));
    REQUIRE (ImageLoader::orientationOf (mos.getData(), mos.getDataSize()) == 1);
}
#endif
