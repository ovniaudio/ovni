// [resize][horizon] — verifica los 3 tamaños fijos (S/M/L) del chasis sobre HORIZON (960×580 base)
// y deja snapshots /tmp/ovni_horizon_{s,m,l}.png para mirar a ojo (el espectro suspendido verde debe
// leerse legible a 768 px de ancho — el S — donde vive el gancho del Reel a 320 px). Headless
// (ScopedJuceInitialiser_GUI lo provee TestMain). Patrón AURORA/PULSAR.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
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

TEST_CASE ("resize: HORIZON S/M/L tamaños exactos + snapshots", "[resize][horizon]")
{
    namespace pid = horizon::params::id;
    horizon::HorizonProcessor proc;

    // FREEZE on + WHISPER 30 % + SPREAD 70 % + RATE SYNC → el espectro suspendido se cristaliza
    // con tiembleo y latido (snapshots ricos para mirar a ojo en los 3 tamaños).
    if (auto* fz = proc.apvts.getParameter (pid::FREEZE))   fz->setValueNotifyingHost (1.0f);
    if (auto* w  = proc.apvts.getParameter (pid::WHISPER))  w->setValueNotifyingHost (0.30f);
    if (auto* s  = proc.apvts.getParameter (pid::SPREAD))   s->setValueNotifyingHost (0.70f);
    if (auto* sy = proc.apvts.getParameter (pid::RATESYNC)) sy->setValueNotifyingHost (1.0f);

    proc.prepareToPlay (48000.0, 512);

    // Señal rica (4 parciales) → telemetría del espectro real (32 bloques > latencia OLA 2048).
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
    auto* base = dynamic_cast<ovni::PluginEditorBase*> (ed.get());
    REQUIRE (base != nullptr);

    using Zoom = ovni::PluginEditorBase::Zoom;

    base->applyZoom (Zoom::small);     // 960×580 × 0.8
    CHECK (ed->getWidth()  == 768);
    CHECK (ed->getHeight() == 464);
    writePng (*ed, "/tmp/ovni_horizon_s.png");

    base->applyZoom (Zoom::medium);    // base
    CHECK (ed->getWidth()  == 960);
    CHECK (ed->getHeight() == 580);
    writePng (*ed, "/tmp/ovni_horizon_m_resize.png");

    base->applyZoom (Zoom::large);     // × 1.25
    CHECK (ed->getWidth()  == 1200);
    CHECK (ed->getHeight() == 725);
    writePng (*ed, "/tmp/ovni_horizon_l.png");
}
