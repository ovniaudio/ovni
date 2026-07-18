// [supernova][ccqueue] — MidiCcQueue SPSC + integración con MidiCcMap: el camino real CC crudo → param.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "analysis/MidiCcQueue.h"
#include "midi/MidiCcMap.h"

using Catch::Approx;

TEST_CASE ("ccqueue: push/pop SPSC preserva orden", "[supernova][ccqueue]")
{
    supernova::MidiCcQueue q (8);
    supernova::MidiCcMsg m;
    REQUIRE_FALSE (q.pop (m));
    q.push ({ 74, 100 });
    q.push ({ 71, 50 });
    REQUIRE (q.pop (m)); REQUIRE (m.cc == 74); REQUIRE (m.value == 100);
    REQUIRE (q.pop (m)); REQUIRE (m.cc == 71); REQUIRE (m.value == 50);
    REQUIRE_FALSE (q.pop (m));
}

TEST_CASE ("ccqueue: drop-on-full nunca bloquea (RNF1)", "[supernova][ccqueue]")
{
    supernova::MidiCcQueue q (2);
    for (int i = 0; i < 100; ++i) q.push ({ i & 127, 64 });   // más de lo que entra → descarta, no crashea
    supernova::MidiCcMsg m; int popped = 0;
    while (q.pop (m)) ++popped;
    REQUIRE (popped <= 2);   // la capacidad acota; nunca desborda
}

TEST_CASE ("ccqueue: camino completo CC crudo → map → hit de param (el drain del editor)", "[supernova][ccqueue]")
{
    supernova::MidiCcQueue q;
    supernova::MidiCcMap  map;
    map.assign (74, "intensity", 0.0f, 1.0f);   // ya mapeado (o venía de un learn)
    q.push ({ 74, 127 });

    supernova::MidiCcMsg m;
    REQUIRE (q.pop (m));
    auto hit = map.feed (m.cc, m.value);
    REQUIRE (hit.has_value());
    REQUIRE (hit->paramId == "intensity");
    REQUIRE (hit->value01 == Approx (1.0f));   // CC 127 → param al máximo normalizado
}
