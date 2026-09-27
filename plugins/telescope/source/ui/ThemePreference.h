#pragma once
#include <functional>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include "lenses/Look.h"
#include "template/PluginEditorBase.h"

// ========================================================================================================
// ThemePreference — el tema (oscuro / claro) es una preferencia del USUARIO, no de cada instancia del plugin
// (prompt 99). Vive en ~/Library/Application Support/OVNI/OVNI.settings (Windows: %APPDATA%\OVNI\), el
// mismo archivo que el zoom S/M/L del sello, bajo la clave `telescopeTheme` = "dark" | "light".
//
// LA REGLA DE D-100 (EYEPIECE-PLUGINS-PROPIOS §2quater, punto 4), porque otro plugin del sello o EYEPIECE
// pueden estar escribiendo el mismo archivo en otro proceso:
//   · se escribe SÓLO esta clave;
//   · se RELEE el archivo justo antes de escribir (un PropertiesFile nuevo lee el disco al construirse);
//   · se escribe con el lock ENTRE PROCESOS de JUCE, el mismo que usa el sello (settingsProcessLock).
//
// Los tests desvían el archivo con setFileForTest: un test no puede tocar las preferencias de quien lo corre.
// ========================================================================================================
namespace telescope
{
class ThemePreference
{
public:
    static constexpr const char* kKey = "telescopeTheme";

    static look::Theme load()
    {
       #if TELESCOPE_TEST_BUILD
        // Las capturas del tema claro: OVNI_TEST_THEME=light abre TODAS las ventanas del runner en claro.
        const auto forced = juce::SystemStats::getEnvironmentVariable ("OVNI_TEST_THEME", {});
        if (forced == "light") return look::Theme::light;
        if (forced == "dark")  return look::Theme::dark;
       #endif
        return getValue (kKey, "dark") == "light" ? look::Theme::light : look::Theme::dark;
    }

    static bool save (look::Theme t)
    {
        return setValue (kKey, t == look::Theme::light ? "light" : "dark");
    }

    // ---- F4 de la 0.2 (T8): el MISMO mecanismo para cualquier clave de TELESCOPE en OVNI.settings ----
    // La tarjeta que presenta a EYEPIECE guarda acá que ya se vio (ui/EyepieceIntro.h). Una sola copia de la
    // regla de D-100: sólo esa clave, relectura justo antes de escribir, lock entre procesos.
    static juce::String getValue (const juce::String& key, const juce::String& fallback)
    {
        auto f = open();
        return f->getValue (key, fallback);
    }

    static bool setValue (const juce::String& key, const juce::String& value)
    {
        const juce::InterProcessLock::ScopedLockType lock (ovni::PluginEditorBase::settingsProcessLock());
        auto f = open();   // relee el disco AHORA, con el lock tomado
       #if TELESCOPE_TEST_BUILD
        if (auto& pause = betweenReadAndWriteForTest()) pause();
       #endif
        f->setValue (key, value);
        return f->save();
    }

    // Avisa a las ventanas abiertas de este proceso (dos instancias de TELESCOPE en el mismo DAW cambian
    // juntas: la preferencia es una sola).
    static juce::ChangeBroadcaster& changed()
    {
        static juce::ChangeBroadcaster b;
        return b;
    }

    static void setFileForTest (const juce::File& f) { overrideFile() = f; }

   #if TELESCOPE_TEST_BUILD
    // Test-only ([lock], F2b de la 0.2): lo que corre ENTRE releer el archivo y escribirlo. El test abre ahí la
    // ventana de la carrera a propósito —otro proceso intenta escribir justo en ese momento— y verifica que el
    // lock la cierra: con el lock tomado, el otro espera; sin él, su escritura se pierde. En el plugin no existe.
    static std::function<void()>& betweenReadAndWriteForTest() { static std::function<void()> f; return f; }
   #endif

private:
    static juce::File& overrideFile() { static juce::File f; return f; }

    static juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "OVNI";
        o.filenameSuffix      = "settings";
        o.folderName          = "OVNI";
        o.osxLibrarySubFolder = "Application Support";
        o.processLock         = &ovni::PluginEditorBase::settingsProcessLock();
        return o;
    }

    static juce::File defaultFile (const juce::PropertiesFile::Options& o)
    {
       #if TELESCOPE_TEST_BUILD
        // El exe de tests nunca lee ni escribe las preferencias de quien lo corre: un archivo propio de la
        // corrida, que no existe hasta que un test guarda algo (o sea: oscuro, el default).
        juce::ignoreUnused (o);
        static const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile ("ovni-telescope-tests")
                                  .getChildFile ("OVNI-" + juce::String (juce::Time::currentTimeMillis()) + ".settings");
        return f;
       #else
        return o.getDefaultFile();
       #endif
    }

    static std::unique_ptr<juce::PropertiesFile> open()
    {
        const auto o = options();
        const auto file = overrideFile() != juce::File() ? overrideFile() : defaultFile (o);
        return std::make_unique<juce::PropertiesFile> (file, o);
    }
};
}
