#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// =====================================================================================
// shared/dsp/LowCut.h — HIGH-PASS estéreo para la ENTRADA WET de una reverb/cola (familia FDN:
// HALO + NEBULA). Saca los graves ANTES de que entren al lazo/tail → la cola no embarra el bajo
// de la mezcla (el truco de mezcla #1 con reverb). Va SOLO sobre el wet; el DRY pasa entero (un
// low-cut no debe adelgazar la señal del usuario, solo limpia la cola).
//
// POR QUÉ TPT (Zavalishin) y NO un biquad: el State-Variable TPT es MODULACIÓN-SAFE — cambiar el
// cutoff en vivo NO genera zipper ni transitorios de inestabilidad (es la razón de existir de la
// topología TPT). Es la elección correcta para un filtro automatizable. Ver
// references/anti-click-clip-truepeak.md (suavizar el PARÁMETRO, no interpolar coeficientes crudos).
//
// 12 dB/oct (Butterworth, sin pico). Rango 20 Hz (≈apagado) .. 500 Hz, logarítmico. Default = 20 Hz
// → transparente, NO cambia el sonido ni los presets existentes hasta que el usuario lo sube.
//
// NO-LINEAL: es lineal (filtro) → no genera alias (house-standard §1: filtro lineal = 0× OS).
// =====================================================================================
namespace ovni::dsp
{

class LowCut
{
public:
    static constexpr float kMinHz = 20.0f;    // piso = prácticamente apagado (solo quita subsónico/DC)
    static constexpr float kMaxHz = 500.0f;   // techo útil para limpiar los graves de una cola

    // Mapa de la perilla 0..1 -> Hz (logarítmico, octava-uniforme). 0 = 20 Hz (off), 1 = 500 Hz.
    static float hzFor01 (float x01) noexcept
    {
        x01 = juce::jlimit (0.0f, 1.0f, x01);
        return kMinHz * std::pow (kMaxHz / kMinHz, x01);
    }

    // Para el readout/display: ¿está prácticamente apagado? (≤ ~22 Hz).
    static bool isOff (float hz) noexcept { return hz <= kMinHz * 1.1f; }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        filt.prepare (spec);
        filt.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        filt.setResonance (0.7071f);                       // Butterworth: máxima planicie, sin pico
        smHz.reset (spec.sampleRate, 0.03);                // 30 ms de de-zipper del cutoff
        smHz.setCurrentAndTargetValue (kMinHz);
        filt.setCutoffFrequency (kMinHz);
    }

    void reset() noexcept { filt.reset(); }

    void setCutoffHz (float hz) noexcept { smHz.setTargetValue (juce::jlimit (kMinHz, kMaxHz, hz)); }

    // Filtra el WET in-place (estéreo). Suaviza el cutoff por bloque; el TPT no clickea al modular.
    void process (juce::AudioBuffer<float>& wet) noexcept
    {
        filt.setCutoffFrequency (smHz.skip (wet.getNumSamples()));
        juce::dsp::AudioBlock<float> block (wet);
        filt.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::StateVariableTPTFilter<float> filt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smHz;
};

} // namespace ovni::dsp
