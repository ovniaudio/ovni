// [telescope][ancla] — T2 (F2 de la 0.2): el EDITOR le pone el ancla a la lente, y la lente cae en un píxel físico
// entero (F2b de la 0.2, veredicto 99, reparo 4).
//
// El test de tiempo de T2 ([editorbudget], «a escala no entera la lente en su lugar cuesta lo que en el origen»)
// le pone el ancla a la lente él mismo (LensAtOriginPainter), así que si `showLens` dejara de llamar a
// `setPixelAnchor`, ese test seguía verde: lo midió la 102 comentando la llamada. Acá no se toca la lente: se
// arma el editor de verdad, se eligen las trece lentes por el parámetro —como lo hace el host— y se le pregunta
// a cada una dónde cae su origen a las escalas de Windows. Sin el ancla, `pixelAlignment` no corre nada y el
// origen queda en medio píxel (M a 1.25: x = 182 × 1.25 = 227.5).
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "lenses/LensIds.h"
#include "ui/ThemePreference.h"

namespace
{
// El origen de la lente en píxeles físicos del editor, con el corrimiento que ELLA misma se aplica al pintar.
juce::Point<float> physicalOrigin (juce::Component& ed, telescope::Lens& lens, float scale)
{
    const auto o = ed.getLocalPoint (&lens, juce::Point<float>());
    const auto u = ed.getLocalPoint (&lens, juce::Point<float> (1.0f, 0.0f)) - o;
    const float chain = u.getDistanceFromOrigin();   // el zoom S/M/L del Canvas
    const auto  d = lens.pixelAlignment (scale * chain);
    return (o + d * chain) * scale;
}

bool whole (float v) { return std::abs (v - std::round (v)) < 1.0f / 64.0f; }
}

TEST_CASE ("telescope: el editor ancla cada lente y la lente cae en un pixel fisico entero", "[telescope][ancla]")
{
    using Zoom = ovni::PluginEditorBase::Zoom;
    const auto settings = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("ovni-telescope-tests").getChildFile ("ancla.settings");
    settings.getParentDirectory().createDirectory();
    settings.deleteFile();
    telescope::ThemePreference::setFileForTest (settings);
    telescope::look::setTheme (telescope::look::Theme::dark);

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);

    int checked = 0, fractionalBefore = 0, off = 0;
    const auto checkActive = [&] (const char* when)
    {
        auto* lens = tel->activeLens();
        REQUIRE (lens != nullptr);
        for (const auto z : { Zoom::medium, Zoom::large })
        {
            tel->applyZoom (z);
            for (const float s : { 1.25f, 1.5f })
            {
                // Sin corrimiento, ¿caería en medio píxel? (si nunca cae, el caso no mide nada)
                const auto o = ed->getLocalPoint (lens, juce::Point<float>()) * s;
                const bool fractional = ! whole (o.x) || ! whole (o.y);
                fractionalBefore += fractional ? 1 : 0;

                const auto p = physicalOrigin (*ed, *lens, s);
                ++checked;
                if (! whole (p.x) || ! whole (p.y))
                {
                    ++off;
                    std::printf ("ANCLA %s lente %s zoom %s escala %.2f: origen fisico (%.3f, %.3f) — NO cae entero\n",
                                 when, lens->name().toRawUTF8(), z == Zoom::medium ? "M" : "L", (double) s,
                                 (double) p.x, (double) p.y);
                }
                CHECK (whole (p.x));
                CHECK (whole (p.y));
            }
        }
    };

    for (int i = 0; i < telescope::kNumLenses; ++i)
    {
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) i));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        REQUIRE (tel->activeLens() != nullptr);
        REQUIRE ((int) tel->activeLens()->id() == i);
        checkActive ("al elegirla");
    }

    // Cambiar de tema REHACE la lente (rebuildLens): la nueva también tiene que nacer anclada.
    tel->toggleTheme();
    checkActive ("despues de cambiar de tema");
    tel->toggleTheme();

    std::printf ("ANCLA %d casos (13 lentes x M/L x 1.25/1.5, y la rehecha por el tema) · %d caian en medio pixel sin "
                 "corrimiento · %d quedaron fuera del pixel entero\n", checked, fractionalBefore, off);
    CHECK (fractionalBefore > 0);   // el caso tiene que tener orígenes fraccionarios, o no prueba nada

    ed.reset();
    proc.releaseResources();
    telescope::ThemePreference::setFileForTest ({});
    telescope::look::setTheme (telescope::look::Theme::dark);
    settings.deleteFile();
}
