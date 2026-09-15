#pragma once
#include "data/Rules.h"
#include "lenses/Strings.h"
#include <juce_core/juce_core.h>
#include <cmath>

// ========================================================================================================
// NoteName — de una frecuencia a la NOTA que suena, en un solo lugar.
//
// La usan SPECTRUM (lente 3, en la lectura del cursor), CQT (6) y SPIRAL (7). Vivía dentro de SpectrumLens
// hasta el prompt 52; se movió acá cuando la pidió la segunda lente, no antes. Tres copias de la misma
// fórmula son tres oportunidades de que se separen.
//
//     cents = 1200 · log2 (f / f_nota)      con A4 = 440 Hz y numeración científica (C4 = do central)
//
// Los nombres van en notación anglosajona (C, C#, D…) porque es la que rotula los ejes de todo el mundo
// —un piano dibujado dice C1, no Do1— y porque el número de octava sólo tiene sentido pegado a esa letra.
// El texto de la TONALIDAD, en cambio, va en castellano ("La menor"): ahí no hay eje que rotular, hay una
// frase que leer. Son dos usos distintos, no una inconsistencia.
// ========================================================================================================
namespace telescope
{
struct NoteReadout
{
    juce::String name;      // "A4", "C#3"…
    int          cents = 0; // desviación respecto de la nota, en cents
    int          midi  = 0; // la nota más cercana, en numeración MIDI (A4 = 69)
};

inline NoteReadout noteForFrequency (double freqHz)
{
    NoteReadout r;
    if (freqHz <= 0.0) return r;

    const double semis = 12.0 * std::log2 (freqHz / 440.0);
    const int    n     = (int) std::lround (semis);
    const double fNote = 440.0 * std::pow (2.0, (double) n / 12.0);
    r.cents = (int) std::lround (1200.0 * std::log2 (freqHz / fNote));

    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    r.midi = n + 69;
    const int pc  = ((r.midi % 12) + 12) % 12;
    const int oct = (int) std::floor ((double) r.midi / 12.0) - 1;   // científica: C4 = do central
    r.name = juce::String (names[pc]) + juce::String (oct);
    return r;
}

// ========================================================================================================
// LOS NOMBRES DE LAS DOCE CLASES, EN LAS TRES CONVENCIONES VIVAS (56b, D-50 · ampliado en el 56c)
//
// No son una traducción: son tres sistemas distintos de nombrar la misma nota, y los tres están en uso
// hoy. Letras (C D E) en el mundo anglosajón; solfeo (Do Re Mi) en el románico; y el germánico, que es
// como el de letras salvo en dos posiciones: el Si se llama H y el Si♭ se llama B. Qué usa cada idioma
// lo dice el campo `notes` de su tabla en Strings.h — a un italiano "A minor" no le dice nada, a un
// alemán "La menor" tampoco, y a un alemán "B" le dice una nota DISTINTA de la que suena.
//
// El nombre con OCTAVA (`noteForFrequency`, más arriba) sigue en anglosajón a propósito, y no es una
// inconsistencia: ahí se rotula un EJE, y los ejes de los analizadores del mundo dicen C1, no Do1 ni H1.
//
// El SUFIJO de modo (mayor/menor) sale de rules::phrase ("key.major" / "key.minor"), la misma tabla que
// usa VERDICT: si CQT dice "La menor", VERDICT tiene que decir exactamente lo mismo, y la única forma de
// garantizarlo es que salga del mismo lugar.
// ========================================================================================================
inline constexpr const char* kClassNamesLetter[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
inline constexpr const char* kClassNamesSolfege[12] = {
    "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"
};
// Igual que las letras salvo en las clases 10 y 11: el Si es H, y el Si♭ —que en anglosajón se escribe
// A#— es B. Ver el bloque de `NoteNaming` en Strings.h para por qué esto no es una preferencia de estilo.
inline constexpr const char* kClassNamesGerman[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "B", "H"
};

inline const char* classNameFor (int pitchClass, const juce::String& language)
{
    if (pitchClass < 0 || pitchClass > 11) return "";
    switch (strings::noteNamingOf (language))
    {
        case strings::NoteNaming::solfege: return kClassNamesSolfege[pitchClass];
        case strings::NoteNaming::german:  return kClassNamesGerman[pitchClass];
        case strings::NoteNaming::letters: break;
    }
    return kClassNamesLetter[pitchClass];
}

// "A minor" / "La menor" / "—" cuando no hay tonalidad estimada.
inline juce::String keyLabel (int tonic, int mode, const juce::String& language)
{
    if (tonic < 0 || tonic > 11) return juce::String::fromUTF8 ("\xe2\x80\x94");
    return juce::String (classNameFor (tonic, language)) + " "
         + juce::String::fromUTF8 (rules::phrase (mode == 1 ? "key.minor" : "key.major",
                                                  language.toRawUTF8()));
}
}
