// [supernova][app] — foundations PURAS de la app standalone (sin Cocoa, sin GPU).
//   AudioSourceModel : selección de fuente de audio (System Audio / device) + round-trip ValueTree.
//   AudioConvert     : de-interleave Float32 + pico para el medidor.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "app/AudioSourceModel.h"
#include "app/AudioConvert.h"

using Catch::Approx;
using supernova::AudioSourceModel;
using Kind = supernova::AudioSourceModel::Kind;

TEST_CASE ("app source model: default is SystemAudio", "[supernova][app]")
{
    AudioSourceModel m;
    REQUIRE (m.kind() == Kind::systemAudio);
    REQUIRE (m.deviceName().isEmpty());
}

TEST_CASE ("app source model: selecting a device switches kind and remembers name", "[supernova][app]")
{
    AudioSourceModel m;
    m.selectInputDevice ("Scarlett 2i2");
    REQUIRE (m.kind() == Kind::inputDevice);
    REQUIRE (m.deviceName() == "Scarlett 2i2");

    m.selectSystemAudio();
    REQUIRE (m.kind() == Kind::systemAudio);
    REQUIRE (m.deviceName() == "Scarlett 2i2");   // recordado para la próxima
}

TEST_CASE ("app source model: round-trips through a ValueTree", "[supernova][app]")
{
    AudioSourceModel a;
    a.selectInputDevice ("UAD Apollo");
    a.setMidiInput ("LPK25");
    auto state = a.toValueTree();

    AudioSourceModel b;
    b.fromValueTree (state);
    REQUIRE (b.kind() == Kind::inputDevice);
    REQUIRE (b.deviceName() == "UAD Apollo");
    REQUIRE (b.midiInput() == "LPK25");
}

TEST_CASE ("app convert: interleaved stereo splits into channels", "[supernova][app]")
{
    // frames = 3, ch = 2, interleaved LRLRLR
    const float inter[6] = { 0.1f, -0.1f, 0.2f, -0.2f, 0.3f, -0.3f };
    juce::AudioBuffer<float> buf (2, 3);
    supernova::deinterleave (inter, /*numCh*/ 2, /*numFrames*/ 3, buf);
    REQUIRE (buf.getSample (0, 0) == Approx ( 0.1f));
    REQUIRE (buf.getSample (1, 0) == Approx (-0.1f));
    REQUIRE (buf.getSample (0, 2) == Approx ( 0.3f));
    REQUIRE (buf.getSample (1, 2) == Approx (-0.3f));
}

TEST_CASE ("app convert: peak is the max abs across channels", "[supernova][app]")
{
    juce::AudioBuffer<float> buf (2, 4);
    buf.clear();
    buf.setSample (1, 2, -0.7f);
    REQUIRE (supernova::bufferPeak (buf) == Approx (0.7f));
}
