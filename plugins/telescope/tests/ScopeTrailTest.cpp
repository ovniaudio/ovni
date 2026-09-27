// [telescope][scopetrail] — la estela de SCOPE arranca en cero, no con lo que había en la memoria (T3, prompt 98).
//
// EL DEFECTO. `raster::Cache::prepare` asignaba la imagen con `juce::Image (ARGB, w, h, false)`: sin
// limpiar. Las otras lentes que usan la caché la pintan entera antes de mostrarla, pero la estela de SCOPE
// no: la ATENÚA (`decayTrail`, × 170/256 por cuadro) y le suma los puntos del hop. Lo que traía la memoria
// se ve como un fogonazo de basura hasta que decae, ~0,3 s después de cada cambio de tamaño.
//
// POR QUÉ HAY QUE ENSUCIAR LA MEMORIA A MANO. Memoria sin inicializar no es memoria sucia: páginas recién
// pedidas al sistema vienen en cero, y el test pasaría con el defecto puesto. Así que antes de cada
// asignación se pide y se suelta una imagen DEL MISMO TAMAÑO llena de 0xFF: el allocator tiende a
// devolver ese bloque en la próxima asignación igual. No es determinista (el allocator puede dar otro
// bloque), por eso son N intentos y el test imprime en cuántos la basura apareció.
//
// LOS DOS NIVELES.
//   1 · la caché sola: `prepare` que crea y `prepare` que redimensiona tienen que dejar TODOS los bytes en
//       cero (transparente: la estela vive encima de la capa estática del goniómetro).
//   2 · la lente entera, en silencio: pintada con la memoria sucia en 0xFF tiene que salir IGUAL, píxel a
//       píxel, que pintada con la memoria en 0x00. Es lo que ve el usuario.
//
//   ./OvniTelescopeTests "[scopetrail]"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <juce_graphics/juce_graphics.h>
#include "PluginProcessor.h"
#include "lenses/Raster.h"
#include "lenses/ScopeLens.h"

namespace
{
constexpr int kAttempts = 24;

// Pide una imagen de w × h con el MISMO constructor que usa la caché, la llena con `fill` y la suelta. El
// bloque vuelve al allocator con ese contenido.
void soilHeap (int w, int h, juce::uint8 fill)
{
    juce::Image dirt (juce::Image::ARGB, w, h, false);
    const juce::Image::BitmapData bd (dirt, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < bd.height; ++y)
        std::memset (bd.getLinePointer (y), fill, (size_t) (bd.width * bd.pixelStride));
}

// Cuántos píxeles de la imagen tienen algún byte distinto de cero.
int nonZeroPixels (const juce::Image& img)
{
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int n = 0;
    for (int y = 0; y < bd.height; ++y)
    {
        const auto* line = bd.getLinePointer (y);
        for (int x = 0; x < bd.width; ++x)
        {
            const auto* px = line + x * bd.pixelStride;
            bool any = false;
            for (int i = 0; i < bd.pixelStride; ++i) any = any || px[i] != 0;
            n += any ? 1 : 0;
        }
    }
    return n;
}

// Cuántos píxeles difieren entre dos imágenes del mismo tamaño, y la caja que los contiene.
int differingPixels (const juce::Image& a, const juce::Image& b, juce::Rectangle<int>& box)
{
    REQUIRE (a.getBounds() == b.getBounds());
    int n = 0;
    box = {};
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
            {
                ++n;
                box = box.isEmpty() ? juce::Rectangle<int> (x, y, 1, 1)
                                    : box.getUnion ({ x, y, 1, 1 });
            }
    return n;
}

// Un cuadro de la lente, en silencio, a escala 1, con la memoria ensuciada justo antes. La estela se
// asigna adentro del paint (`ScopeLens::paintLive`), que es donde la caché conoce la escala física.
juce::Image paintScopeOnce (telescope::TelescopeProcessor& proc, int w, int h, juce::uint8 fill,
                            int trailW, int trailH)
{
    telescope::ScopeLens lens (proc);
    lens.setSize (w, h);
    juce::Image out (juce::Image::ARGB, w, h, true);
    soilHeap (trailW, trailH, fill);
    juce::Graphics g (out);
    lens.paintEntireComponent (g, false);
    return out;
}
}

TEST_CASE ("telescope: la cache de pixeles arranca en cero al crearse y al redimensionarse", "[telescope][scopetrail]")
{
    struct Size { int w, h; };
    const Size sizes[] = { { 360, 300 }, { 512, 420 }, { 700, 560 } };

    int dirtyCreate = 0, dirtyResize = 0, worstCreate = 0, worstResize = 0;
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        const auto& a = sizes[attempt % 3];
        const auto& b = sizes[(attempt + 1) % 3];

        // CREAR: la primera asignación de la caché.
        telescope::raster::Cache cache;
        soilHeap (a.w, a.h, 0xff);
        REQUIRE (cache.prepare (1.0f, a.w, a.h));
        const int nc = nonZeroPixels (cache.image());
        dirtyCreate += nc > 0 ? 1 : 0;
        worstCreate  = juce::jmax (worstCreate, nc);

        // REDIMENSIONAR: la estela ya estaba encendida (se simula con 0xFF) y la ventana cambia de tamaño.
        cache.image().clear (cache.image().getBounds(), juce::Colour (0xffffffffu));
        soilHeap (b.w, b.h, 0xff);
        REQUIRE (cache.prepare (1.0f, b.w, b.h));
        const int nr = nonZeroPixels (cache.image());
        dirtyResize += nr > 0 ? 1 : 0;
        worstResize  = juce::jmax (worstResize, nr);
    }

    std::printf ("SCOPETRAIL[cache] %d intentos · al crear: %d con basura (peor: %d px) · al redimensionar: "
                 "%d con basura (peor: %d px)\n",
                 kAttempts, dirtyCreate, worstCreate, dirtyResize, worstResize);
    CHECK (dirtyCreate == 0);
    CHECK (dirtyResize == 0);
}

TEST_CASE ("telescope: SCOPE en silencio no muestra lo que habia en la memoria", "[telescope][scopetrail]")
{
    const bool reducedBefore = telescope::Lens::reducedMotion();
    telescope::Lens::setReducedMotion (false);   // con reduced-motion la estela se borra en cada cuadro

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kStereo);

    constexpr int kW = 1025, kH = 702;           // el tamaño L del banco de presupuesto

    // El tamaño de la estela sale de la lente, no de una cuenta copiada: es el que `prepare` va a pedir.
    juce::Rectangle<int> trail;
    {
        telescope::ScopeLens probe (proc);
        probe.setSize (kW, kH);
        juce::Image scratch (juce::Image::ARGB, kW, kH, true);
        juce::Graphics g (scratch);
        probe.paintEntireComponent (g, false);
        trail = probe.cacheAreaForTest();
    }
    REQUIRE (trail.getWidth() > 16);
    REQUIRE (trail.getHeight() > 16);

    int dirty = 0, worst = 0;
    juce::Rectangle<int> worstBox;
    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        const auto clean  = paintScopeOnce (proc, kW, kH, 0x00, trail.getWidth(), trail.getHeight());
        const auto soiled = paintScopeOnce (proc, kW, kH, 0xff, trail.getWidth(), trail.getHeight());
        juce::Rectangle<int> box;
        const int n = differingPixels (clean, soiled, box);
        dirty += n > 0 ? 1 : 0;
        if (n > worst) { worst = n; worstBox = box; }
    }

    std::printf ("SCOPETRAIL[lente] estela %dx%d en (%d,%d) · %d intentos · %d con basura a la vista (peor: %d px "
                 "en %d,%d %dx%d)\n",
                 trail.getWidth(), trail.getHeight(), trail.getX(), trail.getY(), kAttempts, dirty, worst,
                 worstBox.getX(), worstBox.getY(), worstBox.getWidth(), worstBox.getHeight());
    CHECK (dirty == 0);

    proc.releaseResources();
    telescope::Lens::setReducedMotion (reducedBefore);
}
