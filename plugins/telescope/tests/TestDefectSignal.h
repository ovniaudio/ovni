#pragma once
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "TestSignals.h"

// ========================================================================================================
// LA SEÑAL CON DEFECTOS EN POSICIONES CONOCIDAS. La comparten [history] y [verdict]: la primera prueba que
// las filas por segundo del archivo y del vivo son las mismas, la segunda que VERDICT encuentra los
// defectos donde están. Una sola definición para las dos, o serían dos señales parecidas y ningún test
// diría nada sobre la otra.
//
//   60 s de ruido rosa estéreo a ~−14 LUFS, con
//     · un HUECO espectral entre 0:20 y 0:35 — se le saca la región de 400 a 1 000 Hz, así que las bandas
//       de ⅓ de octava de 500, 630 y 800 Hz caen muy por debajo de su nivel en el resto del tema;
//     · un TRAMO BAJO entre 0:40 y 0:50 — 16 dB menos, o sea alrededor de −30 LUFS.
// ========================================================================================================
namespace telescope::test
{
inline constexpr double kDefectSr      = 48000.0;
inline constexpr double kDefectSeconds = 60.0;
inline constexpr int    kDefectHoleFrom = 20, kDefectHoleTo = 35;
inline constexpr int    kDefectQuietFrom = 40, kDefectQuietTo = 50;

// Cuatro biquads iguales en cascada: una falda bien empinada sin depender de FilterDesign. No busca ser un
// Butterworth de orden 8 exacto — busca dejar un agujero que no se pueda confundir con nada. Su transición
// es ancha (llega a mover algo las bandas de 200 Hz a 2 kHz) y eso está medido en HISTORY[hueco].
struct DefectCascade
{
    void prepare (const juce::dsp::IIR::Coefficients<float>::Ptr& c)
    {
        for (auto& f : sec) { f.coefficients = c; f.reset(); }
    }
    float process (float x) noexcept
    {
        for (auto& f : sec) x = f.processSample (x);
        return x;
    }
    juce::dsp::IIR::Filter<float> sec[4];
};

inline juce::AudioBuffer<float> makeDefectSignal (double seconds = kDefectSeconds)
{
    const auto total = (int) std::llround (seconds * kDefectSr);
    juce::AudioBuffer<float> buf (2, total);

    const float peak  = std::pow (10.0f, -2.4f / 20.0f);    // rosa con este pico ≈ −14 LUFS (medido)
    const float quiet = std::pow (10.0f, -16.0f / 20.0f);   // −16 dB → ≈ −30 LUFS

    Pink pink[2] { Pink { kPinkSeedA }, Pink { kPinkSeedB } };
    DefectCascade lp[2], hp[2];
    const auto lpc = juce::dsp::IIR::Coefficients<float>::makeLowPass  (kDefectSr, 400.0f);
    const auto hpc = juce::dsp::IIR::Coefficients<float>::makeHighPass (kDefectSr, 1000.0f);
    for (int c = 0; c < 2; ++c) { lp[c].prepare (lpc); hp[c].prepare (hpc); }

    for (int i = 0; i < total; ++i)
    {
        const double t = (double) i / kDefectSr;
        const bool inHole  = t >= (double) kDefectHoleFrom  && t < (double) kDefectHoleTo;
        const bool inQuiet = t >= (double) kDefectQuietFrom && t < (double) kDefectQuietTo;

        for (int c = 0; c < 2; ++c)
        {
            const float x = peak * pink[c].next();
            // Los filtros corren SIEMPRE (si no, al entrar al hueco arrancarían de cero y el borde sería
            // un transitorio en vez de un cambio de contenido).
            const float holed = lp[c].process (x) + hp[c].process (x);
            float v = inHole ? holed : x;
            if (inQuiet) v *= quiet;
            buf.setSample (c, i, v);
        }
    }
    return buf;
}

// ========================================================================================================
// LA SEÑAL DEL PEOR CASO DE DIBUJO — para BUDGET_VERDICT y sólo para eso.
//
// Es la señal con defectos de arriba MÁS todo lo que se le puede sumar sin inventar nada: los agudos de R
// invertidos (contrafase real por encima de 8 kHz, que da un hallazgo por banda), un desbalance sostenido,
// continua, y una ráfaga de picos. No pretende parecerse a una mezcla: pretende darle a la lente la lista
// MÁS LARGA que el motor puede producir sobre audio de verdad, que es lo que hay que cronometrar.
// ========================================================================================================
inline juce::AudioBuffer<float> makeWorstCaseSignal()
{
    auto buf = makeDefectSignal();
    const int total = buf.getNumSamples();

    DefectCascade hp;
    hp.prepare (juce::dsp::IIR::Coefficients<float>::makeHighPass (kDefectSr, 8000.0f));

    for (int i = 0; i < total; ++i)
    {
        const double t = (double) i / kDefectSr;
        float r = buf.getSample (1, i);

        // R = R − 2·HP(R): la parte de arriba de 8 kHz queda en CONTRAFASE con la de L.
        r -= 2.0f * hp.process (r);

        // Desbalance sostenido de +5 dB entre 0:05 y 0:30.
        if (t >= 5.0 && t < 30.0) r *= 1.78f;

        // Continua: 0.02 en los dos canales (−34 dBFS, se come headroom y no suena).
        buf.setSample (0, i, juce::jlimit (-1.0f, 1.0f, buf.getSample (0, i) + 0.02f));
        buf.setSample (1, i, juce::jlimit (-1.0f, 1.0f, r + 0.02f));
    }

    // Una ráfaga de picos a 0:52: seis golpes de escala completa en cuatro segundos.
    for (int k = 0; k < 6; ++k)
    {
        const int at = (int) std::llround ((52.0 + 0.6 * k) * kDefectSr);
        for (int n = 0; n < 64 && at + n < total; ++n)
        {
            buf.setSample (0, at + n, 0.999f);
            buf.setSample (1, at + n, 0.999f);
        }
    }
    return buf;
}
}
