#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>

// ========================================================================================================
// shared/dsp/TruePeak.h — medidor de PICO INTER-SAMPLE (true-peak) por oversampling 4× Catmull-Rom.
//
// QUÉ ES (y qué NO es). El limiter de un plugin limita el pico DE MUESTRA (sample-peak): el máximo |x[i]|.
// Pero los medidores de los DAW y los conversores D/A miden el TRUE-PEAK: el nivel de la onda CONTINUA
// *entre* muestras, que puede pasar 0 dBFS aunque ninguna muestra lo haga (sobre todo con contenido HF).
// Ese es el "clip mínimo que sigue" que cazó ÓRBITA (techo de muestra 0.985 → true-peak 1.033 = +0.28 dBFS;
// el medidor del DAW marcaba clip). Ver references/anti-click-clip-truepeak.md §3.
//
// Este header reconstruye 4 sub-muestras entre cada par de muestras con la spline de Catmull-Rom y toma el
// peak de esa onda interpolada → estima el inter-sample peak que mediría un medidor true-peak.
//
//   ⚠ PROXY CONSERVADOR, NO BS.1770 CERTIFICADO. El estándar ITU-R BS.1770 / EBU R128 especifica un
//   oversampling ≥ 4× con un filtro FIR polifásico de fase lineal definido. Catmull-Rom NO es ese filtro:
//   es una interpolación cúbica local (4 puntos) que SUBESTIMA levemente los picos de banda muy alta y no
//   tiene el ripple controlado del FIR. Por eso lo declaramos honestamente como PROXY: es MUCHO mejor que
//   el sample-peak (capta el inter-sample que el sample-peak ignora del todo) y suficiente como GATE de
//   medición del sello, pero el número publicado se rotula "true-peak (proxy, oversample 4×)", no "BS.1770".
//   Si algún día se necesita el número certificado, se reemplaza la interpolación acá por el FIR de BS.1770
//   y el resto del harness no cambia (la firma de truePeakOf() es estable).
//
// Es la MISMA cuenta que vivía inline en plugins/nebula/tests/TransientTest.cpp (§3 del doc); se extrajo acá
// para que CUALQUIER plugin la reuse sin copy-paste (DRY) y la medición de transientes sea UNIFORME.
// ========================================================================================================
namespace ovni::dsp
{

// True-peak (lineal, NO dB) de un buffer mono `x` por oversampling 4× Catmull-Rom.
// Devuelve el máximo |valor| de la onda reconstruida (≥ sample-peak siempre). Para n<4 devuelve 0
// (no hay suficiente soporte para la spline de 4 puntos).
//
// Uso típico: truePeakOf(canalL) y truePeakOf(canalR), tomar el mayor. Para convertir a dBTP:
//   const float dbtp = 20.0f * std::log10 (truePeakOf (x) + 1e-12f);
inline float truePeakOf (const std::vector<float>& x)
{
    const int n = (int) x.size();
    if (n < 4) return 0.0f;

    constexpr int OS = 4;   // 4 sub-muestras entre cada par (factor de oversampling)

    // Acceso con clamp a los bordes (evita leer fuera del buffer en los extremos de la spline).
    auto at = [&] (int i) -> float { return x[(size_t) juce::jlimit (0, n - 1, i)]; };

    float tp = 0.0f;
    for (int i = 1; i < n - 2; ++i)
    {
        const float P0 = at (i - 1), P1 = at (i), P2 = at (i + 1), P3 = at (i + 2);
        for (int s = 0; s < OS; ++s)
        {
            const float t = (float) s / (float) OS;
            // Spline de Catmull-Rom entre P1 y P2 (snippet de anti-click-clip-truepeak.md §3).
            const float v = 0.5f * ((2.0f * P1)
                          + (-P0 + P2) * t
                          + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t * t
                          + (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t * t * t);
            tp = juce::jmax (tp, std::abs (v));
        }
    }
    return tp;
}

// Conveniencia: true-peak en dBTP (dB true-peak). −inf-safe (piso 1e-12). PROXY (ver nota del header).
inline float truePeakDb (const std::vector<float>& x)
{
    return 20.0f * std::log10 (truePeakOf (x) + 1e-12f);
}

} // namespace ovni::dsp
