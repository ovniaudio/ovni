#pragma once
#include "params/ParameterIDs.h"

// PresetMorph — ease del lado del render entre snapshots de preset (RF6: "sin corte seco"). PURO (sin JUCE):
// el editor le pasa snapshots en UNIDADES de APVTS y lee un snapshot interpolado por tick. NUNCA escribe el
// APVTS → el estado guardado siempre es el destino (round-trip trivial). Vive en el editor (es visual, sólo
// corre con la vista GPU abierta). Interpola los 17 params CONTINUOS (4 héroe + 7 física + 6 vocabulario:
// trails/links/sat/hue/cutout/variation); explode/bypass/preset y los CHOICES (motion/shape) saltan — la
// identidad del mundo cambia de golpe, su materia se desliza. smootherstep (C2, sin overshoot).
namespace supernova
{
inline constexpr double kPresetMorphSeconds = 0.5;   // duración del morph (0.35..0.6 recomendado)

// Los ids continuos morph-elegibles (mismo orden que MorphSnapshot::v).
inline constexpr const char* kMorphIds[] = {
    params::id::INTENSITY, params::id::CHAOS, params::id::PARTICLE_SIZE, params::id::GLOW,
    params::id::CURL_SCALE, params::id::HOME_STRENGTH, params::id::GRAVITY, params::id::MOMENTUM,
    params::id::RADIAL_GAIN, params::id::JITTER_GAIN, params::id::BREATHE_GAIN,
    params::id::TRAILS, params::id::LINKS, params::id::SAT, params::id::HUE,
    params::id::CUTOUT, params::id::VARIATION,
    params::id::DENSITY, params::id::SCATTER, params::id::SPEED,
    params::id::ROTATE, params::id::PUMP, params::id::HUE_CYCLE,
    params::id::DEPTH, params::id::ROT_X, params::id::ROT_Y, params::id::ORBIT, params::id::FORM,
    params::id::COLOR_AMOUNT, params::id::BG
};
// NOTA: los HOTKNOBS (varMovement/varMatter/varCamera/varColor) NO entran al morph a propósito: son macros
// de UI que ESCRIBEN los params reales de su fila (ControlStrip::applyDomainTake) — morphearlos dispararía
// escrituras fantasma. El editor los resetea a 0 al cambiar de preset.

struct MorphSnapshot
{
    static constexpr int N = 30;
    float v[N] = { 50, 30, 40, 50, 50, 50, 0, 50, 50, 50, 50,
                   0, 0, 50, 0, 0, 0,
                   100, 0, 50, 0, 30, 0,
                   0, 0, 0, 0, 100,
                   100, 0 };   // defaults de fábrica (unidades APVTS, mismo orden que kMorphIds)
};

class PresetMorph
{
public:
    // Empieza a ease-ar desde la SALIDA actual hacia `target` en `seconds` (0 → salto). Re-armar en medio
    // reinicia desde la salida actual → nunca hay un salto.
    void setTarget (const MorphSnapshot& target, double seconds) noexcept;

    // Avanza `dt` (s) y devuelve el snapshot interpolado. Idle → devuelve la última salida.
    MorphSnapshot tick (double dt) noexcept;

    // Fija el estado interno a `live` mientras está idle (arrastre directo de knobs pasa de largo y un
    // setTarget posterior ease-a desde el lugar correcto). Llamar en cada tick idle.
    void syncTo (const MorphSnapshot& live) noexcept { from = to = out = live; }

    bool active() const noexcept { return phase < 1.0; }

private:
    static float ease (double t) noexcept   // smootherstep
    { return (float) (t * t * t * (t * (t * 6.0 - 15.0) + 10.0)); }

    MorphSnapshot from {}, to {}, out {};
    double phase = 1.0;   // >=1 → idle
    double dur   = 0.0;
};
}
