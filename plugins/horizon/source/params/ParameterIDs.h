#pragma once

#include <juce_core/juce_core.h>

// =============================================================================
// HORIZON — IDs de parámetros del APVTS (SPL·02). Fuente única de verdad (un solo
// lugar, nunca strings sueltos). IDs en minúscula/camelCase para casar con la
// convención del chasis (ovni::PluginProcessorBase::bypassParamID() default =
// "bypass"). El chasis INYECTA inGain/output/monoSafe — HORIZON NO los declara.
//
// Curaduría 2026-06-10 (ES LEY): 6 controles + SYNC del re-trigger.
//   FREEZE (gesto latch, bool automatable) · RATE (+ SyncControl) · WHISPER ·
//   SPREAD · DUCK · MIX.
// ✂ CORTE de nombre: "Shimmer/Whisper" → etiqueta ÚNICA "WHISPER" (0 = vidrioso
//   coherente, 100 = whisperization). "Shimmer" ya es identidad de HALO.
// ✂ CORTE de modo: el re-trigger v1 es SOLO gate del frame sostenido (NO re-captura
//   viva). Default WHISPER 12 (vivo pero cristalino).
//
// SYNC = control CORE del sello (va en TODOS los plugins). En HORIZON controla su
// elemento rítmico natural: el RE-TRIGGER/gate del freeze (el latido del congelado).
// rateSync (bool) lo engancha al tempo; rateDivision (choice) elige cuántos beats
// dura un ciclo del latido; rate (float Hz, log) = la velocidad en modo FREE.
// Las etiquetas salen de la tabla sync::divs[] (única fuente de verdad compartida
// con el processor y los tests).
// =============================================================================
namespace horizon::params::id
{
    inline constexpr const char* FREEZE   = "freeze";       // bool: gesto central (latch). Captura/suelta el frame espectral
    inline constexpr const char* WHISPER  = "whisper";      // 0..100 % coherencia↔randomización de fase (0 = vidrioso/limpio)
    inline constexpr const char* SPREAD   = "spread";       // 0..100 % des-correlación L/R + paneo de potencia constante del freeze
    inline constexpr const char* DUCK     = "duck";         // 0..100 % el wet (freeze) se aparta cuando pega el dry (envelope del dry)
    inline constexpr const char* MIX      = "mix";          // 0..100 % dry/wet (ley de potencia; el dry viaja retrasado N para alinear)
    inline constexpr const char* BYPASS   = "bypass";       // bool (lo lee el header del chasis)
    inline constexpr const char* RATESYNC = "rateSync";     // bool: latido libre / enganchado al tempo (SYNC core)
    inline constexpr const char* RATEDIV  = "rateDivision"; // choice: beats por ciclo del re-trigger/gate
    inline constexpr const char* RATE     = "rate";         // float Hz: velocidad del re-trigger en FREE (perilla RATE, log; 0 = sostenido)
    inline constexpr const char* LOWCUT   = "lowCut";       // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz, log). Filtro del plugin
    inline constexpr const char* HICUT    = "hiCut";        // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz, log inv). Filtro del plugin
}

namespace horizon::params
{
    // RATE del re-trigger en FREE (Hz). 0 = OFF (frame sostenido = pad clásico).
    // Cota dura: 8 Hz (curaduría: más rápido = AM/zumbido, no groove). El rango del
    // knob arranca en 0 (sostenido) hasta kRateMaxHz; el motor capa a kRateMaxHz.
    inline constexpr float kRateMinHz     = 0.0f;   // 0 = sostenido (pad)
    inline constexpr float kRateMaxHz     = 8.0f;   // cota dura del groove (curaduría)
    inline constexpr float kRateFreeDefaultHz = 0.0f;   // default OFF (sostenido) — curaduría
}

// ── SYNC del re-trigger (control CORE del sello). Tabla de divisiones + helpers
//    PUROS compartidos con el processor y los tests (una sola fuente de verdad —
//    nadie deriva la fórmula por su lado). Subset de curaduría HORIZON:
//    1/16 · 1/8 · 1/4 · 1/2 · 1 bar · 2 bar (el más amplio del catálogo: del
//    stutter glitch al pad gateado lento).
namespace horizon::params::sync
{
    // beatsPerCycle = cuántos beats (negras) dura UN ciclo del latido (un re-trigger
    // completo del gate). El orden DEBE casar 1:1 con el AudioParameterChoice (índice = valor).
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1/16",  0.25f },  // el latido más rápido (stutter glitch)
        { "1/8",   0.5f  },
        { "1/4",   1.f   },  // un beat por ciclo (default — gate al pulso)
        { "1/2",   2.f   },
        { "1 bar", 4.f   },  // un compás 4/4
        { "2 bar", 8.f   }   // dos compases (pad gateado lento)
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    // Índice por defecto = "1/4" (gate al pulso; debe coincidir con el default del AudioParameterChoice).
    inline constexpr int kDefaultIndex = 2;

    // beatsPerCycle de un índice de división (clampeado). Estático y puro.
    inline float beatsForDiv (int divIndex) noexcept
    {
        return divs[juce::jlimit (0, kCount - 1, divIndex)].beatsPerCycle;
    }

    // Frecuencia del re-trigger (Hz, latidos/seg) dado el BPM del host y el índice de
    // división. rateHz = (bpm/60 beats por seg) / (beats por ciclo). Helper compartido
    // con los tests. La COTA DURA (8 Hz) la clampea el motor (física cableada).
    inline float syncRateHz (double bpm, int divIndex) noexcept
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
