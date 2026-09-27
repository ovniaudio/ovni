// [telescope][reacomoda] — VERDICT REACOMODA su texto en vez de recortarlo (F2 de la 0.2, hito 5).
//
// Un usuario (Windows al 125 %, 1344 × 840 puntos): «el veredicto es ilegible». Lo que se ve de VERDICT tiene
// que entrar entero, en los seis idiomas, en S/M/L y a las escalas de Windows (1.25 y 1.5): ningún texto de
// la lente puede salir cortado con elipsis (desde el hito 3 las lentes piden elipsis en vez de perder glifos
// en silencio) ni pasarse del borde de la lente.
//
// Se mide con la sonda de [contraste] (TestProbe.h): cada tanda de glifos que llega al contexto de dibujo,
// con su texto y su caja. La lista de VERDICT tiene scroll: el renglón que asoma cortado al pie de la lista
// es el que sigue, no un recorte (ver ContrastTest.cpp), y se informa aparte.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestProbe.h"
#include "TestSignals.h"
#include "lenses/LensIds.h"
#include "lenses/Look.h"
#include "lenses/Strings.h"
#include "lenses/VerdictLens.h"

using namespace telescope::test::probe;

namespace
{
// F2b de la 0.2 · ¿ENTRABA? Con la misma cuenta que hace JUCE al dibujar un renglón (drawText →
// addCurtailedLineOfText): se arma el renglón recortado al ancho de su caja, sin elipsis, y se cuentan sus
// glifos visibles contra los del renglón entero. Si faltan, el renglón se cortó —con «…» o en silencio—, sin
// importar qué haya pedido el producto (veredicto 99, reparo 4).
int visibleGlyphs (juce::GlyphArrangement& ga)
{
    int n = 0;
    for (int i = 0; i < ga.getNumGlyphs(); ++i) n += ga.getGlyph (i).isWhitespace() ? 0 : 1;
    return n;
}

bool fitsInItsBox (const telescope::look::TextRequest& r)
{
    juce::GlyphArrangement whole, cut;
    whole.addLineOfText (r.font, r.text, 0.0f, 0.0f);
    cut.addCurtailedLineOfText (r.font, r.text, 0.0f, 0.0f, r.box.getWidth(), false);
    return visibleGlyphs (cut) == visibleGlyphs (whole);
}

// Pinta el editor con la sonda y, a la vez, anota cada renglón que VERDICT pidió dibujar.
std::vector<telescope::look::TextRequest> paintAndRecord (juce::Component& ed, std::vector<GlyphRun>& runs,
                                                          GlyphNames& names, float scale)
{
    std::vector<telescope::look::TextRequest> requests;
    telescope::look::textRequestSink() = &requests;
    paintEditor (ed, ProbeContext::Mode::all, -1, &runs, &names, nullptr, scale);
    telescope::look::textRequestSink() = nullptr;
    return requests;
}
}

TEST_CASE ("telescope: VERDICT reacomoda el texto en vez de cortarlo, en los seis idiomas", "[telescope][reacomoda]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    constexpr double kSr = 48000.0;

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    auto* lensParam = proc.apvts.getParameter ("lens");
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::verdict));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    const float peak = std::pow (10.0f, -14.0f / 20.0f);
    const auto pushed = telescope::test::pushExact (proc, 6 * (long long) kSr, kSr, [&] (juce::AudioBuffer<float>& buf, int k)
    {
        for (int i = 0; i < k; ++i) { buf.setSample (0, i, peak * a.next()); buf.setSample (1, i, peak * b.next()); }
    });
    telescope::test::waitDigested (proc, pushed, kSr);

    telescope::Lens::setDirectPaintForTest (true);
    GlyphNames names;
    struct { Zoom z; const char* n; } const zooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };
    int totalCut = 0, totalOut = 0, totalRuns = 0, totalRequests = 0, totalSilent = 0;

    for (const auto& lang : telescope::strings::availableLanguages())
    {
        proc.setVerdictLanguage (lang);
        telescope::strings::setLanguage (proc.apvts.state, lang);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        tel->pumpLensFrames (8);

        for (const auto& z : zooms)
        {
            tel->applyZoom (z.z);
            tel->pumpLensFrames (4);
            for (const float s : { 1.25f, 1.5f })
            {
                const auto lensBox = ed->getLocalArea (tel->activeLens(), tel->activeLens()->getLocalBounds().toFloat()) * s;
                std::vector<GlyphRun> runs;
                const auto requests = paintAndRecord (*ed, runs, names, s);

                // El recorte que no se ve: cada renglón que VERDICT pidió, ¿entraba en su caja?
                int silent = 0;
                for (const auto& r : requests)
                    if (! fitsInItsBox (r))
                    {
                        ++silent;
                        juce::GlyphArrangement whole;
                        whole.addLineOfText (r.font, r.text, 0.0f, 0.0f);
                        std::printf ("REACOMODA lang=%s size=%s escala=%.2f  \"%s\"  <-- NO ENTRA: caja de %.1f, mide %.1f\n",
                                     lang.toRawUTF8(), z.n, (double) s, r.text.toRawUTF8(), (double) r.box.getWidth(),
                                     (double) whole.getBoundingBox (0, -1, false).getWidth());
                    }
                totalRequests += (int) requests.size();
                totalSilent   += silent;
                CHECK (! requests.empty());   // si VERDICT no anota nada, esto no mide nada
                CHECK (silent == 0);

                int cut = 0, out = 0, inLens = 0;
                for (const auto& run : runs)
                {
                    if (! lensBox.intersects (run.box)) continue;   // el marco: no es de VERDICT
                    ++inLens;
                    juce::String why;
                    if (run.text.contains (juce::String::fromUTF8 ("\xe2\x80\xa6")) || run.text.endsWith ("..."))
                    { ++cut; why << "CORTADO"; }
                    // Los bounds del texto dentro de la lente (medio píxel de tolerancia por el suavizado).
                    if (run.box.getX() < lensBox.getX() - 0.5f || run.box.getRight() > lensBox.getRight() + 0.5f)
                    { ++out; why << (why.isEmpty() ? "" : " + ") << "FUERA DE LA LENTE"; }
                    if (why.isNotEmpty())
                        std::printf ("REACOMODA lang=%s size=%s escala=%.2f  \"%s\"  <-- %s\n", lang.toRawUTF8(), z.n,
                                     (double) s, run.text.toRawUTF8(), why.toRawUTF8());
                }
                // Control de que el idioma llegó de verdad: el texto más largo de la lente, una vez por idioma.
                if (z.z == Zoom::medium && s == 1.25f)
                {
                    juce::String longest;
                    for (const auto& run : runs)
                        if (lensBox.intersects (run.box) && run.text.length() > longest.length()) longest = run.text;
                    std::printf ("REACOMODA_MUESTRA lang=%s \"%s\"\n", lang.toRawUTF8(), longest.toRawUTF8());
                }
                totalCut += cut; totalOut += out; totalRuns += inLens;
                std::printf ("REACOMODA_RESUMEN lang=%s size=%s escala=%.2f  textos=%3d  cortados=%d  fuera=%d\n",
                             lang.toRawUTF8(), z.n, (double) s, inLens, cut, out);
                CHECK (inLens > 0);
                CHECK (cut == 0);
                CHECK (out == 0);
            }
        }
    }
    telescope::Lens::setDirectPaintForTest (false);
    std::printf ("REACOMODA total: %d textos de VERDICT, %d cortados, %d fuera de la lente (6 idiomas x S/M/L x 1.25/1.5)\n",
                 totalRuns, totalCut, totalOut);
    std::printf ("REACOMODA pedidos: %d renglones pedidos por VERDICT, %d que no entraban en su caja\n",
                 totalRequests, totalSilent);

    proc.setVerdictLanguage ("en");
    telescope::strings::setLanguage (proc.apvts.state, "en");
    ed.reset();
    proc.releaseResources();
}

// ========================================================================================================
// 1344 × 840 PUNTOS: EL CASO MÍNIMO (un usuario: 1680 × 1050 a 125 % en Windows 11). ¿Qué tamaño abre y entra?
//
// El editor abre en el tamaño guardado (uiZoom en OVNI.settings) y, la primera vez, en M. Antes de ponerse,
// applyZoom lo achica si no entra en el área útil de la pantalla (fitZoomToArea: 24 puntos de margen de ancho
// y 64 de alto para la barra de título y el chrome del host). Se prueba con el área útil de esa pantalla con
// la barra de tareas de Windows (48 px a 125 % ≈ 40 puntos → 1344 × 800) y sin ella.
// ========================================================================================================
TEST_CASE ("telescope: en 1344 x 840 puntos el editor abre entero en M y L se achica para entrar", "[telescope][pantalla]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    constexpr int baseW = 980, baseH = 620;   // TelescopeEditor::setBaseSize

    for (const auto area : { juce::Rectangle<int> (0, 0, 1344, 800), juce::Rectangle<int> (0, 0, 1344, 840) })
    {
        const float s = ovni::PluginEditorBase::fitZoomToArea (ovni::PluginEditorBase::zoomFactor (Zoom::small),  area, baseW, baseH);
        const float m = ovni::PluginEditorBase::fitZoomToArea (ovni::PluginEditorBase::zoomFactor (Zoom::medium), area, baseW, baseH);
        const float l = ovni::PluginEditorBase::fitZoomToArea (ovni::PluginEditorBase::zoomFactor (Zoom::large),  area, baseW, baseH);
        std::printf ("PANTALLA area util %dx%d puntos: S=%.3f (%dx%d)  M=%.3f (%dx%d)  L=%.3f (%dx%d)  ·  M a 125%% = %dx%d px fisicos\n",
                     area.getWidth(), area.getHeight(),
                     (double) s, juce::roundToInt (baseW * s), juce::roundToInt (baseH * s),
                     (double) m, juce::roundToInt (baseW * m), juce::roundToInt (baseH * m),
                     (double) l, juce::roundToInt (baseW * l), juce::roundToInt (baseH * l),
                     juce::roundToInt (baseW * m * 1.25f), juce::roundToInt (baseH * m * 1.25f));
        CHECK (s == ovni::PluginEditorBase::zoomFactor (Zoom::small));    // S entra tal cual
        CHECK (m == ovni::PluginEditorBase::zoomFactor (Zoom::medium));   // M, el que abre la primera vez, entra tal cual
        // L: con la barra de tareas (800 de alto) no entra y se achica; sin ella entra justo (1249 × 839).
        if (area.getHeight() < 839) CHECK (l < ovni::PluginEditorBase::zoomFactor (Zoom::large));
        else                        CHECK (l == ovni::PluginEditorBase::zoomFactor (Zoom::large));
        CHECK (l > m);                                                    // siempre más grande que M
        CHECK (juce::roundToInt (baseW * l) + 24 <= area.getWidth());     // y entra, con el margen del chrome
        CHECK (juce::roundToInt (baseH * l) + 64 <= area.getHeight());
    }
}

// ========================================================================================================
// LA GUARDA de lo de arriba (F2b de la 0.2): [reacomoda] ve el recorte silencioso porque VERDICT dibuja cada
// renglón por look::drawTextLine, que en el runner anota el pedido. Un `g.drawText` directo en VerdictLens.cpp
// no se anotaría y volvería a poder cortar en silencio sin que nada se ponga rojo. Se cuenta en el fuente.
// ========================================================================================================
TEST_CASE ("telescope: VERDICT dibuja cada renglon por look::drawTextLine", "[telescope][reacomoda]")
{
    const auto src = juce::File (juce::String (__FILE__)).getParentDirectory().getParentDirectory()
                         .getChildFile ("source").getChildFile ("lenses").getChildFile ("VerdictLens.cpp");
    REQUIRE (src.existsAsFile());
    const auto text = src.loadFileAsString();
    int direct = 0, routed = 0;
    for (const auto& raw : juce::StringArray::fromLines (text))
    {
        const auto line = raw.upToFirstOccurrenceOf ("//", false, false);   // los comentarios no dibujan
        direct += line.contains ("g.drawText (") || line.contains ("g.drawText(") || line.contains ("drawFittedText") ? 1 : 0;
        routed += line.contains ("look::drawTextLine (") ? 1 : 0;
    }
    std::printf ("REACOMODA guarda: VerdictLens.cpp · %d llamadas por look::drawTextLine · %d g.drawText directos\n",
                 routed, direct);
    CHECK (routed > 0);   // control: el patrón encuentra lo que tiene que encontrar
    CHECK (direct == 0);
}

// ========================================================================================================
// [modo] EL BOTÓN MODO DICE EL MODO ENTERO (F5b de la 0.2, D-126). Tomaba la primera palabra de `mode.live`:
// «EN» en castellano y en francés (que en castellano se lee «inglés»), «AO» en portugués y «DAL» en italiano.
// Ahora cada idioma tiene su rótulo corto (`mode.live.short`, `mode.file.short`).
//
// Se lee LO QUE SE DIBUJA: el renglón que VERDICT pide justo después del rótulo «MODE» de su idioma es el valor del
// botón (paintButton pide el rótulo y después el valor). Tiene que ser el de la tabla de abajo —escrita a mano, es
// la del prompt 111— y ENTRAR en su caja, con la misma cuenta que [reacomoda], en S/M/L a 100, 125 y 150 %, en los
// seis idiomas y en los dos modos.
// ========================================================================================================
TEST_CASE ("telescope: el boton MODO de VERDICT dice el modo entero y entra, en los seis idiomas", "[telescope][modo]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    struct Expected { const char* lang; const char* live; const char* file; };
    const Expected expected[] = {
        { "en", "LIVE",      "FILE"    },
        { "es", "EN VIVO",   "ARCHIVO" },
        { "pt", "AO VIVO",   "ARQUIVO" },
        { "fr", "EN DIRECT", "FICHIER" },
        { "de", "LIVE",      "DATEI"   },
        { "it", "DAL VIVO",  "FILE"    },
    };
    const auto expectedFor = [&] (const juce::String& lang, int mode) -> juce::String
    {
        for (const auto& e : expected)
            if (lang == e.lang) return juce::String::fromUTF8 (mode == telescope::TelescopeProcessor::verdictFile ? e.file : e.live);
        return {};
    };

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    auto* lensParam = proc.apvts.getParameter ("lens");
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::verdict));
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

    telescope::Lens::setDirectPaintForTest (true);
    GlyphNames names;
    struct { Zoom z; const char* n; } const zooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };
    int langs = 0, checked = 0;

    for (const auto& lang : telescope::strings::availableLanguages())
    {
        ++langs;
        const auto mine = expectedFor (lang, 0);
        INFO ("idioma " << lang);
        REQUIRE (mine.isNotEmpty());   // un idioma nuevo tiene que sumar su fila a la tabla de arriba
        proc.setVerdictLanguage (lang);
        telescope::strings::setLanguage (proc.apvts.state, lang);
        const auto modeLabel = juce::String::fromUTF8 (telescope::Verdict::translate ("ui.mode", lang.toRawUTF8()).c_str());

        for (const int mode : { (int) telescope::TelescopeProcessor::verdictLive, (int) telescope::TelescopeProcessor::verdictFile })
        {
            proc.setVerdictMode (mode);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            const auto want = expectedFor (lang, mode);
            const char* modeName = mode == telescope::TelescopeProcessor::verdictFile ? "archivo" : "vivo";
            INFO ("modo " << modeName);
            CHECK (telescope::VerdictLens::modeButtonText (mode, lang) == want);

            float tightest = 1.0e9f; juce::String tightestAt;
            for (const auto& z : zooms)
            {
                tel->applyZoom (z.z);
                tel->pumpLensFrames (3);
                for (const float s : { 1.0f, 1.25f, 1.5f })
                {
                    std::vector<GlyphRun> runs;
                    const auto requests = paintAndRecord (*ed, runs, names, s);
                    int at = -1;
                    for (int i = 0; i + 1 < (int) requests.size(); ++i)
                        if (requests[(size_t) i].text == modeLabel) at = i + 1;
                    INFO ("tamaño " << z.n << " escala " << s);
                    REQUIRE (at >= 0);   // si no aparece el rótulo MODE, esto no mide nada
                    const auto& value = requests[(size_t) at];
                    CHECK (value.text == want);
                    CHECK (fitsInItsBox (value));
                    juce::GlyphArrangement whole;
                    whole.addLineOfText (value.font, value.text, 0.0f, 0.0f);
                    const float margin = value.box.getWidth() - whole.getBoundingBox (0, -1, false).getWidth();
                    if (margin < tightest) { tightest = margin; tightestAt = juce::String (z.n) + "@" + juce::String (s, 2); }
                    ++checked;
                }
            }
            std::printf ("MODO lang=%s modo=%-7s «%s» · lo más justo: %s, sobran %.1f px\n", lang.toRawUTF8(), modeName,
                         telescope::VerdictLens::modeButtonText (mode, lang).toRawUTF8(), tightestAt.toRawUTF8(), (double) tightest);
        }
    }
    telescope::Lens::setDirectPaintForTest (false);
    std::printf ("MODO total: %d idiomas, %d botones medidos (2 modos x S/M/L x 1/1.25/1.5)\n", langs, checked);
    CHECK (langs == 6);
    CHECK (checked == langs * 2 * 3 * 3);

    proc.setVerdictMode (telescope::TelescopeProcessor::verdictLive);
    proc.setVerdictLanguage ("en");
    telescope::strings::setLanguage (proc.apvts.state, "en");
    ed.reset();
    proc.releaseResources();
}
