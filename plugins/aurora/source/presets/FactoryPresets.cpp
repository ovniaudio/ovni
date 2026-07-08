// FactoryPresets.cpp — tabla de presets de fábrica de AURORA (SPL·01).
// Los 6 de la curaduría 2026-06-10 (ES LEY: no agregar, no cambiar valores). Valores en
// UNIDADES del parámetro (% 0..100, tilt -100..+100, bools 1/0, choice = índice, rate en Hz);
// PresetManager::applyFactory resetea a default antes de aplicar → los params no listados
// quedan en su default de fábrica. Categorías mapeadas al enum real (briefing §7):
// Rhythmic → Production · Sound Design → SoundDesign.
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        namespace pid = aurora::params::id;
        using C = Category;

        // Índices de motionDivision (tabla aurora::params::sync::divs, 1:1 con el choice):
        //   0 = "1/4" · 1 = "1/2" · 2 = "1 bar" · 3 = "2 bar".
        static const std::vector<FactoryPreset> presets {
            // ── EL DEFAULT AFINADO — el carácter de la casa ──────────────────────────────────────
            { "First Light", C::Production, {
                { pid::SPREAD, 60.f }, { pid::TILT, 15.f },
                { pid::MONOSAFEAMT, 55.f }, { pid::MIX, 100.f } } },

            // ── PRODUCTION — usables en mezcla ───────────────────────────────────────────────────
            { "Vocal Halo", C::Production, {
                { pid::SPREAD, 45.f }, { pid::TILT, 40.f },
                { pid::MONOSAFEAMT, 65.f }, { pid::MIX, 65.f }, { pid::DUCK, 20.f } } },

            { "Solar Wind", C::Production, {
                { pid::SPREAD, 35.f }, { pid::MOTION, 18.f },
                { pid::MOTIONRATE, 0.08f }, { pid::MIX, 80.f } } },

            // Rhythmic → Production (el abanico late con el track).
            { "Fan 1/2", C::Production, {
                { pid::MOTION, 65.f }, { pid::MOTIONSYNC, 1.f }, { pid::MOTIONDIV, 1.f },
                { pid::SPREAD, 70.f } } },

            { "Drum Breather", C::Production, {
                { pid::SPREAD, 50.f }, { pid::DUCK, 55.f }, { pid::MONOSAFEAMT, 70.f } } },

            // ── SOUND DESIGN — el extremo de carácter ────────────────────────────────────────────
            { "Inverted Sky", C::SoundDesign, {
                { pid::TILT, -85.f }, { pid::SPREAD, 85.f },
                { pid::MOTION, 30.f }, { pid::MOTIONSYNC, 1.f }, { pid::MOTIONDIV, 2.f } } }
        };
        return presets;
    }
}
