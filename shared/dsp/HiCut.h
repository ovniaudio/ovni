#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =====================================================================================
// shared/dsp/HiCut.h — LOW-PASS estéreo (HI CUT) para el WET de una reverb/cola (familia FDN:
// HALO + NEBULA). Oscurece/suaviza la cola cortando los agudos — el par del LowCut. Va SOLO sobre
// el wet; el DRY pasa entero. Complementa al TONE (damping del lazo): el HI CUT es un corte ESTÁTICO
// del wet, control directo del brillo de la cola.
//
// Mismo criterio que LowCut.h: TPT State-Variable (Zavalishin) = MODULACIÓN-SAFE (mover el cutoff no
// genera zipper). 12 dB/oct (Butterworth). Lineal → no genera alias (house-standard §1).
//
// Perilla 0..1 INVERTIDA (más perilla = más oscuro): 0 = 20 kHz (off/transparente), 1 = 1.5 kHz (máx
// cut). Default = 0 → off → no cambia el sonido ni los presets. El cutoff se clampea a 0.45·SR (seguro
// a cualquier sample-rate); el motor además puede bypassear el proceso cuando el param es 0.
// =====================================================================================
namespace ovni::dsp
{

class HiCut
{
public:
    static constexpr float kMinHz = 1500.0f;    // máximo cut (cola más oscura)
    static constexpr float kMaxHz = 20000.0f;   // off (transparente a 48 kHz)

    // Perilla 0..1 -> Hz (log, invertida). 0 = 20 kHz (off), 1 = 1.5 kHz (máx cut).
    static float hzFor01 (float x01) noexcept
    {
        x01 = juce::jlimit (0.0f, 1.0f, x01);
        return kMaxHz * std::pow (kMinHz / kMaxHz, x01);   // x=0 → 20k ; x=1 → 1.5k
    }

    // ¿Prácticamente apagado? (perilla ~0 → cutoff cerca de 20 kHz).
    static bool isOff (float hz) noexcept { return hz >= kMaxHz * 0.95f; }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sr = (float) spec.sampleRate;
        filt.prepare (spec);
        filt.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        filt.setResonance (0.7071f);                       // Butterworth: sin pico
        smHz.reset (spec.sampleRate, 0.03);                // 30 ms de de-zipper del cutoff
        const float off = juce::jmin (kMaxHz, 0.45f * sr);
        smHz.setCurrentAndTargetValue (off);
        filt.setCutoffFrequency (off);
    }

    void reset() noexcept { filt.reset(); }

    void setCutoffHz (float hz) noexcept
    {
        smHz.setTargetValue (juce::jlimit (kMinHz, juce::jmin (kMaxHz, 0.45f * sr), hz));
    }

    void process (juce::AudioBuffer<float>& wet) noexcept
    {
        filt.setCutoffFrequency (smHz.skip (wet.getNumSamples()));
        juce::dsp::AudioBlock<float> block (wet);
        filt.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::StateVariableTPTFilter<float> filt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smHz;
    float sr = 48000.0f;
};

} // namespace ovni::dsp
