// [telescope][eyepiece] — T8 de la 0.2: la tarjeta que presenta a EYEPIECE (D-79, D-113). Sólo en la Mac.
//
//   T8[una-vez]    aparece la primera vez que se abre el editor; con «Cerrar» no vuelve nunca más.
//   T8[principal]  el botón principal también cuenta como vista; sin EYEPIECE abre la página, con EYEPIECE la app.
//   T8[deteccion]  el botón cambia según la detección (reemplazada en el test), en en y es.
//   T8[runner]     en el runner arranca APAGADA: las fotos y los tiempos de siempre no la ven.
//   T8[windows]    en Windows no existe (ni se compila ni se muestra). Lleva [tema] para correr en el paso
//                  estricto del CI de Windows.
//   T8[uisnap]     las fotos de la tarjeta, en en y es, en S/M/L (los dos temas los da la corrida).
// El lock entre procesos de OVNI.settings (otro producto escribiendo a la vez) está en ThemeTest.cpp, con el
// escritor de la F2b: [telescope][eyepiece][lock].
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "lenses/LensIds.h"
#include "lenses/Strings.h"
#include "ui/EyepieceIntro.h"
#include "ui/ThemePreference.h"

namespace
{
constexpr double kSr = 48000.0;

// Todos los casos: un OVNI.settings propio (nunca el de quien corre los tests) y la tarjeta prendida; al salir,
// todo como estaba, así el resto de la suite la sigue viendo apagada.
struct IntroScope
{
    juce::File file;
    IntroScope()
    {
        file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni-telescope-tests")
                   .getChildFile ("eyepiece-" + juce::String (juce::Random::getSystemRandom().nextInt64()) + ".settings");
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        telescope::ThemePreference::setFileForTest (file);
       #if TELESCOPE_HAS_EYEPIECE_INTRO
        telescope::EyepieceIntro::enabledForTest() = true;
        telescope::EyepieceIntro::detectorForTest() = [] { return juce::File(); };
       #endif
    }
    ~IntroScope()
    {
       #if TELESCOPE_HAS_EYEPIECE_INTRO
        telescope::EyepieceIntro::enabledForTest() = false;
        telescope::EyepieceIntro::detectorForTest() = nullptr;
        telescope::EyepieceIntro::launcherForTest() = nullptr;
       #endif
        telescope::ThemePreference::setFileForTest ({});
        file.deleteFile();
    }
};

juce::Component* findChild (juce::Component& root, const juce::String& name)
{
    if (root.getName() == name) return &root;
    for (auto* c : root.getChildren())
        if (auto* f = findChild (*c, name)) return f;
    return nullptr;
}
}

// ============================================================================================ T8[windows]
TEST_CASE ("telescope: T8 · la tarjeta de EYEPIECE existe sólo en la Mac", "[telescope][eyepiece][tema]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    IntroScope scope;   // prendida (en la Mac): si en Windows existiera, acá aparecería
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* child = findChild (*ed, "eyepieceIntro");
    std::printf ("T8[windows] TELESCOPE_HAS_EYEPIECE_INTRO=%d · la tarjeta en el editor: %s\n",
                 (int) TELESCOPE_HAS_EYEPIECE_INTRO, child != nullptr ? "sí" : "no");
   #if JUCE_MAC
    REQUIRE (TELESCOPE_HAS_EYEPIECE_INTRO == 1);
    REQUIRE (child != nullptr);
   #else
    REQUIRE (TELESCOPE_HAS_EYEPIECE_INTRO == 0);
    REQUIRE (child == nullptr);
   #endif
    ed.reset();
    proc.releaseResources();
}

#if TELESCOPE_HAS_EYEPIECE_INTRO
using telescope::EyepieceIntro;
using telescope::EyepieceIntroCard;
using telescope::TelescopeEditor;

// ============================================================================================ T8[runner]
TEST_CASE ("telescope: T8 · en el runner la tarjeta arranca apagada", "[telescope][eyepiece]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    REQUIRE_FALSE (EyepieceIntro::enabledForTest());
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    REQUIRE (tel->eyepieceIntro() == nullptr);
    REQUIRE (findChild (*ed, "eyepieceIntro") == nullptr);
    ed.reset();
    proc.releaseResources();
}

// ============================================================================================ T8[una-vez]
TEST_CASE ("telescope: T8 · aparece una sola vez y Cerrar no la deja volver", "[telescope][eyepiece]")
{
    IntroScope scope;
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    REQUIRE_FALSE (EyepieceIntro::seen());

    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* tel = dynamic_cast<TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        auto* card = tel->eyepieceIntro();
        REQUIRE (card != nullptr);
        REQUIRE (card->isVisible());
        // Adentro del editor, abajo a la derecha de la lente, arriba de ella y sin taparla entera.
        const auto lensB = ed->getLocalArea (tel->activeLens(), tel->activeLens()->getLocalBounds());
        const auto cardB = ed->getLocalArea (card, card->getLocalBounds());
        std::printf ("T8[una-vez] la tarjeta: %s dentro de la lente %s · «%s» / «%s»\n", cardB.toString().toRawUTF8(),
                     lensB.toString().toRawUTF8(), card->primaryLabel().toRawUTF8(), card->closeLabel().toRawUTF8());
        REQUIRE (lensB.contains (cardB));
        REQUIRE (cardB.getWidth() * cardB.getHeight() < lensB.getWidth() * lensB.getHeight() / 3);

        // Cambiar de lente no la esconde: queda arriba de la nueva.
        auto* lensParam = proc.apvts.getParameter ("lens");
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::spectrum));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        REQUIRE (card->isVisible());
        REQUIRE (card->getParentComponent()->getIndexOfChildComponent (card)
                 > card->getParentComponent()->getIndexOfChildComponent (tel->activeLens()));

        card->press (EyepieceIntroCard::close);
        REQUIRE_FALSE (card->isVisible());
    }

    // En el archivo, sólo su clave.
    juce::PropertiesFile::Options o;
    o.applicationName = "OVNI";
    o.filenameSuffix  = "settings";
    juce::PropertiesFile p (scope.file, o);
    std::printf ("T8[una-vez] OVNI.settings después de Cerrar: %s=%s\n", EyepieceIntro::kSeenKey,
                 p.getValue (EyepieceIntro::kSeenKey).toRawUTF8());
    REQUIRE (p.getValue (EyepieceIntro::kSeenKey) == EyepieceIntro::kSeenValue);
    REQUIRE (EyepieceIntro::seen());

    // La segunda vez que se abre el editor (esta instancia u otra), no aparece.
    std::unique_ptr<juce::AudioProcessorEditor> ed2 (proc.createEditor());
    REQUIRE (dynamic_cast<TelescopeEditor*> (ed2.get())->eyepieceIntro() == nullptr);
    telescope::TelescopeProcessor other;
    other.prepareToPlay (kSr, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed3 (other.createEditor());
    REQUIRE (dynamic_cast<TelescopeEditor*> (ed3.get())->eyepieceIntro() == nullptr);
    ed2.reset();
    ed3.reset();
    other.releaseResources();
    proc.releaseResources();
}

// ======================================================================================== T8[principal] y [deteccion]
TEST_CASE ("telescope: T8 · el botón principal cambia según la detección y cuenta como vista", "[telescope][eyepiece]")
{
    struct Case { const char* lang; bool installed; const char* label; };
    const Case cases[] = { { "en", false, "Get EYEPIECE" },  { "en", true, "Open EYEPIECE" },
                           { "es", false, "Bajar EYEPIECE" }, { "es", true, "Abrir EYEPIECE" } };

    const auto fakeApp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni-telescope-tests")
                             .getChildFile ("EYEPIECE.app");
    fakeApp.createDirectory();

    for (const auto& c : cases)
    {
        IntroScope scope;
        EyepieceIntro::detectorForTest() = [&] { return c.installed ? fakeApp : juce::File(); };
        juce::String launched;
        EyepieceIntro::launcherForTest() = [&] (const juce::String& what) { launched = what; };

        EyepieceIntroCard card (c.lang);
        card.setSize (380, card.heightForWidth (380));
        std::printf ("T8[deteccion] %s, EYEPIECE %s: «%s» (esperado «%s»)", c.lang, c.installed ? "instalada" : "no instalada",
                     card.primaryLabel().toRawUTF8(), c.label);
        REQUIRE (card.eyepieceInstalled() == c.installed);
        REQUIRE (card.primaryLabel() == juce::String::fromUTF8 (c.label));

        REQUIRE_FALSE (EyepieceIntro::seen());
        card.press (EyepieceIntroCard::primary);
        std::printf (" · abre «%s»\n", launched.toRawUTF8());
        REQUIRE (launched == (c.installed ? fakeApp.getFullPathName() : juce::String (EyepieceIntro::kGetUrl)));
        REQUIRE (EyepieceIntro::seen());          // el botón principal también cuenta como vista
        REQUIRE_FALSE (card.isVisible());
    }
    fakeApp.deleteRecursively();

    // Los seis idiomas tienen sus cinco textos, y los que no son en no son la copia del inglés.
    for (const char* lang : { "en", "es", "pt", "fr", "de", "it" })
    {
        const auto& t = EyepieceIntro::textsFor (lang);
        std::printf ("T8[textos] %s: «%s» · «%s» / «%s» · «%s»\n", lang, t.title, t.get, t.open, t.close);
        if (juce::String (lang) != "en")
            REQUIRE (juce::String::fromUTF8 (t.body) != juce::String::fromUTF8 (EyepieceIntro::textsFor ("en").body));
    }
}

// ============================================================================================ T8[real]
// La detección de verdad, sin reemplazo: no se exige nada (depende de esta Mac), se dice qué encontró.
TEST_CASE ("telescope: T8 · la detección de verdad dice qué encontró", "[telescope][eyepiece]")
{
    const auto ls = telescope::findApplicationByBundleId (EyepieceIntro::kBundleId);
    const auto f  = EyepieceIntro::findEyepiece();
    std::printf ("T8[real] LaunchServices(%s) = «%s» · findEyepiece = «%s»\n", EyepieceIntro::kBundleId, ls.c_str(),
                 f.getFullPathName().toRawUTF8());
    // Un bundle id que no existe no se encuentra (el control de que la consulta no devuelve cualquier cosa).
    REQUIRE (telescope::findApplicationByBundleId ("com.ovniaudio.no-existe-f4").empty());
    // Y uno que existe en toda Mac, sí.
    REQUIRE_FALSE (telescope::findApplicationByBundleId ("com.apple.finder").empty());
}

// ============================================================================================ T8[uisnap]
TEST_CASE ("telescope: T8 · snapshot de la tarjeta de EYEPIECE en en y es, S/M/L", "[telescope][uisnap]")
{
    for (const char* lang : { "en", "es" })
    {
        IntroScope scope;
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        telescope::strings::setLanguage (proc.apvts.state, lang);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* tel = dynamic_cast<TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        REQUIRE (tel->eyepieceIntro() != nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        // 5 s de seno, digeridos enteros: la lente de atrás con sus números.
        double ph = 0.0;
        const auto pushed = telescope::test::pushExact (proc, 5 * (long long) kSr, kSr, [&] (juce::AudioBuffer<float>& buf, int k)
        {
            for (int i = 0; i < k; ++i) { const float v = 0.2f * (float) std::sin (ph); ph += 0.1309; buf.setSample (0, i, v); buf.setSample (1, i, v); }
        });
        telescope::test::waitDigested (proc, pushed, kSr);
        tel->pumpLensFrames (30);

        for (const auto& z : { std::pair<ovni::PluginEditorBase::Zoom, const char*> { ovni::PluginEditorBase::Zoom::small, "S" },
                               { ovni::PluginEditorBase::Zoom::medium, "M" }, { ovni::PluginEditorBase::Zoom::large, "L" } })
        {
            tel->applyZoom (z.first);
            tel->pumpLensFrames (5);
            const auto path = "/tmp/ovni_telescope_eyepiece_" + juce::String (lang) + "_" + z.second + ".png";
            std::printf ("T8[uisnap] %s · «%s»\n", path.toRawUTF8(), tel->eyepieceIntro()->titleText().toRawUTF8());
            REQUIRE (tel->eyepieceIntro()->isVisible());
            telescope::test::writePng (*tel, path);
        }
        ed.reset();
        proc.releaseResources();
    }
}
#endif
