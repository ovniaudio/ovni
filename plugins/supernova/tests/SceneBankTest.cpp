// [supernova][scene] — SceneBank: guardar/recuperar escenas + persistencia. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "presets/SceneBank.h"

using supernova::SceneBank;
using supernova::MorphSnapshot;
using Catch::Approx;

TEST_CASE ("scene: save/recall preserva el snapshot", "[supernova][scene]")
{
    SceneBank b;
    REQUIRE_FALSE (b.has (0));
    MorphSnapshot s; s.v[0] = 77.0f; s.v[5] = 12.0f;
    b.save (2, s);
    REQUIRE (b.has (2));
    REQUIRE (b.count() == 1);
    REQUIRE (b.recall (2).v[0] == Approx (77.0f));
    REQUIRE (b.recall (2).v[5] == Approx (12.0f));
}

TEST_CASE ("scene: clear libera el slot; recall de slot vacío da defaults", "[supernova][scene]")
{
    SceneBank b;
    MorphSnapshot s; s.v[0] = 5.0f;
    b.save (1, s);
    b.clear (1);
    REQUIRE_FALSE (b.has (1));
    // Un slot nunca escrito devuelve los defaults de MorphSnapshot (v[0]=50).
    REQUIRE (b.recall (3).v[0] == Approx (50.0f));
}

TEST_CASE ("scene: índices fuera de rango son inocuos", "[supernova][scene]")
{
    SceneBank b;
    MorphSnapshot s;
    b.save (-1, s); b.save (999, s);
    REQUIRE (b.count() == 0);
    REQUIRE_FALSE (b.has (-1));
    REQUIRE_FALSE (b.has (999));
}

TEST_CASE ("scene: serialize/deserialize round-trip de varios slots", "[supernova][scene]")
{
    SceneBank b;
    MorphSnapshot a; a.v[0] = 11.0f; a.v[29] = 99.0f;
    MorphSnapshot c; c.v[3] = 42.0f;
    b.save (0, a);
    b.save (7, c);
    const std::string ser = b.serialize();

    SceneBank b2;
    b2.deserialize (ser);
    REQUIRE (b2.count() == 2);
    REQUIRE (b2.has (0));
    REQUIRE (b2.has (7));
    REQUIRE (b2.recall (0).v[0] == Approx (11.0f));
    REQUIRE (b2.recall (0).v[29] == Approx (99.0f));
    REQUIRE (b2.recall (7).v[3] == Approx (42.0f));
    REQUIRE_FALSE (b2.has (1));
}
