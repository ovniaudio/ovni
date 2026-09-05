// [supernova][imgcache] — DecodedImageCache (ronda 4 · R1): el decode BASE (sin la rotación manual) cacheado
// por path, y la rotación derivada de él sin volver a leer el disco ni a correr Vision. El bug de campo era
// "si doy vuelta la imagen tarda en ponerse en el preview": cada ⟳ re-decodificaba el archivo entero y
// re-corría Vision, y girar 90° no cambia ni los bytes del archivo ni el contenido.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "image/DecodedImageCache.h"
#include "image/ImageLoader.h"
#include "render/IRenderer.h"   // kParticleGrid

using namespace supernova;

namespace
{
// Imagen w×h con R = y*16+x (identifica cada píxel), y grillas kParticleGrid² con un valor por celda.
LoadedImage tagged (int w, int h, bool withGrids = true)
{
    LoadedImage im;
    im.width = w; im.height = h;
    im.rgba.resize ((size_t) w * (size_t) h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t* p = im.rgba.data() + ((size_t) y * w + x) * 4;
            p[0] = (uint8_t) ((y * 16 + x) & 0xFF); p[1] = (uint8_t) x; p[2] = (uint8_t) y; p[3] = 255;
        }
    if (withGrids)
    {
        const int g = kParticleGrid;
        im.saliency.resize ((size_t) g * g);
        im.subjectMask.resize ((size_t) g * g);
        for (int y = 0; y < g; ++y)
            for (int x = 0; x < g; ++x)
            {
                im.saliency[(size_t) y * g + x]    = (float) (y * g + x) / (float) (g * g);
                im.subjectMask[(size_t) y * g + x] = (float) (x) / (float) g;
            }
    }
    return im;
}

juce::File writeJpeg (int w, int h)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    {
        juce::Graphics g (img);
        juce::ColourGradient grad (juce::Colours::darkslateblue, 0.0f, 0.0f,
                                   juce::Colours::orange, (float) w, (float) h, false);
        g.setGradientFill (grad); g.fillAll();
        g.setColour (juce::Colours::white);
        g.fillEllipse (w * 0.30f, h * 0.22f, w * 0.28f, h * 0.42f);
    }
    juce::File f = juce::File::createTempFile ("jpg");
    juce::FileOutputStream os (f);
    juce::JPEGImageFormat jpg; jpg.setQuality (0.9f);
    jpg.writeImageToStream (img, os); os.flush();
    return f;
}
}

TEST_CASE ("imgcache: rotateGrid90 gira una grilla como el RGBA (y 4 cuartos son la identidad)",
           "[supernova][imgcache]")
{
    const int w = 3, h = 5;
    std::vector<float> g ((size_t) w * h);
    for (int i = 0; i < w * h; ++i) g[(size_t) i] = (float) i;

    // 90° CW: la celda (x,y) de la fuente cae en (h-1-y, x) del destino, que pasa a ser h×w.
    const auto r1 = rotateGrid90 (g, w, h, 1);
    REQUIRE (r1.size() == g.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            REQUIRE (r1[(size_t) x * h + (h - 1 - y)] == g[(size_t) y * w + x]);

    REQUIRE (rotateGrid90 (g, w, h, 0) == g);
    // cuatro cuartos = identidad (la 2ª y la 4ª vuelven a w×h)
    auto acc = g; int aw = w, ah = h;
    for (int k = 0; k < 4; ++k) { acc = rotateGrid90 (acc, aw, ah, 1); std::swap (aw, ah); }
    REQUIRE (acc == g);
    REQUIRE (rotateGrid90 (g, w, h, -1) == rotateGrid90 (g, w, h, 3));   // turns se toma mod 4
}

TEST_CASE ("imgcache: rotatedCopy gira RGBA y las dos grillas, y no toca la base",
           "[supernova][imgcache]")
{
    const auto base = tagged (7, 4);
    const auto r = rotatedCopy (base, 1);

    // el RGBA es EXACTAMENTE el de ImageLoader::rotate90 (el camino de siempre)
    auto expected = base;
    ImageLoader::rotate90 (expected, 1);
    REQUIRE (r.width == expected.width);
    REQUIRE (r.height == expected.height);
    REQUIRE (r.rgba == expected.rgba);

    // las grillas (kParticleGrid², cuadradas) giran igual
    REQUIRE (r.saliency    == rotateGrid90 (base.saliency,    kParticleGrid, kParticleGrid, 1));
    REQUIRE (r.subjectMask == rotateGrid90 (base.subjectMask, kParticleGrid, kParticleGrid, 1));

    // inmutabilidad: la base quedó intacta
    REQUIRE (base.width == 7);
    REQUIRE (base.rgba == tagged (7, 4).rgba);

    // turns = 0 → copia idéntica; 4 → identidad
    REQUIRE (rotatedCopy (base, 0).rgba == base.rgba);
    REQUIRE (rotatedCopy (base, 4).rgba == base.rgba);
    REQUIRE (rotatedCopy (base, 4).saliency == base.saliency);
}

TEST_CASE ("imgcache: rotatedCopy sobre una base sin grillas no inventa ninguna", "[supernova][imgcache]")
{
    const auto base = tagged (4, 6, false);
    const auto r = rotatedCopy (base, 3);
    REQUIRE (r.saliency.empty());
    REQUIRE (r.subjectMask.empty());
    REQUIRE (r.width == 6);
    REQUIRE (r.height == 4);
}

TEST_CASE ("imgcache: un miss no devuelve nada; tras el put la misma base sale del caché",
           "[supernova][imgcache]")
{
    const juce::File f = writeJpeg (64, 48);
    DecodedImageCache cache;
    REQUIRE (cache.get (f) == nullptr);
    REQUIRE (cache.misses() == 1);
    REQUIRE (cache.hits() == 0);

    auto base = std::make_shared<const LoadedImage> (tagged (64, 48));
    cache.put (f, base);
    REQUIRE (cache.count() == 1);
    REQUIRE (cache.get (f) == base);          // el MISMO objeto: no se copia nada
    REQUIRE (cache.hits() == 1);
    REQUIRE (cache.bytes() >= base->rgba.size());

    f.deleteFile();
}

TEST_CASE ("imgcache: si el archivo cambió en disco la entrada no sirve más", "[supernova][imgcache]")
{
    const juce::File f = writeJpeg (64, 48);
    DecodedImageCache cache;
    cache.put (f, std::make_shared<const LoadedImage> (tagged (64, 48)));
    REQUIRE (cache.get (f) != nullptr);

    // el usuario reemplazó el archivo (mismo path, otro contenido / otro mtime)
    f.deleteFile();
    const juce::File g = writeJpeg (32, 32);
    g.moveFileTo (f);
    f.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5));

    REQUIRE (cache.get (f) == nullptr);
    REQUIRE (cache.count() == 0);   // la entrada podrida se va sola

    f.deleteFile(); g.deleteFile();
}

TEST_CASE ("imgcache: invalidate saca una entrada y clear las saca todas", "[supernova][imgcache]")
{
    const juce::File a = writeJpeg (48, 48);
    const juce::File b = writeJpeg (48, 48);
    DecodedImageCache cache;
    cache.put (a, std::make_shared<const LoadedImage> (tagged (48, 48)));
    cache.put (b, std::make_shared<const LoadedImage> (tagged (48, 48)));
    REQUIRE (cache.count() == 2);

    REQUIRE (cache.contains (a.getFullPathName()));
    REQUIRE (cache.contains (b.getFullPathName()));
    cache.invalidate (a.getFullPathName());
    REQUIRE (cache.count() == 1);
    REQUIRE (! cache.contains (a.getFullPathName()));   // consultar NO cuenta como hit ni mueve el LRU
    REQUIRE (cache.hits() == 0);
    REQUIRE (cache.get (a) == nullptr);
    REQUIRE (cache.get (b) != nullptr);

    cache.clear();
    REQUIRE (cache.count() == 0);
    REQUIRE (cache.bytes() == 0);

    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("imgcache: el presupuesto expulsa la MENOS usada, nunca la última pedida",
           "[supernova][imgcache]")
{
    const juce::File a = writeJpeg (48, 48);
    const juce::File b = writeJpeg (48, 48);
    const juce::File c = writeJpeg (48, 48);
    // cada entrada: 100×100×4 = 40.000 B de RGBA (sin grillas). Presupuesto para DOS.
    DecodedImageCache cache (95000);
    auto mk = [] { return std::make_shared<const LoadedImage> (tagged (100, 100, false)); };

    cache.put (a, mk());
    cache.put (b, mk());
    REQUIRE (cache.count() == 2);
    REQUIRE (cache.get (a) != nullptr);       // `a` pasa a ser la MÁS reciente
    cache.put (c, mk());                      // no entra una tercera: se va `b`
    REQUIRE (cache.count() == 2);
    REQUIRE (cache.bytes() <= cache.budget());
    REQUIRE (cache.get (a) != nullptr);
    REQUIRE (cache.get (c) != nullptr);
    REQUIRE (cache.get (b) == nullptr);

    a.deleteFile(); b.deleteFile(); c.deleteFile();
}

TEST_CASE ("imgcache: diez fotos de 12 MP no pasan el presupuesto", "[supernova][imgcache]")
{
    DecodedImageCache cache;                   // 256 MB por default
    std::vector<juce::File> files;
    for (int i = 0; i < 10; ++i)
    {
        files.push_back (writeJpeg (16, 16));
        LoadedImage big;                       // 4000×3000 RGBA + las dos grillas = ~50 MB
        big.width = 4000; big.height = 3000;
        big.rgba.assign ((size_t) 4000 * 3000 * 4, (uint8_t) i);
        big.saliency.assign ((size_t) kParticleGrid * kParticleGrid, 0.5f);
        big.subjectMask.assign ((size_t) kParticleGrid * kParticleGrid, 0.5f);
        cache.put (files.back(), std::make_shared<const LoadedImage> (std::move (big)));
        REQUIRE (cache.bytes() <= cache.budget());
    }
    REQUIRE (cache.count() >= 1);              // siempre queda al menos la última
    REQUIRE (cache.count() < 10);              // …y NO entraron las diez (500 MB > 256 MB)
    for (auto& f : files) f.deleteFile();
}

// El corazón de R1: la rotación derivada del caché tiene que dar EXACTAMENTE lo mismo que el decode del
// editor con esa rotación. Se cumple por construcción porque el decode SIEMPRE trabaja sobre la base
// (rotación 0) y aplica la rotación encima: los dos caminos son la misma operación.
TEST_CASE ("imgcache: decodeBaseImage + rotatedCopy == el RGBA del camino viejo (fromFile + rotate90)",
           "[supernova][imgcache]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File f = writeJpeg (320, 240);
    const auto base = decodeBaseImage (f);
    REQUIRE (base.valid());
    REQUIRE (base.saliency.size() == (size_t) kParticleGrid * kParticleGrid);

    for (int turns = 0; turns < 4; ++turns)
    {
        auto old = ImageLoader::fromFile (f);            // el camino de siempre: decodificar y girar
        ImageLoader::rotate90 (old, turns);
        const auto viaCache = rotatedCopy (base, turns);
        REQUIRE (viaCache.width  == old.width);
        REQUIRE (viaCache.height == old.height);
        REQUIRE (viaCache.rgba   == old.rgba);           // byte-exacto
        REQUIRE (viaCache.saliency    == rotateGrid90 (base.saliency,    kParticleGrid, kParticleGrid, turns));
        REQUIRE (viaCache.subjectMask == rotateGrid90 (base.subjectMask, kParticleGrid, kParticleGrid, turns));
    }
    f.deleteFile();
}
