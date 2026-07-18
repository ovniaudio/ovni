// [supernova][depth3d] — 3D de presentación + motor geométrico (FIGURA). No mira PNGs de referencia:
// verifica los INVARIANTES del diseño sobre el buffer offscreen real:
//   · identidad: depth/ángulos/figura en reposo ≡ byte-exacto al camino legacy (la promesa de los goldens);
//   · el 3D CAMBIA la imagen (la cámara no es un no-op) y el flip a 180° NO deja un hueco (cáscara);
//   · FIGURA es determinista (mismos inputs → mismos bytes) y distinta de la imagen plana;
//   · la física con figura activa sigue CONTENIDA (las paredes no se negocian).
// Tag [.gpu]: se auto-saltea sin GPU Metal (CI sin Metal sigue verde).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cmath>
#include <cstring>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 512;
constexpr int kPx   = 160;

// Render determinista de N frames idle (sin audio) y devuelve el último frame RGBA.
std::vector<uint8_t> renderFrames (supernova::MetalRenderer& r, const supernova::ParticleParams& pp, int frames)
{
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4, 0);
    supernova::AnalysisFrame af;   // silencio: bass/rms/treble en el default del POD
    for (int f = 0; f < frames; ++f)
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    return rgba;
}

double meanLuma (const std::vector<uint8_t>& rgba)
{
    double sum = 0.0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
    return sum / ((double) rgba.size() / 4.0);
}

size_t bytesDiffer (const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    size_t n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i]) ++n;
    return n;
}

bool gpuOk (supernova::MetalRenderer& r)
{
    if (r.isAvailable()) return true;
    SUCCEED ("sin GPU Metal — test 3D saltado (CI)");
    return false;
}

void prepareWithFactory (supernova::MetalRenderer& r)
{
    r.prepare (kGrid, kGrid);
    auto img = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ img.data(), kGrid, kGrid });
}
}

TEST_CASE ("depth3d: reposo explícito (depth 0, ángulos 0, figura Imagen) ≡ default byte-exacto",
           "[supernova][depth3d][.gpu]")
{
    supernova::MetalRenderer a, b;
    if (! gpuOk (a)) return;
    prepareWithFactory (a);
    prepareWithFactory (b);

    supernova::ParticleParams base;                       // defaults del struct
    supernova::ParticleParams rest = base;
    rest.depthAmt = 0.0f; rest.rotXRad = 0.0f; rest.rotYRad = 0.0f;
    rest.orbitRate = 0.0f; rest.formMode = 0; rest.formAmt = 1.0f;

    const auto fa = renderFrames (a, base, 12);
    const auto fb = renderFrames (b, rest, 12);
    REQUIRE (bytesDiffer (fa, fb) == 0);                  // la identidad del branch, verificada de punta a punta
}

TEST_CASE ("depth3d: la cámara 3D cambia la imagen y el flip 180° NO deja hueco (cáscara)",
           "[supernova][depth3d][.gpu]")
{
    supernova::MetalRenderer flat, front, back;
    if (! gpuOk (flat)) return;
    prepareWithFactory (flat);
    prepareWithFactory (front);
    prepareWithFactory (back);

    supernova::ParticleParams pp;                          // plano (referencia)
    supernova::ParticleParams p3 = pp;
    p3.depthAmt = 0.7f;                                    // volumen + leve yaw: la cámara actúa
    p3.rotYRad  = 0.9f;
    supernova::ParticleParams pf = p3;
    pf.rotYRad  = 3.14159265f;                             // dado vuelta ENTERO

    const auto fFlat  = renderFrames (flat,  pp, 12);
    const auto fFront = renderFrames (front, p3, 12);
    const auto fBack  = renderFrames (back,  pf, 12);

    // La cámara no es un no-op: una fracción sustancial de los bytes cambia.
    REQUIRE (bytesDiffer (fFlat, fFront) > fFlat.size() / 20);

    // El dorso EXISTE: al girar 180° la energía en pantalla se conserva en el mismo orden de magnitud
    // (la cáscara por paridad reparte mitad frente / mitad dorso — sin ella esto caería a ~0).
    const double lumaFront = meanLuma (fFront);
    const double lumaBack  = meanLuma (fBack);
    REQUIRE (lumaBack > lumaFront * 0.35);
    REQUIRE (lumaBack > 1.0);                              // y no es una pantalla negra en términos absolutos
}

TEST_CASE ("depth3d: FIGURA (esfera) — determinista byte-exacto y distinta de la imagen plana",
           "[supernova][depth3d][.gpu]")
{
    supernova::MetalRenderer a, b, plainR;
    if (! gpuOk (a)) return;
    prepareWithFactory (a);
    prepareWithFactory (b);
    prepareWithFactory (plainR);

    supernova::ParticleParams sphere;
    sphere.formMode = 1;                                   // ESFERA
    sphere.formAmt  = 1.0f;
    sphere.depthAmt = 0.6f;
    sphere.rotYRad  = 0.5f;

    supernova::ParticleParams plain;                       // sin figura

    const auto fa = renderFrames (a, sphere, 40);          // 40 frames: el resorte forma la figura
    const auto fb = renderFrames (b, sphere, 40);
    const auto fp = renderFrames (plainR, plain, 40);

    REQUIRE (bytesDiffer (fa, fb) == 0);                   // determinismo del camino extra/formT/θ
    REQUIRE (bytesDiffer (fa, fp) > fa.size() / 20);       // y es OTRO mundo, no un no-op
}

TEST_CASE ("depth3d: con figura + kicks la física sigue CONTENIDA (paredes intactas)",
           "[supernova][depth3d][.gpu]")
{
    supernova::MetalRenderer r;
    if (! gpuOk (r)) return;
    prepareWithFactory (r);

    supernova::ParticleParams pp;
    pp.formMode = 5;                                       // HÉLICE (la figura más excéntrica)
    pp.formAmt  = 1.0f;
    pp.depthAmt = 1.0f;
    pp.intensity = 1.0f; pp.chaos = 1.0f; pp.radialGain = 2.2f; pp.momentum = 0.99f;

    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);
    for (int f = 0; f < 60; ++f)
    {
        supernova::AnalysisFrame af;
        af.onset = (f % 12 == 0);                          // bombardeo de kicks
        af.bass = 0.9f; af.rms = 0.8f; af.treble = 0.7f;
        af.onsetCount = (unsigned) (f / 12 + 1);
        pp.explode = (f % 24 < 4) ? 1.0f : 0.0f;
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    }

    std::vector<float> xy ((size_t) kGrid * kGrid * 2);
    REQUIRE (r.debugReadPositions (xy.data(), kGrid * kGrid));
    unsigned bad = 0, nonFinite = 0;
    for (size_t i = 0; i + 1 < xy.size(); i += 2)
    {
        if (! std::isfinite (xy[i]) || ! std::isfinite (xy[i + 1])) { ++nonFinite; continue; }
        if (xy[i] < -0.085f || xy[i] > 1.085f || xy[i + 1] < -0.085f || xy[i + 1] > 1.085f) ++bad;
    }
    REQUIRE (nonFinite == 0);
    REQUIRE (bad == 0);
}
