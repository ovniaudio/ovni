#pragma once

#include "template/PluginProcessorBase.h"
#include "engines/fdn/FdnReverb.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include <atomic>

// =============================================================================
// NÉBULA — processor concreto. Deriva del chasis del sello (ovni::PluginProcessorBase:
// APVTS, bypass, Program Change->preset, estado, meter atomics, PresetManager+A/B).
// Sólo aporta lo PROPIO de NÉBULA: el motor REVERB FDN compartido (Feedback Delay
// Network: Householder lossless + T60(ω) por absorción + early reflections +
// respiración por LFO propio + freeze + anti-clip). El mismo motor lo hereda HALO.
//
// Las 5 macros perceptuales (Size/Decay/Tone/Breath/Mix, % en el host) se mapean
// 0..1 a ovni::engines::FdnParams. La telemetría lock-free (atomics uiSize/.../uiMix)
// alimenta el visualizer magenta (la nebulosa que respira/esculpe) de Task 5.
//
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace nebula
{

class NebulaProcessor : public ovni::PluginProcessorBase
{
public:
    NebulaProcessor();
    ~NebulaProcessor() override = default;

    // Layout de parámetros (lo pasa al constructor base). IDs append-only.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // devuelve NebulaEditor

    // Acceso al motor FDN SÓLO para diagnóstico (clip-scan [clipscan][nebula]): enganchar el MeterProbe y
    // togglear el limiter para mapear la cadena etapa por etapa. NO se usa en el camino de audio del usuario.
    ovni::engines::FdnReverb& engineForTest() noexcept { return fdn; }

    // Diagnóstico [diccionario][nebula]: devuelve la frecuencia de respiración en Hz que el motor usaría
    // con el BPM dado y el breathDiv actual. Lee pBreathDiv (no la fija a kBarDivIndex).
    float debugBreathRateHz (double bpm) const;

    // Telemetría lock-free para el visualizador (el audio escribe, la UI lee). Una por macro
    // (regla anti-bug PULSAR: ninguna macro sin efecto visual). Todas en 0..1.
    std::atomic<float> uiSize   { 0.5f };   // radio base de la nube
    std::atomic<float> uiDecay  { 0.5f };   // densidad / partículas de la cola
    std::atomic<float> uiTone   { 0.4f };   // tinte / oscuridad (damping HF)
    std::atomic<float> uiBreath { 0.25f };  // expansión-contracción animada (profundidad = knob)
    std::atomic<float> uiBreathLfo { 0.0f }; // FASE real del BreathLFO del motor [−1,1] — la nube respira con esto
    std::atomic<float> uiMix    { 0.35f };  // presencia del wet

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // El LED de clip toma también la reducción del limiter del motor FDN.
    float extraClipPush() const override;

private:
    ovni::engines::FdnReverb fdn;   // motor REVERB FDN (cola difusa + breath + freeze)

    // LOW CUT (high-pass TPT estéreo, bloque compartido shared/dsp/LowCut.h) + HI CUT (low-pass TPT estéreo,
    // shared/dsp/HiCut.h): el filtro pasa-altos / pasa-bajos del PLUGIN. Filtran la SALIDA COMPLETA del motor
    // (dry+wet ya mezclados), NO sólo el wet de entrada a la cola. Antes filtraban el wet de ENTRADA al FDN y
    // restituían lo perdido al dry → a MIX bajo el dry full-range tapaba el corte (medido en HALO: −0.5 dB en
    // la salida total a MIX 40% = imperceptible). Como filtro de la SALIDA, el corte se OYE a cualquier MIX.
    // Encadenados low-cut → hi-cut sobre el buffer de salida (ver processAudio). Default 0 → 20 Hz / 20 kHz →
    // transparente (no cambia el sonido ni los presets ya shippeados). Lineales → no generan alias.
    ovni::dsp::LowCut lowCut;
    ovni::dsp::HiCut  hiCut;

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pSize       = nullptr;
    std::atomic<float>* pDecay      = nullptr;
    std::atomic<float>* pTone       = nullptr;
    std::atomic<float>* pBreath     = nullptr;
    std::atomic<float>* pMix        = nullptr;
    std::atomic<float>* pLowCut     = nullptr;   // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz)
    std::atomic<float>* pHiCut      = nullptr;   // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz)
    std::atomic<float>* pBreathSync = nullptr;   // bool: respiración libre / al tempo
    std::atomic<float>* pBreathDiv  = nullptr;   // índice de la división del ciclo (choice)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaProcessor)
};

} // namespace nebula
