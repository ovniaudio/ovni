// Snapshot [snapshot][nebula] — rinde el editor de NÉBULA ENSAMBLADO (header browser + nube magenta que
// respira/esculpe + knobs Size/Decay/Tone/Breath/Mix + utilidad) a /tmp/ovni_nebula_m.png, para verificar
// a ojo la identidad visual magenta. Setea un par de valores (Size 60 %, Decay 70 %) para que la nube se
// vea grande y poblada en el shot. Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"

TEST_CASE ("snapshot: editor de NEBULA -> /tmp/ovni_nebula_m.png", "[snapshot][nebula]")
{
    nebula::NebulaProcessor proc;

    // Un par de valores para el shot: Size 60 % (nube grande) · Decay 70 % (densa/poblada). apvts es
    // público (el editor lo usa como p.apvts); rango 0..100 → 0.60 / 0.70 normalizados.
    if (auto* s = proc.apvts.getParameter ("size"))  s->setValueNotifyingHost (0.60f);
    if (auto* d = proc.apvts.getParameter ("decay")) d->setValueNotifyingHost (0.70f);

    proc.prepareToPlay (48000.0, 512);

    // Empujar señal para que el meter muestre nivel + la telemetría se escriba (la nube reacciona).
    for (int i = 0; i < 8; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dd = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
                dd[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (i * 512 + n) / 48000.0f);
        }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);   // 2x (Retina)
    REQUIRE (img.isValid());

    auto out = juce::File ("/tmp/ovni_nebula_m.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}

// [shot4k][nebula] — CAPTURA REAL 4x (3600x2400) del editor REAL en tamano M, estado VIVO: nube grande
// (Size 60 / Decay 72) + audio empujado e interleave de processBlock con pumpFrames para que la nube
// RESPIRE y las motas de polvo se asienten antes del PNG (timer no corre headless). Fuente unica de la
// foto de web/ficha. -> docs/redesign/real-shots/nebula.png
TEST_CASE ("shot4k: NEBULA editor real 4x -> real-shots/nebula.png", "[shot4k][nebula]")
{
    nebula::NebulaProcessor proc;
    if (auto* s = proc.apvts.getParameter ("size"))  s->setValueNotifyingHost (0.60f);
    if (auto* d = proc.apvts.getParameter ("decay")) d->setValueNotifyingHost (0.72f);
    if (auto* t = proc.apvts.getParameter ("tone"))  t->setValueNotifyingHost (0.40f);
    if (auto* b = proc.apvts.getParameter ("breath")) b->setValueNotifyingHost (0.35f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* ne = dynamic_cast<nebula::NebulaEditor*> (ed.get());
    REQUIRE (ne != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    int sampleN = 0;
    for (int blk = 0; blk < 48; ++blk)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* dd = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
                dd[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (sampleN + n) / 48000.0f);
        }
        sampleN += 512;
        proc.processBlock (buf, midi);
        ne->dbgPump (2);
    }
    ne->dbgPump (12);   // que la respiracion quede en una fase plena (nube abierta)

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);
    REQUIRE (img.isValid());

    auto out = juce::File ("/path/to/ovni/docs/redesign/real-shots/nebula.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os2 (out);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out.getSize() > 0);
}
