#pragma once

#include <juce_core/juce_core.h>

// =============================================================================
// PULSAR — IDs de parámetros del APVTS. Fuente única de verdad (un solo lugar,
// nunca strings sueltos). Los IDs van en minúscula para casar con la convención
// del chasis del sello (ovni::PluginProcessorBase::bypassParamID() default =
// "bypass"). El chasis INYECTA inGain/output/monoSafe — PULSAR NO los declara.
//
// Rediseño "campo de caos": pocas perillas que acoplan muchos params por física.
// PULSAR no shippeó presets → redefinir el layout es seguro (no rompe recall).
// =============================================================================
namespace pulsar::params::id
{
    inline constexpr const char* MOTION   = "motion";    // 0..100 % energía (acopla caos+distancia+doppler)
    inline constexpr const char* RATE     = "rate";      // free: Hz · sync: knob decorativo (no codifica división)
    inline constexpr const char* SYNC     = "sync";      // bool: free / tempo
    inline constexpr const char* DIVISION = "division";  // choice: 1/4 · 1/2 · 1 bar · 2 bar (sync)
    inline constexpr const char* MIX      = "mix";       // 0..100 % dry/wet
    inline constexpr const char* SHAPE    = "shape";     // 0..100 % morph Orbit->Pendulum->Lorenz->Rossler
    inline constexpr const char* SMEAR    = "smear";     // 0..100 % cola de cometa
    inline constexpr const char* WIDTH    = "width";     // 0..100 % ancho (ITD/head-shadow)
    inline constexpr const char* LOWCUT   = "lowCut";    // 0..100 % HPF de salida (20..500 Hz log) — limpia graves
    inline constexpr const char* HICUT    = "hiCut";     // 0..100 % LPF de salida (20k..1.5k Hz log) — oscurece
    inline constexpr const char* FIELDX   = "fieldX";    // -1..1 centro del atractor (lo setea el visual)
    inline constexpr const char* FIELDY   = "fieldY";    // -1..1
    inline constexpr const char* BYPASS   = "bypass";    // bool (lo lee el header del chasis)
}

namespace pulsar::params::sync
{
    // Divisiones de SYNC. beatsPerCycle = beats que dura un ciclo del movimiento.
    // Orden = como se ven los chips (izq→der). Choice index 0..3.
    struct Div { const char* name; float beatsPerCycle; };
    inline constexpr Div divs[] = {
        { "1/4", 1.f }, { "1/2", 2.f }, { "1 bar", 4.f }, { "2 bar", 8.f }
    };
    inline constexpr int kCount = (int) (sizeof (divs) / sizeof (divs[0]));

    inline juce::StringArray labels()
    {
        juce::StringArray a;
        for (auto& d : divs) a.add (d.name);
        return a;
    }
}
