// Snapshot [snapshot][aurora] — rinde el editor de AURORA ENSAMBLADO (header browser + la aurora
// desplegada verde + banda SYNC + knobs SPREAD/TILT/MOTION/MONO SAFE/DUCK/MIX + utilidad) a
// /tmp/ovni_aurora_m.png, para verificar a ojo la identidad visual verde (Espectral) y que el SYNC
// no pisa la utilidad. Setea valores para que la aurora se vea desplegada (Spread 70 % · Tilt +15 ·
// Motion 35 %) y empuja señal RICA (4 parciales por el espectro) para que las cortinas tengan
// energía/posición reales del motor. Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>   // std::getenv (guard CI headless en [shot4k])
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("snapshot: editor de AURORA -> /tmp/ovni_aurora_m.png", "[snapshot][aurora]")
{
    namespace pid = aurora::params::id;
    aurora::AuroraProcessor proc;

    // Valores para el shot (normalizados): Spread 70 % · Tilt +15 (norm 0.575) · Motion 35 %.
    if (auto* s = proc.apvts.getParameter (pid::SPREAD)) s->setValueNotifyingHost (0.70f);
    if (auto* t = proc.apvts.getParameter (pid::TILT))   t->setValueNotifyingHost (0.575f);
    if (auto* m = proc.apvts.getParameter (pid::MOTION)) m->setValueNotifyingHost (0.35f);

    proc.prepareToPlay (48000.0, 512);

    // Señal rica en espectro (4 parciales: graves→aire) → la telemetría por banda se llena y la
    // aurora del shot muestra cortinas REALES en sus posiciones (24 bloques > latencia OLA de 2048).
    constexpr float freqs[] = { 110.0f, 440.0f, 1760.0f, 7040.0f };
    constexpr float amps[]  = { 0.30f, 0.22f, 0.16f, 0.10f };
    for (int i = 0; i < 24; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
            {
                const float tt = (float) (i * 512 + n) / 48000.0f;
                float v = 0.0f;
                for (int p = 0; p < 4; ++p)
                    v += amps[p] * std::sin (juce::MathConstants<float>::twoPi * freqs[p] * tt);
                d[n] = v;
            }
        }
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 2.0f);   // 2x (Retina)
    REQUIRE (img.isValid());

    auto out = juce::File ("/tmp/ovni_aurora_m.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}

// [shot4k][aurora] — CAPTURA REAL 4x (3840x2320) del editor REAL en tamano M, estado VIVO: aurora
// desplegada (Spread 70 / Tilt +15 / Motion 35) + senal rica (4 parciales) e interleave processBlock
// con pumpFrames -> las cortinas verdes toman energia/posicion REALES del motor antes del PNG.
// Fuente unica web/ficha. -> docs/redesign/real-shots/aurora.png
TEST_CASE ("shot4k: AURORA editor real 4x -> real-shots/aurora.png", "[shot4k][aurora]")
{
    // CI/headless: este [shot4k] regenera la foto de marketing en un path local del autor
    // (docs/redesign/real-shots/*.png) y necesita window-server; en CI se auto-saltea sin tocar
    // su logica (GitHub Actions exporta CI=true). Corre normal en local para rehornear la foto.
    if (std::getenv ("CI") != nullptr) { SUCCEED ("shot4k saltado en CI headless"); return; }
    namespace pid = aurora::params::id;
    aurora::AuroraProcessor proc;
    if (auto* s = proc.apvts.getParameter (pid::SPREAD)) s->setValueNotifyingHost (0.70f);
    if (auto* t = proc.apvts.getParameter (pid::TILT))   t->setValueNotifyingHost (0.575f);
    if (auto* m = proc.apvts.getParameter (pid::MOTION)) m->setValueNotifyingHost (0.35f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* ae = dynamic_cast<aurora::AuroraEditor*> (ed.get());
    REQUIRE (ae != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    constexpr float freqs[] = { 110.0f, 440.0f, 1760.0f, 7040.0f };
    constexpr float amps[]  = { 0.30f, 0.22f, 0.16f, 0.10f };
    int sampleN = 0;
    for (int blk = 0; blk < 48; ++blk)   // > latencia OLA (2048) para que la telemetria por banda se llene
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < 512; ++n)
            {
                const float tt = (float) (sampleN + n) / 48000.0f;
                float v = 0.0f;
                for (int p = 0; p < 4; ++p)
                    v += amps[p] * std::sin (juce::MathConstants<float>::twoPi * freqs[p] * tt);
                d[n] = v;
            }
        }
        sampleN += 512;
        proc.processBlock (buf, midi);
        ae->dbgPump (2);
    }
    ae->dbgPump (10);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);
    REQUIRE (img.isValid());

    auto out = juce::File ("/path/to/ovni/docs/redesign/real-shots/aurora.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os2 (out);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out.getSize() > 0);
}
