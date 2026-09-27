// [telescope][tema] — el tema claro es una PREFERENCIA DEL USUARIO que vive en OVNI.settings, y ese archivo
// lo comparten otros productos (F2 de la 0.2; la regla es la de D-100, EYEPIECE-PLUGINS-PROPIOS §2quater,
// punto 4: cada uno escribe sólo su clave, relee justo antes de escribir y usa el lock entre procesos).
//
// Todo contra un archivo TEMPORAL (ThemePreference::setFileForTest): un test no toca las preferencias de
// quien lo corre.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <vector>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/ThemePreference.h"
#include "lenses/FieldLens.h"
#include "lenses/SpectrogramLens.h"
#include "lenses/StereoSpectrogramLens.h"
#include "lenses/WaterfallLens.h"
#include "TestHelpers.h"
#include "TestSignals.h"

namespace
{
juce::PropertiesFile::Options plainOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "OVNI";
    o.filenameSuffix  = "settings";
    return o;
}

// Un archivo temporal con claves de OTROS productos, como las dejaría el sello o EYEPIECE.
juce::File settingsWithForeignKeys()
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("ovni-telescope-tests").getChildFile ("tema-" + juce::String (juce::Random::getSystemRandom().nextInt64()) + ".settings");
    f.getParentDirectory().createDirectory();
    juce::PropertiesFile p (f, plainOptions());
    p.setValue ("uiZoom", 2);
    p.setValue ("eyepieceNoMostrarTelescope", true);
    REQUIRE (p.save());
    return f;
}

struct Restore
{
    ~Restore()
    {
        telescope::ThemePreference::setFileForTest ({});
        telescope::look::setTheme (telescope::look::Theme::dark);
    }
};
}

TEST_CASE ("telescope: guardar el tema escribe sólo su clave y relee antes de escribir", "[telescope][tema]")
{
    Restore restore;
    const auto file = settingsWithForeignKeys();
    telescope::ThemePreference::setFileForTest (file);

    // Sin la clave, el default es el OSCURO.
    REQUIRE (telescope::ThemePreference::load() == telescope::look::Theme::dark);

    REQUIRE (telescope::ThemePreference::save (telescope::look::Theme::light));
    {
        juce::PropertiesFile p (file, plainOptions());
        CHECK (p.getValue ("telescopeTheme") == "light");
        CHECK (p.getIntValue ("uiZoom") == 2);                      // la del sello, intacta
        CHECK (p.getBoolValue ("eyepieceNoMostrarTelescope"));      // la de EYEPIECE, intacta
    }
    CHECK (telescope::ThemePreference::load() == telescope::look::Theme::light);

    // OTRO producto escribe su clave DESPUÉS (con su propia copia del archivo). Si TELESCOPE guardara una copia
    // vieja, la borraría: lo que se verifica es que relee justo antes de escribir.
    {
        juce::PropertiesFile other (file, plainOptions());
        other.setValue ("eyepieceUltimoProyecto", "tema.als");
        REQUIRE (other.save());
    }
    REQUIRE (telescope::ThemePreference::save (telescope::look::Theme::dark));
    {
        juce::PropertiesFile p (file, plainOptions());
        CHECK (p.getValue ("telescopeTheme") == "dark");
        CHECK (p.getValue ("eyepieceUltimoProyecto") == "tema.als");
        CHECK (p.getIntValue ("uiZoom") == 2);
    }
    file.deleteFile();
}

TEST_CASE ("telescope: el clic en la fila del tema alterna, guarda y repinta con la otra tinta", "[telescope][tema]")
{
    Restore restore;
    const auto file = settingsWithForeignKeys();
    telescope::ThemePreference::setFileForTest (file);
    telescope::look::setTheme (telescope::look::Theme::dark);

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);

    // En oscuro el marco es el del sello, sin tocar (FrameInk por defecto).
    CHECK (tel->getFrameInk().base == ovni::ui::theme::bg0);
    CHECK (tel->getFrameInk().atmosphere);

    // El brillo del pozo de la lente, pintado de verdad: si la capa estática no se rehorneara al cambiar de
    // tema, el pozo seguiría oscuro en el claro.
    const auto wellLuma = [&]
    {
        auto* l = tel->activeLens();
        const auto img = l->createComponentSnapshot (l->getLocalBounds(), true, 1.0f);
        return img.getPixelAt (img.getWidth() / 2, img.getHeight() / 2).getPerceivedBrightness();
    };
    const float darkLuma = wellLuma();
    tel->toggleTheme();
    CHECK (telescope::look::theme() == telescope::look::Theme::light);
    CHECK (telescope::ThemePreference::load() == telescope::look::Theme::light);   // quedó guardado
    CHECK (tel->getFrameInk().base == telescope::look::lightInk().bg0);             // el aviso llegó al editor
    CHECK_FALSE (tel->getFrameInk().atmosphere);
    CHECK (tel->activeLens() != nullptr);
    const float lightLuma = wellLuma();
    std::printf ("TEMA pozo de la lente: oscuro %.3f -> claro %.3f\n", darkLuma, lightLuma);
    CHECK (darkLuma < 0.2f);
    CHECK (lightLuma > 0.8f);   // la lente se rehizo con la tinta clara

    // La tinta clara es otra: el texto primario es el grafito de la marca v2, no el blanco del sello.
    CHECK (juce::Colour (telescope::look::txtPrimary) == juce::Colour (0xff0a0c14));

    tel->toggleTheme();
    CHECK (telescope::look::theme() == telescope::look::Theme::dark);
    CHECK (telescope::ThemePreference::load() == telescope::look::Theme::dark);
    CHECK (tel->getFrameInk().base == ovni::ui::theme::bg0);
    CHECK (juce::Colour (telescope::look::txtPrimary) == ovni::ui::theme::txt);

    ed.reset();
    proc.releaseResources();
    file.deleteFile();
}

// ========================================================================================================
// [telescope][pantalla-datos] — D-109 (F2b de la 0.2): en el tema claro, las cuatro lentes de mapa de calor
// conservan su PANTALLA oscura y la paleta del oscuro. La auditora lo midió en la hoja de la F2: en STEREO
// SPECTROGRAM la luminancia máxima de la zona de datos era 11 de 255 en claro y 217 en oscuro (la rampa de
// fase terminaba en el grafito, y «mono» salía negro sobre negro).
//
// El criterio es el de ella: la luminancia de la zona de datos. La misma música, el mismo editor, las dos
// tintas (toggleTheme rehace la lente sobre los mismos anillos del motor): el máximo y la media de la zona
// de datos en claro tienen que ser los del oscuro. Y cuántos píxeles son idénticos, que es lo más fuerte.
namespace
{
struct LumaStats { double max = 0.0, mean = 0.0; int n = 0; std::vector<juce::uint32> px; };

juce::Rectangle<int> dataZoneOf (telescope::Lens* l)
{
    if (auto* a = dynamic_cast<telescope::SpectrogramLens*> (l))       return a->cacheAreaForTest();
    if (auto* a = dynamic_cast<telescope::StereoSpectrogramLens*> (l)) return a->cacheAreaForTest();
    if (auto* a = dynamic_cast<telescope::WaterfallLens*> (l))         return a->cacheAreaForTest();
    if (auto* a = dynamic_cast<telescope::FieldLens*> (l))             return a->cacheAreaForTest();
    return {};
}

// La zona de datos de la lente activa, fotografiada a 2× (Retina), sin su borde de 1 px lógico.
LumaStats dataZoneLuma (telescope::TelescopeEditor& tel)
{
    auto* l = tel.activeLens();
    REQUIRE (l != nullptr);
    const auto zone = dataZoneOf (l).reduced (1);
    REQUIRE (! zone.isEmpty());
    const auto img = l->createComponentSnapshot (l->getLocalBounds(), true, 2.0f);
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    LumaStats st;
    double sum = 0.0;
    for (int y = zone.getY() * 2; y < zone.getBottom() * 2; ++y)
        for (int x = zone.getX() * 2; x < zone.getRight() * 2; ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            const double v = 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue();
            st.max = std::max (st.max, v);
            sum += v;
            st.px.push_back (c.getARGB());
            ++st.n;
        }
    st.mean = st.n > 0 ? sum / st.n : 0.0;
    return st;
}
}

TEST_CASE ("telescope: en claro las cuatro lentes de datos conservan su pantalla oscura (D-109)",
           "[telescope][tema][pantalla-datos]")
{
    Restore restore;
    const auto file = settingsWithForeignKeys();
    telescope::ThemePreference::setFileForTest (file);
    telescope::look::setTheme (telescope::look::Theme::dark);

    const struct { telescope::LensId id; const char* name; } lenses[] = {
        { telescope::LensId::spectrogram,       "SPECTROGRAM" },
        { telescope::LensId::waterfall,         "WATERFALL" },
        { telescope::LensId::stereoSpectrogram, "STEREO SPECTROGRAM" },
        { telescope::LensId::field,             "FIELD" },
    };

    for (const auto& lens : lenses)
    {
        telescope::look::setTheme (telescope::look::Theme::dark);
        REQUIRE (telescope::ThemePreference::save (telescope::look::Theme::dark));

        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        proc.setBandsWindowIndex (0);
        auto* lensParam = proc.apvts.getParameter ("lens");
        REQUIRE (lensParam != nullptr);
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) lens.id));

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        REQUIRE (telescope::look::theme() == telescope::look::Theme::dark);

        // Una mezcla con lo que estas lentes existen para mostrar: un cuerpo MONO de banda ancha (lo que en
        // STEREO SPECTROGRAM es «mono», casi blanco en oscuro) y agudos anchos, distintos en L y en R.
        telescope::test::Pink mono { telescope::test::kPinkSeedA }, side { telescope::test::kPinkSeedB };
        telescope::test::HighPass6 hp (3000.0, 48000.0);
        const long long total = 4 * 48000;   // 4 s: 40 hops enteros
        telescope::test::pushExact (proc, total, 48000.0, [&] (juce::AudioBuffer<float>& buf, int k)
        {
            for (int i = 0; i < k; ++i)
            {
                const float m = 0.35f * mono.next();
                const float s = 1.5f * hp.process (side.next());
                buf.setSample (0, i, m + s);
                buf.setSample (1, i, m - s);
            }
        });
        telescope::test::waitDigested (proc, total, 48000.0);

        tel->pumpLensFrames (4);
        const auto dark = dataZoneLuma (*tel);

        tel->toggleTheme();                                   // rehace la lente con la tinta clara
        REQUIRE (telescope::look::theme() == telescope::look::Theme::light);
        tel->pumpLensFrames (4);
        const auto light = dataZoneLuma (*tel);

        REQUIRE (dark.n == light.n);
        int same = 0;
        for (size_t i = 0; i < dark.px.size(); ++i) same += dark.px[i] == light.px[i] ? 1 : 0;
        std::printf ("PANTALLA-DATOS %-18s luma max  oscuro %6.1f  claro %6.1f · media  oscuro %6.2f  claro %6.2f"
                     " · %d de %d px idénticos\n",
                     lens.name, dark.max, light.max, dark.mean, light.mean, same, dark.n);

        CHECK (dark.max > 120.0);                       // hay dato que ver (si no, el test no mide nada)
        CHECK (std::abs (light.max - dark.max) <= 1.0);
        CHECK (std::abs (light.mean - dark.mean) <= 1.0);
        // Y lo más fuerte: la pantalla es la MISMA, píxel a píxel. La rejilla de encima del sonograma, por
        // ejemplo, casi no mueve la luminancia (el control sin ScreenInk: 8496 px de 1.3 M) y sólo esto la ve.
        CHECK (same == dark.n);

        telescope::look::setTheme (telescope::look::Theme::dark);
        ed.reset();
        proc.releaseResources();
    }
    file.deleteFile();
}

// ========================================================================================================
// [telescope][tema][lock] — EL LOCK ENTRE PROCESOS (F2b de la 0.2, veredicto 99, reparo 4).
//
// La preferencia del tema vive en OVNI.settings, que escriben también otros productos del sello (EYEPIECE, el
// zoom de los plugins) desde OTROS procesos. La regla de D-100: cada uno relee y escribe sólo su clave con el
// lock entre procesos de JUCE tomado. Sin el lock, el que guarda puede releer, perder el procesador, y escribir
// su copia vieja DESPUÉS de que otro guardó la suya: la clave del otro se pierde.
//
// Esa carrera, dejada al azar, se ve poco: 300 escrituras de cada lado sin el lock la mostraron en 2 corridas
// de 5 (F2b, 14:4x). Por eso acá se provoca: el runner se lanza a sí mismo dos veces (el caso oculto
// [.escritor-settings]) y el gancho de test de ThemePreference::save deja al TEMA parado entre releer y escribir
// hasta que el OTRO avisa que escribió (o 2 s):
//   · con el lock, el otro no puede escribir mientras el tema lo tiene: espera, y escribe después;
//   · sin el lock, el otro escribe en la ventana y el tema lo pisa con su copia vieja: la clave se pierde.
// ========================================================================================================
namespace
{
constexpr const char* kEscritorEnv = "OVNI_ESCRITOR_SETTINGS";
constexpr const char* kOtroKey     = "eyepieceContador";

void setEnv (const char* name, const juce::String& value)
{
   #if JUCE_WINDOWS
    _putenv_s (name, value.toRawUTF8());
   #else
    if (value.isEmpty()) unsetenv (name); else setenv (name, value.toRawUTF8(), 1);
   #endif
}

juce::File signal (const juce::File& settings, const char* what) { return settings.getSiblingFile (settings.getFileName() + "." + what); }
}

TEST_CASE ("telescope: escritor de OVNI.settings para el test del lock (lo lanza el test, en otro proceso)",
           "[.escritor-settings]")
{
    const auto parts = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable (kEscritorEnv, {}), "|", "");
    if (parts.size() != 2) { WARN ("sólo corre lanzado por el test del lock"); return; }
    const juce::File file (parts[1]);
    if (parts[0] == "tema" || parts[0] == "intro")
    {
        // F4 de la 0.2 (T8): «intro» es la tarjeta de EYEPIECE, que guarda su clave con el MISMO mecanismo
        // (ThemePreference::setValue). El gancho de la ventana es el mismo.
        const bool intro = parts[0] == "intro";
        // Espera a que el otro esté vivo, relee, y se queda en la ventana hasta que el otro escribe (o 2 s).
        REQUIRE (telescope::test::waitUntil ([&] { return signal (file, "otro-listo").existsAsFile(); }, 30000));
        bool otherWroteInside = false;
        telescope::ThemePreference::setFileForTest (file);
        telescope::ThemePreference::betweenReadAndWriteForTest() = [&]
        {
            signal (file, "tema-leyo").create();
            otherWroteInside = telescope::test::waitUntil ([&] { return signal (file, "otro-escribio").existsAsFile(); }, 2000);
        };
        if (intro) REQUIRE (telescope::ThemePreference::setValue ("telescopeEyepieceIntro", "seen"));
        else       REQUIRE (telescope::ThemePreference::save (telescope::look::Theme::light));
        telescope::ThemePreference::betweenReadAndWriteForTest() = nullptr;
        telescope::ThemePreference::setFileForTest ({});
        std::printf ("ESCRITOR %s guardado · el otro escribio dentro de la ventana=%d\n", intro ? "intro" : "tema",
                     otherWroteInside ? 1 : 0);
    }
    else
    {
        // Otro producto que cumple D-100: lock, releer, sólo su clave, guardar. Intenta en cuanto el tema releyó.
        signal (file, "otro-listo").create();
        REQUIRE (telescope::test::waitUntil ([&] { return signal (file, "tema-leyo").existsAsFile(); }, 30000));
        {
            const juce::InterProcessLock::ScopedLockType lock (ovni::PluginEditorBase::settingsProcessLock());
            auto o = plainOptions();
            o.processLock = &ovni::PluginEditorBase::settingsProcessLock();
            juce::PropertiesFile p (file, o);
            p.setValue (kOtroKey, 1);
            REQUIRE (p.save());
        }
        signal (file, "otro-escribio").create();
        std::printf ("ESCRITOR otro guardado\n");
    }
}

namespace
{
// El caso del lock, con el rol del que guarda: «tema» (F2b) o «intro» (la tarjeta de EYEPIECE, F4).
void lockCase (const char* role, const juce::String& key, const juce::String& want)
{
    const auto dir  = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni-telescope-tests");
    dir.createDirectory();
    const auto file = dir.getChildFile ("lock-" + juce::String (juce::Random::getSystemRandom().nextInt64()) + ".settings");
    const auto cleanUp = [&]
    {
        for (const auto* w : { "", ".otro-listo", ".tema-leyo", ".otro-escribio" })
            file.getSiblingFile (file.getFileName() + w).deleteFile();
    };
    cleanUp();

    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    const auto launch = [&] (juce::ChildProcess& cp, const char* who)
    {
        setEnv (kEscritorEnv, juce::String (who) + "|" + file.getFullPathName());
        const bool ok = cp.start (juce::StringArray { exe, "[.escritor-settings]" });
        setEnv (kEscritorEnv, {});
        return ok;
    };
    juce::ChildProcess tema, otro;
    REQUIRE (launch (tema, role));
    REQUIRE (launch (otro, "otro"));
    REQUIRE (tema.waitForProcessToFinish (60000));
    REQUIRE (otro.waitForProcessToFinish (60000));
    const auto outTema = tema.readAllProcessOutput(), outOtro = otro.readAllProcessOutput();
    CHECK (tema.getExitCode() == 0);
    CHECK (otro.getExitCode() == 0);
    REQUIRE (outTema.contains ("ESCRITOR " + juce::String (role) + " guardado"));   // que los dos corrieron de verdad (no un WARN)
    REQUIRE (outOtro.contains ("ESCRITOR otro guardado"));

    const int inside = outTema.fromFirstOccurrenceOf ("ventana=", false, false).getIntValue();
    juce::PropertiesFile p (file, plainOptions());
    const int  otherKey = p.getIntValue (kOtroKey, 0);
    const auto themeKey = p.getValue (key, "(no esta)");
    std::printf ("LOCK [%s] el otro escribio dentro de la ventana: %d · al final %s=%d (esperado 1) · %s=%s "
                 "(esperado %s)\n", role, inside, kOtroKey, otherKey, key.toRawUTF8(), themeKey.toRawUTF8(), want.toRawUTF8());
    CHECK (inside == 0);          // con el lock, el otro no pudo escribir mientras el tema releía y escribía
    CHECK (otherKey == 1);        // y su clave no se perdió
    CHECK (themeKey == want);     // ni la del que guardó
    cleanUp();
}
}

TEST_CASE ("telescope: dos procesos escriben OVNI.settings a la vez y no se pierde ninguna clave",
           "[telescope][tema][lock]")
{
    lockCase ("tema", telescope::ThemePreference::kKey, "light");
}

// F4 de la 0.2 (T8): lo mismo con la clave de la tarjeta que presenta a EYEPIECE: EYEPIECE puede estar escribiendo
// OVNI.settings mientras TELESCOPE guarda que ya se vio, y la clave del otro sobrevive.
TEST_CASE ("telescope: T8 · la tarjeta de EYEPIECE guarda su clave sin pisar la de otro producto",
           "[telescope][eyepiece][lock]")
{
    lockCase ("intro", "telescopeEyepieceIntro", "seen");
}
