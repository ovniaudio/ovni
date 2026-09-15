#pragma once
#include <cmath>
#include <juce_core/juce_core.h>
#include "analysis/SpectrumFrame.h"

// ========================================================================================================
// TestShelf — el shelf de agudos de los tests de TONAL BALANCE, en UN solo lugar.
//
// Lo usan el test del módulo ([ref] REF[tilt]) y el de la lente ([uisnap]/[tonal]). Dos copias del mismo
// filtro serían dos números que algún día dejan de ser comparables — la misma razón por la que la señal
// mixta del prompt 51 vive en TestSignals.h y no copiada en cada test.
// ========================================================================================================
namespace telescope::test
{
// ------------------------------------------------------------------------------------------------------
// EL TILT DE PRUEBA: un shelf de agudos RBJ (biquad, Audio EQ Cookbook), con frecuencia y ganancia a
// elección y S = 1.
//
// POR QUÉ ÉSTE Y NO "sumar el rosa pasa-altos" (que era la otra opción). Sumar x + hp(x) con el pasa-altos
// de 6 polos de la casa NO da un shelf: da un shelf con muescas. En la banda de transición hp(x) llega con
// mucha fase acumulada y se CANCELA contra x — medido acá: -4.0 dB a 4 kHz, cuando la intención era +6.
// Un test montado sobre eso mediría la fase del filtro, no la lente. El biquad de shelf tiene la respuesta
// que dice tener, y —lo que importa— esa respuesta se puede CALCULAR, así que el test no compara contra
// "más o menos 6 dB" sino contra la curva exacta del filtro, banda por banda.
// ------------------------------------------------------------------------------------------------------
struct HighShelf
{
    HighShelf (double fc, double gainDb, double sr) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * fc / sr;
        const double c  = std::cos (w0);
        const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);   // S = 1
        const double sa = 2.0 * std::sqrt (A) * alpha;

        const double a0 =        (A + 1.0) - (A - 1.0) * c + sa;
        b0 =        A * ((A + 1.0) + (A - 1.0) * c + sa) / a0;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c)       / a0;
        b2 =        A * ((A + 1.0) + (A - 1.0) * c - sa)  / a0;
        a1 =  2.0 *     ((A - 1.0) - (A + 1.0) * c)       / a0;
        a2 =            ((A + 1.0) - (A - 1.0) * c - sa)  / a0;
    }

    float process (float x) noexcept
    {
        const double v = (double) x;
        const double y = b0 * v + z1;
        z1 = b1 * v - a1 * y + z2;
        z2 = b2 * v - a2 * y;
        return (float) y;
    }

    // |H(e^{jw})| en dB, calculado de los coeficientes: la respuesta que el test espera ver en el delta.
    double magnitudeDb (double hz, double sr) const noexcept
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2.0 * w), s2 = std::sin (2.0 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10 ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    // La respuesta MEDIA en la banda de ⅓ de octava `b`, ponderada como el ruido rosa (densidad 1/f): es
    // el número con el que se compara el delta de esa banda.
    double bandDb (int band, double sr) const noexcept
    {
        constexpr double lo6 = 0.8908987181403393, hi6 = 1.1224620483093730;
        const double lo = telescope::kThirdOctaveHz[band] * lo6, hi = telescope::kThirdOctaveHz[band] * hi6;
        constexpr int kSteps = 256;
        double num = 0.0, den = 0.0;
        for (int i = 0; i < kSteps; ++i)
        {
            const double f = lo + (hi - lo) * ((double) i + 0.5) / (double) kSteps;
            const double w = 1.0 / f;                                     // densidad del rosa
            num += w * std::pow (10.0, magnitudeDb (f, sr) / 10.0);
            den += w;
        }
        return 10.0 * std::log10 (num / den);
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

}
