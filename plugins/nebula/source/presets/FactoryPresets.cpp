// FactoryPresets.cpp — tabla de presets de fábrica de NÉBULA: los MODOS con nombre que enseñan el rango
// del reverb FDN (size/decay/tone/breath/mix). La INTERFAZ (FactoryPreset/PresetParam/Category +
// factoryPresets()) la define shared/presets/PresetTypes.h. Valores en unidades del parámetro (% 0..100);
// el PresetManager los aplica con convertTo0to1 sobre el APVTS.
//
// Familias: Production = espacios "usables" de mezcla (de sala chica a catedral); SoundDesign = los
// extremos de carácter del motor (etéreo, oscuro, móvil, congelado). Cada preset elige números sensatos
// según su descripción para que, recorriéndolos, el oído aprenda qué hace cada macro.
//
// HONESTIDAD: NO hay preset "Inverso" (reverse-reverb). El motor FDN v1 no implementa la cola que crece
// desde el vacío; ponerlo sería un nombre que no hace lo que promete. Queda como feature futura.
//
// FREEZE: el processor congela la cola cuando DECAY ≥ 99% (kFreezeThreshold). "Infinito" usa DECAY=100
// → drone sostenido que igual respira.
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        namespace pid = nebula::params::id;
        using C = Category;

        static const std::vector<FactoryPreset> presets {
            // ── PRODUCTION — espacios de mezcla, de chico a enorme ────────────────────────────────

            // SALA CORTA — espacio chico y controlado: size bajo, decay corto, tono medio (algo de aire),
            // respiración apenas perceptible, wet discreto. El "ambiente" que no se nota pero está.
            { "Short Room", C::Production, {
                { pid::SIZE, 28.f }, { pid::DECAY, 22.f }, { pid::TONE, 45.f },
                { pid::BREATH, 10.f }, { pid::MIX, 22.f } } },

            // PLACA — brillante y densa (emula la placa metálica): size medio, decay medio, tono BAJO
            // (poco damping → agudos vivos), SIN respiración (la placa no "respira"), wet medio.
            { "Plate", C::Production, {
                { pid::SIZE, 50.f }, { pid::DECAY, 48.f }, { pid::TONE, 18.f },
                { pid::BREATH, 0.f }, { pid::MIX, 38.f } } },

            // CATEDRAL — grande y largo: size alto, decay alto (cola que se queda), tono medio, una
            // respiración leve que le da vida al recinto, wet generoso.
            { "Cathedral", C::Production, {
                { pid::SIZE, 88.f }, { pid::DECAY, 82.f }, { pid::TONE, 42.f },
                { pid::BREATH, 18.f }, { pid::MIX, 46.f } } },

            // ── SOUND DESIGN — los extremos de carácter del motor ─────────────────────────────────

            // VACÍO — mínimo etéreo: espacio chico-medio, decay corto, breath leve, tono claro y wet
            // discreto. Casi un aliento de reverb: sugiere el espacio sin ocuparlo.
            { "Void", C::SoundDesign, {
                { pid::SIZE, 35.f }, { pid::DECAY, 18.f }, { pid::TONE, 30.f },
                { pid::BREATH, 22.f }, { pid::MIX, 30.f } } },

            // AGUJERO — oscuro y denso: tono ALTO (mucho damping HF → cola apagada), decay largo, size
            // alto, sin respiración. La cola se hunde como en un pozo sin agudos.
            { "Black Hole", C::SoundDesign, {
                { pid::SIZE, 80.f }, { pid::DECAY, 78.f }, { pid::TONE, 92.f },
                { pid::BREATH, 8.f }, { pid::MIX, 50.f } } },

            // ENJAMBRE — mucho movimiento: BREATH alto (el espacio inhala/exhala marcado), decay
            // medio-alto, size medio-alto, tono medio. La cola nunca está quieta.
            { "Swarm", C::SoundDesign, {
                { pid::SIZE, 65.f }, { pid::DECAY, 62.f }, { pid::TONE, 50.f },
                { pid::BREATH, 85.f }, { pid::MIX, 48.f } } },

            // INFINITO — freeze: DECAY=100 (≥ kFreezeThreshold) → el processor congela la cola: drone
            // sostenido que igual respira. Wet alto para vivir dentro del espacio congelado.
            { "Infinite", C::SoundDesign, {
                { pid::SIZE, 72.f }, { pid::DECAY, 100.f }, { pid::TONE, 45.f },
                { pid::BREATH, 60.f }, { pid::MIX, 70.f } } },
        };
        return presets;
    }
}
