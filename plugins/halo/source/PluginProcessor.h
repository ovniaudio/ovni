#pragma once

#include "template/PluginProcessorBase.h"
#include "engine/HaloEngine.h"
#include <atomic>

// =============================================================================
// HALO — processor concreto (SPC·02). Deriva del chasis del sello
// (ovni::PluginProcessorBase: APVTS, bypass, Program Change->preset, estado, meter
// atomics, PresetManager+A/B). Aporta lo PROPIO de HALO: el motor SHIMMER ESPACIAL
// GLACIAL (HaloEngine = pre-delay + BLOOM + difusor FDN + PitchShifter octava+quinta
// con OS 4× en el lazo + pan binaural ITD/ILD del wash fuera del lazo). El shimmer del
// catálogo, ahora glacial y orbitando alrededor de la cabeza.
//
// REDISEÑO "VASTEDAD" (base técnica): 6 macros perceptuales (Mix/Size/Decay/Shimmer/Tone/
// Orbit, % en el host) → HaloParams + FREEZE (toggle). HONESTIDAD (base técnica §1): NO
// hay SCALE ni VOICING — las voces son FIJAS (octava + quinta) en el motor. SYNC (control
// CORE del sello): orbitSync + orbitDiv enganchan el RATE de la ÓRBITA espacial (la
// trayectoria del halo) al tempo del host — el elemento rítmico natural de HALO.
//
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace halo
{

class HaloProcessor : public ovni::PluginProcessorBase
{
public:
    HaloProcessor();
    ~HaloProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // devuelve HaloEditor

    // Acceso al motor SÓLO para tests (anti-clip/estabilidad del HaloProcessor real).
    HaloEngine& engineForTest() noexcept { return engine; }

    // Diagnóstico [diccionario][halo]: frecuencia orbital (Hz) que el motor usaría con el BPM dado y el
    // orbitDiv actual. Lee pOrbitDiv (no la fija a una división) → espeja el test de NÉBULA. Puro/RT-safe.
    float debugOrbitRateHz (double bpm) const;

    // Diagnóstico [diccionario][halo]: velocidad orbital EFECTIVA (Hz) que recibe el motor, honrando el modo
    // — FREE = la perilla RATE (orbitRate); SYNC = la división. Es lo que prueba que el RATE NO es inerte.
    float debugEffectiveOrbitRateHz (double bpm) const { return computeOrbitRateHz (bpm); }

    // Telemetría lock-free para el visualizador (el audio escribe, la UI lee). Una por macro (regla
    // anti-bug PULSAR: ninguna macro sin efecto visual). Todas en 0..1 salvo loopEnergy (RMS del lazo).
    std::atomic<float> uiShimmer { 0.55f };   // brillo / cantidad de anillos pitched
    std::atomic<float> uiDecay   { 0.75f };   // cuánto persisten / suben los anillos (cola)
    std::atomic<float> uiSize    { 0.65f };   // radio base del halo
    std::atomic<float> uiTone    { 0.45f };   // tinte / oscuridad de los anillos
    std::atomic<float> uiOrbit   { 0.40f };   // cuánto orbita el halo (rotación de los anillos)
    std::atomic<float> uiMix     { 0.40f };   // presencia del wet
    std::atomic<float> uiFreeze  { 0.0f };    // FREEZE activo (0/1) → el halo se congela
    std::atomic<float> uiLoopRms { 0.0f };    // energía del lazo (florecer de los anillos)
    std::atomic<float> uiRateNorm{ 0.19f };   // VELOCIDAD (0..1) de la órbita/RATE → con qué rapidez nacen,
                                              // suben y orbitan los anillos. La maneja el knob RATE (FREE) o
                                              // la división (SYNC). default ≈ 0.01 Hz. Resuelve "los circulitos
                                              // van rápido y el knob no los toca": ahora el RATE SÍ los controla.

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    float extraClipPush() const override;   // el LED de clip toma la reducción del limiter del motor

private:
    HaloEngine engine;   // motor SHIMMER espacial glacial (pre-delay+BLOOM+FDN+pitch OS en el lazo + pan binaural ITD/ILD)

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pMix       = nullptr;
    std::atomic<float>* pSize      = nullptr;
    std::atomic<float>* pDecay     = nullptr;
    std::atomic<float>* pShimmer   = nullptr;
    std::atomic<float>* pTone      = nullptr;
    std::atomic<float>* pOrbit     = nullptr;
    std::atomic<float>* pLowCut    = nullptr;   // %: high-pass del wet de entrada a la cola (0 = off)
    std::atomic<float>* pHiCut     = nullptr;   // %: low-pass del wet de la cola (0 = off)
    std::atomic<float>* pFreeze    = nullptr;   // bool: captura la nube
    std::atomic<float>* pOrbitSync = nullptr;   // bool: órbita libre / enganchada al tempo (SYNC core)
    std::atomic<float>* pOrbitDiv  = nullptr;   // índice de la división (compases por vuelta, choice)
    std::atomic<float>* pOrbitRate = nullptr;   // float Hz: velocidad de la órbita en FREE (perilla RATE)

    // Velocidad orbital efectiva (Hz) según SYNC/FREE. RT-safe (solo lecturas + aritmética). Una sola fuente
    // de verdad: la usa processAudio (audio) y el getter de diagnóstico (tests).
    float computeOrbitRateHz (double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloProcessor)
};

} // namespace halo
