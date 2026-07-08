#pragma once
#include <array>
#include <cstddef>

namespace ovni::engines {

// =====================================================================================
// ScaleQuantizer — PURO, header-only, sin estado, sin audio (spec §3.2).
//
// El corazón de la honestidad del módulo Escala+Tono: dado un MODO (escala) y un VOICING
// (qué intervalos apilar), devuelve los OFFSETS en semitonos *consonantes con la escala*.
// La 3ra es +4 en Mayor y +3 en Menor (el modo fija el sabor); la octava es +12 SIEMPRE.
//
// "Pegar a la TONALIDAD (root)" sobre poly NO está acá (requiere re-afinado espectral) — es
// un plugin futuro. Este quantizer da intervalos PARALELOS consonantes, robusto en cualquier
// fuente (pads, acordes, voz, percusión). Ver spec §2.
//
// Reusable: HALU/HALO toma las voces de acá y se las pasa al PitchShifter; cualquier plugin
// key-aware futuro le sumará tracking/espectral POR ENCIMA y reusa esta tabla.
// =====================================================================================

class ScaleQuantizer
{
public:
    // Modos soportados (spec §3.2). El COLOR de las 3ras/6tas/7mas lo da el modo.
    enum class Scale
    {
        Major,          // jónico       {0,2,4,5,7,9,11}
        MinorNatural,   // eólico       {0,2,3,5,7,8,10}
        Dorian,         //              {0,2,3,5,7,9,10}
        Phrygian,       //              {0,1,3,5,7,8,10}
        Lydian,         //              {0,2,4,6,7,9,11}
        Mixolydian,     //              {0,2,4,5,7,9,10}
        HarmonicMinor,  //              {0,2,3,5,7,8,11}
        Pentatonic      // mayor pent.  {0,2,4,7,9}
    };

    // VOICING = qué intervalos apila el shimmer. Devuelve offsets ascendentes (todos suben).
    enum class Voicing
    {
        Octave,      // {+12}                  — clásico, SIEMPRE en tono, default seguro
        Fifth,       // {+7}                   — 5ta perfecta, consonante en cualquier modo
        Third,       // {+4 mayor | +3 menor}  — el MODO fija el sabor
        ThirdFifth,  // {3ra del modo, +7}
        Triad        // {3ra del modo, +7, +12} — la tríada del modo
    };

    // ── Capacidad fija (sin asignaciones): el voicing más grande (Triad) son 3 offsets. Result es
    //    un array embebido + un tamaño; se itera con begin()/end() y size(). RT-safe.
    static constexpr int kMaxVoices = 3;

    struct Offsets
    {
        std::array<int, (size_t) kMaxVoices> data {};
        int count = 0;

        constexpr const int* begin() const noexcept { return data.data(); }
        constexpr const int* end()   const noexcept { return data.data() + count; }
        constexpr int        size()  const noexcept { return count; }
        constexpr int        operator[] (int i) const noexcept { return data[(size_t) i]; }
    };

    // ── Tabla de escalas: el set de semitonos (0..11) de cada modo, ascendente desde la raíz.
    //    Capacidad 7 (la pentatónica usa 5). Única fuente de verdad (los tests comparan contra esto).
    struct Degrees
    {
        std::array<int, 7> data {};
        int count = 0;
        constexpr const int* begin() const noexcept { return data.data(); }
        constexpr const int* end()   const noexcept { return data.data() + count; }
        constexpr int        size()  const noexcept { return count; }
        constexpr int        operator[] (int i) const noexcept { return data[(size_t) i]; }
    };

    static constexpr Degrees scaleDegrees (Scale s) noexcept
    {
        switch (s)
        {
            case Scale::Major:         return { { 0, 2, 4, 5, 7, 9, 11 }, 7 };
            case Scale::MinorNatural:  return { { 0, 2, 3, 5, 7, 8, 10 }, 7 };
            case Scale::Dorian:        return { { 0, 2, 3, 5, 7, 9, 10 }, 7 };
            case Scale::Phrygian:      return { { 0, 1, 3, 5, 7, 8, 10 }, 7 };
            case Scale::Lydian:        return { { 0, 2, 4, 6, 7, 9, 11 }, 7 };
            case Scale::Mixolydian:    return { { 0, 2, 4, 5, 7, 9, 10 }, 7 };
            case Scale::HarmonicMinor: return { { 0, 2, 3, 5, 7, 8, 11 }, 7 };
            case Scale::Pentatonic:    return { { 0, 2, 4, 7, 9, 0, 0 }, 5 };
        }
        return { { 0, 2, 4, 5, 7, 9, 11 }, 7 };   // unreachable; Major por seguridad
    }

    // ── La 3ra DIATÓNICA del modo = el 3er grado de la escala (índice 2 del set ascendente).
    //    Major/Lydian/Mixolydian/Pentatonic -> +4 (mayor); Minor/Dorian/Phrygian/HarmMinor -> +3.
    //    Derivado de la tabla (no hardcodeado por modo) -> imposible que derive de scaleDegrees.
    static constexpr int thirdSemitones (Scale s) noexcept
    {
        const Degrees d = scaleDegrees (s);
        return d[2];   // 0=raíz, 1=2da, 2=3ra
    }

    // ── 5ta PERFECTA: +7. Consonante en todos los modos listados (todos contienen +7).
    static constexpr int fifthSemitones (Scale) noexcept { return 7; }

    // ── Octava: +12, SIEMPRE (independiente del modo).
    static constexpr int octaveSemitones (Scale) noexcept { return 12; }

    // ── El cálculo central: (escala, voicing) -> offsets en semitonos consonantes, ascendentes.
    static constexpr Offsets voicingOffsets (Scale s, Voicing v) noexcept
    {
        const int third = thirdSemitones (s);
        switch (v)
        {
            case Voicing::Octave:     return { { 12, 0, 0 }, 1 };
            case Voicing::Fifth:      return { {  7, 0, 0 }, 1 };
            case Voicing::Third:      return { { third, 0, 0 }, 1 };
            case Voicing::ThirdFifth: return { { third, 7, 0 }, 2 };
            case Voicing::Triad:      return { { third, 7, 12 }, 3 };
        }
        return { { 12, 0, 0 }, 1 };   // unreachable; Octave por seguridad
    }

    // ── Pertenencia a la escala (consonancia): ¿el offset (en semitonos, puede ser >12) cae en
    //    algún grado de la escala módulo 12? La octava +12 -> grado 0 (la raíz transpuesta).
    static constexpr bool isInScale (Scale s, int semitones) noexcept
    {
        int pc = semitones % 12;
        if (pc < 0) pc += 12;
        const Degrees d = scaleDegrees (s);
        for (int i = 0; i < d.size(); ++i)
            if (d[i] == pc) return true;
        return false;
    }
};

} // namespace ovni::engines
