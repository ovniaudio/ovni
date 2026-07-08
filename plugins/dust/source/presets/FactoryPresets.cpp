// FactoryPresets.cpp — tabla de presets de fábrica de DUST (MOV·03), transcrita 1:1 de la
// curaduría ICP (docs/superpowers/2026-06-10-paso0-curaduria-dust-aurora-horizon.md). Valores en
// UNIDADES del parámetro (% 0..100, RATE en ms, bools 1/0, choices como índice de la tabla
// params::sync::divs). Categorías informales de la curaduría mapeadas al enum del sello:
// Rhythmic→Production · Ambient/Texture/Sound Design→SoundDesign. Lo no listado queda en su
// default (applyFactory hace reset-a-default antes de aplicar).
#include "presets/PresetTypes.h"
#include "params/ParameterIDs.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        namespace pid = dust::params::id;
        using C = Category;

        // Índices de división (tabla dust::params::sync::divs: 1/16=0 · 1/8=1 · 1/4=2 · 1/2=3).
        constexpr float kDiv18 = 1.f;
        constexpr float kDiv14 = 2.f;

        static const std::vector<FactoryPreset> presets {
            // ── THE DEFAULT — the house character ─────────────────────────────────────────────────
            // A burst of bubbles around the head that decays clean. Instant wow.
            { "Bubbles", C::Production, {
                { pid::MIX, 35.f }, { pid::RATE, 250.f }, { pid::DENSITY, 40.f },
                { pid::SPREAD, 60.f }, { pid::VIDA, 25.f }, { pid::DUCK, 0.f },
                { pid::ORIGINX, 0.f }, { pid::ORIGINY, 0.35f } } },

            // ── PRODUCTION — mix-ready ────────────────────────────────────────────────────────────
            // Tempo-locked mix echo (Rhythmic -> Production): pulses with the track.
            { "Cosmic Ping", C::Production, {
                { pid::MIX, 28.f }, { pid::RATESYNC, 1.f }, { pid::RATEDIV, kDiv18 },
                { pid::DENSITY, 30.f }, { pid::SPREAD, 85.f }, { pid::VIDA, 10.f },
                { pid::DUCK, 20.f } } },

            // The echo steps aside when you sing (SYNC 1/4 + high DUCK): breathes with the performance.
            { "Breathe With Me", C::Production, {
                { pid::MIX, 32.f }, { pid::RATESYNC, 1.f }, { pid::RATEDIV, kDiv14 },
                { pid::DENSITY, 50.f }, { pid::DUCK, 65.f } } },

            // ── SOUND DESIGN — the character extremes ─────────────────────────────────────────────
            // The cloud that grows: density and life near max, the whole field.
            { "Dust Storm", C::SoundDesign, {
                { pid::MIX, 55.f }, { pid::DENSITY, 88.f }, { pid::SPREAD, 100.f },
                { pid::VIDA, 85.f } } },

            // Slow drifting echoes (Ambient): satellites orbiting the head.
            { "Satellites", C::SoundDesign, {
                { pid::MIX, 35.f }, { pid::RATE, 700.f }, { pid::DENSITY, 45.f },
                { pid::SPREAD, 80.f }, { pid::VIDA, 65.f } } },

            // Micro-echoes that border on texture (Texture): short RATE, the cloud turns fine-grained.
            { "Fine Dust", C::SoundDesign, {
                { pid::MIX, 22.f }, { pid::RATE, 45.f }, { pid::DENSITY, 60.f },
                { pid::SPREAD, 70.f }, { pid::VIDA, 35.f } } }
        };
        return presets;
    }
}
