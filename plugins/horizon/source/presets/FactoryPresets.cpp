// FactoryPresets.cpp — tabla de presets de fábrica de HORIZON (SPL·02).
// Los 6 de la curaduría 2026-06-10 (ES LEY: no agregar, no cambiar valores). Valores en
// UNIDADES del parámetro (% 0..100, bools 1/0, choice = índice, rate en Hz);
// PresetManager::applyFactory resetea a default antes de aplicar → los params no listados
// quedan en su default de fábrica. Categorías mapeadas al enum real (briefing §7):
// Rhythmic → Production · Ambient/Texture/Transition → SoundDesign.
//
// Índices de rateDivision (tabla horizon::params::sync::divs, 1:1 con el choice):
//   0="1/16" · 1="1/8" · 2="1/4" · 3="1/2" · 4="1 bar" · 5="2 bar".
// ⚠ Los presets con Freeze ON arrancan ya congelados al cargar (el gesto es automatable).
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        namespace pid = horizon::params::id;
        using C = Category;

        static const std::vector<FactoryPreset> presets {
            // ── PRODUCTION — usables en mezcla ───────────────────────────────────────────────────
            // 1. Event Horizon — pad eterno bajo el track. Rate off (sostenido), congelado.
            { "Event Horizon", C::Production, {
                { pid::FREEZE, 1.f }, { pid::WHISPER, 10.f }, { pid::SPREAD, 55.f }, { pid::MIX, 60.f } } },

            // 2. Pulse 1/8 (Rhythmic → Production) — gate al octavo, esparcido.
            { "Pulse 1/8", C::Production, {
                { pid::FREEZE, 1.f }, { pid::RATESYNC, 1.f }, { pid::RATEDIV, 1.f },   // 1/8
                { pid::SPREAD, 45.f }, { pid::MIX, 100.f }, { pid::DUCK, 15.f } } },

            // 3. Trance Gate (Rhythmic → Production) — stutter a 1/16, vidrioso (whisper 0).
            { "Trance Gate", C::Production, {
                { pid::FREEZE, 1.f }, { pid::RATESYNC, 1.f }, { pid::RATEDIV, 0.f },   // 1/16
                { pid::WHISPER, 0.f }, { pid::SPREAD, 30.f }, { pid::MIX, 100.f } } },

            // ── SOUND DESIGN — los extremos de carácter ──────────────────────────────────────────
            // 4. Ghost Choir — nube aireada (whisper alto), ancha, respirando.
            { "Ghost Choir", C::SoundDesign, {
                { pid::FREEZE, 1.f }, { pid::WHISPER, 85.f }, { pid::SPREAD, 80.f },
                { pid::MIX, 50.f }, { pid::DUCK, 45.f } } },

            // 5. Riser Gate (Transition → SoundDesign) — gate a 1/4, whisper medio, ancho pleno.
            { "Riser Gate", C::SoundDesign, {
                { pid::FREEZE, 1.f }, { pid::RATESYNC, 1.f }, { pid::RATEDIV, 2.f },   // 1/4
                { pid::WHISPER, 60.f }, { pid::SPREAD, 100.f }, { pid::MIX, 85.f } } },

            // 6. Slow Tide (Ambient → SoundDesign) — latido lento en FREE (~0.5 Hz), ancho, respira.
            { "Slow Tide", C::SoundDesign, {
                { pid::FREEZE, 1.f }, { pid::RATE, 0.5f },   // FREE ~0.5 Hz
                { pid::WHISPER, 30.f }, { pid::SPREAD, 70.f }, { pid::MIX, 70.f }, { pid::DUCK, 30.f } } }
        };
        return presets;
    }
}
