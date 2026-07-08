#pragma once
#include "dsp/SchroederAllpass.h"
#include <array>

// =============================================================================
// PULSAR — DECORRELATOR: lo que ABRE el estéreo de verdad.
//
// El paneo + ITD + head-shadow operan sobre una señal MONO correlacionada -> la
// correlación L/R queda alta -> el vectorscope NO se abre. Para abrir fuerte hace
// falta DECORRELACIÓN real: pasar L y R por cadenas de allpass con delays DISTINTOS
// por canal. Cada cadena es unity-gain (no colorea), pero sus respuestas de FASE
// difieren -> L y R quedan decorrelacionadas (correlación ~0) = imagen ANCHA.
// Física: modela el campo DIFUSO/multipath (la fuente llega a cada oído por caminos
// distintos), no una reverb de sala.
//
// Crossfade dry->decorrelacionado por `mix` (= WIDTH escalado). monoSafe/mix~0 ->
// bypass total (vuelve al paneo puro, en fase = mono-compatible).
// =============================================================================
namespace pulsar::dsp
{

class Decorrelator
{
public:
    void prepare (double sr, int maxBlock)
    {
        juce::dsp::ProcessSpec s { sr, (juce::uint32) juce::jmax (1, maxBlock), 1 };
        // Delays PRIMOS y DISTINTOS L vs R (segundos): decorrelación broadband sin eco discreto.
        static constexpr float dL[kStages] = { 0.0089f, 0.0191f, 0.0373f, 0.0531f };
        static constexpr float dR[kStages] = { 0.0127f, 0.0263f, 0.0441f, 0.0619f };
        for (int i = 0; i < kStages; ++i)
        {
            apL[i].prepare (s, dL[i], (i & 1) ? -kG : kG);   // signos alternados = dispersión más densa
            apR[i].prepare (s, dR[i], (i & 1) ?  kG : -kG);
        }
        reset();
    }

    void reset() noexcept { for (auto& a : apL) a.reset(); for (auto& a : apR) a.reset(); }

    // mix01 = cantidad de decorrelación (típicamente WIDTH). monoSafe -> bypass (en fase).
    void process (juce::AudioBuffer<float>& buf, float mix01, bool monoSafe) noexcept
    {
        const int n = buf.getNumSamples();
        if (buf.getNumChannels() < 2 || n <= 0 || monoSafe) return;

        const float m = juce::jlimit (0.0f, 1.0f, mix01) * kMaxMix;   // techo: deja un hilo de centro
        if (m <= 1.0e-4f) return;

        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float dl = L[i], dr = R[i];
            for (int s = 0; s < kStages; ++s)
            {
                dl = apL[s].processSample (0, dl);
                dr = apR[s].processSample (0, dr);
            }
            L[i] = L[i] + m * (dl - L[i]);   // crossfade dry -> decorrelacionado
            R[i] = R[i] + m * (dr - R[i]);
        }
    }

private:
    static constexpr int   kStages = 4;
    static constexpr float kG      = 0.7f;    // coef de los allpass (decorrelación fuerte)
    static constexpr float kMaxMix = 0.9f;    // a WIDTH=100% queda 90% decorrelacionado + 10% centro
    std::array<SchroederAllpass, kStages> apL, apR;
};

} // namespace pulsar::dsp
