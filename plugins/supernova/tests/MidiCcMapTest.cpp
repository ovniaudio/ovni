// [supernova][cc] — MidiCcMap: MIDI-learn + CC → param (asignar/resolver/persistir). Puro, sin GPU ni JUCE.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "midi/MidiCcMap.h"

using supernova::MidiCcMap;
using Catch::Approx;

TEST_CASE ("cc: learn bindea el próximo CC al param armado y devuelve el valor", "[supernova][cc]")
{
    MidiCcMap m;
    m.armLearn ("intensity", 0.0f, 100.0f);
    REQUIRE (m.isLearning());
    auto hit = m.feed (74, 127);                 // CC74 a fondo
    REQUIRE (hit.has_value());
    REQUIRE (hit->paramId == "intensity");
    REQUIRE (hit->value01 == Approx (100.0f));   // mapea al rango [0,100]
    REQUIRE_FALSE (m.isLearning());              // learn se consume con el primer CC
    REQUIRE (m.ccForParam ("intensity") == 74);
}

TEST_CASE ("cc: un CC ya asignado resuelve sin learn", "[supernova][cc]")
{
    MidiCcMap m;
    m.assign (21, "chaos", 0.0f, 1.0f);
    auto hit = m.feed (21, 64);
    REQUIRE (hit.has_value());
    REQUIRE (hit->paramId == "chaos");
    REQUIRE (hit->value01 == Approx (64.0f / 127.0f));
    REQUIRE_FALSE (m.feed (99, 10).has_value());   // CC sin asignar → nada
}

TEST_CASE ("cc: reasignar limpia colisiones (un CC ↔ un param)", "[supernova][cc]")
{
    MidiCcMap m;
    m.assign (10, "glow", 0, 1);
    m.assign (10, "size", 0, 1);          // el CC10 se muda a size
    REQUIRE (m.paramForCc (10) == "size");
    REQUIRE (m.ccForParam ("glow") == -1);
    m.assign (11, "size", 0, 1);          // size se muda al CC11
    REQUIRE (m.ccForParam ("size") == 11);
    REQUIRE (m.paramForCc (10).empty());
    REQUIRE (m.size() == 1);
}

TEST_CASE ("cc: serialize/deserialize round-trip", "[supernova][cc]")
{
    MidiCcMap m;
    m.assign (74, "intensity", 0.0f, 100.0f);
    m.assign (71, "chaos", 0.0f, 1.0f);
    const std::string s = m.serialize();

    MidiCcMap m2;
    m2.deserialize (s);
    REQUIRE (m2.size() == 2);
    REQUIRE (m2.ccForParam ("intensity") == 74);
    REQUIRE (m2.ccForParam ("chaos") == 71);
    auto hit = m2.feed (74, 127);
    REQUIRE (hit->paramId == "intensity");
    REQUIRE (hit->value01 == Approx (100.0f));
}

TEST_CASE ("cc: deserialize tolera basura sin crashear", "[supernova][cc]")
{
    MidiCcMap m;
    m.deserialize ("garbage;;74,intensity,0,100;x,y,z;");
    REQUIRE (m.ccForParam ("intensity") == 74);   // el registro válido entra, la basura se omite
}
