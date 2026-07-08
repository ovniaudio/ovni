// Snapshot [snapshot][halo] — rinde el editor de HALO ENSAMBLADO (header browser + halo que orbita magenta
// + FREEZE + SYNC + knobs MIX/SIZE/DECAY/SHIMMER/TONE/ORBIT + utilidad) a /tmp/ovni_halo_m.png, para
// verificar a ojo la identidad visual magenta y que el FREEZE/SYNC se ven sin pisar la utilidad. Setea
// valores para que el halo se vea poblado y orbitando (Shimmer 70 % · Decay 75 % · Orbit 70 %).
// Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>   // std::getenv (guard CI headless en [shot4k])
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("snapshot: editor de HALO -> /tmp/ovni_halo_m.png", "[snapshot][halo]")
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;

    // Valores para el shot: Shimmer 70 % · Decay 75 % (halo cargado) · Orbit 70 % (anillos rotando).
    if (auto* s = proc.apvts.getParameter (pid::SHIMMER)) s->setValueNotifyingHost (0.70f);
    if (auto* d = proc.apvts.getParameter (pid::DECAY))   d->setValueNotifyingHost (0.75f);
    if (auto* o = proc.apvts.getParameter (pid::ORBIT))   o->setValueNotifyingHost (0.70f);

    proc.prepareToPlay (48000.0, 512);

    // Empujar señal para que el meter muestre nivel + la telemetría se escriba (el halo reacciona).
    for (int i = 0; i < 16; ++i)
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

    auto out = juce::File ("/tmp/ovni_halo_m.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}

// [shot4k][halo] — CAPTURA REAL 4x (3600x2400) del editor REAL en tamano M, estado VIVO: halo cargado
// y orbitando (Shimmer 70 / Decay 75 / Orbit 70) + interleave processBlock con pumpFrames -> el funnel
// de coronas magenta ROTA y florece antes del PNG. Fuente unica web/ficha.
// -> docs/redesign/real-shots/halo.png
TEST_CASE ("shot4k: HALO editor real 4x -> real-shots/halo.png", "[shot4k][halo]")
{
    // CI/headless: este [shot4k] regenera la foto de marketing en un path local del autor
    // (docs/redesign/real-shots/*.png) y necesita window-server; en CI se auto-saltea sin tocar
    // su logica (GitHub Actions exporta CI=true). Corre normal en local para rehornear la foto.
    if (std::getenv ("CI") != nullptr) { SUCCEED ("shot4k saltado en CI headless"); return; }
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    if (auto* s = proc.apvts.getParameter (pid::SHIMMER)) s->setValueNotifyingHost (0.70f);
    if (auto* d = proc.apvts.getParameter (pid::DECAY))   d->setValueNotifyingHost (0.75f);
    if (auto* o = proc.apvts.getParameter (pid::ORBIT))   o->setValueNotifyingHost (0.70f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* he = dynamic_cast<halo::HaloEditor*> (ed.get());
    REQUIRE (he != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    int sampleN = 0;
    for (int blk = 0; blk < 64; ++blk)
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
        he->dbgPump (3);   // 3 frames por bloque -> la orbita gira un arco visible entre capturas
    }
    he->dbgPump (10);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);
    REQUIRE (img.isValid());

    auto out = juce::File ("/path/to/ovni/docs/redesign/real-shots/halo.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os2 (out);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out.getSize() > 0);
}
