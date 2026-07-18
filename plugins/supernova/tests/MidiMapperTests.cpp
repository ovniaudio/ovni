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
    REQUIRE (m.map (0x90, 72, 100).type == MidiTriggerType::None);   // fuera de la zona de preset
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
