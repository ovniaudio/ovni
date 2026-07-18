// [supernova][fit] — FIT del aspecto: el contenido conserva el aspecto de la IMAGEN dentro del viewport
// (letterbox/pillarbox centrado) — la foto vertical se ve vertical, no estirada. Tag [.gpu]: auto-skip.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 512;

bool gpuOk (supernova::MetalRenderer& r)
{
    if (r.isAvailable()) return true;
    SUCCEED ("sin GPU Metal — test [fit] saltado (CI)");
    return false;
}

// Render de N frames idle con la imagen de fábrica (cuadrada) → RGBA de w×h.
std::vector<uint8_t> render (supernova::MetalRenderer& r, int w, int h)
{
    std::vector<uint8_t> rgba ((size_t) w * h * 4, 0);
    supernova::ParticleParams pp;   // defaults
    supernova::AnalysisFrame af;
    for (int f = 0; f < 20; ++f)
        REQUIRE (r.renderOffscreen (af, pp, w, h, rgba.data()));
    return rgba;
}

// Luma media de una banda vertical [x0,x1) sobre todo el alto.
double bandLumaX (const std::vector<uint8_t>& rgba, int w, int h, int x0, int x1)
{
    double sum = 0.0; long n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t* p = &rgba[((size_t) y * w + x) * 4];
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    return n > 0 ? sum / n : 0.0;
}
double bandLumaY (const std::vector<uint8_t>& rgba, int w, int h, int y0, int y1)
{
    double sum = 0.0; long n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x)
        {
            const uint8_t* p = &rgba[((size_t) y * w + x) * 4];
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            ++n;
        }
    return n > 0 ? sum / n : 0.0;
}

void prepareFactory (supernova::MetalRenderer& r)
{
    r.prepare (kGrid, kGrid);
    auto img = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ img.data(), kGrid, kGrid });   // cuadrada → imgAspect 1
}
}

TEST_CASE ("fit: imagen cuadrada en viewport ANCHO → pillarbox (bordes L/R oscuros)",
           "[supernova][fit][.gpu]")
{
    supernova::MetalRenderer r;
    if (! gpuOk (r)) return;
    prepareFactory (r);

    const int w = 400, h = 200;                       // vpAspect 2 · imgAspect 1 → fitX 0.5
    const auto img = render (r, w, h);
    const double left   = bandLumaX (img, w, h, 0,   w / 5);            // 20% izquierdo
    const double center = bandLumaX (img, w, h, 2 * w / 5, 3 * w / 5); // 20% central
    const double right  = bandLumaX (img, w, h, 4 * w / 5, w);          // 20% derecho
    REQUIRE (center > left  * 2.0);                   // el contenido vive en el centro
    REQUIRE (center > right * 2.0);
}

TEST_CASE ("fit: imagen cuadrada en viewport ALTO → letterbox (bordes arriba/abajo oscuros)",
           "[supernova][fit][.gpu]")
{
    supernova::MetalRenderer r;
    if (! gpuOk (r)) return;
    prepareFactory (r);

    const int w = 200, h = 400;                       // vpAspect 0.5 · imgAspect 1 → fitY 0.5
    const auto img = render (r, w, h);
    const double top    = bandLumaY (img, w, h, 0,   h / 5);
    const double center = bandLumaY (img, w, h, 2 * h / 5, 3 * h / 5);
    const double bottom = bandLumaY (img, w, h, 4 * h / 5, h);
    REQUIRE (center > top    * 2.0);
    REQUIRE (center > bottom * 2.0);
}
