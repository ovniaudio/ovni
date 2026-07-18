// [supernova][tempo] — BeatClock: tempo sync (host/tap/auto-BPM) + fase de beat + cuantizador. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "tempo/BeatClock.h"

using supernova::BeatClock;
using Catch::Approx;

namespace { BeatClock::HostInfo noHost() { return {}; } }

TEST_CASE ("tempo: free-run a 120 BPM avanza 2 beats por segundo", "[supernova][tempo]")
{
    BeatClock c; c.reset();
    REQUIRE (c.bpm() == Approx (120.0));
    // 1 segundo en pasos de 10 ms → 2 beats.
    for (int i = 0; i < 100; ++i) c.advance (0.01, noHost());
    REQUIRE (c.phaseInBeats() == Approx (2.0).margin (0.01));
    REQUIRE (c.beatPhase()    == Approx (0.0).margin (0.01));
}

TEST_CASE ("tempo: el host manda tempo y ENGANCHA la fase a su ppqPosition", "[supernova][tempo]")
{
    BeatClock c; c.reset();
    BeatClock::HostInfo h; h.valid = true; h.isPlaying = true; h.bpm = 140.0; h.ppqPosition = 3.5;
    c.advance (0.01, h);
    REQUIRE (c.bpm() == Approx (140.0));
    REQUIRE (c.phaseInBeats() == Approx (3.5));         // fase = ppq del host, sin deriva
    REQUIRE (c.beatPhase()    == Approx (0.5));
    h.ppqPosition = 8.0; c.advance (0.01, h);
    REQUIRE (c.phaseInBeats() == Approx (8.0));         // salto de loop → sigue al host
}

TEST_CASE ("tempo: tap tempo deriva el BPM del intervalo", "[supernova][tempo]")
{
    BeatClock c; c.reset();
    // 4 taps a 0.5 s → 120 BPM.
    c.tap (0.0); c.tap (0.5); c.tap (1.0); c.tap (1.5);
    REQUIRE (c.bpm() == Approx (120.0).margin (1.0));
    // taps más rápidos (0.4 s → 150 BPM) mueven el promedio hacia arriba.
    c.tap (1.9); c.tap (2.3);
    REQUIRE (c.bpm() > 120.0);
}

TEST_CASE ("tempo: auto-BPM estima de onsets regulares y pliega al rango musical", "[supernova][tempo]")
{
    BeatClock c; c.reset();
    // Onsets cada 0.25 s = 240 pulsos/min; se pliega a 120 (rango 70..170).
    double t = 0.0;
    for (int i = 0; i < 16; ++i) { c.onOnset (t); t += 0.25; }
    REQUIRE (c.bpm() == Approx (120.0).margin (2.0));
}

TEST_CASE ("tempo: host gana sobre tap y auto", "[supernova][tempo]")
{
    BeatClock c; c.reset();
    c.tap (0.0); c.tap (0.5);                     // tap → ~120
    BeatClock::HostInfo h; h.valid = true; h.isPlaying = true; h.bpm = 90.0; h.ppqPosition = 0.0;
    c.advance (0.01, h);
    REQUIRE (c.bpm() == Approx (90.0));            // el host pisa al tap
}

TEST_CASE ("tempo: cruces de beat y bar + cuantizador", "[supernova][tempo]")
{
    BeatClock c; c.reset();                        // 120 BPM: 0.5 s/beat, 2.0 s/bar
    // A 0.5 s justo cruza el beat 1.
    bool sawBeat = false;
    double acc = 0.0;
    for (int i = 0; i < 50; ++i) { c.advance (0.01, noHost()); acc += 0.01; if (c.crossedBeat()) sawBeat = true; }
    REQUIRE (sawBeat);
    // Cuantizador: al inicio de un beat fresco, faltan ~0.5 s para el próximo beat.
    BeatClock c2; c2.reset();
    c2.advance (0.001, noHost());
    const double toBeat = c2.secondsToNextDivision (1.0);
    REQUIRE (toBeat > 0.45);
    REQUIRE (toBeat <= 0.5);
    const double toBar = c2.secondsToNextDivision (4.0);
    REQUIRE (toBar > 1.9);
    REQUIRE (toBar <= 2.0);
}

TEST_CASE ("tempo: syncPhase de un LFO al compás corre 0..1 por ciclo", "[supernova][tempo]")
{
    BeatClock c; c.reset();                        // 120 BPM
    // 1 bar = 4 beats = 2 s. A 1 s (medio bar) la fase de un LFO de 4 beats es ~0.5.
    for (int i = 0; i < 100; ++i) c.advance (0.01, noHost());
    REQUIRE (c.syncPhase (4.0) == Approx (0.5).margin (0.02));
    REQUIRE (c.syncPhase (1.0) == Approx (0.0).margin (0.02));   // a 2 beats justos, un LFO de 1 beat cierra ciclo
}
