// [telescope][null] — el contrato de TELESCOPE: con el plugin insertado y en default, el audio del canal
// sale BIT-EXACTO (output == input, muestra a muestra). El plugin sólo "escucha": processAudio lee L/R y
// los empuja al motor de análisis, nunca escribe el buffer. Y no puede: latencia 0, tail 0.
//
// Espeja plugins/supernova/tests/PassThroughTest.cpp y lo extiende a las variantes que un DAW real produce:
// bypass on/off, cada lente seleccionada, y tamaños de bloque de 1 a 4096.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <vector>
#include "PluginProcessor.h"
#include "lenses/LensIds.h"

namespace
{
// Llena `in` con ruido estéreo determinista y copia a `work`.
void fillDeterministicNoise (juce::AudioBuffer<float>& in, juce::AudioBuffer<float>& work, int seed)
{
    juce::Random rng (seed);
    for (int ch = 0; ch < in.getNumChannels(); ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const float v = rng.nextFloat() * 2.0f - 1.0f;
            in.setSample   (ch, i, v);
            work.setSample (ch, i, v);
        }
}

int countMismatches (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    int mismatches = 0;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample (ch, i) != b.getSample (ch, i))   // bit-exacto: comparación EXACTA a propósito
                ++mismatches;
    return mismatches;
}
}

TEST_CASE ("telescope: pass-through bit-exacto en default", "[telescope][null]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const int n = 512;
    juce::AudioBuffer<float> in (2, n), work (2, n);
    fillDeterministicNoise (in, work, 20260907);

    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b)          // varios bloques (el tap no debe alterar nada)
        proc.processBlock (work, midi);

    const int mismatches = countMismatches (work, in);
    std::printf ("NULL_MISMATCHES=%d (default)\n", mismatches);
    REQUIRE (mismatches == 0);
}

TEST_CASE ("telescope: pass-through bit-exacto con bypass ON", "[telescope][null]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* bypass = proc.apvts.getParameter ("bypass");
    REQUIRE (bypass != nullptr);
    bypass->setValueNotifyingHost (1.0f);

    const int n = 512;
    juce::AudioBuffer<float> in (2, n), work (2, n);
    fillDeterministicNoise (in, work, 11111);

    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b)
        proc.processBlock (work, midi);

    const int mismatches = countMismatches (work, in);
    std::printf ("NULL_MISMATCHES=%d (bypass on)\n", mismatches);
    REQUIRE (mismatches == 0);
}

TEST_CASE ("telescope: pass-through bit-exacto con cada lente seleccionada", "[telescope][null]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lens = proc.apvts.getParameter ("lens");
    REQUIRE (lens != nullptr);
    REQUIRE (lens->getNumSteps() == telescope::kNumLenses);

    int total = 0;
    for (int i = 0; i < telescope::kNumLenses; ++i)
    {
        lens->setValueNotifyingHost (lens->convertTo0to1 ((float) i));

        const int n = 512;
        juce::AudioBuffer<float> in (2, n), work (2, n);
        fillDeterministicNoise (in, work, 3000 + i);

        juce::MidiBuffer midi;
        for (int b = 0; b < 4; ++b)
            proc.processBlock (work, midi);

        total += countMismatches (work, in);
    }
    std::printf ("NULL_MISMATCHES=%d (las %d lentes)\n", total, telescope::kNumLenses);
    REQUIRE (total == 0);
}

TEST_CASE ("telescope: pass-through bit-exacto con bloques de 1 a 4096", "[telescope][null]")
{
    const int sizes[] = { 1, 7, 64, 4096 };
    int total = 0;
    for (const int n : sizes)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (48000.0, 4096);   // el host declara el MÁXIMO; los bloques reales varían

        juce::AudioBuffer<float> in (2, n), work (2, n);
        fillDeterministicNoise (in, work, 777 + n);

        juce::MidiBuffer midi;
        for (int b = 0; b < 8; ++b)
            proc.processBlock (work, midi);

        total += countMismatches (work, in);
    }
    std::printf ("NULL_MISMATCHES=%d (bloques 1/7/64/4096)\n", total);
    REQUIRE (total == 0);
}

// LIMITACIÓN DECLARADA DEL CHASIS: PluginProcessorBase::isBusesLayoutSupported exige salida ESTÉREO
// (entrada mono o estéreo). Un canal mono 1→1 NO es un layout válido; el DAW instancia mono→estéreo.
// Se documenta acá y en el README en vez de sobreescribirlo (el chasis es intocable en este prompt).
TEST_CASE ("telescope: mono a la entrada — el chasis exige salida estéreo", "[telescope][null]")
{
    telescope::TelescopeProcessor proc;

    juce::AudioProcessor::BusesLayout monoToMono;
    monoToMono.inputBuses .add (juce::AudioChannelSet::mono());
    monoToMono.outputBuses.add (juce::AudioChannelSet::mono());
    REQUIRE_FALSE (proc.isBusesLayoutSupported (monoToMono));   // limitación conocida del chasis

    juce::AudioProcessor::BusesLayout monoToStereo;
    monoToStereo.inputBuses .add (juce::AudioChannelSet::mono());
    monoToStereo.outputBuses.add (juce::AudioChannelSet::stereo());
    REQUIRE (proc.isBusesLayoutSupported (monoToStereo));

    // Y con una fuente mono real (canal 1 con señal, canal 2 en silencio) el audio sigue bit-exacto.
    proc.prepareToPlay (48000.0, 512);
    const int n = 512;
    juce::AudioBuffer<float> in (2, n), work (2, n);
    juce::Random rng (424242);
    for (int i = 0; i < n; ++i)
    {
        const float v = rng.nextFloat() * 2.0f - 1.0f;
        in.setSample (0, i, v);    work.setSample (0, i, v);
        in.setSample (1, i, 0.0f); work.setSample (1, i, 0.0f);
    }
    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b) proc.processBlock (work, midi);

    const int mismatches = countMismatches (work, in);
    std::printf ("NULL_MISMATCHES=%d (fuente mono)\n", mismatches);
    REQUIRE (mismatches == 0);
}

TEST_CASE ("telescope: latencia 0 y tail 0 — no puede desalinear la sesión", "[telescope][null]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    REQUIRE (proc.getLatencySamples() == 0);
    REQUIRE (proc.getTailLengthSeconds() == 0.0);
}

TEST_CASE ("telescope: lens y target sobreviven el round-trip de estado", "[telescope][null]")
{
    juce::MemoryBlock state;
    {
        telescope::TelescopeProcessor proc;
        proc.apvts.getParameter ("lens")  ->setValueNotifyingHost (
            proc.apvts.getParameter ("lens")  ->convertTo0to1 (2.0f));   // SPECTRUM
        proc.apvts.getParameter ("target")->setValueNotifyingHost (
            proc.apvts.getParameter ("target")->convertTo0to1 (1.0f));   // Spotify
        proc.getStateInformation (state);
    }

    telescope::TelescopeProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (restored.apvts.getParameter ("lens")  ->getCurrentValueAsText() == "SPECTRUM");
    REQUIRE (restored.apvts.getParameter ("target")->getCurrentValueAsText() == "Spotify");
}
