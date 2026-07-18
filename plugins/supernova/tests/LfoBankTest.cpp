// [supernova][lfo] — LfoBank: moduladores sync al tempo. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "tempo/LfoBank.h"

using supernova::LfoBank;
using supernova::LfoShape;
using Catch::Approx;

TEST_CASE ("lfo: slot deshabilitado o sin destino → 0 (identidad)", "[supernova][lfo]")
{
    LfoBank b;
    REQUIRE (b.valueFor (0, 3.7) == Approx (0.0f));   // default: disabled
    b.slot (0).enabled = true;                         // pero sin target
    REQUIRE (b.valueFor (0, 3.7) == Approx (0.0f));
}

TEST_CASE ("lfo: seno bipolar recorre el ciclo al ritmo de beatsPerCycle", "[supernova][lfo]")
{
    LfoBank b;
    auto& s = b.slot (0);
    s.enabled = true; s.target = "hue"; s.shape = LfoShape::Sine; s.depth = 1.0f; s.bipolar = true;
    s.beatsPerCycle = 4.0f;   // 1 ciclo por compás
    REQUIRE (b.valueFor (0, 0.0) == Approx (0.0f).margin (1e-4));   // seno bipolar en fase 0 = 0
    REQUIRE (b.valueFor (0, 1.0) == Approx (1.0f).margin (1e-4));   // 1/4 de ciclo = pico +1
    REQUIRE (b.valueFor (0, 3.0) == Approx (-1.0f).margin (1e-4));  // 3/4 = −1
}

TEST_CASE ("lfo: unipolar queda en [0, depth]", "[supernova][lfo]")
{
    LfoBank b;
    auto& s = b.slot (1);
    s.enabled = true; s.target = "glow"; s.shape = LfoShape::Triangle; s.depth = 0.5f; s.bipolar = false;
    s.beatsPerCycle = 1.0f;
    const float v0 = b.valueFor (1, 0.0);
    const float vHalf = b.valueFor (1, 0.5);   // pico del triángulo
    REQUIRE (v0 >= 0.0f);
    REQUIRE (vHalf == Approx (0.5f).margin (1e-4));   // depth·1
    REQUIRE (v0 <= 0.5f);
}

TEST_CASE ("lfo: square alterna 1/0 por mitad de ciclo", "[supernova][lfo]")
{
    LfoBank b;
    auto& s = b.slot (2);
    s.enabled = true; s.target = "x"; s.shape = LfoShape::Square; s.depth = 1.0f; s.bipolar = false;
    s.beatsPerCycle = 1.0f;
    REQUIRE (b.valueFor (2, 0.25) == Approx (1.0f));
    REQUIRE (b.valueFor (2, 0.75) == Approx (0.0f));
}

TEST_CASE ("lfo: serialize/deserialize round-trip preserva slots", "[supernova][lfo]")
{
    LfoBank b;
    b.slot (0) = { true, 2.0f, LfoShape::Saw, 0.75f, 0.1f, false, "intensity" };
    b.slot (3) = { true, 8.0f, LfoShape::Square, 0.3f, 0.0f, true, "chaos" };
    const std::string s = b.serialize();

    LfoBank b2;
    b2.deserialize (s);
    REQUIRE (b2.slot (0).enabled);
    REQUIRE (b2.slot (0).target == "intensity");
    REQUIRE (b2.slot (0).beatsPerCycle == Approx (2.0f));
    REQUIRE ((int) b2.slot (0).shape == (int) LfoShape::Saw);
    REQUIRE (b2.slot (3).target == "chaos");
    REQUIRE (b2.slot (3).bipolar);
    REQUIRE_FALSE (b2.slot (1).enabled);
}
