// [resize][halo] — verifica los 3 tamaños fijos (S/M/L) del chasis sobre HALO (900×600 base)
// y deja snapshots /tmp/ovni_halo_{s,m,l}.png para mirar a ojo. Headless (ScopedJuceInitialiser_GUI
// lo provee TestMain de S3). Paridad con PULSAR/AURORA/DUST/HORIZON (QA de catálogo 2026-07-16).
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

TEST_CASE ("resize: HALO S/M/L tamaños exactos + snapshots", "[resize][halo]")
{
    halo::HaloProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* base = dynamic_cast<ovni::PluginEditorBase*> (ed.get());
    REQUIRE (base != nullptr);

    using Zoom = ovni::PluginEditorBase::Zoom;

    base->applyZoom (Zoom::small);        // 900×600 × 0.8
    CHECK (ed->getWidth()  == 720);
    CHECK (ed->getHeight() == 480);
    writePng (*ed, "/tmp/ovni_halo_s.png");

    base->applyZoom (Zoom::medium);
    CHECK (ed->getWidth()  == 900);
    CHECK (ed->getHeight() == 600);
    writePng (*ed, "/tmp/ovni_halo_m.png");

    base->applyZoom (Zoom::large);        // 900×600 × 1.25
    CHECK (ed->getWidth()  == 1125);
    CHECK (ed->getHeight() == 750);
    writePng (*ed, "/tmp/ovni_halo_l.png");
}
