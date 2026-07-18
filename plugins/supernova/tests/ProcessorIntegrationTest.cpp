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
