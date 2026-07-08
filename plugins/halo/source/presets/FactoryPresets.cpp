// FactoryPresets.cpp — tabla de presets de fábrica de HALO: los MODOS con nombre que enseñan el rango del
// shimmer espacial glacial (mix/size/decay/shimmer/tone/orbit). La INTERFAZ (FactoryPreset/PresetParam/
// Category + factoryPresets()) la define shared/presets/PresetTypes.h. Valores en unidades del parámetro
// (% 0..100); el PresetManager los aplica con convertTo0to1 sobre el APVTS.
//
// "VASTEDAD" es el default de fábrica (base técnica §9): el carácter glacial envolvente — bloom ≥ 2 s,
// octava+quinta, órbita binaural. El resto enseña el rango: del shimmer sutil a la nube congelada.
//
// HONESTIDAD (base técnica §1): NO hay presets de "escala"/"armonía" — las voces son FIJAS (octava+quinta).
// FREEZE es un toggle aparte (no un preset), pero "Nube Capturada" lo deja activo para mostrar el modo.
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        namespace pid = halo::params::id;
        using C = Category;

        static const std::vector<FactoryPreset> presets {
            // ── EL DEFAULT — el carácter de la casa ───────────────────────────────────────────────

            // VASTEDAD — el shimmer glacial envolvente (base técnica §9). Wet medio, espacio grande, cola
            // larga, shimmer presente, TONE medio (≈7 kHz), órbita media. El halo florece, sube y orbita.
            { "Vastness", C::SoundDesign, {
                { pid::MIX, 40.f }, { pid::SIZE, 65.f }, { pid::DECAY, 75.f },
                { pid::SHIMMER, 55.f }, { pid::TONE, 45.f }, { pid::ORBIT, 40.f } } },

            // ── PRODUCTION — usables en mezcla ────────────────────────────────────────────────────

            // AURA — shimmer sutil sobre una voz/pad: wet discreto, espacio medio, cola media, shimmer
            // moderado, TONE oscuro (cola apagada, no se pelea con la fuente), órbita leve. El brillo que
            // "está" sin tapar.
            { "Aura", C::Production, {
                { pid::MIX, 28.f }, { pid::SIZE, 50.f }, { pid::DECAY, 55.f },
                { pid::SHIMMER, 40.f }, { pid::TONE, 62.f }, { pid::ORBIT, 25.f } } },

            // CATEDRAL DE LUZ — grande y brillante: wet generoso, espacio enorme, cola larga, shimmer alto,
            // TONE claro (agudos vivos, el "coro celestial"), órbita media. El épico de pads/coros.
            { "Cathedral of Light", C::Production, {
                { pid::MIX, 45.f }, { pid::SIZE, 85.f }, { pid::DECAY, 82.f },
                { pid::SHIMMER, 65.f }, { pid::TONE, 30.f }, { pid::ORBIT, 45.f } } },

            // ── SOUND DESIGN — los extremos de carácter ───────────────────────────────────────────

            // ÓRBITA LEJANA — el halo gira marcado alrededor de la cabeza: órbita ALTA (envolvimiento
            // pleno), espacio grande, cola larga, TONE medio. Para auriculares: la cola te rodea.
            { "Distant Orbit", C::SoundDesign, {
                { pid::MIX, 42.f }, { pid::SIZE, 72.f }, { pid::DECAY, 80.f },
                { pid::SHIMMER, 55.f }, { pid::TONE, 48.f }, { pid::ORBIT, 90.f } } },

            // GLACIAR — oscuro y lentísimo: TONE alto (mucho damping → cola muy apagada), cola larga, size
            // alto, shimmer medio, órbita lenta/baja. La nube gris que avanza sin prisa.
            { "Glacier", C::SoundDesign, {
                { pid::MIX, 44.f }, { pid::SIZE, 80.f }, { pid::DECAY, 85.f },
                { pid::SHIMMER, 48.f }, { pid::TONE, 80.f }, { pid::ORBIT, 30.f } } },

            // NUBE CAPTURADA — FREEZE activo: la nube queda congelada para tocar encima. Wet pleno, órbita
            // media (sigue girando), shimmer alto. "Capturá la nube y tocá sobre ella."
            { "Captured Cloud", C::SoundDesign, {
                { pid::MIX, 50.f }, { pid::SIZE, 70.f }, { pid::DECAY, 100.f },
                { pid::SHIMMER, 60.f }, { pid::TONE, 45.f }, { pid::ORBIT, 50.f },
                { pid::FREEZE, 1.f } } }
        };
        return presets;
    }
}
