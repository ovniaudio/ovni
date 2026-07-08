// [pitch][scale] — ScaleQuantizer: cada (escala, voicing) -> offsets en semitonos esperados.
// Función PURA, determinística, sin estado, sin audio. Es el corazón de la honestidad de la marca
// ("la 3ra menor en modo menor", "la octava siempre +12"). Ver spec §3.2 y §6.
#include <catch2/catch_test_macros.hpp>
#include "ScaleQuantizer.h"
#include <vector>

using namespace ovni::engines;
using Scale   = ScaleQuantizer::Scale;
using Voicing = ScaleQuantizer::Voicing;

// Helper: materializa los offsets de un (scale, voicing) a un std::vector<int> para comparar fácil.
static std::vector<int> offs (Scale s, Voicing v) {
    const auto r = ScaleQuantizer::voicingOffsets (s, v);
    return std::vector<int> (r.begin(), r.end());
}

// ── Octave: SIEMPRE +12, en todos los modos (el shimmer clásico, default seguro) ─────────────────
TEST_CASE ("pitch/scale: Octave es +12 en todos los modos", "[pitch][scale]") {
    for (Scale s : { Scale::Major, Scale::MinorNatural, Scale::Dorian, Scale::Phrygian,
                     Scale::Lydian, Scale::Mixolydian, Scale::HarmonicMinor, Scale::Pentatonic })
        REQUIRE (offs (s, Voicing::Octave) == std::vector<int> { 12 });
}

// ── 5th: perfecta +7, consonante en cualquiera de los modos listados ─────────────────────────────
TEST_CASE ("pitch/scale: 5th es +7 en todos los modos", "[pitch][scale]") {
    for (Scale s : { Scale::Major, Scale::MinorNatural, Scale::Dorian, Scale::Phrygian,
                     Scale::Lydian, Scale::Mixolydian, Scale::HarmonicMinor, Scale::Pentatonic })
        REQUIRE (offs (s, Voicing::Fifth) == std::vector<int> { 7 });
}

// ── 3rd: el MODO fija el sabor. Mayor=+4, menor=+3 (la 3ra menor en menor). LA honestidad. ───────
TEST_CASE ("pitch/scale: la 3ra depende del modo (mayor +4, menor +3)", "[pitch][scale]") {
    // Modos con 3ra MAYOR (+4): Major, Lydian, Mixolydian, Pentatonic (mayor)
    REQUIRE (offs (Scale::Major,       Voicing::Third) == std::vector<int> { 4 });
    REQUIRE (offs (Scale::Lydian,      Voicing::Third) == std::vector<int> { 4 });
    REQUIRE (offs (Scale::Mixolydian,  Voicing::Third) == std::vector<int> { 4 });
    REQUIRE (offs (Scale::Pentatonic,  Voicing::Third) == std::vector<int> { 4 });

    // Modos con 3ra MENOR (+3): Minor natural, Dorian, Phrygian, Harmonic Minor
    REQUIRE (offs (Scale::MinorNatural,  Voicing::Third) == std::vector<int> { 3 });
    REQUIRE (offs (Scale::Dorian,        Voicing::Third) == std::vector<int> { 3 });
    REQUIRE (offs (Scale::Phrygian,      Voicing::Third) == std::vector<int> { 3 });
    REQUIRE (offs (Scale::HarmonicMinor, Voicing::Third) == std::vector<int> { 3 });
}

// ── 3rd+5th: apila la 3ra del modo + la 5ta perfecta ─────────────────────────────────────────────
TEST_CASE ("pitch/scale: 3rd+5th apila la 3ra del modo y la 5ta", "[pitch][scale]") {
    REQUIRE (offs (Scale::Major,        Voicing::ThirdFifth) == std::vector<int> { 4, 7 });
    REQUIRE (offs (Scale::MinorNatural, Voicing::ThirdFifth) == std::vector<int> { 3, 7 });
    REQUIRE (offs (Scale::Dorian,       Voicing::ThirdFifth) == std::vector<int> { 3, 7 });
    REQUIRE (offs (Scale::Lydian,       Voicing::ThirdFifth) == std::vector<int> { 4, 7 });
}

// ── Triad: 3ra del modo + 5ta + octava (la tríada del modo, una capa coral consonante) ───────────
TEST_CASE ("pitch/scale: Triad = 3ra del modo + 5ta + octava", "[pitch][scale]") {
    REQUIRE (offs (Scale::Major,        Voicing::Triad) == std::vector<int> { 4, 7, 12 });  // tríada mayor
    REQUIRE (offs (Scale::MinorNatural, Voicing::Triad) == std::vector<int> { 3, 7, 12 });  // tríada menor
    REQUIRE (offs (Scale::Dorian,       Voicing::Triad) == std::vector<int> { 3, 7, 12 });
    REQUIRE (offs (Scale::Mixolydian,   Voicing::Triad) == std::vector<int> { 4, 7, 12 });
    REQUIRE (offs (Scale::HarmonicMinor,Voicing::Triad) == std::vector<int> { 3, 7, 12 });
}

// ── Invariantes generales: todas las voces son consonantes con la escala (pertenecen al set de la
//    escala módulo 12, salvo la octava que es la raíz transpuesta). Determinístico, sin sorpresas. ─
TEST_CASE ("pitch/scale: toda voz cae en la escala (consonancia), no hay offsets fuera del set", "[pitch][scale]") {
    for (Scale s : { Scale::Major, Scale::MinorNatural, Scale::Dorian, Scale::Phrygian,
                     Scale::Lydian, Scale::Mixolydian, Scale::HarmonicMinor, Scale::Pentatonic })
    {
        for (Voicing v : { Voicing::Octave, Voicing::Fifth, Voicing::Third,
                           Voicing::ThirdFifth, Voicing::Triad })
        {
            const auto r = ScaleQuantizer::voicingOffsets (s, v);
            REQUIRE (r.size() >= 1);
            for (int semis : r) {
                REQUIRE (semis > 0);                 // todas suben (shimmer hacia arriba)
                REQUIRE (ScaleQuantizer::isInScale (s, semis));   // pertenece a la escala (consonante)
            }
            // offsets en orden ascendente (apilado coherente)
            for (size_t i = 1; i < r.size(); ++i)
                REQUIRE (r[i] > r[i - 1]);
        }
    }
}

// ── La tabla de escalas en sí: cada escala expone su set de semitonos (0..11) canónico (spec §3.2) ─
TEST_CASE ("pitch/scale: la tabla de semitonos de cada escala es la canónica", "[pitch][scale]") {
    auto degs = [] (Scale s) {
        const auto r = ScaleQuantizer::scaleDegrees (s);
        return std::vector<int> (r.begin(), r.end());
    };
    REQUIRE (degs (Scale::Major)         == std::vector<int> { 0, 2, 4, 5, 7, 9, 11 });
    REQUIRE (degs (Scale::MinorNatural)  == std::vector<int> { 0, 2, 3, 5, 7, 8, 10 });
    REQUIRE (degs (Scale::Dorian)        == std::vector<int> { 0, 2, 3, 5, 7, 9, 10 });
    REQUIRE (degs (Scale::Phrygian)      == std::vector<int> { 0, 1, 3, 5, 7, 8, 10 });
    REQUIRE (degs (Scale::Lydian)        == std::vector<int> { 0, 2, 4, 6, 7, 9, 11 });
    REQUIRE (degs (Scale::Mixolydian)    == std::vector<int> { 0, 2, 4, 5, 7, 9, 10 });
    REQUIRE (degs (Scale::HarmonicMinor) == std::vector<int> { 0, 2, 3, 5, 7, 8, 11 });
    REQUIRE (degs (Scale::Pentatonic)    == std::vector<int> { 0, 2, 4, 7, 9 });        // mayor pentatónica
}
