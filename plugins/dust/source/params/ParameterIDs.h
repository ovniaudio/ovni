#pragma once

#include <juce_core/juce_core.h>

// =============================================================================
// DUST — IDs de parámetros del APVTS. Fuente única de verdad (un solo lugar,
// nunca strings sueltos en processor/editor/tests). IDs en minúscula/camelCase
// para casar con la convención del chasis (bypassParamID() default = "bypass").
// El chasis INYECTA inGain/output/monoSafe — DUST NO los declara.
//
// Curaduría (2026-06-10): 5 macros + DUCK + ORIGIN. CORTES firmes: sin knob
// TONE/DAMP (damping interno fijo en el motor) y sin ORIGIN orbitando (el drag
// del visualizador es la estrella). No agregar lo cortado.
// =============================================================================
namespace dust::params::id
{
    inline constexpr const char* MIX      = "mix";          // 0..100 % dry/wet (ley de potencia)
    inline constexpr const char* RATE     = "rate";         // FREE: ms entre ecos (20..2000, log)
    inline constexpr const char* RATESYNC = "rateSync";     // bool: FREE / enganchado al tempo
    inline constexpr const char* RATEDIV  = "rateDivision"; // choice: 1/16 · 1/8 · 1/4 · 1/2
    inline constexpr const char* DENSITY  = "density";      // 0..100 % "Densidad": estallido -> nube (feedback + nº de taps)
    inline constexpr const char* SPREAD   = "spread";       // 0..100 % varianza angular alrededor del ORIGIN
    inline constexpr const char* VIDA     = "vida";         // 0..100 % deriva por tap + jitter temporal acotado
    inline constexpr const char* DUCK     = "duck";         // 0..100 % sidechain interno (el dry agacha el wet)
    inline constexpr const char* ORIGINX  = "originX";      // -1..1 (lo arrastra el visualizador; + = derecha)
    inline constexpr const char* ORIGINY  = "originY";      // -1..1 (+ = frente; def 0.35 = centro-frente)
    inline constexpr const char* BYPASS   = "bypass";       // bool (lo lee el header del chasis)
    inline constexpr const char* LOWCUT   = "lowCut";       // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz, log). Filtro del plugin
    inline constexpr const char* HICUT    = "hiCut";        // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz, log inv). Filtro del plugin
}

namespace dust::params
{
    // Rango FREE del RATE (curaduría: 20 ms – 2 s, mapeo log). Mismo rango que clampea el motor.
    inline constexpr float kRateMinMs         = 20.0f;
    inline constexpr float kRateMaxMs         = 2000.0f;
    inline constexpr float kRateFreeDefaultMs = 250.0f;

    // ORIGIN default = centro-frente (curaduría): x 0, y 0.35.
    inline constexpr float kOriginXDefault = 0.0f;
    inline constexpr float kOriginYDefault = 0.35f;
}

namespace dust::params::sync
{
    // Divisiones de SYNC del RATE (subset contiguo del set canónico del diccionario, curaduría DUST:
    // 1/16 · 1/8 · 1/4 · 1/2). beatsPerCycle = beats (negras) entre ecos. Orden = chips izq→der;
    // el índice del AudioParameterChoice DEBE casar 1:1 con esta tabla.
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1/16", 0.25f },
        { "1/8",  0.5f },    // default — el eco de mezcla clásico
        { "1/4",  1.f },
        { "1/2",  2.f }
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    // Índice por defecto = "1/8" (debe coincidir con el default del AudioParameterChoice).
    inline constexpr int kDefaultIndex = 1;

    // beatsPerCycle de un índice de división (clampeado). Estático y puro.
    inline float beatsForDiv (int divIndex) noexcept
    {
        return divs[juce::jlimit (0, kCount - 1, divIndex)].beatsPerCycle;
    }

    // Espaciado entre ecos (ms) al tempo: rateMs = 60000·beatsPerCycle / bpm, clampeado al rango
    // FREE del motor. Helper puro compartido entre processAudio y los tests (una fuente de verdad).
    inline float rateMsForDiv (double bpm, int divIndex) noexcept
    {
        const double msPerBeat = 60000.0 / juce::jmax (1.0, bpm);
        return juce::jlimit (kRateMinMs, kRateMaxMs,
                             (float) (msPerBeat * (double) beatsForDiv (divIndex)));
    }

    // Etiquetas de la tabla divs[] como StringArray (una sola fuente de verdad para el SyncControl).
    inline juce::StringArray labels()
    {
        juce::StringArray a;
        for (auto& d : divs) a.add (d.name);
        return a;
    }
}
