// [telescope][contraste] — ¿SE LEE? El contraste de CADA texto contra el fondo que tiene detrás, medido en
// la imagen y no a ojo (F2 de la 0.2, «que se pueda leer»).
//
// Tres usuarios dijeron lo mismo con palabras distintas: TELESCOPE no se lee bien. Uno marcó los
// números de los ejes, «ref. ±3 dB» y las frecuencias de TONAL BALANCE: gris oscuro chico sobre verde
// oscuro. El criterio es el de WCAG 2.x para texto chico: 4.5:1 o más, entre el píxel del texto y el píxel
// del fondo que tiene detrás.
//
// CÓMO SE MIDE. Se pinta el editor REAL (el que arma el host) sobre una imagen, a través de un contexto de
// dibujo que envuelve al de verdad (ProbeContext) y ve pasar cada tanda de glifos: dónde cae, de qué color
// y de qué tamaño. Con eso se pinta tres veces:
//
//   B    el editor entero SIN NINGÚN TEXTO: el fondo real (degradados, rejillas, el verde de la banda de
//        referencia, el dato) tal como queda debajo de cada rótulo;
//   C_i  el editor con UNA SOLA tanda de texto, la i: los píxeles donde C_i y B difieren son los del
//        rótulo i y de nadie más (dos rótulos pegados no se confunden).
//
// De los píxeles del rótulo se toma el cuarto MÁS CUBIERTO (el núcleo del glifo, no el borde suavizado) y
// en cada uno se calcula el contraste WCAG entre C_i y B en ese mismo píxel. Se informa el percentil 10
// ("medido"): el peor núcleo típico del rótulo, no un píxel suelto. Aparte, "nominal": el color declarado
// del texto compuesto sobre ese fondo, que es lo que daría un glifo infinitamente grueso — si el medido
// queda muy por debajo del nominal, el problema es el tamaño o el peso, no el color.
//
// La capa estática de las lentes (donde viven los ejes) se hornea con un Graphics propio sobre otra imagen,
// que esta sonda no vería: por eso se pinta en modo directo (Lens::setDirectPaintForTest).
//
// Se mide a escala 2 (una pantalla Retina, donde el núcleo de un glifo de 8 px llega a cubrirse entero) en
// S/M/L, con 4 s de ruido rosa adentro. El resultado va a stdout (una línea por rótulo) y, si se pide con
// OVNI_CONTRASTE_CSV=<ruta>, a un CSV para las tablas del reporte.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestProbe.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "lenses/LensIds.h"
#include "lenses/Look.h"
#include "lenses/TonalBalanceLens.h"
#include "lenses/VerdictLens.h"

namespace
{
constexpr double kSr = 48000.0;
constexpr double kMinContrast = 4.5;   // WCAG 2.x AA, texto chico

// La sonda (WCAG, ProbeContext, paintEditor, measureRun) vive en TestProbe.h.
}  // namespace (se reabre abajo)
using namespace telescope::test::probe;
namespace
{
constexpr float kScale = 2.0f;

// Una escena por lente: la lente elegida antes de abrir el editor, 4 s de rosa y el motor digerido.
struct Scene
{
    telescope::TelescopeProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    telescope::TelescopeEditor* tel = nullptr;

    explicit Scene (telescope::LensId id)
    {
        proc.prepareToPlay (kSr, 512);
        auto* lensParam = proc.apvts.getParameter ("lens");
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) id));
        ed.reset (proc.createEditor());
        tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        const float peak = std::pow (10.0f, -18.0f / 20.0f);
        const auto pushed = telescope::test::pushExact (proc, 4 * (long long) kSr, kSr,
                                                        [&] (juce::AudioBuffer<float>& buf, int k)
        {
            for (int i = 0; i < k; ++i) { buf.setSample (0, i, peak * a.next()); buf.setSample (1, i, peak * b.next()); }
        });
        telescope::test::waitDigested (proc, pushed, kSr);
        tel->pumpLensFrames (30);
    }
    ~Scene() { ed.reset(); proc.releaseResources(); }
};

juce::String hex (juce::Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }

// El CONTROL del instrumento: un rótulo de color conocido sobre un fondo conocido.
struct KnownLabel : juce::Component
{
    juce::Colour ink, paper;
    float        height;
    KnownLabel (juce::Colour i, juce::Colour p, float h) : ink (i), paper (p), height (h) { setSize (200, 40); }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (paper);
        g.setColour (ink);
        g.setFont (juce::FontOptions (height));
        g.drawText ("Control 1234", getLocalBounds(), juce::Justification::centred, false);
    }
};

double measureKnown (juce::Colour ink, juce::Colour paper, float height)
{
    KnownLabel k (ink, paper, height);
    std::vector<GlyphRun> runs;
    paintEditor (k, ProbeContext::Mode::all, -1, &runs, nullptr);
    const auto b = paintEditor (k, ProbeContext::Mode::none, -1, nullptr, nullptr);
    REQUIRE (runs.size() == 1);
    const auto c = paintEditor (k, ProbeContext::Mode::only, runs[0].index, nullptr, nullptr);
    return measureRun (b, c, runs[0]).measured;
}
}

// El instrumento, contra la cuenta a mano: #777777 sobre negro es 4.69:1 y #555555 sobre negro 2.87:1. Un
// rótulo grande (20 px) tiene que dar el número de la cuenta; si el instrumento midiera bordes suavizados o
// el fondo equivocado, daría otro.
TEST_CASE ("telescope: el instrumento de contraste da la cuenta de WCAG en un caso conocido", "[telescope][contraste]")
{
    const double pass = measureKnown (juce::Colour (0xff777777), juce::Colour (0xff000000), 20.0f);
    const double fail = measureKnown (juce::Colour (0xff555555), juce::Colour (0xff000000), 20.0f);
    const double exactPass = contrast (juce::Colour (0xff777777), juce::Colour (0xff000000));
    const double exactFail = contrast (juce::Colour (0xff555555), juce::Colour (0xff000000));
    std::printf ("CONTRASTE_CONTROL #777 sobre negro: medido %.3f, cuenta %.3f  ·  #555: medido %.3f, cuenta %.3f\n",
                 pass, exactPass, fail, exactFail);
    CHECK (std::abs (pass - exactPass) < 0.05);
    CHECK (std::abs (fail - exactFail) < 0.05);
    CHECK (pass >= kMinContrast);
    CHECK (fail < kMinContrast);
}

TEST_CASE ("telescope: todo texto de las lentes contrasta 4.5:1 con su fondo real", "[telescope][contraste]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    struct { Zoom z; const char* n; } const zooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };

    std::unique_ptr<juce::FileOutputStream> csv;
    const auto csvPath = juce::SystemStats::getEnvironmentVariable ("OVNI_CONTRASTE_CSV", {});
    if (csvPath.isNotEmpty())
    {
        juce::File (csvPath).deleteFile();
        csv = std::make_unique<juce::FileOutputStream> (juce::File (csvPath));
        csv->writeText ("tema,lente,tamano,zona,indice,texto,color,fondo,alto_diseno_px,medido,nominal,pixeles,borde\n", false, false, nullptr);
    }

    telescope::Lens::setDirectPaintForTest (true);
    GlyphNames names;
    int worstLensRuns = 0, totalLensRuns = 0;

    for (int li = 0; li < telescope::kNumLenses; ++li)
    {
        const auto id = (telescope::LensId) li;
        Scene sc (id);

      for (const auto th : { telescope::look::Theme::dark, telescope::look::Theme::light })
      {
        // F2 hito 4: la MISMA escena en los dos temas. En claro se exige además el MARCO (header, tira):
        // lo pinta el sello con las tintas que le pasa TELESCOPE. En oscuro el marco es el del sello, igual
        // para los ocho plugins, y se informa sin exigirse.
        telescope::look::setTheme (th);
        sc.tel->applyTheme();
        sc.tel->pumpLensFrames (30);
        const bool light = th == telescope::look::Theme::light;
        const char* tn = light ? "claro" : "oscuro";

        for (const auto& z : zooms)
        {
            sc.tel->applyZoom (z.z);
            sc.tel->pumpLensFrames (3);
            const float zoomF = ovni::PluginEditorBase::zoomFactor (z.z);
            const auto  lensBox = sc.ed->getLocalArea (sc.tel->activeLens(),
                                                       sc.tel->activeLens()->getLocalBounds().toFloat()) * kScale;
            // F2b de la 0.2: el borde de abajo de la lista de VERDICT, en píxeles de la imagen (ver «el borde de
            // una lista con scroll», abajo). Las otras lentes no tienen lista: el borde queda fuera de todo.
            float listBottom = 1.0e9f;
            if (auto* v = dynamic_cast<telescope::VerdictLens*> (sc.tel->activeLens()))
                listBottom = (sc.ed->getLocalArea (v, v->listArea().toFloat()) * kScale).getBottom();

            std::vector<GlyphRun> runs;
            int countAll = 0, countNone = 0;
            const auto all = paintEditor (*sc.ed, ProbeContext::Mode::all, -1, &runs, &names, &countAll);
            const auto pngDir = juce::SystemStats::getEnvironmentVariable ("OVNI_CONTRASTE_PNG_DIR", {});
            if (pngDir.isNotEmpty())
            {
                auto f = juce::File (pngDir).getChildFile (juce::String (telescope::lensName (id)).replace (" ", "_")
                                                           + "_" + z.n + "_" + tn + ".png");
                f.deleteFile();
                juce::FileOutputStream os (f);
                juce::PNGImageFormat().writeImageToStream (all, os);
            }
            const auto b = paintEditor (*sc.ed, ProbeContext::Mode::none, -1, nullptr, nullptr, &countNone);
            REQUIRE (countAll == countNone);   // el orden de las tandas es el mismo en cada pintada

            double worst = 1.0e9, worstFrame = 1.0e9;
            int below = 0, inLens = 0, small = 0, cut = 0, overlaps = 0, clippedByEdge = 0, frameBelow = 0, inFrame = 0;
            std::vector<const GlyphRun*> seen;
            for (const auto& run : runs)
            {
                const auto c  = paintEditor (*sc.ed, ProbeContext::Mode::only, run.index, nullptr, nullptr);
                const auto m  = measureRun (b, c, run);
                if (m.pixels == 0) continue;   // recortada o fuera de la imagen: no se ve
                const bool lens = lensBox.intersects (run.box);
                const float design = run.deviceHeight / (kScale * zoomF);
                juce::String flags;
                // El BORDE DE UNA LISTA CON SCROLL (VERDICT): el último renglón visible queda cortado por el
                // marco de la lista y se ve menos del 30 % de su alto (un renglón entero de sólo minúsculas o un ✓
                // dan 44 % o más). No es un rótulo que se lea mal: es el renglón que sigue, asomado. Se informa
                // aparte y no entra en las cuentas.
                //
                // F2b de la 0.2: además, por GEOMETRÍA. Una tanda cuya caja cruza el borde de abajo de la lista es el
                // renglón que asoma, se vea lo que se vea de ella. El 30 % de tinta solo no alcanzaba: con el pie en
                // su propio renglón la lista quedó 10 px más corta, asomó otro renglón, y su ✓ mostraba más del 30 %
                // pero sólo sus trazos finos (medía 4.31:1; el mismo ✓ entero, 5.74).
                const bool crossesListEdge = run.box.getY() < listBottom && run.box.getBottom() > listBottom + 0.5f;
                const bool edge = lens && ((float) m.inkHeight < 0.3f * run.box.getHeight() || crossesListEdge);
                if (edge) { ++clippedByEdge; flags << "  (recortado por el borde: se ve " << m.inkHeight << " de "
                                                   << juce::roundToInt (run.box.getHeight()) << " px)"; }
                if (lens && ! edge)
                {
                    ++inLens;
                    worst = std::min (worst, m.measured);
                    if (m.measured < kMinContrast) { ++below; flags << "  <-- BAJO"; }
                    // El piso de tamaño (Look.h, kMinTextPx): lo que llega al contexto, no lo que se pidió.
                    if (design < telescope::look::kMinTextPx - 0.05f) { ++small; flags << "  <-- CHICO"; }
                    // Un texto que no entra en su caja: JUCE lo corta con una elipsis.
                    if (run.text.contains (juce::String::fromUTF8 ("\xe2\x80\xa6")) || run.text.endsWith ("..."))
                    { ++cut; flags << "  <-- CORTADO"; }
                    // Dos rótulos DISTINTOS encimados (un glow son varias pasadas del mismo texto en el mismo lugar).
                    const auto core = run.box.reduced (1.5f * kScale);
                    for (const auto* o : seen)
                        if (o->text != run.text && core.intersects (o->box.reduced (1.5f * kScale)))
                        { ++overlaps; flags << "  <-- ENCIMA de \"" << o->text << "\""; break; }
                    seen.push_back (&run);
                }
                if (! lens)
                {
                    ++inFrame;
                    worstFrame = std::min (worstFrame, m.measured);
                    if (light && m.measured < kMinContrast) { ++frameBelow; flags << "  <-- BAJO (marco)"; }
                }
                std::printf ("CONTRASTE tema=%-6s lens=%-18s size=%s zona=%-5s i=%3d alto=%5.2f px  medido=%5.2f  "
                             "nominal=%5.2f  color=%s fondo=%s  \"%s\"%s\n",
                             tn, telescope::lensName (id), z.n, lens ? "lente" : "marco", run.index, (double) design,
                             m.measured, m.nominal, hex (run.colour).toRawUTF8(), hex (m.background).toRawUTF8(),
                             run.text.toRawUTF8(), flags.toRawUTF8());
                if (csv != nullptr)
                    csv->writeText (juce::String (tn) + "," + telescope::lensName (id) + "," + z.n + ","
                                        + (lens ? "lente" : "marco") + "," + juce::String (run.index) + ",\""
                                        + run.text.replace ("\"", "'") + "\"," + hex (run.colour) + ","
                                        + hex (m.background) + "," + juce::String (design, 2) + ","
                                        + juce::String (m.measured, 2) + "," + juce::String (m.nominal, 2) + ","
                                        + juce::String (m.pixels) + "," + (edge ? "borde" : "") + "\n",
                                    false, false, nullptr);
            }

            totalLensRuns += inLens;
            worstLensRuns += below;
            std::printf ("CONTRASTE_LENTE tema=%-6s lens=%-18s size=%s  rotulos=%3d  bajo 4.5=%3d  peor=%5.2f  "
                         "chicos=%d  cortados=%d  encimados=%d  recortados por el borde=%d  ·  marco: %d rotulos, "
                         "peor=%5.2f%s\n",
                         tn, telescope::lensName (id), z.n, inLens, below, worst, small, cut, overlaps, clippedByEdge,
                         inFrame, worstFrame, light ? (frameBelow > 0 ? "  <-- MARCO BAJO" : "") : " (no se exige)");
            CHECK (inLens > 0);        // el control: la sonda ve texto adentro de la lente
            CHECK (below == 0);
            CHECK (small == 0);
            CHECK (cut == 0);
            CHECK (overlaps == 0);
            if (light) CHECK (frameBelow == 0);
        }
      }
      telescope::look::setTheme (telescope::look::Theme::dark);
    }
    telescope::Lens::setDirectPaintForTest (false);
    std::printf ("CONTRASTE resumen: %d de %d rotulos de las lentes por debajo de %.1f:1\n", worstLensRuns,
                 totalLensRuns, kMinContrast);
}

// ========================================================================================================
// F4 de la 0.2 (T6) · LA TIRA DE LA REFERENCIA. La escena de arriba no carga referencia, así que la tira no
// aparece en ella: acá se arma TONAL BALANCE con una referencia y un tramo elegido, y se miden los rótulos que
// caen ADENTRO de la tira (el tramo, «ref · 0:06–0:11», y la ayuda cuando entra), con la misma sonda y el
// mismo criterio: 4.5:1, el piso de 11 px y nada cortado. La tira es una pantalla de datos (D-109): en claro
// conserva la tinta del oscuro, y eso también se mide.
TEST_CASE ("telescope: los rotulos de la tira de TONAL BALANCE contrastan 4.5:1", "[telescope][contraste]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    struct { Zoom z; const char* n; } const zooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };

    // 12 s de rosa estéreo: la referencia. El tramo, [6, 11).
    auto pa = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedA);
    auto pb = std::make_shared<telescope::test::Pink> (telescope::test::kPinkSeedB);
    const auto wav = telescope::test::writeWav ("contraste_tira_ref.wav", kSr, 2, (juce::int64) (12.0 * kSr),
                                                [pa, pb] (juce::int64)
                                                { return std::pair<float, float> { 0.25f * pa->next(), 0.25f * pb->next() }; });

    Scene sc (telescope::LensId::tonalBalance);
    sc.proc.loadReference (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! sc.proc.referenceBusy(); }, 60000));
    sc.proc.setReferenceRange (6.0, 11.0);
    REQUIRE (telescope::test::waitUntil ([&] { return ! sc.proc.referenceBusy(); }, 60000));
    auto* tonal = dynamic_cast<telescope::TonalBalanceLens*> (sc.tel->activeLens());
    REQUIRE (tonal != nullptr);
    REQUIRE (telescope::test::waitUntil ([&] { sc.tel->pumpLensFrames (1); return tonal->waveformReady(); }, 30000));

    telescope::Lens::setDirectPaintForTest (true);
    GlyphNames names;
    for (const auto th : { telescope::look::Theme::dark, telescope::look::Theme::light })
    {
        telescope::look::setTheme (th);
        sc.tel->applyTheme();
        tonal = dynamic_cast<telescope::TonalBalanceLens*> (sc.tel->activeLens());   // el tema rehace la lente
        REQUIRE (tonal != nullptr);
        REQUIRE (telescope::test::waitUntil ([&] { sc.tel->pumpLensFrames (1); return tonal->waveformReady(); }, 30000));
        const char* tn = th == telescope::look::Theme::light ? "claro" : "oscuro";

        for (const auto& z : zooms)
        {
            sc.tel->applyZoom (z.z);
            sc.tel->pumpLensFrames (3);
            REQUIRE (tonal->stripVisible());
            const float zoomF = ovni::PluginEditorBase::zoomFactor (z.z);
            const auto  strip = sc.ed->getLocalArea (tonal, tonal->stripArea().toFloat()) * kScale;

            std::vector<GlyphRun> runs;
            int countAll = 0, countNone = 0;
            paintEditor (*sc.ed, ProbeContext::Mode::all, -1, &runs, &names, &countAll);
            const auto b = paintEditor (*sc.ed, ProbeContext::Mode::none, -1, nullptr, nullptr, &countNone);
            REQUIRE (countAll == countNone);

            int inStrip = 0, below = 0, small = 0, cut = 0;
            double worst = 1.0e9;
            for (const auto& run : runs)
            {
                if (! strip.contains (run.box.getCentre())) continue;
                const auto c = paintEditor (*sc.ed, ProbeContext::Mode::only, run.index, nullptr, nullptr);
                const auto m = measureRun (b, c, run);
                if (m.pixels == 0) continue;
                ++inStrip;
                worst = std::min (worst, m.measured);
                const float design = run.deviceHeight / (kScale * zoomF);
                if (m.measured < kMinContrast) ++below;
                if (design < telescope::look::kMinTextPx - 0.05f) ++small;
                if (run.text.contains (juce::String::fromUTF8 ("\xe2\x80\xa6")) || run.text.endsWith ("...")) ++cut;
                std::printf ("CONTRASTE_TIRA tema=%-6s size=%s alto=%5.2f px medido=%5.2f nominal=%5.2f color=%s fondo=%s \"%s\"\n",
                             tn, z.n, (double) design, m.measured, m.nominal, hex (run.colour).toRawUTF8(),
                             hex (m.background).toRawUTF8(), run.text.toRawUTF8());
            }
            std::printf ("CONTRASTE_TIRA tema=%-6s size=%s rotulos=%d bajo 4.5=%d peor=%5.2f chicos=%d cortados=%d\n",
                         tn, z.n, inStrip, below, worst, small, cut);
            CHECK (inStrip >= 1);   // el control: la sonda ve el rótulo del tramo
            CHECK (below == 0);
            CHECK (small == 0);
            CHECK (cut == 0);
        }
    }
    telescope::look::setTheme (telescope::look::Theme::dark);
    telescope::Lens::setDirectPaintForTest (false);
}
