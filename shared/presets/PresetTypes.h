#pragma once
#include <vector>

// Tipos base de presets del sello OVNI. La librería define SÓLO el tipo; **cada plugin** provee su
// propia tabla implementando `factoryPresets()` (ver `shared/template/_README.md`). Valores en unidades
// del parámetro (los choices como índice). El PresetManager los aplica vía `convertTo0to1` sobre el APVTS,
// así un mismo número sirve para floats, choices y bools.
namespace ovni::presets
{
enum class Category { Production, SoundDesign };

struct PresetParam  { const char* id; float value; };
struct FactoryPreset { const char* name; Category category; std::vector<PresetParam> params; };

// PLUGIN: define esta función con tu propia tabla de presets de fábrica. La base sólo la declara.
const std::vector<FactoryPreset>& factoryPresets();
}
