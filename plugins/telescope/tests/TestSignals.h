#pragma once
#include <cmath>
#include <cstdint>
#include <utility>

// ========================================================================================================
// Señales de prueba de TELESCOPE.
//
// `Pink` es COPIA EXACTA del generador de ~/PLUGINS/orbita/tests/StereoMeasure.cpp:25-40 (a su vez portado
// de PULSAR): ruido rosa de Paul Kellet "economy" sobre un LCG de semilla fija. Se copia idéntico A
// PROPÓSITO: si TELESCOPE mide el mismo estéreo con las mismas muestras que ÓRBITA y PULSAR, los números
// son DIRECTAMENTE comparables entre plugins del sello — que es todo el punto de tener un medidor propio.
// Determinista: misma semilla → mismas muestras → mismos números al bit, corrida tras corrida.
// ========================================================================================================
namespace telescope::test
{
struct Pink
{
    explicit Pink (std::uint32_t seed = 0x13572468u) : s (seed) {}

    float white() noexcept
    {
        s = s * 1664525u + 1013904223u;
        return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f;
    }

    float next() noexcept
    {
        const float w = white();
        b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
        b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
        b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
        const float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
        return p * 0.11f;   // ~[-1,1]
    }

    std::uint32_t s;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
};

// La otra semilla, para el caso "dos fuentes independientes". No está elegida a dedo: es la de la casa
// con los dígitos corridos.
inline constexpr std::uint32_t kPinkSeedA = 0x13572468u;
inline constexpr std::uint32_t kPinkSeedB = 0x2468ACE0u;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

// ========================================================================================================
// Pasa-altos Butterworth de 6 polos (tres biquads RBJ con Q = 0.7071), y con él LA SEÑAL MIXTA del
// prompt 51: graves MONO (seno de 80 Hz en L = R) más agudos FUERA DE FASE (ruido rosa pasa-altos de
// 2 kHz con R = −L).
//
// Es la señal que define la promesa del producto —"te digo EN QUÉ FRECUENCIAS está fuera de fase"— así
// que vive acá y no copiada en cada test: si el módulo, la lente, la foto y el presupuesto no midieran
// exactamente la misma señal, los números de los cuatro no serían comparables.
//
// Los 6 polos no son decoración: con dos polos, a 160 Hz el ruido sigue 44 dB abajo del corte y ENSUCIA
// las bandas graves (que deben quedar dominadas por el seno, que es lo que las hace mono). Con seis,
// a 160 Hz atenúa ~130 dB y la separación es limpia.
// ========================================================================================================
struct HighPass6
{
    HighPass6 (double fc, double sr) noexcept
    {
        const double twoPi = 6.283185307179586476925286766559;
        const double w0 = twoPi * fc / sr, c = std::cos (w0), sn = std::sin (w0);
        const double alpha = sn / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        b0 = ((1.0 + c) * 0.5) / a0;
        b1 = (-(1.0 + c)) / a0;
        b2 = b0;
        a1 = (-2.0 * c) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    float process (float x) noexcept
    {
        double v = (double) x;
        for (auto& st : stages)
        {
            const double y = b0 * v + st.z1;
            st.z1 = b1 * v - a1 * y + st.z2;
            st.z2 = b2 * v - a2 * y;
            v = y;
        }
        return (float) v;
    }

    struct State { double z1 = 0.0, z2 = 0.0; };
    State  stages[3];
    double b0 = 0.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct MonoLowPhaseHigh
{
    explicit MonoLowPhaseHigh (double sampleRate = 48000.0) noexcept : sr (sampleRate), hp (2000.0, sampleRate) {}

    std::pair<float, float> next() noexcept
    {
        const double twoPi = 6.283185307179586476925286766559;
        const auto low  = (float) (0.5 * std::sin (twoPi * 80.0 * (double) n++ / sr));
        const float high = 3.0f * hp.process (pink.next());
        return { low + high, low - high };
    }

    double    sr;
    Pink      pink { kPinkSeedA };
    HighPass6 hp;
    long long n = 0;
};
}
