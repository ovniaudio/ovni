#pragma once
#include <functional>
#include <cmath>
#include <algorithm>
#include "render/ParticleParams.h"
#include "params/ParameterIDs.h"

// Fuente ÚNICA del mapeo params (unidades APVTS 0-100; gravity/hue bipolares) → ParticleParams (unidades
// físicas). El editor pasa un getter que lee el APVTS; el render tool, uno que lee una fila de preset. 50% de
// cada coeficiente de física reproduce EXACTAMENTE los magic numbers de M2 → un patch en default renderiza
// como M2. Acá también vive applyVariation (el motor de "miles de variaciones").
namespace supernova
{
// Choice MOTION (orden de curaduría de la UI) → motionMode del shader. Contornos es el default elegido por
// Joaquín; el modo 5 (natural) quedó PODADO del vocabulario público — su valor vive en 3+cutout.
inline constexpr int kMotionModes[] = { 3 /*Contornos*/, 1 /*Materia*/, 2 /*Onda*/, 4 /*Vórtices*/, 0 /*Radial*/ };
inline constexpr int kMotionChoiceCount = 5;
inline constexpr int kShapeChoiceCount  = 7;
inline constexpr int kFigureChoiceCount = 6;   // Imagen/Esfera/Espiral/Anillos/Grilla/Hélice (motor geométrico)

inline ParticleParams mapParticleParams (const std::function<float (const char*)>& get)
{
    namespace pid = params::id;
    const auto map = [&] (const char* id, float lo, float hi) { return lo + get (id) / 100.0f * (hi - lo); };

    ParticleParams pp;
    // Los 12 destinos de LFO se CLAMPEAN a su dominio físico: la modulación se suma DESPUÉS de que el APVTS
    // recortó, y sin esto un LFO a fondo (o dos al mismo destino) manda tamaño/saturación negativos al shader.
    // Con valores en rango el clamp es identidad → los goldens no se mueven.
    pp.intensity    = std::clamp (get (pid::INTENSITY) / 100.0f, 0.0f, 1.0f);
    pp.chaos        = std::clamp (get (pid::CHAOS) / 100.0f, 0.0f, 1.0f);
    pp.particleSize = std::clamp (0.5f + get (pid::PARTICLE_SIZE) / 100.0f * 3.5f, 0.5f, 4.0f);   // 0.5..4 px
    pp.glow         = std::clamp (get (pid::GLOW) / 100.0f, 0.0f, 1.0f);
    pp.explode      = get (pid::EXPLODE);                                // 0/1
    pp.cutoutAmt    = get (pid::CUTOUT) / 100.0f;                         // 0..1 — borrar fondo (escultura)

    pp.curlScale    = map (pid::CURL_SCALE,    0.30f, 2.70f);
    pp.homeStrength = map (pid::HOME_STRENGTH, 0.20f, 2.60f);
    pp.gravity      = get (pid::GRAVITY) / 100.0f * 0.60f;               // −100..100 → −0.6..0.6
    pp.momentum     = map (pid::MOMENTUM,      0.80f, 0.99f);
    pp.radialGain   = map (pid::RADIAL_GAIN,   0.20f, 2.20f);
    pp.jitterGain   = map (pid::JITTER_GAIN,   0.00f, 0.32f);
    pp.breatheGain  = map (pid::BREATHE_GAIN,  0.00f, 0.56f);

    // Vocabulario visual (capas 1-3 + paleta). Los choices llegan como índice crudo.
    const int motionIdx = std::clamp ((int) get (pid::MOTION), 0, kMotionChoiceCount - 1);
    pp.motionMode = kMotionModes[motionIdx];
    pp.shapeMode  = std::clamp ((int) get (pid::SHAPE), 0, kShapeChoiceCount - 1);
    pp.trailAmt   = std::clamp (get (pid::TRAILS) / 100.0f, 0.0f, 1.0f);
    pp.linksAmt   = std::clamp (get (pid::LINKS) / 100.0f, 0.0f, 1.0f);
    pp.satAmt     = std::clamp (get (pid::SAT) / 50.0f, 0.0f, 2.0f);      // 0..100 → 0..2 (50 = neutro)
    pp.hueShift   = std::clamp (get (pid::HUE), -180.0f, 180.0f) * 0.01745329252f;   // grados → radianes

    // TIER 1 PRO (fila 3). SPEED es LOGARÍTMICO: 0→×0.25, 50→×1.0 EXACTO, 100→×4 (16^0.5·0.25 = 1).
    pp.densityAmt   = std::clamp (get (pid::DENSITY) / 100.0f, 0.01f, 1.0f);
    pp.scatterAmt   = std::clamp (get (pid::SCATTER) / 100.0f, 0.0f, 1.0f);
    pp.speedMul     = 0.25f * std::pow (16.0f, get (pid::SPEED) / 100.0f);
    pp.rotateRate   = std::clamp (get (pid::ROTATE), -100.0f, 100.0f) / 100.0f * 0.5235988f;   // ±30°/s en rad
    pp.pumpAmt      = get (pid::PUMP) / 100.0f * 2.0f;                    // 0..2 (30 → 0.6 = clásico exacto)
    pp.hueCycleRate = get (pid::HUE_CYCLE) / 100.0f * 1.0471976f;         // ±100 → ±60°/s en rad
    static constexpr int kKaleidoSegs[] = { 0, 2, 4, 6, 8 };
    pp.kaleidoSeg   = kKaleidoSegs[std::clamp ((int) get (pid::KALEIDO), 0, 4)];

    // 3D + FIGURA (fila 4). Defaults (0/0/0/0, Imagen, 100) = identidad byte-exacta: depth 0 y ángulos 0
    // toman el camino legacy exacto del vertex; FIGURA Imagen apaga el remap de hogar aunque FORM sea 100.
    pp.depthAmt   = std::clamp (get (pid::DEPTH) / 100.0f, 0.0f, 1.0f);
    pp.rotXRad    = get (pid::ROT_X) * 0.01745329252f;                    // grados → rad (pitch)
    pp.rotYRad    = get (pid::ROT_Y) * 0.01745329252f;                    // grados → rad (yaw)
    pp.orbitRate  = std::clamp (get (pid::ORBIT), -100.0f, 100.0f) / 100.0f * 0.7853982f;   // ±45°/s en rad
    pp.formMode   = std::clamp ((int) get (pid::FIGURE), 0, kFigureChoiceCount - 1);
    pp.formAmt    = get (pid::FORM) / 100.0f;

    // COLOR LAB: el índice viaja crudo (el renderer hornea/cachea la rampa); Original (0) apaga el map.
    pp.paletteIdx = std::max (0, (int) get (pid::PALETTE));
    pp.rampAmt    = get (pid::COLOR_AMOUNT) / 100.0f;
    pp.bgAmt      = get (pid::BG) / 100.0f;
    return pp;
}

// ================================ VARIATION — "miles de variaciones" ================================
// Desplaza física + geometría dentro de rangos CURADOS alrededor del preset activo, sin tocar su identidad
// (shape/motion quedan; trails/links varían RELATIVO — un mundo sin estelas nunca las inventa). Determinista:
// el mismo (semilla de preset, variation) da SIEMPRE el mismo mundo → automatizable, recuperable, morphable.
// El recorrido es SUAVE (senos con fase por-campo): girar el knob viaja por mundos vecinos sin saltos.
namespace detail
{
inline float varNoise (int seed, int field, float v01, float cycles) noexcept
{
    const float phase = std::fabs (std::sin ((float) (seed * 31 + field) * 12.9898f) * 43758.5453f);
    return std::sin (6.2831853f * (v01 * cycles + (phase - std::floor (phase))));
}
}

inline void applyVariation (ParticleParams& pp, int presetSeed, float v01) noexcept
{
    if (v01 <= 0.001f) return;                                    // 0 = preset PURO (identidad exacta)
    v01 = std::min (1.0f, v01);
    // Envolvente: cerca de 0 el desvío entra suave (girar apenas el knob = mundos apenas distintos).
    const float e = std::min (1.0f, v01 / 0.12f);
    const auto n = [&] (int field, float cycles) { return e * detail::varNoise (presetSeed, field, v01, cycles); };

    // REFORZADO ×2 (2026-07-12, pedido "no se entiende qué hace"): girar el knob SE SIENTE — los desvíos
    // duplican su alcance manteniendo clamps, envolvente suave cerca de 0, determinismo y ejes-identidad
    // relativos (un mundo sin estelas jamás las inventa). NOTA: los HOTKNOBS por dominio NO pasan por acá —
    // escriben los params REALES de su fila desde la UI (ControlStrip::applyDomainTake), así los knobs se
    // MUEVEN a la vista (pedido de campo "cuando muevo el hotknob no los mueve").
    const auto mul   = [] (float x, float k, float lo, float hi) { return std::clamp (x * (1.0f + k), lo, hi); };
    pp.curlScale    = mul (pp.curlScale,    0.50f * n (0, 1.0f), 0.30f, 2.70f);
    pp.homeStrength = mul (pp.homeStrength, 0.40f * n (1, 2.0f), 0.20f, 2.60f);
    pp.gravity      = std::clamp (pp.gravity + 0.20f * n (2, 1.0f), -0.60f, 0.60f);
    pp.momentum     = std::clamp (pp.momentum + 0.09f * n (3, 2.0f), 0.80f, 0.99f);
    pp.radialGain   = mul (pp.radialGain,   0.44f * n (4, 1.0f), 0.20f, 2.20f);
    pp.jitterGain   = mul (pp.jitterGain,   0.60f * n (5, 2.0f), 0.00f, 0.32f);
    pp.breatheGain  = mul (pp.breatheGain,  0.60f * n (6, 1.0f), 0.00f, 0.56f);
    pp.chaos        = mul (pp.chaos,        0.44f * n (7, 2.0f), 0.00f, 1.00f);
    pp.intensity    = mul (pp.intensity,    0.20f * n (8, 1.0f), 0.00f, 1.00f);
    pp.particleSize = mul (pp.particleSize, 0.44f * n (9, 1.0f), 0.50f, 4.00f);
    pp.glow         = mul (pp.glow,         0.30f * n (10, 2.0f), 0.00f, 1.00f);
    pp.trailAmt     = mul (pp.trailAmt,     0.60f * n (11, 1.0f), 0.00f, 1.00f);   // relativo: 0 queda 0
    pp.linksAmt     = mul (pp.linksAmt,     0.60f * n (12, 2.0f), 0.00f, 1.00f);   // relativo: 0 queda 0
    pp.hueShift     = pp.hueShift + 0.70f * n (13, 1.0f);                          // deriva de tono ±40°
    pp.satAmt       = mul (pp.satAmt,       0.24f * n (14, 2.0f), 0.00f, 2.00f);

    // Tier 1 PRO — 6 ejes más para el recorrido (21 métricas en total). Los que definen IDENTIDAD del mundo
    // (rotate/hueCycle/scatter en 0) varían RELATIVO: un mundo quieto no empieza a girar solo.
    pp.densityAmt   = mul (pp.densityAmt,   0.30f * n (15, 1.0f), 0.05f, 1.00f);
    pp.scatterAmt   = mul (pp.scatterAmt,   0.70f * n (16, 2.0f), 0.00f, 1.00f);   // relativo: 0 queda 0
    pp.speedMul     = mul (pp.speedMul,     0.36f * n (17, 1.0f), 0.25f, 4.00f);
    pp.rotateRate   = pp.rotateRate * (1.0f + 0.60f * n (18, 1.0f));               // relativo: 0 queda 0
    pp.pumpAmt      = mul (pp.pumpAmt,      0.50f * n (19, 2.0f), 0.00f, 2.00f);
    pp.hueCycleRate = pp.hueCycleRate * (1.0f + 0.60f * n (20, 1.0f));             // relativo: 0 queda 0

    // 3D + FIGURA — 3 ejes más (24 métricas). Todos RELATIVOS (un mundo plano queda plano; la cámara fija
    // no se toca — los ángulos son composición, no textura). formAmt solo respira si hay figura activa.
    pp.depthAmt     = mul (pp.depthAmt,     0.50f * n (21, 1.0f), 0.00f, 1.00f);   // relativo: 0 queda 0
    pp.orbitRate    = pp.orbitRate * (1.0f + 0.60f * n (22, 1.0f));               // relativo: 0 queda 0
    if (pp.formMode != 0)
        pp.formAmt  = mul (pp.formAmt,      0.40f * n (23, 2.0f), 0.00f, 1.00f);
}
}
