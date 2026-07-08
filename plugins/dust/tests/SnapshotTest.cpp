// Snapshot [snapshot][dust] — rinde el editor de DUST ENSAMBLADO (header browser + campo de burbujas
// = POZO PROFUNDO vertical cian con polvo baked + planos de profundidad + eje L/R + telemetría en las
// 4 esquinas + marcador del ORIGIN; rail izquierdo EJE/CADENCIA = SyncControl RATE; rail inferior
// DENSIDAD/SPREAD/VIDA/MIX; columna utilidad meter·DUCK·IN/OUT·IN PHASE) a /tmp/ovni_dust_m.png, para
// verificar a ojo la identidad visual cian (Movimiento) y que el rail/utilidad se ven sin pisarse.
// Setea valores que pueblan el campo
// (Densidad 70 · Spread 85 · Vida 60) y empuja señal para que el FIFO de burbujas y el meter tengan
// datos REALES. Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("snapshot: editor de DUST -> /tmp/ovni_dust_m.png", "[snapshot][dust]")
{
    namespace pid = dust::params::id;
    dust::DustProcessor proc;

    // Valores para el shot: campo poblado y abierto (la nube de burbujas se tiene que LEER).
    if (auto* d = proc.apvts.getParameter (pid::DENSITY)) d->setValueNotifyingHost (0.70f);
    if (auto* s = proc.apvts.getParameter (pid::SPREAD))  s->setValueNotifyingHost (0.85f);
    if (auto* v = proc.apvts.getParameter (pid::VIDA))    v->setValueNotifyingHost (0.60f);

    proc.prepareToPlay (48000.0, 512);

    // Empujar señal: el meter muestra nivel, la telemetría se escribe y el FIFO publica nacimientos
    // REALES (el campo del shot nace del motor encendido, no de un estado inventado).
    for (int i = 0; i < 32; ++i)
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

    auto out = juce::File ("/tmp/ovni_dust_m.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}

// [shot4k][dust] — CAPTURA REAL 4x (3840x2320) del editor REAL en tamano M, estado VIVO: campo poblado
// (Densidad 70 / Spread 85 / Vida 60) + interleave processBlock con pumpFrames -> el FIFO de burbujas
// nace y el campo se llena de nacimientos REALES antes del PNG. Fuente unica web/ficha.
// -> docs/redesign/real-shots/dust.png
TEST_CASE ("shot4k: DUST editor real 4x -> real-shots/dust.png", "[shot4k][dust]")
{
    namespace pid = dust::params::id;
    dust::DustProcessor proc;
    if (auto* d = proc.apvts.getParameter (pid::DENSITY)) d->setValueNotifyingHost (0.70f);
    if (auto* s = proc.apvts.getParameter (pid::SPREAD))  s->setValueNotifyingHost (0.85f);
    if (auto* v = proc.apvts.getParameter (pid::VIDA))    v->setValueNotifyingHost (0.60f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* de = dynamic_cast<dust::DustEditor*> (ed.get());
    REQUIRE (de != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    int sampleN = 0;
    for (int blk = 0; blk < 200; ++blk)
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
        de->dbgPump (3);   // el motor + el spawner idle pueblan el campo: muchas burbujas vivas a la vez
    }
    de->dbgPump (6);       // settle final del bloom (la mayoría de las burbujas siguen vivas en captura)

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);
    REQUIRE (img.isValid());

    auto out = juce::File ("/path/to/ovni/docs/redesign/real-shots/dust.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os2 (out);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out.getSize() > 0);
}
