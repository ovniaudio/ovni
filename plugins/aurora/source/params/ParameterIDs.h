#pragma once

#include <juce_core/juce_core.h>

// =============================================================================
// AURORA — IDs de parámetros del APVTS. Fuente única de verdad (un solo lugar,
// nunca strings sueltos). IDs en minúscula/camelCase para casar con la convención
// del chasis (ovni::PluginProcessorBase::bypassParamID() default = "bypass"). El
// chasis INYECTA inGain/output/monoSafe — AURORA NO los declara.
//
// Curaduría 2026-06-10 (ES LEY): 6 macros + SYNC del MOTION.
//   SPREAD / TILT / MOTION (+ SyncControl RATE·SYNC·división) / MONO SAFE / DUCK / MIX.
// ✂ CORTE de curaduría: NO hay curvas custom dibujables (TILT bipolar cubre el
// carácter del mapeo; candidato v2). NO agregar.
//
// ⚠ MONOSAFEAMT: id "monoSafeAmt" — DISTINTO del "monoSafe" que inyecta el chasis
// (ese es el toggle IN PHASE, bass-mono genérico post-DSP). Este knob es la red
// PROPIA de AURORA: la frecuencia de colapso-al-centro del despliegue espectral
// (0–100 → 60→700 Hz log; 0 = sin red). Dos conceptos, dos ids, cero colisión.
//
// SYNC = control CORE del sello (va en TODOS los plugins). En AURORA controla su
// elemento rítmico natural: el MOTION del abanico espectral (el despliegue se
// abre/cierra). motionSync (bool) lo engancha al tempo; motionDivision (choice)
// elige cuántos beats dura un ciclo; motionRate (float Hz, log) = la velocidad
// en modo FREE. Las etiquetas salen de la tabla sync::divs[] (única fuente de
// verdad compartida con el processor y los tests).
// =============================================================================
namespace aurora::params::id
{
    inline constexpr const char* SPREAD      = "spread";        // 0..100 % cuánto se separan las bandas (escala global del ángulo por bin)
    inline constexpr const char* TILT        = "tilt";          // -100..+100 curva frecuencia→posición (0 = graves-centro/agudos-bordes; negativo invierte)
    inline constexpr const char* MOTION      = "motion";        // 0..100 % profundidad del abanico que se abre/cierra (LFO sobre el ángulo global)
    inline constexpr const char* MONOSAFEAMT = "monoSafeAmt";   // 0..100 % red mono: corte de colapso-al-centro 60→700 Hz log (0 = sin red). ≠ "monoSafe" del chasis (IN PHASE)
    inline constexpr const char* DUCK        = "duck";          // 0..100 % el despliegue se cierra cuando pega el dry (envelope-follower → γ)
    inline constexpr const char* MIX         = "mix";           // 0..100 % dry/wet (ley de potencia; el dry viaja retrasado N para alinear)
    inline constexpr const char* BYPASS      = "bypass";        // bool (lo lee el header del chasis)
    inline constexpr const char* MOTIONSYNC  = "motionSync";    // bool: MOTION libre / enganchado al tempo (SYNC core)
    inline constexpr const char* MOTIONDIV   = "motionDivision";// choice: beats por ciclo del abanico
    inline constexpr const char* MOTIONRATE  = "motionRate";    // float Hz: velocidad del MOTION en FREE (perilla RATE, log)
    inline constexpr const char* LOWCUT      = "lowCut";        // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz, log). Filtro del plugin
    inline constexpr const char* HICUT       = "hiCut";         // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz, log inv). Filtro del plugin
}

namespace aurora::params
{
    // RATE del MOTION en FREE (Hz). Default 0.3 Hz = el abanico respira lento, listo
    // para subir Motion sin tocar nada más (curaduría: "rate FREE listo en 0.3 Hz").
    inline constexpr float kMotionRateMinHz     = 0.02f;
    inline constexpr float kMotionRateMaxHz     = 8.0f;
    inline constexpr float kMotionFreeDefaultHz = 0.3f;
}

// ── SYNC del MOTION (control CORE del sello). Tabla de divisiones + helpers PUROS
//    compartidos con el processor y los tests (una sola fuente de verdad — nadie
//    deriva la fórmula por su lado). Subset de curaduría: 1/4 · 1/2 · 1 bar · 2 bar
//    (contiguo del set canónico; mismo subset que PULSAR).
namespace aurora::params::sync
{
    // beatsPerCycle = cuántos beats (negras) dura UN ciclo completo del abanico
    // (abrir + cerrar). El orden DEBE casar 1:1 con el AudioParameterChoice (índice = valor).
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1/4",   1.f },   // un beat por ciclo (el más rápido del subset)
        { "1/2",   2.f },
        { "1 bar", 4.f },   // un compás 4/4 (default — late con el compás)
        { "2 bar", 8.f }
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    // Índice por defecto = "1 bar" (debe coincidir con el default del AudioParameterChoice).
    inline constexpr int kDefaultIndex = 2;

    // beatsPerCycle de un índice de división (clampeado). Estático y puro.
    inline float beatsForDiv (int divIndex) noexcept
    {
        return divs[juce::jlimit (0, kCount - 1, divIndex)].beatsPerCycle;
    }

    // Frecuencia del MOTION (Hz, ciclos/seg) dado el BPM del host y el índice de división.
    // motionRateHz = (bpm/60 beats por seg) / (beats por ciclo). Helper compartido con los tests.
    // La COTA ANTI-AM (< 20 Hz) NO vive acá: la clampea el motor (física cableada, AuroraEngine).
    inline float motionRateHz (double bpm, int divIndex) noexcept
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
