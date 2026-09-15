// FactoryPresets.cpp — las "vistas" de fábrica de TELESCOPE: una lente + un objetivo de plataforma.
// La INTERFAZ (FactoryPreset/PresetParam + factoryPresets()) la define shared/presets/PresetTypes.h.
// Los valores van en unidades del parámetro (los choices, como índice — ver source/lenses/LensIds.h y
// source/data/StreamingTargets.h).
#include "presets/PresetTypes.h"

namespace ovni::presets
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets {
        // nombre                lente (índice)   objetivo (índice)
        { "Loudness · libre",     C::Production, { { "lens", 0 }, { "target", 0 } } },
        { "Loudness · Spotify",   C::Production, { { "lens", 0 }, { "target", 1 } } },
        { "Loudness · Apple",     C::Production, { { "lens", 0 }, { "target", 2 } } },
        { "Loudness · YouTube",   C::Production, { { "lens", 0 }, { "target", 3 } } },
    };
    return presets;
}
}
