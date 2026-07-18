// [supernova][links] — PLEXUS (capa 3): conexiones entre elementos cercanos.
// Contrato observable en píxeles: con LINKS=0 el render es el clásico; con LINKS alto las líneas SUMAN luz
// (más píxeles encendidos / más luma total) sin romper nada (frames completos, sin NaN visual). El ORDEN de
// emisión de líneas es atómico (no determinista) → los goldens NO cubren links; este test cubre el contrato.
// Tag [.gpu]: se auto-saltea sin GPU Metal.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cmath>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 128;   // 16k partículas: separación 1/128 ≈ 0.008 << radio de vecindad → hay líneas
constexpr int kPx   = 384;

double lumaSum (const std::vector<uint8_t>& rgba)
{
    double s = 0.0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        s += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
    return s;
}
}

TEST_CASE ("links: el plexus suma conexiones visibles y no rompe el render", "[supernova][links][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable())
    {
        SUCCEED ("sin GPU Metal — test de plexus saltado (CI)");
        return;
    }

    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    supernova::AnalysisFrame af;
    af.rms = 0.3f; af.energy = 0.3f; af.bass = 0.15f;

    supernova::ParticleParams pp;
    pp.intensity = 0.55f; pp.chaos = 0.3f; pp.particleSize = 1.2f;

    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);

    // Baseline sin plexus.
    pp.linksAmt = 0.0f;
    for (int f = 0; f < 10; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    const double base = lumaSum (rgba);
    REQUIRE (base > 0.0);

    // Con plexus denso: las líneas SUMAN luz de forma clara (>4% de luma extra medida en la práctica es
    // muchísimo más; el umbral es conservador para no ser frágil).
    pp.linksAmt = 0.9f;
    for (int f = 0; f < 10; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    const double linked = lumaSum (rgba);
    REQUIRE (linked > base * 1.04);

    // Volver a 0 apaga el plexus (no queda estado pegado).
    pp.linksAmt = 0.0f;
    for (int f = 0; f < 10; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    const double off = lumaSum (rgba);
    REQUIRE (std::abs (off - base) < base * 0.10);   // mismo orden que el baseline (la sim siguió avanzando)
}
