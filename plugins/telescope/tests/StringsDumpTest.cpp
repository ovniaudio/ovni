// [strings-dump] — vuelca la MATRIZ DE IDIOMAS (todas las claves × los 6 idiomas) a
// plugins/telescope/docs/strings-matrix.md.
//
// POR QUÉ EXISTE. TELESCOPE promete seis idiomas con fallback por clave. Esa promesa se puede auditar de
// dos maneras: leyendo 447 líneas de `Strings.h` + 539 de `Rules.h` con seis tablas entreveradas, o
// mirando una tabla. Esto genera la tabla, y la genera DESDE EL CÓDIGO: si mañana alguien agrega una
// clave y se olvida del portugués, el documento lo muestra en la fila de esa clave, no en un comentario.
//
// SE VUELCA EL VALOR CRUDO, NO EL RESUELTO. `strings::get()` y `rules::phrase()` caen a inglés cuando a
// un idioma le falta la clave — que es lo correcto en pantalla y sería una MENTIRA en esta matriz: se
// vería el inglés en la celda del portugués y parecería traducido. Acá se lee `t->s[i]` / `l.table[i]`
// directo, y lo que falta se marca `— (fallback en)`.
//
// TAG OCULTO. `[.]` hace que Catch2 lo saltee en la corrida normal: es un generador de documentación, no
// una verificación, y no tiene por qué gastar tiempo en cada `ctest`. Corre con
// `OvniTelescopeTests "[strings-dump]"`, y el test de ctest `telescope-strings-matrix` lo corre contra un
// temporal y compara con el archivo commiteado (ver check-strings-matrix.sh): así la matriz vieja se pone
// roja sola.
//
// DESTINO. `TELESCOPE_DOCS_DIR` (definido por CMake) apunta a plugins/telescope/docs. La variable de
// entorno `TELESCOPE_STRINGS_OUT` lo pisa, y es lo que usa el test de frescura para escribir en /tmp.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstring>
#include <juce_core/juce_core.h>
#include "data/Rules.h"
#include "lenses/Strings.h"

#ifndef TELESCOPE_DOCS_DIR
  #define TELESCOPE_DOCS_DIR "."
#endif

namespace
{
// Los seis códigos, en el orden en que viven en las dos tablas. Se comprueba que sean LOS MISMOS: si
// `Strings.h` tuviera japonés y `Rules.h` no, la matriz saldría con una columna vacía y nadie lo notaría.
const char* const kCodes[] = { "en", "es", "pt", "fr", "de", "it" };
constexpr int kNumCodes = (int) (sizeof (kCodes) / sizeof (kCodes[0]));

// Los que no pasaron por un hablante nativo. Va en el encabezado del documento y en la fila de la
// columna, porque una tabla que se lee sin leer el encabezado es una tabla que se lee mal.
bool pendingNativeReview (const char* code)
{
    return std::strcmp (code, "en") != 0 && std::strcmp (code, "es") != 0;
}

// Una celda de tabla Markdown: los `|` se escapan y los saltos se aplanan, si no la tabla se rompe.
juce::String cell (const char* raw)
{
    if (raw == nullptr || *raw == '\0') return "— *(fallback en)*";
    return juce::String::fromUTF8 (raw)
             .replace ("|", "\\|")
             .replace ("\n", " ")
             .replace ("\r", " ");
}

// El texto que ESA tabla tiene para esa clave de VERDICT, sin fallback.
const char* rawPhrase (const char* code, const char* key)
{
    for (const auto& l : telescope::rules::kLanguages)
        if (std::strcmp (l.code, code) == 0)
            for (int i = 0; i < l.count; ++i)
                if (std::strcmp (l.table[i].key, key) == 0) return l.table[i].text;
    return nullptr;
}
}

TEST_CASE ("telescope: vuelca la matriz de idiomas a docs/strings-matrix.md", "[.][strings-dump]")
{
    using namespace telescope;

    // --- Los dos catálogos declaran los mismos idiomas, en el mismo orden. ---
    REQUIRE ((int) strings::tables().size() == kNumCodes);
    REQUIRE (rules::kNumLanguages == kNumCodes);
    for (int c = 0; c < kNumCodes; ++c)
    {
        REQUIRE (juce::String (strings::tables()[(size_t) c]->code) == kCodes[c]);
        REQUIRE (juce::String (rules::kLanguages[c].code)           == kCodes[c]);
    }

    juce::String out;
    out << "# TELESCOPE — matriz de idiomas / language matrix\n\n"
        << "> **Generado, no escrito a mano.** Sale de `OvniTelescopeTests \"[strings-dump]\"`, que lee las\n"
        << "> tablas del código (`source/lenses/Strings.h` y `source/data/Rules.h`) y las vuelca acá. El test\n"
        << "> de ctest `telescope-strings-matrix` lo regenera a un temporal y compara: si alguien agrega una\n"
        << "> clave o toca una traducción y no regenera este archivo, se pone rojo.\n>\n"
        << "> **Se muestra el valor CRUDO de cada tabla, no el resuelto.** En pantalla, una clave que le falta\n"
        << "> a un idioma sale en inglés (fallback por clave). Acá eso se marca `— (fallback en)`: pintar el\n"
        << "> inglés en la celda del portugués haría pasar por traducido lo que no lo está.\n\n"
        << "## Estado por idioma\n\n"
        << "| Código | Idioma | Revisión |\n|---|---|---|\n";

    for (int c = 0; c < kNumCodes; ++c)
    {
        const auto* t = strings::tables()[(size_t) c];
        out << "| `" << t->code << "` | " << juce::String::fromUTF8 (t->endonym) << " | "
            << (pendingNativeReview (t->code) ? "traducido, **pendiente de revisión de hablante nativo**"
                                              : "**revisado**")
            << " |\n";
    }

    // ---------------------------------------------------------------- 1. rótulos de UI (Strings.h)
    out << "\n## 1 · Rótulos de la interfaz — `source/lenses/Strings.h`\n\n"
        << strings::kNumKeys << " claves × " << kNumCodes << " idiomas.\n\n"
        << "| Clave |";
    for (int c = 0; c < kNumCodes; ++c)
        out << " " << kCodes[c] << (pendingNativeReview (kCodes[c]) ? " ⚠" : "") << " |";
    out << "\n|---|";
    for (int c = 0; c < kNumCodes; ++c) out << "---|";
    out << "\n";

    int missingUi = 0;
    for (int k = 0; k < strings::kNumKeys; ++k)
    {
        out << "| `" << strings::keyNames()[(size_t) k] << "` |";
        for (int c = 0; c < kNumCodes; ++c)
        {
            const auto* raw = strings::tables()[(size_t) c]->s[(size_t) k];
            if (raw == nullptr || *raw == '\0') ++missingUi;
            out << " " << cell (raw) << " |";
        }
        out << "\n";
    }

    // ---------------------------------------------------------------- 2. frases de VERDICT (Rules.h)
    const auto& en = rules::kLanguages[0];
    out << "\n## 2 · Frases de VERDICT — `source/data/Rules.h`\n\n"
        << en.count << " claves × " << kNumCodes << " idiomas.\n\n"
        << "| Clave |";
    for (int c = 0; c < kNumCodes; ++c)
        out << " " << kCodes[c] << (pendingNativeReview (kCodes[c]) ? " ⚠" : "") << " |";
    out << "\n|---|";
    for (int c = 0; c < kNumCodes; ++c) out << "---|";
    out << "\n";

    int missingRules = 0;
    for (int i = 0; i < en.count; ++i)
    {
        const auto* key = en.table[i].key;
        out << "| `" << key << "` |";
        for (int c = 0; c < kNumCodes; ++c)
        {
            const auto* raw = rawPhrase (kCodes[c], key);
            if (raw == nullptr || *raw == '\0') ++missingRules;
            out << " " << cell (raw) << " |";
        }
        out << "\n";
    }

    // ---------------------------------------------------------------- 3. las claves que sobran
    // Una clave que existe en otro idioma pero NO en inglés no la dibuja nadie (la búsqueda de VERDICT
    // recorre el idioma pedido y después el inglés, pero la lista de claves a pedir sale del código, que
    // usa las de inglés). Si aparece una, es texto muerto y hay que decirlo.
    juce::StringArray orphans;
    for (int c = 1; c < kNumCodes; ++c)
    {
        const auto& l = rules::kLanguages[c];
        for (int i = 0; i < l.count; ++i)
            if (rawPhrase ("en", l.table[i].key) == nullptr)
                orphans.addIfNotAlreadyThere (juce::String (l.code) + " · " + l.table[i].key);
    }
    out << "\n## 3 · Resumen\n\n"
        << "| | |\n|---|---|\n"
        << "| Claves de UI sin valor propio (salen por fallback a inglés) | **" << missingUi << "** |\n"
        << "| Frases de VERDICT sin valor propio (idem) | **" << missingRules << "** |\n"
        << "| Claves que existen en otro idioma pero no en inglés (texto muerto) | **"
        << orphans.size() << "** |\n";
    if (! orphans.isEmpty())
        out << "\n" << orphans.joinIntoString ("\n") << "\n";

    // --- Escribir. ---
    juce::String dir (juce::String::fromUTF8 (TELESCOPE_DOCS_DIR));
    juce::File dest;
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("TELESCOPE_STRINGS_OUT", {});
        env.isNotEmpty())
        dest = juce::File (env);
    else
        dest = juce::File (dir).getChildFile ("strings-matrix.md");

    dest.getParentDirectory().createDirectory();
    REQUIRE (dest.replaceWithText (out, false, false, "\n"));
    std::printf ("STRINGS_MATRIX=%s  claves UI=%d  frases VERDICT=%d  faltantes=%d/%d  huerfanas=%d\n",
                 dest.getFullPathName().toRawUTF8(), strings::kNumKeys, en.count,
                 missingUi, missingRules, orphans.size());
}
