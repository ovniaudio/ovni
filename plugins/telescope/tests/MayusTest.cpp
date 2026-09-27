// [mayus] — LA CAJA DE TELESCOPE NO DEPENDE DEL LOCALE (F5b de la 0.2, D-126).
//
// Los títulos de VERDICT salían «CóMO SE VA A SENTIR», «DóNDE TRADUCE», «QUé REVISAR Y DóNDE»: `toUpperCase()`
// llama a towupper, que depende del locale del PROCESO. Con el «C» —el de un programa que no lo cambia— no sube
// las letras con tilde; con es_AR.UTF-8, sí. En un DAW el proceso es del host, y el plugin no puede tocarle el
// locale. `look::upper` / `look::lower` deciden la caja con una tabla fija.
//
// TODO ESTE ARCHIVO CORRE CON EL LOCALE «C» PUESTO A PROPÓSITO (`ScopedCLocale`), y lo restaura al salir. El proceso
// de los tests es nuestro: acá sí se puede. En el plugin, nunca.
//
//   MAYUS[casos]     los casos del prompt 111 y los de la minúscula, uno por uno.
//   MAYUS[tablas]    todas las frases de Rules.h y todos los rótulos de Strings.h, en los seis idiomas: después de
//                    look::upper no queda ninguna minúscula, después de look::lower ninguna mayúscula. Los títulos de
//                    sección de VERDICT, además, sin ninguna letra que la tabla no conozca.
//   MAYUS[lente]     VERDICT, pintada con la señal de defectos en los seis idiomas: los títulos que se DIBUJAN son
//                    la mayúscula de los de Rules.h, sin minúsculas («CÓMO SE VA A SENTIR»).
//   MAYUS[guarda]    ningún toUpperCase en el código de TELESCOPE, y los toLowerCase que quedan son los de la lista.
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include "PluginProcessor.h"
#include "TestDefectSignal.h"
#include "TestHelpers.h"
#include "TestWav.h"
#include "data/Rules.h"
#include "lenses/Lens.h"
#include "lenses/Look.h"
#include "lenses/Strings.h"
#include "lenses/VerdictLens.h"

namespace
{
// El locale «C», puesto a propósito y restaurado al salir.
struct ScopedCLocale
{
    std::string previous;
    ScopedCLocale()
    {
        const char* p = std::setlocale (LC_ALL, nullptr);
        previous = p != nullptr ? p : "C";
        REQUIRE (std::setlocale (LC_ALL, "C") != nullptr);
    }
    ~ScopedCLocale() { std::setlocale (LC_ALL, previous.c_str()); }
};

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

// Un oráculo PROPIO de la caja: no puede ser iswlower / iswupper, que dependen del mismo locale que se está
// probando. Las letras de los seis idiomas son de Latin-1, más Œ œ Ÿ.
bool isLowerLetter (juce::juce_wchar c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || c == 0x153; }
bool isUpperLetter (juce::juce_wchar c) noexcept { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || c == 0x152 || c == 0x178; }
// Fuera de ASCII y Latin-1: Œ œ Ÿ son letras que la tabla conoce; la raya y el menos de las frases no son letras.
// Cualquier otro carácter de ahí arriba en un título es una letra que look::upper no sabe subir (un idioma nuevo
// con «ł» o «ő»): rojo, en vez de pasar por no mirarlo.
bool isKnownChar (juce::juce_wchar c) noexcept { return c <= 0xFF || c == 0x152 || c == 0x153 || c == 0x178 || c == 0x2014 || c == 0x2212; }

bool hasLower (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();) if (isLowerLetter (p.getAndAdvance())) return true;
    return false;
}
bool hasUpper (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();) if (isUpperLetter (p.getAndAdvance())) return true;
    return false;
}
bool allKnown (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();) if (! isKnownChar (p.getAndAdvance())) return false;
    return true;
}
bool hasNonAsciiLetter (const juce::String& s)
{
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c > 0x7F && (isLowerLetter (c) || isUpperLetter (c))) return true;
    }
    return false;
}

// Los títulos de sección de VERDICT, leídos de la tabla CRUDA de cada idioma (sin el fallback a inglés, que
// escondería una clave que falta).
std::vector<std::pair<juce::String, juce::String>> sectionTitles()
{
    std::vector<std::pair<juce::String, juce::String>> out;   // (idioma, título)
    for (const auto& l : telescope::rules::kLanguages)
        for (int i = 0; i < l.count; ++i)
            if (std::strncmp (l.table[i].key, "section.", 8) == 0)
                out.emplace_back (juce::String (l.code), u8 (l.table[i].text));
    return out;
}
}

TEST_CASE ("telescope: look::upper y look::lower no dependen del locale (los casos)", "[telescope][mayus]")
{
    const ScopedCLocale cLocale;
    // Lo que se arregla, medido en esta corrida: con el locale «C», ¿toUpperCase sube la ó? Se informa, no se
    // exige: depende de la libc (en la Mac, no). El control del arreglo es la mutación del reporte.
    std::printf ("MAYUS locale «%s» · juce toUpperCase: «%s» → «%s»\n", std::setlocale (LC_ALL, nullptr),
                 u8 ("Cómo").toRawUTF8(), u8 ("Cómo").toUpperCase().toRawUTF8());

    const struct { const char* in; const char* out; } ups[] = {
        { "Cómo se va a sentir", "CÓMO SE VA A SENTIR" },
        { "Dónde traduce",       "DÓNDE TRADUCE" },
        { "Qué revisar y dónde", "QUÉ REVISAR Y DÓNDE" },
        { "où ça",               "OÙ ÇA" },
        { "cœur",                "CŒUR" },
        { "straße",              "STRASSE" },
        { "grün",                "GRÜN" },
        { "não",                 "NÃO" },
        { "perché",              "PERCHÉ" },
        { "ÿ þ æ ø å ñ",         "Ÿ Þ Æ Ø Å Ñ" },
        { "÷ × 1/3 oct · −6 dB", "÷ × 1/3 OCT · −6 DB" },           // ÷ y × no son letras
        { "ya MAYÚSCULA",        "YA MAYÚSCULA" },
        { "日本語のテキスト",       "日本語のテキスト" },                 // el japonés no tiene caja
        { "",                    "" },
    };
    for (const auto& c : ups)
    {
        INFO ("look::upper «" << c.in << "»");
        CHECK (telescope::look::upper (u8 (c.in)) == u8 (c.out));
    }

    const struct { const char* in; const char* out; } downs[] = {
        { "FENÊTRE",       "fenêtre" },     // trLower en francés: «fenÊtre» con toLowerCase y el locale «C»
        { "BALANÇO",       "balanço" },     // trLower en portugués
        { "CONFIANÇA",     "confiança" },
        { "ÉCRASÉ À ŒUVRE", "écrasé à œuvre" },
        { "Ÿ Þ × ÷",       "ÿ þ × ÷" },
        { "STRASSE",       "strasse" },
        { "日本語",         "日本語" },
    };
    for (const auto& c : downs)
    {
        INFO ("look::lower «" << c.in << "»");
        CHECK (telescope::look::lower (u8 (c.in)) == u8 (c.out));
    }
}

TEST_CASE ("telescope: despues de look::upper no queda ninguna minuscula, en los seis idiomas", "[telescope][mayus]")
{
    const ScopedCLocale cLocale;

    // Los títulos de sección de VERDICT, que son los que se dibujan en mayúscula.
    const auto titles = sectionTitles();
    int withAccent = 0;
    for (const auto& [lang, title] : titles)
    {
        const auto up = telescope::look::upper (title);
        INFO (lang << " «" << title << "» → «" << up << "»");
        CHECK_FALSE (hasLower (up));
        CHECK (allKnown (title));
        withAccent += hasNonAsciiLetter (title) ? 1 : 0;
        std::printf ("MAYUS titulo %s «%s» → «%s»\n", lang.toRawUTF8(), title.toRawUTF8(), up.toRawUTF8());
    }
    std::printf ("MAYUS titulos: %d (6 idiomas x 4 secciones) · %d con una letra fuera del ASCII\n",
                 (int) titles.size(), withAccent);
    CHECK (titles.size() == 6 * (size_t) telescope::rules::Section::kNumSections);
    CHECK (withAccent > 0);   // control: si ningún título tuviera tilde, esto no probaría la tilde

    // Y todas las frases y todos los rótulos: lo que cualquier lente pase por upper o por lower.
    int phrases = 0, labels = 0;
    for (const auto& l : telescope::rules::kLanguages)
        for (int i = 0; i < l.count; ++i)
        {
            const auto s = u8 (l.table[i].text);
            INFO (l.code << " " << l.table[i].key << " «" << s << "»");
            CHECK_FALSE (hasLower (telescope::look::upper (s)));
            CHECK_FALSE (hasUpper (telescope::look::lower (s)));
            ++phrases;
        }
    for (const auto& lang : telescope::strings::availableLanguages())
        for (int k = 0; k < telescope::strings::kNumKeys; ++k)
        {
            const auto s = telescope::strings::get ((telescope::strings::Key) k, lang);
            INFO (lang << " clave " << k << " «" << s << "»");
            CHECK_FALSE (hasLower (telescope::look::upper (s)));
            CHECK_FALSE (hasUpper (telescope::look::lower (s)));
            ++labels;
        }
    std::printf ("MAYUS tablas: %d frases de Rules.h y %d rótulos de Strings.h, subidos y bajados\n", phrases, labels);
    CHECK (phrases > 0);
    CHECK (labels == 6 * telescope::strings::kNumKeys);
}

TEST_CASE ("telescope: VERDICT dibuja los titulos con su tilde en los seis idiomas", "[telescope][mayus]")
{
    const ScopedCLocale cLocale;
    constexpr double kSr = telescope::test::kDefectSr;

    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    const auto sig = telescope::test::makeDefectSignal();
    const auto wav = telescope::test::writeWav ("mayus_defect.wav", kSr, 2, (juce::int64) sig.getNumSamples(),
                                                [&] (juce::int64 i)
                                                {
                                                    return std::pair<float, float> { sig.getSample (0, (int) i),
                                                                                     sig.getSample (1, (int) i) };
                                                });
    proc.loadVerdictFile (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.verdictFileBusy(); }, 180000));
    REQUIRE (proc.verdictAnalysis().valid);

    telescope::Lens::setDirectPaintForTest (true);
    int drawnTitles = 0;
    for (const auto& lang : telescope::strings::availableLanguages())
    {
        proc.setVerdictLanguage (lang);
        telescope::VerdictLens lens (proc);
        lens.setSize (1025, 4000);   // alto de sobra: la lista entera a la vista, sin scroll
        lens.pumpFrames (4);

        std::vector<telescope::look::TextRequest> requests;
        telescope::look::textRequestSink() = &requests;
        const auto img = lens.createComponentSnapshot (lens.getLocalBounds(), true, 1.0f);
        telescope::look::textRequestSink() = nullptr;
        REQUIRE (img.isValid());

        juce::StringArray drawn;
        for (const auto& r : requests) drawn.add (r.text);

        // Cada sección que el informe tiene se dibuja con su título en mayúscula, igual a look::upper de la tabla.
        int found = 0;
        for (const auto& [code, title] : sectionTitles())
        {
            if (code != lang) continue;
            const auto want = telescope::look::upper (title);
            if (drawn.contains (want))
            {
                ++found;
                std::printf ("MAYUS lente %s «%s»\n", lang.toRawUTF8(), want.toRawUTF8());
            }
            // La forma rota («CóMO SE VA A SENTIR»: el ASCII arriba y la tilde abajo) no puede aparecer. Es lo que
            // dibuja toUpperCase con el locale «C».
            for (const auto& d : drawn)
                if (d != want && telescope::look::upper (d) == want && ! d.containsAnyOf ("abcdefghijklmnopqrstuvwxyz"))
                    FAIL_CHECK ("se dibujó «" << d << "» en vez de «" << want << "»");
        }
        INFO ("idioma " << lang << ": " << drawn.joinIntoString (" | "));
        CHECK (found == (int) telescope::rules::Section::kNumSections);   // la señal de defectos llena las cuatro
        drawnTitles += found;
    }
    telescope::Lens::setDirectPaintForTest (false);
    std::printf ("MAYUS lente: %d títulos dibujados en mayúscula (6 idiomas x 4)\n", drawnTitles);
    CHECK (drawnTitles == 6 * (int) telescope::rules::Section::kNumSections);

    proc.setVerdictLanguage ("en");
    proc.releaseResources();
    wav.deleteFile();
}

TEST_CASE ("telescope: ningun toUpperCase en el codigo de TELESCOPE (la guarda de la F5b)", "[telescope][mayus]")
{
    // Se cuenta en el fuente, como la guarda de [reacomoda]: un toUpperCase nuevo en una lente vuelve a depender
    // del locale del host, y ningún test de imagen lo vería en inglés.
    const auto root = juce::File (juce::String (__FILE__)).getParentDirectory().getParentDirectory();
    const auto repo = root.getParentDirectory().getParentDirectory();
    const auto count = [] (const juce::File& dir, const char* what, juce::StringArray& where)
    {
        int n = 0;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, "*.cpp;*.h"))
        {
            int line = 0;
            for (const auto& raw : juce::StringArray::fromLines (f.loadFileAsString()))
            {
                ++line;
                const auto code = raw.upToFirstOccurrenceOf ("//", false, false);   // los comentarios no cuentan
                if (code.contains (what)) { ++n; where.add (f.getFileName() + ":" + juce::String (line)); }
            }
        }
        return n;
    };

    juce::StringArray up, low, ctl;
    const int upper = count (root.getChildFile ("source"), ".toUpperCase", up);
    const int lower = count (root.getChildFile ("source"), ".toLowerCase", low);
    // Control: el patrón encuentra el que sí existe, el del nombre del plugin en la plantilla compartida (ASCII;
    // no se toca).
    const int control = count (repo.getChildFile ("shared").getChildFile ("template"), ".toUpperCase", ctl);
    std::printf ("MAYUS guarda: toUpperCase en TELESCOPE %d [%s] · toLowerCase %d [%s] · control en shared/template %d [%s]\n",
                 upper, up.joinIntoString (" ").toRawUTF8(), lower, low.joinIntoString (" ").toRawUTF8(),
                 control, ctl.joinIntoString (" ").toRawUTF8());
    CHECK (control >= 1);
    CHECK (upper == 0);
    // Los toLowerCase que quedan son de códigos, no de texto que se dibuja: el código de idioma (ASCII, tres en
    // Strings.h) y la extensión de un archivo (VerdictLens.cpp). Uno nuevo se mira antes de sumarlo acá.
    juce::StringArray files;
    for (const auto& w : low) files.add (w.upToFirstOccurrenceOf (":", false, false));
    files.sort (false);
    CHECK (lower == 4);
    CHECK (files.joinIntoString (" ") == "Strings.h Strings.h Strings.h VerdictLens.cpp");
}
