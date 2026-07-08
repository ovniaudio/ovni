// =============================================================================
// Test/QA [s2] del UI-kit del sello: renderiza la Gallery (Theme+Knob+Meter+Panel+Controls+
// VisualizerBase) a un snapshot a resolución FÍSICA (Retina, scale 2x) y verifica que salga
// NÍTIDO (px = lógico×scale) y con la PALETA del sello (mayormente negro + acentos cian).
//
// Dos modos (selecciona uno por macro de compilación):
//   -DOVNI_UIKIT_SNAPSHOT_MAIN  -> int main(): escribe /tmp/ovni_uikit.png y devuelve 0/1.
//   -DOVNI_UIKIT_WITH_CATCH2    -> TEST_CASE(..., "[s2]") para el runner de S5.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Gallery.h"
#include <atomic>
#include <iostream>

using namespace juce;

namespace
{
// AudioProcessor stub mínimo: sólo existe para hospedar un APVTS con los params que la Gallery
// bindeará (un Choice para SegControl + un Bool para ToggleButton). No procesa audio.
class DemoProcessor : public AudioProcessor
{
public:
    DemoProcessor()
        : AudioProcessor (BusesProperties().withInput  ("In",  AudioChannelSet::stereo(), true)
                                           .withOutput ("Out", AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", makeLayout()) {}

    static AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        AudioProcessorValueTreeState::ParameterLayout l;
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "demoSeg", 1 }, "Shape",
                                                       StringArray { "Circle", "Spiral", "Pendulum" }, 1));
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { "demoEngage", 1 }, "Engage", true));
        return l;
    }

    const String getName() const override            { return "OvniUikitDemo"; }
    void prepareToPlay (double, int) override         {}
    void releaseResources() override                  {}
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override {}
    double getTailLengthSeconds() const override      { return 0.0; }
    bool acceptsMidi() const override                 { return false; }
    bool producesMidi() const override                { return false; }
    AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                   { return false; }
    int getNumPrograms() override                     { return 1; }
    int getCurrentProgram() override                  { return 0; }
    void setCurrentProgram (int) override             {}
    const String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override  {}
    void setStateInformation (const void*, int) override {}

    AudioProcessorValueTreeState apvts;
};

struct Stats { int w = 0, h = 0; double darkFrac = 0.0; int cyanish = 0; double meanLuma = 0.0; };

Stats analyze (const Image& img)
{
    Stats s; s.w = img.getWidth(); s.h = img.getHeight();
    if (! img.isValid()) return s;
    int dark = 0, total = 0, cyan = 0; uint64 sumLuma = 0;
    const int step = jmax (1, img.getHeight() / 400);   // muestreo (rápido y suficiente)
    for (int y = 0; y < img.getHeight(); y += step)
        for (int x = 0; x < img.getWidth(); x += step)
        {
            const auto c = img.getPixelAt (x, y);
            const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
            const int luma = (r * 30 + g * 59 + b * 11) / 100;
            sumLuma += (uint64) luma;
            if (luma < 60) ++dark;
            if (r < 150 && g > 150 && b > 160) ++cyan;   // cian / blanco-cian de los acentos del sello
            ++total;
        }
    if (total > 0) { s.darkFrac = (double) dark / total; s.cyanish = cyan; s.meanLuma = (double) sumLuma / total; }
    return s;
}

bool renderSnapshot (const File& outPng, String& msg)
{
    const int W = 720, H = 460; const float scale = 2.0f;   // 2x = Retina (prueba el cache px-físico)
    DemoProcessor proc;
    std::atomic<float> peak { 0.0f }, clip { 0.0f };
    ovni::ui::Gallery gallery (proc.apvts, peak, clip, "demoSeg", "demoEngage");
    gallery.setBounds (0, 0, W, H);
    gallery.primeForSnapshot();

    const Image img = gallery.createComponentSnapshot (gallery.getLocalBounds(), true, scale);

    const int  expW = roundToInt (W * scale), expH = roundToInt (H * scale);
    const Stats st  = analyze (img);
    const bool sharp = (st.w == expW && st.h == expH);
    const bool drew  = img.isValid() && st.meanLuma > 3.0 && st.darkFrac > 0.45 && st.cyanish > 80;

    if (img.isValid())
    {
        outPng.deleteFile();
        if (auto os = std::unique_ptr<FileOutputStream> (outPng.createOutputStream()))
        {
            PNGImageFormat png; png.writeImageToStream (img, *os);
        }
    }

    msg << "[s2] ui-kit snapshot\n"
        << "  out          : " << outPng.getFullPathName() << "\n"
        << "  px           : " << st.w << "x" << st.h << " (esperado " << expW << "x" << expH
        << ", scale " << String (scale, 1) << "x)\n"
        << "  darkFrac     : " << String (st.darkFrac, 3)
        << "   meanLuma : " << String (st.meanLuma, 1)
        << "   cyanPx(sample) : " << st.cyanish << "\n"
        << "  sharp(Retina): " << (sharp ? "OK" : "FAIL")
        << "    paleta-sello : " << (drew ? "OK" : "FAIL") << "\n";
    return sharp && drew;
}
} // namespace

#if defined(OVNI_UIKIT_SNAPSHOT_MAIN)
int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::String msg;
    const bool ok = renderSnapshot (juce::File ("/tmp/ovni_uikit.png"), msg);
    std::cout << msg << (ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
#endif

#if defined(OVNI_UIKIT_WITH_CATCH2)
#include <catch2/catch_test_macros.hpp>
TEST_CASE ("ui-kit gallery renders sharp + on-palette", "[s2]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::String msg;
    const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni_uikit.png");
    const bool ok = renderSnapshot (out, msg);
    INFO (msg.toStdString());
    REQUIRE (ok);
}
#endif
