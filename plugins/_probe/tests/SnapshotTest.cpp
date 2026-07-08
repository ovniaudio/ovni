// Snapshot [snapshot] — rinde el editor del _probe ENSAMBLADO (header browser de S2/S3 + knob + meter del
// sello) a /tmp/ovni_probe.png, para verificar a ojo que la identidad visual del sello sobrevive a la
// integración. Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("snapshot: editor del _probe -> /tmp/ovni_probe.png", "[snapshot]")
{
    PluginProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // Empujar señal para que el meter muestre nivel en el shot.
    for (int i = 0; i < 8; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
                d[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (i * 512 + n) / 48000.0f);
        }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    ed->setSize (560, 400);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);   // 2x (Retina)
    REQUIRE (img.isValid());

    auto out = juce::File ("/tmp/ovni_probe.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}
