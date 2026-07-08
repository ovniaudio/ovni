#pragma once
#include <juce_dsp/juce_dsp.h>
#include "dsp/SchroederAllpass.h"
#include <array>

// =============================================================================
// PULSAR — SMEAR: la "cola de cometa" (el sonido salvaje que ÓRBITA no tiene).
//
// Difusión estéreo en feedback: por canal, una cadena de DOS allpass de Schroeder
// CORTOS (delays primos ~7/11 ms con coef ±0.6) dispersa la energía en el tiempo
// (cola densa, no eco discreto), seguida de un delay corto de cola. El feedback es
// CRUZADO por el azimut del movimiento: la estela "sigue" a la partícula (deriva
// hacia donde va la fuente). ACOTADO Y ESTABLE:
//   · cada allpass es unity-gain (|H(e^jw)|=1) -> no agrega energía,
//   · el feedback global tiene techo 0.88 -> el lazo converge (no autooscila),
//   · todo se mezcla wet/dry; el limiter del motor (techo 0.85) es la red final.
//
// Toda ganancia modulada (wet/feedback) se rampea por-sample (anti-zipper).
// DSP puro (sin JUCE GUI). Ver references/anti-click-clip-truepeak.md.
// =============================================================================
namespace pulsar::dsp
{

class Smear
{
public:
    void prepare (double sr, int maxBlock)
    {
        juce::dsp::ProcessSpec s { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };

        // Dos allpass cortos por canal (delays primos: densifica sin patrón métrico audible).
        ap[0].prepare (s, 0.0071f,  apCoef);   // ~7 ms,  +g
        ap[1].prepare (s, 0.0113f, -apCoef);   // ~11 ms, -g

        // Delay de cola (la estela que se queda atrás de la partícula).
        delay.setMaximumDelayInSamples ((int) (sr * 0.18) + 4);
        delay.prepare (s);
        delay.setDelay ((float) (sr * 0.09));   // 90 ms

        reset();
    }

    void reset()
    {
        for (auto& a : ap) a.reset();
        delay.reset();
        fbL = fbR = 0.0f;
        wetSm = 0.0f; fbSm = 0.0f; xSm = 0.5f;
        limGain = 1.0f;
    }

    // amount01 = SMEAR. azimuth [-1,1] cruza el feedback (estela sigue el movimiento).
    void process (juce::AudioBuffer<float>& buf, float amount01, float azimuth)
    {
        const int n = buf.getNumSamples();
        if (buf.getNumChannels() < 2 || n <= 0) return;

        const float amt   = juce::jlimit (0.0f, 1.0f, amount01);
        const float fbTgt  = 0.88f * amt;                              // techo 0.88 (estable)
        const float wetTgt = amt;                                      // mezcla wet
        const float xTgt   = juce::jlimit (-1.0f, 1.0f, azimuth) * 0.5f + 0.5f;  // 0..1 cruce L/R

        // Rampas por-sample (anti-zipper) de wet / feedback / cruce.
        const float invN = 1.0f / (float) n;
        const float wStep = (wetTgt - wetSm) * invN;
        const float fStep = (fbTgt  - fbSm)  * invN;
        const float xStep = (xTgt   - xSm)   * invN;

        auto* L = buf.getWritePointer (0);
        auto* R = buf.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            const float wet = wetSm; const float fb = fbSm; const float x = xSm;
            wetSm += wStep; fbSm += fStep; xSm += xStep;

            // Entrada al difusor = señal + feedback (cruzado entre canales).
            float dl = L[i] + fbL;
            float dr = R[i] + fbR;

            for (auto& a : ap) { dl = a.processSample (0, dl); dr = a.processSample (1, dr); }

            delay.pushSample (0, dl);
            delay.pushSample (1, dr);
            const float tl = delay.popSample (0);
            const float tr = delay.popSample (1);

            // Feedback cruzado por azimut: la estela deriva hacia donde va la fuente.
            fbR = fb * (tl * x         + tr * (1.0f - x));
            fbL = fb * (tr * (1.0f - x) + tl * x);   // simétrico
            // Saneo defensivo (corta cualquier NaN/inf antes de realimentar).
            fbL = juce::jlimit (-1.5f, 1.5f, fbL);
            fbR = juce::jlimit (-1.5f, 1.5f, fbR);

            // Mezcla wet/dry. El wet (la cola) se atenúa (kWetTrim) -> gain-staging:
            // con material broadband el lazo densifica, y un wet a ganancia plena podría
            // sumar > 0 dBFS. Trim + limiter de abajo lo mantienen acotado (skill §2/§4).
            float oL = L[i] * (1.0f - wet * 0.6f) + tl * wet * kWetTrim;
            float oR = R[i] * (1.0f - wet * 0.6f) + tr * wet * kWetTrim;

            // Limiter de salida estéreo-linked, attack instantáneo + release suave (latencia 0).
            // Smear corre DESPUÉS del motor (post-limiter del motor) -> es su propia red anti-clip.
            const float pk  = juce::jmax (std::abs (oL), std::abs (oR));
            const float tgt = (pk > kCeiling) ? kCeiling / pk : 1.0f;
            if (tgt < limGain) limGain = tgt;                       // attack instantáneo
            else               limGain += (tgt - limGain) * kRel;   // release suave
            L[i] = oL * limGain;
            R[i] = oR * limGain;
        }
    }

private:
    std::array<SchroederAllpass, 2> ap;   // [0]=~7ms +g, [1]=~11ms -g (por canal vía processSample(ch))
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 1 << 14 };

    float fbL = 0.0f, fbR = 0.0f;
    float wetSm = 0.0f, fbSm = 0.0f, xSm = 0.5f;   // estados rampeados (anti-zipper)
    float limGain = 1.0f;                          // estado del limiter de salida

    static constexpr float apCoef   = 0.6f;        // coef de difusión de los allpass (Schroeder)
    static constexpr float kWetTrim = 0.7f;        // trim del wet (gain-staging del lazo)
    static constexpr float kCeiling = 0.85f;       // techo del sello (0.85 ≈ -1.4 dB true-peak): la cola wet
                                                   // entra en el presupuesto anti-clip; PULSAR no supera 0.85
    static constexpr float kRel     = 0.0008f;     // release del limiter (~suave, sin bombeo audible)
};

} // namespace pulsar::dsp
