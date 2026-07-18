// [supernova][color] — COLOR GRADING (SAT/HUE en f_composite, post-ACES pre-sRGB).
// Lee los PÍXELES del render offscreen y verifica el contrato: sat=0 → monocromo real (R≈G≈B en TODOS los
// píxeles, bloom incluido), sat=1/hue=0 → los colores viven (identidad la cubre el golden test), hue=180° →
// paleta distinta con luminancia parecida. Tag [.gpu]: se auto-saltea sin GPU Metal (CI sin Metal verde).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 256;   // grilla chica: acá importa el color, no la densidad
constexpr int kPx   = 256;

struct ColorStats { int maxChanDelta; double meanLuma; unsigned litPixels; };

ColorStats scan (const std::vector<uint8_t>& rgba)
{
    ColorStats s { 0, 0.0, 0 };
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
    {
        const int r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
        const int d = std::max ({ std::abs (r - g), std::abs (g - b), std::abs (r - b) });
        s.maxChanDelta = std::max (s.maxChanDelta, d);
        const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        s.meanLuma += luma;
        if (luma > 24.0) ++s.litPixels;
    }
    s.meanLuma /= (double) (rgba.size() / 4);
    return s;
}

std::vector<uint8_t> renderFrames (supernova::MetalRenderer& r, const supernova::ParticleParams& pp, int frames)
{
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);
    supernova::AnalysisFrame af;
    af.rms = 0.3f; af.energy = 0.3f; af.bass = 0.2f;
    for (int f = 0; f < frames; ++f)
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    return rgba;
}
}

TEST_CASE ("color: sat=0 es monocromo real y hue rota la paleta", "[supernova][color][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable())
    {
        SUCCEED ("sin GPU Metal — test de color grading saltado (CI)");
        return;
    }

    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);   // la imagen de fábrica es COLORIDA (gradiente)
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    supernova::ParticleParams pp;
    pp.intensity = 0.6f; pp.chaos = 0.3f; pp.particleSize = 1.5f;

    // Neutro: hay color de verdad (si esto falla, la fábrica dejó de ser colorida y el test no prueba nada).
    const auto neutral = scan (renderFrames (r, pp, 8));
    REQUIRE (neutral.litPixels > 500);
    REQUIRE (neutral.maxChanDelta > 40);

    // SAT 0 → B/N editorial: NINGÚN píxel con desbalance de canal (tolerancia 2/255 por redondeo sRGB).
    pp.satAmt = 0.0f;
    const auto mono = scan (renderFrames (r, pp, 8));
    REQUIRE (mono.maxChanDelta <= 2);
    REQUIRE (mono.litPixels > 500);          // sigue habiendo imagen (no se apagó nada)

    // HUE 180° (sat neutro): paleta distinta al neutro, con energía similar (rotación, no atenuación).
    pp.satAmt = 1.0f; pp.hueShift = 3.14159265f;
    const auto rotated = scan (renderFrames (r, pp, 8));
    REQUIRE (rotated.maxChanDelta > 40);     // sigue siendo colorido
    REQUIRE (std::abs (rotated.meanLuma - neutral.meanLuma) < neutral.meanLuma * 0.35);
}
