// [supernova][still] — la promesa de CLEAR: con INTENSITY 0 el lienzo queda QUIETO DE VERDAD, incluso
// con música sonando (bass/rms/onset gateados). Con INTENSITY ≥ 4% el motor es EXACTAMENTE el histórico
// (la identidad la prueban los goldens, que no se regeneran). Tag [.gpu]: auto-skip sin Metal.
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
constexpr int kPx   = 160;

bool gpuOk (supernova::MetalRenderer& r)
{
    if (r.isAvailable()) return true;
    SUCCEED ("sin GPU Metal — test [still] saltado (CI)");
    return false;
}

void prepareWithFactory (supernova::MetalRenderer& r)
{
    r.prepare (kGrid, kGrid);
    auto img = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ img.data(), kGrid, kGrid });
}

// Frame de MÚSICA fuerte (el peor caso para la quietud): graves + nivel + onset periódico.
supernova::AnalysisFrame loudFrame (int f)
{
    supernova::AnalysisFrame af;
    af.bass = 0.85f; af.rms = 0.55f; af.energy = 0.55f; af.treble = 0.4f;
    af.onset = (f % 30 == 0);
    return af;
}

// El snapshot lienzo de CLEAR en unidades de ParticleParams (espejo de CanvasState, polo quieto).
supernova::ParticleParams clearParams()
{
    supernova::ParticleParams pp;
    pp.intensity = 0.0f; pp.chaos = 0.0f;
    pp.jitterGain = 0.0f; pp.breatheGain = 0.0f; pp.gravity = 0.0f;
    pp.radialGain = 0.20f;   // BLAST 0 (piso del mapeo)
    pp.pumpAmt = 0.0f;
    return pp;               // el resto ya es identidad (trails/links/scatter/depth/... = 0)
}

std::vector<uint8_t> render (supernova::MetalRenderer& r, const supernova::ParticleParams& pp,
                             int from, int count)
{
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4, 0);
    for (int f = from; f < from + count; ++f)
        REQUIRE (r.renderOffscreen (loudFrame (f), pp, kPx, kPx, rgba.data()));
    return rgba;
}
}

TEST_CASE ("still: INTENSITY 0 = foto congelada aunque suene música (frame N == frame N+30)",
           "[supernova][still][.gpu]")
{
    supernova::MetalRenderer r;
    if (! gpuOk (r)) return;
    prepareWithFactory (r);

    const auto pp = clearParams();
    const auto a = render (r, pp, 0, 120);     // asentar: velocidad residual muere por momentum
    const auto b = render (r, pp, 120, 30);    // 30 frames más de música fuerte + onsets
    const bool frozen = (a == b);              // bool: si falla, Catch no imprime los 100 KB del frame
    REQUIRE (frozen);                          // byte-idéntico: quietud REAL
}

TEST_CASE ("still: la vida vuelve al subir INTENSITY (el fader maestro)",
           "[supernova][still][.gpu]")
{
    supernova::MetalRenderer r;
    if (! gpuOk (r)) return;
    prepareWithFactory (r);

    auto pp = clearParams();
    pp.intensity = 0.5f;                       // default histórico
    const auto a = render (r, pp, 0, 60);
    const auto b = render (r, pp, 60, 1);
    const bool alive = (a != b);
    REQUIRE (alive);                           // con vida, los frames difieren
}

TEST_CASE ("still: CLEAR es un CORTE — snapToHome deja el lienzo EXACTO en el primer frame",
           "[supernova][still][snap][.gpu]")
{
    // A: mundo SUCIO de verdad (kicks, scatter, cámara girando, hue derivando) 60 frames...
    supernova::MetalRenderer a, b;
    if (! gpuOk (a)) return;
    prepareWithFactory (a);
    prepareWithFactory (b);

    supernova::ParticleParams dirty;
    dirty.chaos = 0.8f; dirty.scatterAmt = 0.6f; dirty.rotateRate = 0.5f;
    dirty.hueCycleRate = 0.9f; dirty.orbitRate = 0.6f; dirty.depthAmt = 0.7f;
    std::vector<uint8_t> tmp ((size_t) kPx * kPx * 4);
    for (int f = 0; f < 60; ++f)
        REQUIRE (a.renderOffscreen (loudFrame (f), dirty, kPx, kPx, tmp.data()));

    // ...snap + params lienzo → el PRIMER frame ya es el lienzo (posiciones=hogar, acumuladores=0)...
    a.snapToHome();
    const auto pp = clearParams();
    std::vector<uint8_t> snapped ((size_t) kPx * kPx * 4);
    REQUIRE (a.renderOffscreen (supernova::AnalysisFrame {}, pp, kPx, kPx, snapped.data()));

    // ...idéntico BYTE a un renderer FRESCO con los mismos params (sin viaje, sin giro residual).
    std::vector<uint8_t> fresh ((size_t) kPx * kPx * 4);
    REQUIRE (b.renderOffscreen (supernova::AnalysisFrame {}, pp, kPx, kPx, fresh.data()));
    const bool cut = (snapped == fresh);
    REQUIRE (cut);
}

TEST_CASE ("breathe: vive en Contours — 0 vs 100 cambia el hervor con nivel",
           "[supernova][still][breathe][.gpu]")
{
    supernova::MetalRenderer a, b;
    if (! gpuOk (a)) return;
    prepareWithFactory (a);
    prepareWithFactory (b);

    supernova::ParticleParams pa;   // defaults (motionMode 3, breatheGain 0.28)
    supernova::ParticleParams pb;
    pa.breatheGain = 0.0f;          // BREATHE 0
    pb.breatheGain = 0.56f;         // BREATHE 100

    // Con rms alto el simmer escala con breatheGain → los mundos divergen.
    std::vector<uint8_t> ra ((size_t) kPx * kPx * 4), rb (ra.size());
    supernova::AnalysisFrame af;
    af.rms = 0.55f; af.energy = 0.55f;
    for (int f = 0; f < 40; ++f)
    {
        REQUIRE (a.renderOffscreen (af, pa, kPx, kPx, ra.data()));
        REQUIRE (b.renderOffscreen (af, pb, kPx, kPx, rb.data()));
    }
    const bool differs = (ra != rb);
    REQUIRE (differs);
}

TEST_CASE ("breathe: en silencio da igual (el simmer es rms-driven)",
           "[supernova][still][breathe][.gpu]")
{
    supernova::MetalRenderer a, b;
    if (! gpuOk (a)) return;
    prepareWithFactory (a);
    prepareWithFactory (b);

    supernova::ParticleParams pa, pb;
    pa.breatheGain = 0.0f;
    pb.breatheGain = 0.56f;

    std::vector<uint8_t> ra ((size_t) kPx * kPx * 4), rb (ra.size());
    supernova::AnalysisFrame silence;   // POD: todo 0
    for (int f = 0; f < 40; ++f)
    {
        REQUIRE (a.renderOffscreen (silence, pa, kPx, kPx, ra.data()));
        REQUIRE (b.renderOffscreen (silence, pb, kPx, kPx, rb.data()));
    }
    const bool same = (ra == rb);
    REQUIRE (same);
}
