// [supernova][bounds] — CONTENCIÓN dura (bug de campo: "se van fuera del recuadro y queda negro").
// No mira PNGs: lee las POSICIONES reales de las 262k partículas del buffer GPU y verifica numéricamente que
// TODAS quedan dentro del recuadro extendido, bajo un bombardeo peor que cualquier preset de fábrica:
// física al límite del rango automatizable + kicks repetidos + EXPLODE sostenido + rms/bass/treble al mango.
// Tag [.gpu]: se auto-saltea sin GPU Metal (CI sin Metal sigue verde).
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
constexpr int kGrid = 512;                 // 262 144 partículas (default del spec)
constexpr int kPx   = 128;                 // render chico: acá importan las posiciones, no los píxeles

// Contrato del shader: clamp DURO post-integración a [−0.08, 1.08] (el resorte de pared es solo pre-freno).
// ε de tolerancia por redondeo float.
constexpr float kLo = -0.085f, kHi = 1.085f;

struct Extremes { float minX, maxX, minY, maxY; unsigned outOfBounds, nonFinite; };

Extremes scanPositions (const std::vector<float>& xy)
{
    Extremes e { 1e9f, -1e9f, 1e9f, -1e9f, 0, 0 };
    for (size_t i = 0; i + 1 < xy.size(); i += 2)
    {
        const float x = xy[i], y = xy[i + 1];
        if (! std::isfinite (x) || ! std::isfinite (y)) { ++e.nonFinite; continue; }
        e.minX = std::min (e.minX, x); e.maxX = std::max (e.maxX, x);
        e.minY = std::min (e.minY, y); e.maxY = std::max (e.maxY, y);
        if (x < kLo || x > kHi || y < kLo || y > kHi) ++e.outOfBounds;
    }
    return e;
}
}

TEST_CASE ("bounds: bombardeo extremo y NINGUNA partícula sale del recuadro", "[supernova][bounds][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable())
    {
        SUCCEED ("sin GPU Metal — test de contención saltado (CI)");
        return;
    }

    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    // Física MÁS violenta que cualquier preset de fábrica: todos los rangos automatizables al extremo malo.
    supernova::ParticleParams pp;
    pp.intensity    = 1.0f;
    pp.chaos        = 1.0f;
    pp.curlScale    = 2.7f;    // tope del rango
    pp.homeStrength = 0.2f;    // resorte MÍNIMO (peor caso de re-armado)
    pp.gravity      = 0.6f;    // tirando para abajo todo el tiempo
    pp.momentum     = 0.99f;   // casi sin fricción (peor caso balístico)
    pp.radialGain   = 2.2f;    // explosión al tope
    pp.jitterGain   = 0.32f;
    pp.breatheGain  = 0.56f;

    supernova::AnalysisFrame af;
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);
    std::vector<float>   xy ((size_t) kGrid * kGrid * 2);

    const int totalFrames = 480;           // 8 s simulados
    unsigned worstOut = 0, worstNonFinite = 0;
    float gMinX = 1e9f, gMaxX = -1e9f, gMinY = 1e9f, gMaxY = -1e9f;

    for (int f = 0; f < totalFrames; ++f)
    {
        af.bass   = 0.95f;                          // graves al mango sostenidos
        af.rms    = 0.90f; af.energy = 0.90f;       // nivel altísimo sostenido (breathe al máximo)
        af.treble = 0.80f;
        af.onset  = (f % 20 == 0);                  // kick cada 20 frames (¡3 por segundo!)
        af.onsetCount = (unsigned) (f / 20 + 1);
        pp.explode = (f >= 200 && f < 300) ? 1.0f : 0.0f;   // 100 frames de EXPLODE SOSTENIDO (modo tormenta)
        pp.rayTrigger = (f % 45 == 0);              // rayos MIDI también
        pp.rayAngle   = (float) f * 0.13f;

        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));

        if (f % 10 == 0 || f == totalFrames - 1)    // muestrear posiciones cada 10 frames
        {
            REQUIRE (r.debugReadPositions (xy.data(), (unsigned) (kGrid * kGrid)));
            const auto e = scanPositions (xy);
            worstOut        = std::max (worstOut, e.outOfBounds);
            worstNonFinite  = std::max (worstNonFinite, e.nonFinite);
            gMinX = std::min (gMinX, e.minX); gMaxX = std::max (gMaxX, e.maxX);
            gMinY = std::min (gMinY, e.minY); gMaxY = std::max (gMaxY, e.maxY);
        }
    }

    INFO ("rango observado tras 480 frames de bombardeo: x=[" << gMinX << ", " << gMaxX
          << "]  y=[" << gMinY << ", " << gMaxY << "]  (paredes en [-0.08, 1.08], tolerancia [" << kLo << ", " << kHi << "])");
    CHECK (worstNonFinite == 0);          // jamás NaN/Inf
    CHECK (worstOut == 0);                // NINGUNA partícula fuera del recuadro extendido, en ningún muestreo
    CHECK (gMinX >= kLo); CHECK (gMaxX <= kHi);
    CHECK (gMinY >= kLo); CHECK (gMaxY <= kHi);
}

TEST_CASE ("bounds: tras el bombardeo la imagen se RE-ARMA (vuelve a casa)", "[supernova][bounds][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable())
    {
        SUCCEED ("sin GPU Metal — test de re-armado saltado (CI)");
        return;
    }

    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    supernova::ParticleParams pp;                   // física DEFAULT (la que ve un usuario sin tocar nada)
    supernova::AnalysisFrame af;
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);
    std::vector<float>   xy ((size_t) kGrid * kGrid * 2);

    // 1) kick fuerte → 2) 240 frames de silencio (decay) → medir distancia media al hogar.
    for (int f = 0; f < 250; ++f)
    {
        af = {};
        af.onset = (f == 5);
        af.onsetCount = (f >= 5) ? 1u : 0u;
        af.bass = (f >= 5 && f < 9) ? 0.9f : 0.05f;
        af.rms = 0.05f; af.energy = 0.05f;          // silencio: la imagen debe quedar QUIETA y ARMADA
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
    }

    REQUIRE (r.debugReadPositions (xy.data(), (unsigned) (kGrid * kGrid)));

    // Los hogares son la grilla permutada — no los conocemos acá, pero la MÉTRICA de re-armado que importa es
    // la cobertura: en una imagen re-armada las posiciones llenan el cuadro uniformemente (la grilla), sin
    // hueco central. Medimos la densidad en un disco central (r<0.1 ≈ 3.1% del área ⇒ ~8.2k de 262k) y
    // exigimos al menos la MITAD de lo esperado (un hueco como el del bug daba ~0 acá).
    unsigned inCenter = 0;
    for (size_t i = 0; i + 1 < xy.size(); i += 2)
    {
        const float dx = xy[i] - 0.5f, dy = xy[i + 1] - 0.5f;
        if (dx * dx + dy * dy < 0.1f * 0.1f) ++inCenter;
    }
    const unsigned expected = (unsigned) ((float) kGrid * kGrid * 3.14159f * 0.01f);   // πr²·N
    INFO ("partículas en el disco central r<0.1: " << inCenter << " (esperado ≈ " << expected << ")");
    CHECK (inCenter > expected / 2);
}
