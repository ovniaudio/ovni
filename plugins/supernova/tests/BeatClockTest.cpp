// [supernova][tempo] — BeatClock: tempo sync (host/tap/auto-BPM) + fase de beat + cuantizador. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "tempo/BeatClock.h"
#include <cmath>

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

// Backlog 0.3.3: el reloj trabaja en SEGUNDOS, pero quien lo alimenta divide el tamaño del bloque por el
// sample rate — y el cue de una nota MIDI suma su offset en muestras. Si en algún lado se colara un 48 000
// supuesto, a 44,1 kHz el reloj correría un 8,8 % lento y a 96 kHz al doble. Esto lo fija.
TEST_CASE ("beatclock: la fase avanza los mismos beats a 44,1 · 48 · 96 kHz", "[supernova][tempo]")
{
    const double bpm = 126.0, segundos = 8.0;

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bloque : { 64, 512, 1024 })
        {
            BeatClock c;
            c.reset();
            c.setDefaultBpm (bpm);
            BeatClock::HostInfo sinHost;    // standalone: free-run al bpm_
            // Un número ENTERO de bloques no cae en 8,000 s exactos a toda tasa: la referencia es el tiempo
            // que de verdad transcurrió, no el nominal. Lo que se prueba es que la fase siga a los SEGUNDOS.
            const int    bloques = (int) std::llround (segundos * sr / (double) bloque);
            const double reales  = (double) bloques * (double) bloque / sr;
            const double esperado = reales * bpm / 60.0;
            for (int i = 0; i < bloques; ++i) c.advance ((double) bloque / sr, sinHost);

            INFO ("sr=" << sr << " bloque=" << bloque << " s=" << reales
                  << " fase=" << c.phaseInBeats() << " esperado=" << esperado);
            CHECK (std::abs (c.phaseInBeats() - esperado) < 1.0e-6);   // acumulado sobre ~11 000 bloques
            CHECK (c.bpm() == bpm);
        }
}

TEST_CASE ("beatclock: el cue de una nota usa el sample rate REAL, no 48 kHz", "[supernova][tempo]")
{
    const double bpm = 120.0;                       // 2 beats/segundo
    const int    muestras = 22050;                  // media vuelta de reloj a 44,1 kHz

    // El MISMO offset en MUESTRAS cae en tiempos distintos según el sample rate: medio segundo a 44,1 kHz,
    // ~0,459 s a 48 kHz, ~0,23 s a 96 kHz. En beats, eso es 1,0 · 0,919 · 0,459.
    CHECK (BeatClock::beatAtSampleOffset (0.0, bpm, muestras, 44100.0) == Approx (1.0));
    CHECK (BeatClock::beatAtSampleOffset (0.0, bpm, muestras, 48000.0) == Approx (0.91875));
    CHECK (BeatClock::beatAtSampleOffset (0.0, bpm, muestras, 96000.0) == Approx (0.459375));

    // El mismo TIEMPO da el mismo beat en las tres tasas (que es lo que de verdad importa).
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        CHECK (BeatClock::beatAtSampleOffset (4.0, bpm, (int) std::llround (0.25 * sr), sr)
               == Approx (4.5));

    // Bordes: sin sample rate (nunca preparado) o sin offset, la fase del bloque sale intacta.
    CHECK (BeatClock::beatAtSampleOffset (3.25, bpm, 512, 0.0) == Approx (3.25));
    CHECK (BeatClock::beatAtSampleOffset (3.25, bpm, 0,   48000.0) == Approx (3.25));
}

