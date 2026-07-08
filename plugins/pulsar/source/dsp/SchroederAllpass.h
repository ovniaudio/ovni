#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =============================================================================
// Allpass de Schroeder de un solo delay: y[n] = -g*x[n] + d[n]; push(x[n] + g*y[n]).
// Estable y UNITY-GAIN (|H(e^jw)| = 1) para |g| < 1: dispersa la FASE sin tocar la
// magnitud -> decorrela/difunde sin colorear. processSample(ch, x) usa el canal `ch`
// de su línea interna (mono por defecto). Compartido por Smear (cola) y Decorrelator
// (ancho). Ver references/anti-click-clip-truepeak.md.
// =============================================================================
namespace pulsar::dsp
{

class SchroederAllpass
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec, float delaySeconds, float coef) noexcept
    {
        g = juce::jlimit (-0.95f, 0.95f, coef);
        const int maxD = juce::jmax (4, (int) std::ceil (spec.sampleRate * (double) delaySeconds) + 2);
        line.setMaximumDelayInSamples (maxD);
        line.prepare (spec);
        line.setDelay ((float) (spec.sampleRate * (double) delaySeconds));
        reset();
    }
    void reset() noexcept { line.reset(); }

    float processSample (int ch, float x) noexcept
    {
        const float d = line.popSample (ch);
        const float y = -g * x + d;
        line.pushSample (ch, x + g * y);
        return y;
    }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 8192 };
    float g = 0.6f;
};

} // namespace pulsar::dsp
