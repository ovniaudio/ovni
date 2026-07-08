#pragma once

#include <juce_core/juce_core.h>

// =============================================================================
// HALO — IDs de parámetros del APVTS. Fuente única de verdad (un solo lugar,
// nunca strings sueltos). IDs en minúscula para casar con la convención del
// chasis (ovni::PluginProcessorBase::bypassParamID() default = "bypass"). El
// chasis INYECTA inGain/output/monoSafe — HALO NO los declara.
//
// REDISEÑO "VASTEDAD" (base técnica §7): 6 macros + SYNC + FREEZE.
//   MIX / SIZE / DECAY / SHIMMER / TONE / ORBIT  +  bypass  +  FREEZE (toggle).
// HONESTIDAD (base técnica §1): NO se expone SCALE ni VOICING — la matemática del
// feedback desmiente el "scale-aware" (sólo la octava es invariante bajo recursión).
// Las voces del shimmer son FIJAS (octava + quinta), hardcodeadas en el motor. BREATH
// también sale de la UI (pasa a interno fijo ≈0.30).
//
// SYNC = control CORE del sello OVNI (va en TODOS los plugins). En HALO controla su
// elemento rítmico natural: el RATE de la ÓRBITA espacial (la trayectoria del halo
// alrededor de la cabeza). orbitSync (bool) la engancha al tempo del host; orbitDiv
// (choice) elige cuántos compases dura una vuelta. orbitRate (float, Hz) = la VELOCIDAD
// de la órbita en modo FREE, elegida por el usuario con la perilla RATE del SyncControl
// (antes era fija a 0.08 Hz → Joaquín pidió poder elegirla). En SYNC manda orbitDiv.
//
// Las etiquetas de orbitDiv salen de la tabla sync::divs[] (compartida con el processor
// y los tests → una sola fuente de verdad).
// =============================================================================
namespace halo::params::id
{
    inline constexpr const char* MIX        = "mix";        // 0..100 % dry/wet
    inline constexpr const char* SIZE       = "size";       // 0..100 % tamaño del difusor FDN + pre-delay
    inline constexpr const char* DECAY      = "decay";      // 0..100 % feedback del lazo (piso log; "cuánto dura")
    inline constexpr const char* SHIMMER    = "shimmer";    // 0..100 % capa pitched reinyectada al lazo
    inline constexpr const char* TONE       = "tone";       // 0..100 % LP de banda del lazo (oscuridad = estabilidad)
    inline constexpr const char* ORBIT      = "orbit";      // 0..100 % profundidad del pan binaural ITD/ILD de la órbita (cuánto te rodea el halo); 0 = wash quieto, 100 = órbita plena
    inline constexpr const char* LOWCUT     = "lowCut";     // 0..100 % high-pass del WET que entra a la cola (20 Hz=off → 500 Hz); saca los graves antes del lazo, el DRY pasa entero
    inline constexpr const char* HICUT      = "hiCut";      // 0..100 % low-pass del WET de la cola (20 kHz=off → 1.5 kHz); oscurece la cola cortando agudos, el DRY pasa entero
    inline constexpr const char* BYPASS     = "bypass";     // bool (lo lee el header del chasis)
    inline constexpr const char* FREEZE     = "freeze";     // bool: captura la nube (feedback=1, input→0)
    inline constexpr const char* ORBITSYNC  = "orbitSync";  // bool: órbita libre / enganchada al tempo (SYNC core)
    inline constexpr const char* ORBITDIV   = "orbitDiv";   // choice: compases por vuelta de la órbita
    inline constexpr const char* ORBITRATE  = "orbitRate";  // float Hz: velocidad de la órbita en FREE (perilla RATE)
}

// Velocidad orbital por defecto en FREE (Hz). Glacial — Joaquín pidió que arranque MÁS LENTO (incluso el
// 0.02 Hz del primer intento le quedaba rápido para hacerlo musical). 0.01 Hz ≈ 1 vuelta cada 100 s.
namespace halo::params { inline constexpr float kOrbitFreeDefaultHz = 0.01f; }

// ── SYNC de la órbita (control CORE del sello). Tabla de divisiones + helpers PUROS compartidos con el
//    processor y los tests (una sola fuente de verdad — nadie deriva la fórmula por su lado). Mismo
//    mecanismo que NÉBULA, aplicado al RATE orbital (vueltas largas: el halo orbita lento, glacial).
namespace halo::params::sync
{
    // beatsPerCycle = cuántos beats (negras) dura UNA vuelta completa de la órbita. Más beats = órbita más
    // lenta. El orden DEBE casar 1:1 con el AudioParameterChoice (índice = valor).
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1 bar",  4.f },    // un compás 4/4 (rápido)
        { "2 bars", 8.f },
        { "4 bars", 16.f },   // cuatro compases (default — vuelta lenta, glacial)
        { "8 bars", 32.f }
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    // Índice por defecto = "4 bars" (vuelta lenta; debe coincidir con el default del AudioParameterChoice).
    inline constexpr int kDefaultIndex = 2;

    // beatsPerCycle de un índice de división (clampeado). Estático y puro.
    inline float beatsForDiv (int divIndex) noexcept
    {
        return divs[juce::jlimit (0, kCount - 1, divIndex)].beatsPerCycle;
    }

    // Frecuencia orbital (Hz, vueltas/seg) dado el BPM del host y el índice de división.
    // orbitRateHz = (bpm/60 beats por seg) / (beats por vuelta). Helper compartido con los tests.
    inline float orbitRateHz (double bpm, int divIndex) noexcept
    {
        const double bps = juce::jmax (1.0e-6, bpm) / 60.0;            // beats por segundo
        return (float) (bps / (double) juce::jmax (0.01f, beatsForDiv (divIndex)));
    }

    // Etiquetas de la tabla divs[] como StringArray (una sola fuente de verdad para el editor y el SyncControl).
    inline juce::StringArray labels()
    {
        juce::StringArray a;
        for (auto& d : divs) a.add (d.name);
        return a;
    }
}
