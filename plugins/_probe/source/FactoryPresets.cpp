// FactoryPresets.cpp — tabla de presets de fábrica del _probe (2 modos que ejercitan el único knob).
// La INTERFAZ (FactoryPreset/PresetParam + factoryPresets()) la declara shared/presets/PresetTypes.h (S3);
// cada plugin define SU tabla. Valores en unidades del parámetro: "mix" es float 0..1, "bypass" es bool
// (0 = activo). El PresetManager los aplica con convertTo0to1, así un mismo número sirve para float y bool.
#include "presets/PresetTypes.h"

namespace ovni::presets
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets = {
        { "Wide Sweep", C::Production,  { { "mix", 1.0f }, { "bypass", 0.0f } } },
        { "Centered",   C::SoundDesign, { { "mix", 0.0f }, { "bypass", 0.0f } } },
    };
    return presets;
}
}
