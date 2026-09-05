// [supernova][thumbs][media] — MediaThumbCache (MEDIA SESSION PRO): la reducción pura conserva el aspecto y
// promedia colores; loadThumb decodifica un PNG real, aplica la rotación manual (dims intercambiadas) y falla
// limpio con basura. Sin GPU. El hilo se prueba con un round-trip get() → onThumbReady.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "image/MediaThumbs.h"

using supernova::MediaThumbCache;
using supernova::MediaThumb;

namespace
{
// RGBA w×h: mitad izquierda roja, mitad derecha azul.
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

juce::File writePng (int w, int h, juce::Colour c)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    img.clear (img.getBounds(), c);
    juce::File f = juce::File::createTempFile ("png");
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    os.flush();
    return f;
}
}

TEST_CASE ("thumbs: makeThumbnail reduce al lado mayor conservando el aspecto y promedia por bloque",
           "[supernova][thumbs][media]")
{
    const int w = 400, h = 200;
    const auto px = makeSplit (w, h);
    const juce::Image t = MediaThumbCache::makeThumbnail (px.data(), w, h, 100);
    REQUIRE (t.isValid());
    REQUIRE (t.getWidth()  == 100);
    REQUIRE (t.getHeight() == 50);
    // izquierda roja, derecha azul (el promedio de bloques uniformes es exacto)
    REQUIRE (t.getPixelAt (10, 25) == juce::Colour (255, 0, 0));
    REQUIRE (t.getPixelAt (90, 25) == juce::Colour (0, 0, 255));
    REQUIRE (t.getPixelAt (10, 25).getAlpha() == 255);

    // vertical → el ALTO es el lado mayor
    const auto pv = makeSplit (30, 90);
    const juce::Image tv = MediaThumbCache::makeThumbnail (pv.data(), 30, 90, 45);
    REQUIRE (tv.getWidth()  == 15);
    REQUIRE (tv.getHeight() == 45);

    // ya chica → no se agranda
    const juce::Image ts = MediaThumbCache::makeThumbnail (pv.data(), 30, 90, 500);
    REQUIRE ((ts.getWidth() == 30 && ts.getHeight() == 90));

    // basura → nula, sin crash
    REQUIRE (MediaThumbCache::makeThumbnail (nullptr, 10, 10, 50).isNull());
    REQUIRE (MediaThumbCache::makeThumbnail (px.data(), 0, 10, 50).isNull());
}

TEST_CASE ("thumbs: loadThumb decodifica un PNG real, aplica la rotación manual y reporta las dims fuente",
           "[supernova][thumbs][media]")
{
    const juce::File f = writePng (300, 120, juce::Colours::orange);   // horizontal 2.5:1

    const MediaThumb t0 = MediaThumbCache::loadThumb (f, 0, false, 116);
    REQUIRE (! t0.failed);
    REQUIRE (t0.image.isValid());
    REQUIRE (t0.srcW == 300);
    REQUIRE (t0.srcH == 120);
    REQUIRE (t0.image.getWidth() == 116);
    REQUIRE (t0.image.getHeight() == 46);
    REQUIRE (t0.aspect() == Catch::Approx (2.5f));

    const MediaThumb t1 = MediaThumbCache::loadThumb (f, 1, false, 116);   // ⟳ una vez → vertical
    REQUIRE (! t1.failed);
    REQUIRE (t1.srcW == 120);
    REQUIRE (t1.srcH == 300);
    REQUIRE (t1.image.getHeight() == 116);
    REQUIRE (t1.image.getWidth() == 46);

    f.deleteFile();

    // archivo inexistente / basura → failed, nunca crashea
    REQUIRE (MediaThumbCache::loadThumb (juce::File ("/tmp/no-existe-jamas.png"), 0, false, 116).failed);
    juce::File g = juce::File::createTempFile ("png");
    g.replaceWithText ("esto no es un png");
    REQUIRE (MediaThumbCache::loadThumb (g, 0, false, 116).failed);
    g.deleteFile();
}

TEST_CASE ("thumbs: el cache encola una vez, decodifica en fondo y avisa por onThumbReady",
           "[supernova][thumbs][media]")
{
    juce::ScopedJuceInitialiser_GUI gui;   // MessageManager para el callAsync
    const juce::File f = writePng (64, 32, juce::Colours::teal);

    MediaThumbCache cache;
    int readyCount = 0;
    cache.onThumbReady = [&] { ++readyCount; };

    REQUIRE (cache.get (f.getFullPathName(), 0, false) == nullptr);   // primera vez: encola
    REQUIRE (cache.get (f.getFullPathName(), 0, false) == nullptr);   // no duplica el job

    // Esperar al worker (bombeando el message loop para que llegue el callAsync).
    std::shared_ptr<const MediaThumb> got;
    for (int i = 0; i < 200 && got == nullptr; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        got = cache.peek (f.getFullPathName(), 0);
    }
    REQUIRE (got != nullptr);
    REQUIRE (! got->failed);
    REQUIRE (got->image.getWidth() == 64);    // ya es chica: no se agranda
    REQUIRE (got->image.getHeight() == 32);
    REQUIRE (got->srcW == 64);
    REQUIRE (cache.get (f.getFullPathName(), 0, false) == got);        // ahora sí, directo
    REQUIRE (cache.peek (f.getFullPathName(), 1) == nullptr);           // otra rotación = otra clave
    for (int i = 0; i < 20 && readyCount == 0; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    REQUIRE (readyCount >= 1);

    f.deleteFile();
}
