// [telescope][tira] — los nombres de las trece lentes entran ENTEROS en la tira, en los seis idiomas y en S/M/L
// (F2b de la 0.2). La auditora vio, en castellano a M, «CORRELACIÓN POR BAN…» y «ESPECTROGRAMA ESTÉ…»: con el
// piso de 11 px de la F2 los nombres largos dejaron de entrar en un renglón. Ahora un nombre que no entra va en
// dos (LensStrip::linesFor), sin ensanchar la tira ni achicar la letra.
//
// Se mide con la sonda de [contraste] (TestProbe.h): cada tanda de glifos que llega al contexto de dibujo, con
// su texto y su caja. Y se compara contra el nombre que la fila TIENE que decir (strings::get), no contra una
// elipsis: así también se ve un recorte silencioso, el de `drawText (…, false)`, que pierde glifos sin avisar.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <memory>
#include <vector>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestProbe.h"
#include "lenses/Strings.h"
#include "ui/LensStrip.h"

using namespace telescope::test::probe;

namespace
{
telescope::LensStrip* stripIn (juce::Component& c)
{
    for (auto* child : c.getChildren())
    {
        if (auto* s = dynamic_cast<telescope::LensStrip*> (child)) return s;
        if (auto* s = stripIn (*child)) return s;
    }
    return nullptr;
}

juce::String withoutSpaces (const juce::String& s) { return s.removeCharacters (" "); }
}

TEST_CASE ("telescope: los nombres de la tira entran enteros en los seis idiomas y en S/M/L", "[telescope][tira]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    constexpr float kScale = 2.0f;

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    auto* strip = stripIn (*ed);
    REQUIRE (strip != nullptr);

    GlyphNames names;
    struct { Zoom z; const char* n; } const zooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };
    int rows = 0, bad = 0, twoLines = 0;

    for (const auto& lang : telescope::strings::availableLanguages())
    {
        telescope::strings::setLanguage (proc.apvts.state, lang);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        REQUIRE (tel->stripLanguage() == lang);

        for (const auto& z : zooms)
        {
            tel->applyZoom (z.z);
            std::vector<GlyphRun> runs;
            paintEditor (*ed, ProbeContext::Mode::all, -1, &runs, &names, nullptr, kScale);

            for (int i = 0; i < telescope::kNumLenses; ++i)
            {
                const auto rowBox = ed->getLocalArea (strip, strip->rowBounds (i)) * kScale;
                const auto expected = telescope::strings::get (telescope::LensStrip::keyFor (i), lang);

                juce::String shown;
                int lines = 0;
                bool outside = false, ellipsis = false;
                for (const auto& run : runs)
                {
                    if (! rowBox.contains (run.box.getCentre())) continue;
                    ++lines;
                    shown << run.text;
                    ellipsis = ellipsis || run.text.contains (juce::String::fromUTF8 ("\xe2\x80\xa6")) || run.text.endsWith ("...");
                    // La caja del texto dentro de su fila (medio píxel de tolerancia por el suavizado).
                    outside = outside || run.box.getX() < rowBox.getX() - 0.5f || run.box.getRight() > rowBox.getRight() + 0.5f
                                      || run.box.getY() < rowBox.getY() - 0.5f || run.box.getBottom() > rowBox.getBottom() + 0.5f;
                }

                const bool whole = withoutSpaces (shown) == withoutSpaces (expected);
                ++rows;
                twoLines += lines == 2 ? 1 : 0;
                if (! whole || ellipsis || outside || lines < 1 || lines > 2)
                {
                    ++bad;
                    std::printf ("TIRA lang=%s size=%s fila %2d  espera \"%s\"  muestra \"%s\" (%d renglones)%s%s\n",
                                 lang.toRawUTF8(), z.n, i, expected.toRawUTF8(), shown.toRawUTF8(), lines,
                                 ellipsis ? "  <-- ELIPSIS" : "", outside ? "  <-- FUERA DE SU FILA" : "");
                }
                else if (lines == 2 && z.z == Zoom::medium)
                {
                    std::printf ("TIRA_DOS_RENGLONES lang=%s fila %2d \"%s\"\n", lang.toRawUTF8(), i, expected.toRawUTF8());
                }
                CHECK (whole);
                CHECK_FALSE (ellipsis);
                CHECK_FALSE (outside);
                CHECK (lines >= 1);
                CHECK (lines <= 2);
            }
        }
    }
    std::printf ("TIRA total: %d filas (6 idiomas x S/M/L x 13), %d mal, %d en dos renglones\n", rows, bad, twoLines);

    telescope::strings::setLanguage (proc.apvts.state, "en");
    ed.reset();
    proc.releaseResources();
}
