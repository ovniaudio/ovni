#pragma once

#include <juce_core/juce_core.h>   // juce::jlimit/jmax (helpers de la tabla de divisiones del SYNC)

// =============================================================================
// NÉBULA — IDs de parámetros del APVTS. Fuente única de verdad (un solo lugar,
// nunca strings sueltos). Los IDs van en minúscula para casar con la convención
// del chasis del sello (ovni::PluginProcessorBase::bypassParamID() default =
// "bypass"). El chasis INYECTA inGain/output/monoSafe — NÉBULA NO los declara.
//
// Las 5 macros perceptuales del reverb FDN (spec §5) + bypass. El processor las
// lee con getRawParameterValue y las mapea %→0..1 a ovni::engines::FdnParams.
//
// SYNC de la respiración (append-only, NO rompe recall): breathSync (bool) engancha
// el Breath al tempo del host; breathDiv (choice) elige la división del ciclo.
// =============================================================================
namespace nebula::params::id
{
    inline constexpr const char* SIZE       = "size";       // 0..100 % escala las longitudes de delay (espacio)
    inline constexpr const char* DECAY      = "decay";      // 0..100 % T60 (mapeo interno a s); 100% = freeze (cola infinita)
    inline constexpr const char* TONE       = "tone";       // 0..100 % damping HF en el lazo (agudos decaen antes)
    inline constexpr const char* BREATH     = "breath";     // 0..100 % profundidad de la respiración (mod lenta de Size)
    inline constexpr const char* MIX        = "mix";        // 0..100 % dry/wet
    inline constexpr const char* LOWCUT     = "lowCut";     // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz); pasa-altos del plugin, corta todo lo que sale → se oye a cualquier MIX
    inline constexpr const char* HICUT      = "hiCut";      // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz); pasa-bajos del plugin, corta todo lo que sale → se oye a cualquier MIX
    inline constexpr const char* BYPASS     = "bypass";     // bool (lo lee el header del chasis)
    inline constexpr const char* BREATHSYNC = "breathSync"; // bool: respiración libre / enganchada al tempo
    inline constexpr const char* BREATHDIV  = "breathDiv";  // choice: división del ciclo de respiración
}

namespace nebula::params::sync
{
    // Divisiones para la respiración (Breath) en modo sync. beatsPerCycle = cuántos beats (negras) dura
    // UN ciclo completo de la respiración (inhala+exhala). Más beats = respiración más lenta. El orden de
    // la tabla DEBE casar 1:1 con el orden del AudioParameterChoice (índice = valor del choice).
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1/2",    2.f },   // medio compás 4/4
        { "1 bar",  4.f },   // un compás (default)
        { "2 bars", 8.f },
        { "4 bars", 16.f }
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    // Índice por defecto = "1 bar" (debe coincidir con el default del AudioParameterChoice del layout).
    inline constexpr int kDefaultIndex = 1;

    // División FIJA del SYNC = "1 bar" (4 beats/ciclo). El editor ya NO expone el selector de divisiones
    // ("con el SYNC solo estamos"): cuando SYNC está on, el motor engancha SIEMPRE a un compás. El param
    // breathDiv sigue en el layout (append-only, recall de presets viejos) pero el processor usa esto.
    inline constexpr int kBarDivIndex = 1;   // == "1 bar" en la tabla divs[]

    // beatsPerCycle de un índice de división (clampeado). Helper público → el processor y los tests
    // comparten esta tabla (no derivan). Estático y puro.
    inline float beatsForDiv (int divIndex) noexcept
    {
        return divs[juce::jlimit (0, kCount - 1, divIndex)].beatsPerCycle;
    }

    // Frecuencia de respiración (Hz, ciclos/seg) dado el BPM del host y el índice de división.
    // breathRateHz = (bpm/60 beats por seg) / (beats por ciclo). Helper compartido con los tests.
    inline float breathRateHz (double bpm, int divIndex) noexcept
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
