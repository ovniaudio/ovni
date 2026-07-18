// [supernova][null] — RNF1 / criterio de aceptación #7: con SUPERNOVA insertado y en default, el audio del
// canal es BIT-EXACTO (output == input, muestra a muestra). El plugin solo "escucha" (tap read-only al FIFO).
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"

TEST_CASE ("supernova: audio pass-through bit-exacto (RNF1)", "[supernova][null]")
{
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    const int n = 512;
    juce::AudioBuffer<float> in (2, n), work (2, n);
    juce::Random rng (20260710);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            const float v = rng.nextFloat() * 2.0f - 1.0f;
            in.setSample (ch, i, v);
            work.setSample (ch, i, v);
        }

    juce::MidiBuffer midi;
    for (int b = 0; b < 4; ++b)          // varios bloques (el tap no debe alterar nada)
        proc.processBlock (work, midi);

    // Nota: processBlock corre 4 veces pero como no altera el buffer, tras el último sigue == input.
    // (Re-generamos input idéntico por bloque no hace falta: el buffer no se toca.)
    int mismatches = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            if (work.getSample (ch, i) != in.getSample (ch, i))
                ++mismatches;

    std::printf ("NULL_MISMATCHES=%d\n", mismatches);
    REQUIRE (mismatches == 0);
}
