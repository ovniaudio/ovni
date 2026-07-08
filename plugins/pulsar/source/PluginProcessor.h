#pragma once

#include "template/PluginProcessorBase.h"
#include "engines/binaural/SpatialEngine.h"
#include "dsp/TrajectoryBank.h"
#include "dsp/Smear.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include <atomic>

// =============================================================================
// PULSAR — processor concreto. Deriva del chasis del sello (ovni::PluginProcessorBase:
// APVTS, bypass, Program Change->preset, estado, meter atomics, PresetManager+A/B).
// Sólo aporta lo PROPIO de PULSAR: el banco de trayectorias (carácter) + el motor
// BINAURAL HRIR compartido (mono -> 2 voces HRIR + ITD + near-field + reflexiones +
// crosstalk). El binaural ABRE el estéreo de verdad (externalizado), a diferencia del
// paneo que colapsaba a mono a WIDTH alto. El motor ya hace Doppler+distancia internamente.
//
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace pulsar
{

class PulsarProcessor : public ovni::PluginProcessorBase
{
public:
    PulsarProcessor();
    ~PulsarProcessor() override = default;

    // Layout de parámetros (lo pasa al constructor base). IDs append-only.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // devuelve PulsarEditor

    // Test-only: rateHz efectivo en SYNC para un BPM dado (misma cuenta que processAudio).
    float debugSyncRateHz (double bpm) const;

    // Telemetría lock-free para el visualizador (el audio escribe, la UI lee).
    std::atomic<float> uiAzimuth  { 0.0f };  // [-1,1] posición azimutal (último sample del bloque)
    std::atomic<float> uiDepth    { 0.0f };  // [-1,1] eje frente-atrás
    std::atomic<float> uiHeat     { 0.0f };  // [0,1] |v_radial| -> temperatura del trail (Doppler)
    std::atomic<float> uiDistance { 0.5f };  // [0,1] distancia base -> radio/escala en el pad
    std::atomic<float> uiMotion   { 0.0f };  // [0,1] MOTION -> tamaño del orbe del centro / energía visual
    std::atomic<float> uiShape    { 0.0f };  // [0,1] SHAPE  -> carácter del movimiento
    std::atomic<float> uiSmear    { 0.0f };  // [0,1] SMEAR  -> longitud/densidad de la estela

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // El LED de clip toma también la reducción del limiter del motor.
    float extraClipPush() const override;

private:
    ovni::engines::SpatialEngine  engine;   // motor binaural HRIR (apertura/externalización real)
    dsp::TrajectoryBank           traj;
    dsp::Smear                    smear;
    ovni::dsp::LowCut             lowCut;    // PRO: HPF de salida (limpia graves del wet)
    ovni::dsp::HiCut              hiCut;     // PRO: LPF de salida (oscurece/suaviza)

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pMotion   = nullptr;
    std::atomic<float>* pRate     = nullptr;
    std::atomic<float>* pSync     = nullptr;
    std::atomic<float>* pDivision = nullptr;
    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pShape    = nullptr;
    std::atomic<float>* pSmear    = nullptr;
    std::atomic<float>* pWidth    = nullptr;
    std::atomic<float>* pLowCut   = nullptr;
    std::atomic<float>* pHiCut    = nullptr;
    std::atomic<float>* pFieldX   = nullptr;
    std::atomic<float>* pFieldY   = nullptr;

    float syncRateHz (double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PulsarProcessor)
};

} // namespace pulsar
