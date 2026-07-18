#include "presets/PresetMorph.h"
#include <algorithm>

namespace supernova
{
void PresetMorph::setTarget (const MorphSnapshot& target, double seconds) noexcept
{
    from  = out;                              // arranca desde donde está el render AHORA (sin salto)
    to    = target;
    dur   = std::max (1.0e-4, seconds);
    phase = 0.0;
}

MorphSnapshot PresetMorph::tick (double dt) noexcept
{
    if (phase >= 1.0) return out;
    phase = std::min (1.0, phase + dt / dur);
    const float e = ease (phase);
    for (int i = 0; i < MorphSnapshot::N; ++i)
        out.v[i] = from.v[i] + (to.v[i] - from.v[i]) * e;
    return out;
}
}
