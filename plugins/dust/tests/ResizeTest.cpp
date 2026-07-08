// [resize][dust] — verifica los 3 tamaños fijos (S/M/L) del chasis sobre DUST (960×580 base)
// y deja snapshots /tmp/ovni_dust_{s,m,l}.png para mirar a ojo. Headless (ScopedJuceInitialiser_GUI
// lo provee TestMain). Factores del chasis: 0.8 / 1.0 / 1.25 (PluginEditorBase).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
void writePng (juce::AudioProcessorEditor& ed, const char* path)
{
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), false, 2.0f);   // 2x (Retina)
    REQUIRE (img.isValid());
    juce::File out (path); out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}
}

TEST_CASE ("resize: DUST S/M/L tamaños exactos + snapshots", "[resize][dust]")
{
    dust::DustProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* base = dynamic_cast<ovni::PluginEditorBase*> (ed.get());
    REQUIRE (base != nullptr);

    using Zoom = ovni::PluginEditorBase::Zoom;

    base->applyZoom (Zoom::small);     // 960×580 × 0.8
    CHECK (ed->getWidth()  == 768);
    CHECK (ed->getHeight() == 464);
    writePng (*ed, "/tmp/ovni_dust_s.png");

    base->applyZoom (Zoom::medium);    // tamaño base
    CHECK (ed->getWidth()  == 960);
    CHECK (ed->getHeight() == 580);
    writePng (*ed, "/tmp/ovni_dust_m.png");

    base->applyZoom (Zoom::large);     // × 1.25
    CHECK (ed->getWidth()  == 1200);
    CHECK (ed->getHeight() == 725);
    writePng (*ed, "/tmp/ovni_dust_l.png");
}
