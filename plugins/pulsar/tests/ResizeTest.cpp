// [resize][pulsar] — verifica los 3 tamaños fijos (S/M/L) del chasis sobre PULSAR (960×580 base)
// y deja snapshots /tmp/ovni_pulsar_{s,m,l}.png para mirar a ojo. Headless (ScopedJuceInitialiser_GUI
// lo provee TestMain de S3).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

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

TEST_CASE ("resize: PULSAR S/M/L tamaños exactos + snapshots", "[resize][pulsar]")
{
    pulsar::PulsarProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* base = dynamic_cast<ovni::PluginEditorBase*> (ed.get());
    REQUIRE (base != nullptr);

    using Zoom = ovni::PluginEditorBase::Zoom;

    base->applyZoom (Zoom::small);
    CHECK (ed->getWidth()  == 768);
    CHECK (ed->getHeight() == 464);
    writePng (*ed, "/tmp/ovni_pulsar_s.png");

    base->applyZoom (Zoom::medium);
    CHECK (ed->getWidth()  == 960);
    CHECK (ed->getHeight() == 580);
    writePng (*ed, "/tmp/ovni_pulsar_m.png");

    base->applyZoom (Zoom::large);
    CHECK (ed->getWidth()  == 1200);
    CHECK (ed->getHeight() == 725);
    writePng (*ed, "/tmp/ovni_pulsar_l.png");
}

// [shot4k][pulsar] — CAPTURA REAL 4x (3440x2400) del editor REAL en tamaño M, estado VIVO: MOTION/SMEAR
// altos + audio empujado e interleave de processBlock con pumpFrames → el atractor ORBITA y la estela
// cometa (cian->magenta->ambar) + el bloom de la fuente se ACUMULAN antes del PNG (el timer no corre
// headless). Fuente unica de la foto de web/ficha. -> docs/redesign/real-shots/pulsar.png
TEST_CASE ("shot4k: PULSAR editor real 4x -> real-shots/pulsar.png", "[shot4k][pulsar]")
{
    namespace pid = pulsar::params::id;
    pulsar::PulsarProcessor proc;

    // Estado evocativo (mockup pulsar-a): SHAPE bajo = sistema ORBIT → una ELIPSE ABIERTA y limpia que
    // barre todo el pozo (no un ovillo de caos central). MOTION medio-alto = órbita energética y
    // excéntrica con cabeza-cometa brillante; SMEAR alto = cola difusa larga; ancho pleno.
    if (auto* m = proc.apvts.getParameter (pid::MOTION)) m->setValueNotifyingHost (0.70f);
    if (auto* s = proc.apvts.getParameter (pid::SMEAR))  s->setValueNotifyingHost (0.82f);
    if (auto* sh= proc.apvts.getParameter (pid::SHAPE))  sh->setValueNotifyingHost (0.08f);
    if (auto* w = proc.apvts.getParameter (pid::WIDTH))  w->setValueNotifyingHost (0.85f);
    if (auto* mx= proc.apvts.getParameter (pid::MIX))    mx->setValueNotifyingHost (0.65f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* pe = dynamic_cast<pulsar::PulsarEditor*> (ed.get());
    REQUIRE (pe != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());   // tamano M base (960x580)

    // Interleave FIEL a 30 fps reales: a 48 kHz / 512 = 93.75 bloques/s y el visual corre a 30 fps -> ~3
    // bloques por frame. Procesamos 3 bloques por cada pump (el atractor avanza por frame como EN VIVO,
    // no 3x mas lento) durante 200 frames (~3 loops a RATE 0.5 Hz) -> el FOSFORO (larga exposicion) se LLENA:
    // la forma (elipse en SHAPE bajo) queda dibujada con un degrade de calor cian->magenta->ambar + la
    // cabeza-cometa brillante. (Solo cantidad de frames/bloques: el DSP no se toca.)
    int sampleN = 0;
    for (int frame = 0; frame < 200; ++frame)
    {
        for (int sub = 0; sub < 3; ++sub)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int n = 0; n < 512; ++n)
                    d[n] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (sampleN + n) / 48000.0f);
            }
            sampleN += 512;
            proc.processBlock (buf, midi);
        }
        pe->dbgPump (1);   // 1 frame del visual por cada ~3 bloques (fiel a 30 fps reales)
    }

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);   // 4x (real 4K)
    REQUIRE (img.isValid());

    auto out = juce::File ("/Users/musik/PLUGINS/ovni/docs/redesign/real-shots/pulsar.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}
