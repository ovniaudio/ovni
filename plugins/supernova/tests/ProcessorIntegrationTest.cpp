// [supernova][integration] — end-to-end POR EL PROCESSOR (headless, sin GPU): un CC MIDI en processBlock llega
// a la cola CC; el playhead del host maneja el BeatClock; y el audio sigue bit-exacto (RNF1) con MIDI+playhead.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include "PluginProcessor.h"

using Catch::Approx;

namespace {
struct MockPlayHead : juce::AudioPlayHead
{
    double bpm = 128.0, ppq = 2.5; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        return p;
    }
};
}

TEST_CASE ("integration: un CC MIDI en processBlock llega a la cola CC", "[supernova][integration]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 100), 0);
    proc.processBlock (buf, midi);

    supernova::MidiCcMsg m;
    REQUIRE (proc.midiCcQueue().pop (m));   // el CC crudo se encoló (lo drena el editor → mapea a un param)
    REQUIRE (m.cc == 74);
    REQUIRE (m.value == 100);
}

TEST_CASE ("integration: el playhead del host maneja el BeatClock", "[supernova][integration]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    MockPlayHead ph;
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);

    REQUIRE (proc.tempoBpm() == Approx (128.0));       // el host manda tempo
    REQUIRE (proc.phaseInBeats() == Approx (2.5));     // la fase se engancha al ppq del transport
    REQUIRE (proc.transportPlaying());
    proc.setPlayHead (nullptr);
}

// Fix 2 (app tap-tempo): sin host y con audio SILENCIOSO, el tap DEBE mover el BPM. Prueba el mecanismo que
// el heartbeat de silencio de la app garantiza — que processBlock corra aunque no fluya audio → consume el tap.
TEST_CASE ("integration: tap-tempo sin host con audio silencioso mueve el BPM (fix 2)", "[supernova][integration][tempo]")
{
    supernova::SupernovaProcessor proc;
    constexpr double SR = 48000.0;
    constexpr int    BLK = 480;                 // 480/48000 = 0.01 s por bloque exacto → dt de tap determinista
    proc.prepareToPlay (SR, BLK);
    // SIN playhead: standalone. El BeatClock corre libre (bug original: se quedaba en 120 sin tap).
    juce::AudioBuffer<float> buf (2, BLK);
    juce::MidiBuffer midi;
    auto silentBlock = [&] { buf.clear(); midi.clear(); proc.processBlock (buf, midi); };

    // Control: audio silencioso SOLO (sin tap) → el BPM se queda en 120 (free-run no deriva el tempo).
    for (int i = 0; i < 120; ++i) silentBlock();
    REQUIRE (proc.tempoBpm() == Approx (120.0));

    // Tap a 0.60 s de intervalo (= 60 bloques) → 100 BPM. El 1er bloque tras cada tap lo consume.
    for (int t = 0; t < 4; ++t)
    {
        proc.tapTempo();
        for (int i = 0; i < 60; ++i) silentBlock();
    }
    REQUIRE (proc.tempoBpm() == Approx (100.0).margin (0.5));   // el tap movió el tempo SIN audio ni host
    REQUIRE (proc.tempoBpm() != Approx (120.0));
}

TEST_CASE ("integration: pass-through bit-exacto (RNF1) con MIDI+playhead", "[supernova][integration]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buf (2, 256);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 256; ++n)
            buf.setSample (ch, n, std::sin ((float) n * 0.13f) * 0.7f);
    juce::AudioBuffer<float> before; before.makeCopyOf (buf);

    MockPlayHead ph; proc.setPlayHead (&ph);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0);
    proc.processBlock (buf, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 256; ++n)
            REQUIRE (buf.getSample (ch, n) == before.getSample (ch, n));   // el audio jamás se toca
    proc.setPlayHead (nullptr);
}

// RONDA 3b · F6 — el arranque no arrastra eventos. `prepareEngine` reseteaba el FIFO de audio, la cola de
// triggers visuales y la de CC, pero se había olvidado de la de CUES DE FOTO (ronda 3): un pad tocado
// mientras el host re-prepara (cambio de sample rate, transport reset) podía aparecer como un corte de foto
// en el primer tick del editor, sin que nadie lo hubiera pedido entonces.
TEST_CASE ("integration: prepareToPlay deja las colas MIDI vacías (el arranque no arrastra eventos)",
           "[supernova][integration][midi]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 74, (juce::uint8) 100), 0);        // 74 = cue del tile 3
    midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 64), 1);             // un CC cualquiera
    proc.processBlock (buf, midi);

    supernova::MidiCueMsg cue;
    supernova::MidiCcMsg  cc;
    // (nadie las drenó: el editor no está abierto)
    proc.prepareToPlay (48000.0, 512);
    REQUIRE (! proc.midiCueQueue().pop (cue));
    REQUIRE (! proc.midiCcQueue().pop (cc));
}

// ================== RONDA 3c · F9 — el estampado del beatPos lleva el offset de sample ==================
// El cue viaja con el beatPos de LA NOTA: la fase del comienzo del bloque MÁS el offset del mensaje dentro
// del bloque (`meta.samplePosition`). Hasta acá ningún test movía el samplePosition de 0, así que la mitad
// del cálculo — la que hace que una nota tocada a mitad de bloque no se atrase un bloque entero — nunca se
// ejercitaba. Hallazgo M3 de la auditoría de la ronda 3b.
TEST_CASE ("integration: el cue MIDI viaja con el beatPos de la nota, offset de sample incluido",
           "[supernova][integration][midi]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    MockPlayHead ph;
    ph.bpm = 120.0;      // 2 beats por segundo: el offset se lee a ojo
    ph.ppq = 2.0;
    proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512); buf.clear();

    juce::MidiBuffer empty;
    proc.processBlock (buf, empty);                 // el BeatClock se engancha al transport
    REQUIRE (proc.phaseInBeats() == Approx (2.0));

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 74, (juce::uint8) 100), 0);     // en el borde del bloque
    midi.addEvent (juce::MidiMessage::noteOn (1, 75, (juce::uint8) 100), 256);   // medio bloque más tarde
    proc.processBlock (buf, midi);

    supernova::MidiCueMsg first, later;
    REQUIRE (proc.midiCueQueue().pop (first));
    REQUIRE (proc.midiCueQueue().pop (later));
    REQUIRE (first.beatPos == Approx (2.0));        // sin offset: la fase del comienzo del bloque
    REQUIRE (later.beatPos == Approx (2.0 + 256.0 / 48000.0 * 120.0 / 60.0));
    REQUIRE (later.beatPos - first.beatPos == Approx (0.0106667).margin (1.0e-6));

    proc.setPlayHead (nullptr);
}
