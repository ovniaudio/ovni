// [.uisnap] — herramienta MANUAL (oculta del run normal por el tag con punto): captura la franja de
// controles REAL (JUCE puro, sin Metal) a PNG para los mockups/gate visual. Correr:
//   ./OvniSupernovaTests "[uisnap]"   →  /tmp/snv-ui-strip.png (2×)
// La franja es 100% render JUCE → el snapshot ES la UI que ve Joaquín (la vista Metal vive aparte).
#include <catch2/catch_test_macros.hpp>
#include "PluginProcessor.h"
#include "ui/ControlStrip.h"
#include "ui/LfoPanel.h"
#include "tempo/LfoBank.h"
#include "params/ParameterIDs.h"
#include "ui/theme.h"

TEST_CASE ("uisnap: franja de controles agrupada por dominios → /tmp/snv-ui-strip.png",
           "[supernova][.uisnap]")
{
    supernova::SupernovaProcessor proc;
    supernova::ControlStrip strip (proc.apvts, supernova::look::hue);
    strip.setSize (936, supernova::ControlStrip::kHeight);

    const auto img = strip.createComponentSnapshot (strip.getLocalBounds(), true, 2.0f);
    REQUIRE (img.isValid());

    juce::File f ("/tmp/snv-ui-strip.png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// [.uisnap] — el EDITOR COMPLETO del plugin (app-paridad 2026-07-16: TopBar de 2 filas + canvas flexible +
// strip, sin chrome de catálogo). La vista Metal es una capa NATIVA (no sale en el snapshot JUCE): acá se
// verifica la BARRA y el layout general — el mismo cuerpo que muestra la app.
TEST_CASE ("uisnap: editor del plugin (TopBar app-paridad) → /tmp/snv-plugin-editor*.png",
           "[supernova][.uisnap]")
{
    supernova::SupernovaProcessor proc;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    REQUIRE (editor != nullptr);

    // Los 3 breakpoints del editor flexible: mínimo (720×480), default (1100×760) y wide (1920×1080).
    const struct { int w, h; const char* path; } sizes[] = {
        { 720,  480,  "/tmp/snv-plugin-editor-min.png"  },
        { 1100, 760,  "/tmp/snv-plugin-editor.png"      },
        { 1920, 1080, "/tmp/snv-plugin-editor-wide.png" },
    };
    for (const auto& s : sizes)
    {
        editor->setBounds (0, 0, s.w, s.h);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true,
                                                          s.w >= 1920 ? 1.0f : 2.0f);
        REQUIRE (img.isValid());
        juce::File f (s.path);
        f.deleteFile();
        juce::FileOutputStream os (f);
        REQUIRE (os.openedOk());
        juce::PNGImageFormat png;
        REQUIRE (png.writeImageToStream (img, os));
    }
}

// [.uisnap] — fix 5 · width cap: on a wide app window the strip runs full-bleed but the 8-column grid
// must stay a bounded, CENTRED cluster (no sprawled knobs). Dump at a wide width to eyeball the cap.
TEST_CASE ("uisnap: franja capeada y centrada en ventana ancha → /tmp/snv-ui-strip-wide.png",
           "[supernova][.uisnap]")
{
    supernova::SupernovaProcessor proc;
    supernova::ControlStrip strip (proc.apvts, supernova::look::hue);
    strip.setSize (1920, supernova::ControlStrip::kHeight);

    const auto img = strip.createComponentSnapshot (strip.getLocalBounds(), true, 1.0f);
    REQUIRE (img.isValid());

    juce::File f ("/tmp/snv-ui-strip-wide.png");
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

// [.uisnap] — the LFO panel (fix 5 · "nivel alto"): pro cards + the clickable waveform-icon SHAPE strip
// drawn from LfoBank::wave. 100% JUCE render → the snapshot IS the panel Joaquín sees. A variety of
// enabled/disabled slots + shapes so the whole strip and the dimmed state show up. Dumps at two sizes to
// prove the content cap/centring on a wide window.
static void dumpLfo (int w, int h, const juce::String& path)
{
    namespace pid = supernova::params::id;
    supernova::LfoBank bank;
    auto& s0 = bank.slot (0); s0.enabled = true;  s0.target = pid::HUE;    s0.beatsPerCycle = 1.0f;  s0.shape = supernova::LfoShape::Sine;       s0.depth = 0.50f;
    auto& s1 = bank.slot (1); s1.enabled = true;  s1.target = pid::PARTICLE_SIZE; s1.beatsPerCycle = 0.5f; s1.shape = supernova::LfoShape::Triangle; s1.depth = 0.75f;
    auto& s2 = bank.slot (2); s2.enabled = false; s2.target = pid::GLOW;   s2.beatsPerCycle = 2.0f;  s2.shape = supernova::LfoShape::Square;     s2.depth = 0.30f;
    auto& s3 = bank.slot (3); s3.enabled = true;  s3.target = pid::ROTATE; s3.beatsPerCycle = 4.0f;  s3.shape = supernova::LfoShape::SampleHold; s3.depth = 0.90f;
    s0.phaseOffset = 0.25f;                       // PHASE a 90°
    s1.bipolar = false;                           // fila UNI
    s3.freeHz = true; s3.hz = 3.5f;               // modo libre en Hz → aparece el slider de frecuencia

    supernova::LfoPanel panel (bank);
    panel.setSize (w, h);

    const auto img = panel.createComponentSnapshot (panel.getLocalBounds(), true, 2.0f);
    REQUIRE (img.isValid());
    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
}

TEST_CASE ("uisnap: panel de LFOs con strip de formas → /tmp/snv-lfo-panel*.png", "[supernova][.uisnap]")
{
    dumpLfo (1120, 640, "/tmp/snv-lfo-panel.png");        // app-body size
    dumpLfo (1680, 900, "/tmp/snv-lfo-panel-wide.png");   // wide window → content should cap + centre
}
