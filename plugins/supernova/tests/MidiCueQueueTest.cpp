// [supernova][cuequeue] — MidiCueQueue SPSC: el transporte del CUE DE FOTOS por MIDI (ronda 3). Productor:
// el audio thread (processAudio). Consumidor: el timer del editor (message thread), que es el único que
// puede tocar la PhotoSequence. Mismo patrón que MidiCcQueue: lock-free, drop-on-full, el audio no bloquea.
#include <catch2/catch_test_macros.hpp>
#include "analysis/MidiCueQueue.h"

using supernova::MidiCueMsg;
using supernova::MidiCueQueue;
using supernova::PhotoCueKind;

TEST_CASE ("cuequeue: push/pop SPSC preserva orden y contenido", "[supernova][cuequeue]")
{
    MidiCueQueue q (8);
    MidiCueMsg m;
    REQUIRE_FALSE (q.pop (m));

    q.push ({ PhotoCueKind::Tile, 3, 100 });
    q.push ({ PhotoCueKind::Next, -1, 64 });

    REQUIRE (q.pop (m));
    REQUIRE (m.kind == PhotoCueKind::Tile);
    REQUIRE (m.index == 3);
    REQUIRE (m.velocity == 100);

    REQUIRE (q.pop (m));
    REQUIRE (m.kind == PhotoCueKind::Next);
    REQUIRE (m.index == -1);

    REQUIRE_FALSE (q.pop (m));
}

TEST_CASE ("cuequeue: drop-on-full nunca bloquea (RNF1)", "[supernova][cuequeue]")
{
    MidiCueQueue q (2);
    for (int i = 0; i < 100; ++i) q.push ({ PhotoCueKind::Tile, i, 127 });   // más de lo que entra → descarta

    MidiCueMsg m;
    int popped = 0;
    while (q.pop (m)) ++popped;
    REQUIRE (popped <= 2);   // la capacidad acota; nunca desborda
}

TEST_CASE ("cuequeue: reset la deja vacía", "[supernova][cuequeue]")
{
    MidiCueQueue q (4);
    q.push ({ PhotoCueKind::Random, -1, 90 });
    q.reset();
    MidiCueMsg m;
    REQUIRE_FALSE (q.pop (m));
}
