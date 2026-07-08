// Entry point de los tests [s3]: inicializa el MessageManager de JUCE (lo necesitan APVTS / AsyncUpdater /
// ValueTree) y corre la sesión de Catch2.
#include <juce_gui_basics/juce_gui_basics.h>
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    return Catch::Session().run (argc, argv);
}
