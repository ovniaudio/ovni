// Snapshot [snapshot][horizon] — rinde el editor de HORIZON ENSAMBLADO (header browser + el espectro
// congelado verde que pulsa + botón FREEZE + banda SYNC + knobs WHISPER/SPREAD/DUCK/MIX + utilidad) a
// /tmp/ovni_horizon_m.png, para verificar a ojo la identidad visual verde (Espectral) y que el SYNC/
// FREEZE no pisan la utilidad. Congela una señal rica + RATE SYNC para que el espectro suspendido tenga
// energía/posición reales del motor y el latido se vea. Headless (ScopedJuceInitialiser_GUI lo provee TestMain).
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>   // std::getenv (guard CI headless en [shot4k])
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("snapshot: editor de HORIZON -> /tmp/ovni_horizon_m.png", "[snapshot][horizon]")
{
    namespace pid = horizon::params::id;
    horizon::HorizonProcessor proc;

    // Valores para el shot: FREEZE on, WHISPER 20 %, SPREAD 70 %, RATE SYNC (latido visible).
    if (auto* fz = proc.apvts.getParameter (pid::FREEZE))   fz->setValueNotifyingHost (1.0f);
    if (auto* w  = proc.apvts.getParameter (pid::WHISPER))  w->setValueNotifyingHost (0.20f);
    if (auto* s  = proc.apvts.getParameter (pid::SPREAD))   s->setValueNotifyingHost (0.70f);
    if (auto* sy = proc.apvts.getParameter (pid::RATESYNC)) sy->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (48000.0, 512);

    // Señal rica (4 parciales) → la telemetría del espectro congelado se llena y el campo del
    // shot muestra barras REALES (32 bloques > latencia OLA de 2048 + captura + cross-fade).
    constexpr float freqs[] = { 110.0f, 440.0f, 1760.0f, 7040.0f };
    constexpr float amps[]  = { 0.30f, 0.22f, 0.16f, 0.10f };
    for (int i = 0; i < 32; ++i)
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

    auto out = juce::File ("/tmp/ovni_horizon_m.png");
    out.deleteFile();
    juce::FileOutputStream os (out);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (out.getSize() > 0);
}

// [shot4k][horizon] — CAPTURA REAL 4x (3840x2320) del editor REAL en tamano M, estado VIVO: FREEZE on +
// Whisper 20 / Spread 70 / RATE SYNC + senal rica (4 parciales) e interleave processBlock con pumpFrames
// -> el espectro congelado verde toma barras REALES y el latido se ve antes del PNG. Fuente unica
// web/ficha. -> docs/redesign/real-shots/horizon.png
TEST_CASE ("shot4k: HORIZON editor real 4x -> real-shots/horizon.png", "[shot4k][horizon]")
{
    // CI/headless: este [shot4k] regenera la foto de marketing en un path local del autor
    // (docs/redesign/real-shots/*.png) y necesita window-server; en CI se auto-saltea sin tocar
    // su logica (GitHub Actions exporta CI=true). Corre normal en local para rehornear la foto.
    if (std::getenv ("CI") != nullptr) { SUCCEED ("shot4k saltado en CI headless"); return; }
    namespace pid = horizon::params::id;
    horizon::HorizonProcessor proc;
    if (auto* fz = proc.apvts.getParameter (pid::FREEZE))   fz->setValueNotifyingHost (1.0f);
    if (auto* w  = proc.apvts.getParameter (pid::WHISPER))  w->setValueNotifyingHost (0.20f);
    if (auto* s  = proc.apvts.getParameter (pid::SPREAD))   s->setValueNotifyingHost (0.70f);
    if (auto* sy = proc.apvts.getParameter (pid::RATESYNC)) sy->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* he = dynamic_cast<horizon::HorizonEditor*> (ed.get());
    REQUIRE (he != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    constexpr float freqs[] = { 110.0f, 440.0f, 1760.0f, 7040.0f };
    constexpr float amps[]  = { 0.30f, 0.22f, 0.16f, 0.10f };
    int sampleN = 0;
    for (int blk = 0; blk < 56; ++blk)   // > latencia OLA (2048) + captura + cross-fade del freeze
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
        he->dbgPump (2);
    }
    he->dbgPump (10);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 4.0f);
    REQUIRE (img.isValid());

    auto out = juce::File ("/path/to/ovni/docs/redesign/real-shots/horizon.png");
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os2 (out);
    REQUIRE (os2.openedOk());
    juce::PNGImageFormat png2;
    REQUIRE (png2.writeImageToStream (img, os2));
    os2.flush();
    REQUIRE (out.getSize() > 0);
}
