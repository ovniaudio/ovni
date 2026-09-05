// [supernova][persistencia] — las 3 persistencias PLANAS del plugin (LfoBank, MidiCcMap, SceneBank) tienen
// que sobrevivir a un proceso cuyo LC_NUMERIC use COMA decimal. OVNI nunca llama setlocale, pero un host o
// un plugin vecino (Qt/GTK) sí: es un defecto latente cuyo modo de falla es "el VJ pierde su sesión en vivo".
// Informe 24 · M5 / D-25: hoy las tres se rompen con es_AR, de_DE, fr_FR, it_IT, pt_BR, ru_RU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "tempo/LfoBank.h"
#include "midi/MidiCcMap.h"
#include "presets/SceneBank.h"
#include <clocale>
#include <cmath>
#include <string>
#include <vector>

using Catch::Approx;
using supernova::LfoBank;
using supernova::LfoShape;
using supernova::MidiCcMap;
using supernova::SceneBank;
using supernova::MorphSnapshot;

namespace
{
// Fija LC_NUMERIC mientras vive y lo restaura al salir (aunque el test falle).
struct ScopedNumericLocale
{
    explicit ScopedNumericLocale (const char* name)
    {
        const char* prev = std::setlocale (LC_NUMERIC, nullptr);
        previous = prev != nullptr ? prev : "C";
        ok = std::setlocale (LC_NUMERIC, name) != nullptr;
    }
    ~ScopedNumericLocale() { std::setlocale (LC_NUMERIC, previous.c_str()); }
    bool available() const noexcept { return ok; }

    std::string previous;
    bool        ok = false;
};

LfoBank sampleLfoBank()
{
    LfoBank b;
    b.slot (0) = { true, 2.0f,  LfoShape::Saw,    0.75f, 0.125f, false, "intensity" };
    b.slot (1) = { true, 0.25f, LfoShape::Square, 0.30f, 0.0f,   true,  "chaos" };
    b.slot (3) = { true, 8.0f,  LfoShape::Sine,   0.5f,  0.375f, true,  "glow" };
    return b;
}
MidiCcMap sampleCcMap()
{
    MidiCcMap m;
    m.assign (74, "glow", 0.25f, 0.75f);
    m.assign (71, "chaos", 0.0f, 1.0f);
    return m;
}
SceneBank sampleScenes()
{
    MorphSnapshot s;
    for (int k = 0; k < MorphSnapshot::N; ++k) s.v[k] = 12.5f + (float) k * 1.25f;
    SceneBank b; b.save (2, s); b.save (5, s);
    return b;
}

const char* kLocales[] = { "es_AR.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8", "it_IT.UTF-8", "pt_BR.UTF-8", "ru_RU.UTF-8" };
}

TEST_CASE ("persistencia: round-trip de LfoBank / MidiCcMap / SceneBank bajo es_AR, de_DE, fr_FR",
           "[supernova][persistencia]")
{
    // Referencia: lo que sale bajo el locale C. El formato tiene que ser BYTE-IDÉNTICO en cualquier locale.
    const std::string lfoRef = sampleLfoBank().serialize();
    const std::string ccRef  = sampleCcMap().serialize();
    const std::string scRef  = sampleScenes().serialize();

    std::vector<std::string> missing;
    int tested = 0;

    for (const char* name : kLocales)
    {
        ScopedNumericLocale loc (name);
        if (! loc.available()) { missing.emplace_back (name); continue; }
        ++tested;
        INFO ("locale " << name);

        // ---- LfoBank
        {
            const std::string ser = sampleLfoBank().serialize();
            INFO ("LfoBank serialize = " << ser);
            REQUIRE (ser == lfoRef);                       // el separador decimal NO puede ser la coma: es el
            LfoBank back; back.deserialize (ser);          // separador de campos del propio formato
            REQUIRE (back.slot (0).enabled);
            REQUIRE (back.slot (0).target == "intensity");
            REQUIRE (back.slot (0).beatsPerCycle == Approx (2.0f));
            REQUIRE (back.slot (0).depth == Approx (0.75f));
            REQUIRE (back.slot (0).phaseOffset == Approx (0.125f));
            REQUIRE_FALSE (back.slot (0).bipolar);
            REQUIRE (back.slot (1).target == "chaos");
            REQUIRE (back.slot (1).beatsPerCycle == Approx (0.25f));
            REQUIRE (back.slot (3).phaseOffset == Approx (0.375f));
        }
        // ---- MidiCcMap
        {
            const std::string ser = sampleCcMap().serialize();
            INFO ("MidiCcMap serialize = " << ser);
            REQUIRE (ser == ccRef);
            MidiCcMap back; back.deserialize (ser);
            const auto hit = back.feed (74, 127);
            REQUIRE (hit.has_value());
            REQUIRE (hit->paramId == "glow");
            REQUIRE (hit->value01 == Approx (0.75f));      // el tope del rango aprendido, no 0
        }
        // ---- SceneBank
        {
            const std::string ser = sampleScenes().serialize();
            INFO ("SceneBank serialize = " << ser);
            REQUIRE (ser == scRef);
            SceneBank back; back.deserialize (ser);
            REQUIRE (back.has (2));
            REQUIRE (back.has (5));
            REQUIRE (back.recall (2).v[0] == Approx (12.5f));        // 12.5 no puede volver como 12
            REQUIRE (back.recall (5).v[MorphSnapshot::N - 1]
                     == Approx (12.5f + (float) (MorphSnapshot::N - 1) * 1.25f));
        }
    }

    for (const auto& m : missing) WARN ("locale no instalado en esta máquina, no se probó: " << m);
    if (tested == 0) SKIP ("ninguno de los locales con coma decimal está instalado en esta máquina");
}

TEST_CASE ("persistencia: un estado guardado con el formato viejo (std::to_string) se sigue leyendo",
           "[supernova][persistencia]")
{
    // Compatibilidad hacia atrás: los proyectos ya guardados traen "2.000000" / "0.750000".
    LfoBank b;
    b.deserialize ("1,2.000000,2,0.750000,0.125000,0,intensity;0,4.000000,0,0.500000,0.000000,1,;");
    REQUIRE (b.slot (0).enabled);
    REQUIRE (b.slot (0).beatsPerCycle == Approx (2.0f));
    REQUIRE (b.slot (0).depth == Approx (0.75f));
    REQUIRE (b.slot (0).phaseOffset == Approx (0.125f));
    REQUIRE (b.slot (0).target == "intensity");

    MidiCcMap m;
    m.deserialize ("74,glow,0.25,0.75;");
    const auto hit = m.feed (74, 127);
    REQUIRE (hit.has_value());
    REQUIRE (hit->value01 == Approx (0.75f));

    SceneBank sc;
    std::string rec = "2:";
    for (int k = 0; k < MorphSnapshot::N; ++k) rec += std::string (k ? "," : "") + "12.5";
    sc.deserialize (rec + ";");
    REQUIRE (sc.has (2));
    REQUIRE (sc.recall (2).v[0] == Approx (12.5f));
}
