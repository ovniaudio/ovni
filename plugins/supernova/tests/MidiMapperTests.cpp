// [supernova][midimap] — MidiMapper: bytes MIDI sintéticos → eventos tipados (RF5). Puro, sin GPU ni JUCE-audio.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "midi/MidiMapper.h"

using supernova::MidiMapper;
using supernova::MidiTriggerType;
namespace { constexpr float kTwoPi = 6.283185307179586f; }

TEST_CASE ("midimap: pitch del octava de rayo → ángulo (12 direcciones)", "[supernova][midimap]")
{
    MidiMapper m;
    // C1 (24) = 0 rad (+X); cada semitono suma 2π/12; velocity → strength.
    auto e0 = m.map (0x90, 24, 100);
    REQUIRE (e0.type == MidiTriggerType::DirectionalRay);
    REQUIRE (e0.angle == Catch::Approx (0.0f).margin (1e-5));
    REQUIRE (e0.strength == Catch::Approx (100.0f / 127.0f).margin (1e-4));

    REQUIRE (m.map (0x90, 27, 100).angle == Catch::Approx (3.0f * kTwoPi / 12.0f).margin (1e-5));  // 3 semitonos
    REQUIRE (m.map (0x90, 30, 100).angle == Catch::Approx (6.0f * kTwoPi / 12.0f).margin (1e-5));  // 6 (π)
    REQUIRE (m.map (0x90, 35, 100).angle == Catch::Approx (11.0f * kTwoPi / 12.0f).margin (1e-5)); // 11

    // monotonía + espaciado uniforme
    for (int note = 24; note < 35; ++note)
    {
        const float d = m.map (0x90, (uint8_t) (note + 1), 100).angle - m.map (0x90, (uint8_t) note, 100).angle;
        REQUIRE (d == Catch::Approx (kTwoPi / 12.0f).margin (1e-5));
    }
}

TEST_CASE ("midimap: cualquier tecla del octava de explosión dispara; velocity → strength", "[supernova][midimap]")
{
    MidiMapper m;
    auto full = m.map (0x90, 36, 127);
    REQUIRE (full.type == MidiTriggerType::Explosion);
    REQUIRE (full.strength == Catch::Approx (1.0f).margin (1e-4));

    auto half = m.map (0x90, 42, 64);   // pitch distinto dentro del octava → mismo tipo
    REQUIRE (half.type == MidiTriggerType::Explosion);
    REQUIRE (half.strength == Catch::Approx (64.0f / 127.0f).margin (1e-4));
}

TEST_CASE ("midimap: nota → preset con clamp al presetCount real", "[supernova][midimap]")
{
    MidiMapper m (MidiMapper::Config { 24, 35, 36, 47, 48, 71, 4 });
    auto e0 = m.map (0x90, 48, 100);
    REQUIRE (e0.type == MidiTriggerType::PresetChange);
    REQUIRE (e0.preset == 0);
    REQUIRE_FALSE (e0.fromProgramChange);

    REQUIRE (m.map (0x90, 51, 100).preset == 3);   // note 51-48 = 3
    REQUIRE (m.map (0x90, 55, 100).preset == 3);   // 7 → clamp a 3
    // Arriba de prsHi ya no hay preset. (La 72 dejó de ser "nada": desde la ronda 3 es el cue del tile 1;
    // el silencio de verdad arranca después del mapa de cue, en la 91.)
    REQUIRE (m.map (0x90, 72, 100).type != MidiTriggerType::PresetChange);
    REQUIRE (m.map (0x90, 100, 100).type == MidiTriggerType::None);
}

TEST_CASE ("midimap: Program-Change → PresetChange{fromProgramChange} + clamp; canal ignorado", "[supernova][midimap]")
{
    MidiMapper m (MidiMapper::Config { 24, 35, 36, 47, 48, 71, 4 });
    auto e = m.map (0xC0, 2, 0);
    REQUIRE (e.type == MidiTriggerType::PresetChange);
    REQUIRE (e.preset == 2);
    REQUIRE (e.fromProgramChange);

    REQUIRE (m.map (0xC5, 100, 0).preset == 3);   // clamp desde 100; canal 5 ignorado
}

TEST_CASE ("midimap: release (NoteOff / vel 0) NO dispara", "[supernova][midimap]")
{
    MidiMapper m;
    REQUIRE (m.map (0x80, 36, 64).type == MidiTriggerType::None);   // NoteOff
    REQUIRE (m.map (0x90, 36, 0).type  == MidiTriggerType::None);   // NoteOn vel 0
    REQUIRE (m.mapNoteOn (36, 0).type  == MidiTriggerType::None);
}

TEST_CASE ("midimap: fuera de rango y canal-agnóstico", "[supernova][midimap]")
{
    MidiMapper m;
    REQUIRE (m.map (0x90, 20, 100).type == MidiTriggerType::None);   // bajo 24
    REQUIRE (m.map (0x90, 127, 100).type == MidiTriggerType::None);  // sobre 71

    // canal 16 (0x9F) idéntico a canal 1 (0x90)
    auto a = m.map (0x90, 24, 100);
    auto b = m.map (0x9F, 24, 100);
    REQUIRE (b.type == MidiTriggerType::DirectionalRay);
    REQUIRE (b.angle == Catch::Approx (a.angle).margin (1e-6));

    REQUIRE (m.map (0xB0, 7, 64).type == MidiTriggerType::None);     // CC volume → None
}

// ======================= RONDA 3 · CUE DE FOTOS POR MIDI (el instrumento en vivo) =======================
// En inmersivo o fullscreen la tira no se ve y sólo quedan ← → y Space: un VJ dispara las fotos desde un
// pad. Contrato por NÚMERO de nota, igual que el resto del mapper: 72-87 = tiles 1..16, 88 = siguiente,
// 89 = anterior, 90 = aleatoria. No pisa rayo (24-35), explosión (36-47) ni preset (48-71).
TEST_CASE ("midimap: 72-87 cuean el tile 1..16 (contrato por número de nota)", "[supernova][midimap]")
{
    MidiMapper m;
    auto first = m.map (0x90, 72, 100);
    REQUIRE (first.type == MidiTriggerType::PhotoCue);
    REQUIRE (first.cue  == supernova::PhotoCueKind::Tile);
    REQUIRE (first.photo == 0);                                        // tile 1 = índice 0
    REQUIRE (first.strength == Catch::Approx (100.0f / 127.0f).margin (1e-4));

    REQUIRE (m.map (0x90, 73, 100).photo == 1);
    REQUIRE (m.map (0x90, 87, 100).photo == 15);                       // tile 16 = el último del rango
    REQUIRE (m.map (0x90, 87, 100).type  == MidiTriggerType::PhotoCue);
}

TEST_CASE ("midimap: 88 / 89 / 90 = siguiente / anterior / aleatoria", "[supernova][midimap]")
{
    MidiMapper m;
    auto nx = m.map (0x90, 88, 127);
    REQUIRE (nx.type == MidiTriggerType::PhotoCue);
    REQUIRE (nx.cue  == supernova::PhotoCueKind::Next);
    REQUIRE (nx.photo == -1);                                          // los relativos no traen índice

    REQUIRE (m.map (0x90, 89, 127).cue == supernova::PhotoCueKind::Prev);
    REQUIRE (m.map (0x90, 90, 127).cue == supernova::PhotoCueKind::Random);
    REQUIRE (m.map (0x90, 91, 127).type == MidiTriggerType::None);     // fuera del mapa: nada
}

TEST_CASE ("midimap: el cue por MIDI no pisa rayo/explosión/preset ni dispara en el release", "[supernova][midimap]")
{
    MidiMapper m;
    REQUIRE (m.map (0x90, 24, 100).type == MidiTriggerType::DirectionalRay);   // 24-35 intactos
    REQUIRE (m.map (0x90, 35, 100).type == MidiTriggerType::DirectionalRay);
    REQUIRE (m.map (0x90, 36, 100).type == MidiTriggerType::Explosion);        // 36-47 intactos
    REQUIRE (m.map (0x90, 47, 100).type == MidiTriggerType::Explosion);
    REQUIRE (m.map (0x90, 48, 100).type == MidiTriggerType::PresetChange);     // 48-71 intactos
    REQUIRE (m.map (0x90, 71, 100).type == MidiTriggerType::PresetChange);

    REQUIRE (m.map (0x90, 72, 0).type  == MidiTriggerType::None);              // NoteOn vel 0 = release
    REQUIRE (m.map (0x80, 72, 64).type == MidiTriggerType::None);              // NoteOff
    REQUIRE (m.mapNoteOn (88, 0).type  == MidiTriggerType::None);
}

TEST_CASE ("midimap: los rangos del cue son inyectables (Config)", "[supernova][midimap]")
{
    MidiMapper::Config c;
    c.cueLo = 96; c.cueHi = 99; c.cueNext = 100; c.cuePrev = 101; c.cueRandom = 102;
    MidiMapper m (c);
    REQUIRE (m.map (0x90, 96, 100).photo == 0);
    REQUIRE (m.map (0x90, 99, 100).photo == 3);
    REQUIRE (m.map (0x90, 100, 100).cue == supernova::PhotoCueKind::Next);
    REQUIRE (m.map (0x90, 72,  100).type == MidiTriggerType::None);   // el rango default ya no vale
}
