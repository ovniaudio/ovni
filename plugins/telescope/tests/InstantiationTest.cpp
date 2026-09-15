// [telescope][smoke] — el plugin se construye, se prepara a cualquier sample rate / bloque razonable,
// abre su editor y se destruye en orden (editor -> processor) sin crash ni leaks.
// Es el canario del chasis: si esto no pasa, nada de lo que sigue tiene sentido medir.
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include "PluginProcessor.h"

TEST_CASE ("telescope: instancia + prepare en toda la grilla de sr/bloque", "[telescope][smoke]")
{
    const double rates[]  = { 44100.0, 48000.0, 96000.0 };
    const int    blocks[] = { 64, 512, 2048 };

    for (const double sr : rates)
        for (const int blockSize : blocks)
        {
            telescope::TelescopeProcessor proc;
            proc.prepareToPlay (sr, blockSize);

            // TELESCOPE no procesa: no puede introducir latencia ni cola.
            REQUIRE (proc.getLatencySamples() == 0);
            REQUIRE (proc.getTailLengthSeconds() == 0.0);
            REQUIRE (proc.getSampleRate() == sr);

            proc.releaseResources();
        }
}

TEST_CASE ("telescope: el editor se crea y se destruye antes que el processor", "[telescope][smoke]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        REQUIRE (ed != nullptr);
        REQUIRE (ed->getWidth()  > 0);
        REQUIRE (ed->getHeight() > 0);
    }   // <- el editor muere ACÁ, con el processor todavía vivo (orden correcto)

    proc.releaseResources();
}
