// [supernova][smoke] — instanciación GPU-opcional + apertura/cierre del editor (R1 teardown headless).
// En CI (macos-latest sin GPU usable) esto ejercita el camino fallback (device nil → panel, host intacto);
// en la máquina de Joaquín ejercita el camino Metal real. En ambos: construir/destruir no debe crashear.
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"

TEST_CASE ("supernova instancia y procesa audio sin crash", "[supernova][smoke]")
{
    supernova::SupernovaProcessor proc;
    REQUIRE (proc.getName() == juce::String ("SUPERNOVA"));
    // Nota: acceptsMidi()==JucePlugin_WantsMidiInput es una macro del plugin build (NEEDS_MIDI_INPUT TRUE),
    // no está definida en este console-app. La aceptación MIDI real la verifica pluginval sobre el VST3/AU.

    proc.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    juce::Random rng (1234);
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < 512; ++n)
            buf.setSample (ch, n, rng.nextFloat() * 2.0f - 1.0f);

    for (int i = 0; i < 8; ++i)
        proc.processBlock (buf, midi);

    SUCCEED ("procesó 8 bloques sin crashear");
}

TEST_CASE ("supernova abre y cierra el editor repetidamente (R1 teardown)", "[supernova][smoke]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    for (int i = 0; i < 5; ++i)                          // simula el open/close que hacen Live/Logic
    {
        juce::AudioProcessorEditor* ed = proc.createEditor();
        REQUIRE (ed != nullptr);
        ed->setBounds (0, 0, 960, 580);
        delete ed;                                       // destruye MetalViewComponent → prueba el teardown ordenado
    }

    SUCCEED ("5 ciclos de editor sin crash/leak");
}
