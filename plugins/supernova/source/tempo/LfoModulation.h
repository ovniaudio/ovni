#pragma once
// LfoModulation — cómo la salida del LfoBank llega a UN parámetro, en unidades APVTS. Pura (sin JUCE) para
// que el test mida EXACTAMENTE la función que usa el render, no una réplica.
//
// Dos reglas, las dos verificadas por LfoModulationTest:
//  1) ESCALA — un LFO "a fondo" (depth = 100 %) recorre exactamente el rango del parámetro, no el doble:
//        bipolar  → valueFor ∈ [−depth, +depth] → ×(rango/2) → excursión = depth·rango, centrada en la base
//        unipolar → valueFor ∈ [0, +depth]      → ×rango     → excursión = depth·rango, de la base hacia arriba
//  2) CLAMP — la suma (base + todos los slots que apuntan al param) se recorta al rango legal. Sin esto, dos
//     LFO al mismo destino se suman sin normalizar y el shader recibe tamaño de partícula o saturación
//     NEGATIVOS (medido: particleSize = −1.25 px).
#include "tempo/LfoBank.h"
#include <algorithm>

namespace supernova
{
// Unidades del parámetro por unidad de valueFor(), para un slot y un rango [lo,hi].
inline float lfoSlotScale (const LfoSlot& s, float lo, float hi) noexcept
{
    const float range = hi - lo;
    return s.bipolar ? range * 0.5f : range;
}

// Valor del parámetro con la modulación de TODOS los slots habilitados que lo apuntan, clampeado a [lo,hi].
inline float lfoModulated (const LfoBank& bank, const char* paramId, float base,
                           float lo, float hi, double phaseInBeats, double timeSeconds = 0.0) noexcept
{
    float v = base;
    for (int i = 0; i < LfoBank::kNum; ++i)
    {
        const LfoSlot& s = bank.slot (i);
        if (! s.enabled || s.target.empty() || s.target != paramId) continue;
        v += bank.valueFor (i, phaseInBeats, timeSeconds) * lfoSlotScale (s, lo, hi);
    }
    return std::clamp (v, std::min (lo, hi), std::max (lo, hi));
}
}
