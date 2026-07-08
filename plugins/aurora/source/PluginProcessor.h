#pragma once

#include "template/PluginProcessorBase.h"
#include "engine/AuroraEngine.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include <array>
#include <atomic>

// =============================================================================
// AURORA — processor concreto (SPL·01). Deriva del chasis del sello
// (ovni::PluginProcessorBase: APVTS, bypass, Program Change→preset, estado,
// meter atomics, PresetManager+A/B). Aporta lo PROPIO de AURORA: el motor de
// PANEO ESPECTRAL (AuroraEngine = STFT/OLA compartido + reparto L/R por bin con
// ley de potencia y fase preservada + MOTION sincable + MONO SAFE + DUCK).
//
// LATENCIA HONESTA (gate [latency], house-standard §1): el pipeline OLA retarda
// N samples (2048 @44.1/48k) → se DECLARA con setLatencySamples() en
// prepareEngine y el DRY pasa por el MISMO retardo dentro del motor (el MIX
// cruza señales ALINEADAS: cero comb a mix intermedio, y reportada == real).
//
// BYPASS COMPENSADO (review 2026-06-10, HIGH): el pass-through instantáneo del
// chasis vale para plugins de latencia 0, pero AURORA declara N=2048 → conmutar
// el power saltaba ~43 ms en el tiempo (click/doblaje) y en bypass la pista
// quedaba ADELANTADA respecto del PDC del host. AuroraProcessor overridea
// processBlock: en bypass el dry sale RETRASADO getLatencySamples() (PDC
// constante, "latencia declarada == real" también en bypass) y la conmutación
// cruza wet↔dry-retrasado con un micro-fade (~12 ms) sobre señales ALINEADAS
// (anti-click). El motor sigue corriendo en bypass (CPU ~0.2%) para que el
// pipeline OLA esté caliente al volver — sin garble de re-entrada.
//
// SYNC (control CORE del sello): motionSync + motionDivision enganchan el RATE
// del MOTION (el abanico que se abre/cierra) al tempo del host — el elemento
// rítmico natural de AURORA. DUCK = la palanca que REACCIONA (ADN musical).
//
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace aurora
{

class AuroraProcessor : public ovni::PluginProcessorBase
{
public:
    AuroraProcessor();
    ~AuroraProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // AuroraEditor (chasis verde SPL·01 + AuroraField)

    // BYPASS COMPENSADO (ver cabecera): pisa el pass-through instantáneo del chasis con
    // dry retrasado N + micro-fade. El camino ACTIVO delega intacto al chasis.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // Acceso al motor SÓLO para tests (anti-clip/estabilidad del AuroraProcessor real).
    AuroraEngine& engineForTest() noexcept { return engine; }

    // Diagnóstico [diccionario]/[measure]: rate del MOTION (Hz) que daría la división
    // actual con el BPM dado (lee pMotionDiv, no lo fija). Puro/RT-safe.
    float debugMotionRateHz (double bpm) const;

    // Diagnóstico: rate EFECTIVO (Hz) que recibe el motor honrando el modo — FREE = la
    // perilla RATE (motionRate); SYNC = la división. Prueba que el RATE no es inerte.
    float debugEffectiveMotionRateHz (double bpm) const { return computeMotionRateHz (bpm); }

    // ── Telemetría lock-free para el visualizador (el audio escribe, la UI lee). Una por
    //    macro (regla anti-bug PULSAR: ninguna macro sin efecto visual) + el despliegue
    //    por banda (energía + posición) para pintar la aurora de la etapa 3.
    std::atomic<float> uiSpread   { 0.55f };  // 0..1 apertura base del abanico
    std::atomic<float> uiTilt     { 0.0f };   // -1..+1 carácter del mapeo (curva de la aurora)
    std::atomic<float> uiMotion   { 0.0f };   // 0..1 profundidad del latido
    std::atomic<float> uiMonoSafe { 0.5f };   // 0..1 cuánta red mono (graves amarrados al centro)
    std::atomic<float> uiDuck     { 0.0f };   // 0..1 profundidad del sidechain
    std::atomic<float> uiMix      { 1.0f };   // 0..1 presencia del wet
    std::atomic<float> uiGamma    { 0.55f };  // γ EFECTIVO (spread·motion·duck): el abanico late/respira
    std::atomic<float> uiDuckEnv  { 0.0f };   // 0..1 envolvente del dry (se VE la pegada que cierra el abanico)
    std::atomic<float> uiRateNorm { 0.45f };  // 0..1 velocidad del MOTION (mapeo log 0.02–8 Hz) — FREE o SYNC

    static constexpr int kVizBands = AuroraEngine::kVizBands;
    std::array<std::atomic<float>, kVizBands> uiBandEnergy {};  // energía por banda (lineal ~0..1)
    std::array<std::atomic<float>, kVizBands> uiBandPos {};     // posición -1..+1 por banda (el despliegue)

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    float extraClipPush() const override;   // el LED de clip toma la reducción del limiter del motor

private:
    AuroraEngine engine;   // motor de paneo espectral (STFT compartido + reparto por bin)

    // LOW CUT (high-pass TPT) + HI CUT (low-pass TPT) del PLUGIN: filtran la SALIDA COMPLETA (dry+wet ya
    // mezclados) DESPUÉS del motor — el corte se OYE a cualquier MIX. Bloques compartidos shared/dsp/.
    // Default 0 → 20 Hz / 20 kHz → transparente (no cambia el sonido ni los presets). Lineales → sin alias.
    ovni::dsp::LowCut lowCut;
    ovni::dsp::HiCut  hiCut;

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pSpread     = nullptr;
    std::atomic<float>* pTilt       = nullptr;
    std::atomic<float>* pMotion     = nullptr;
    std::atomic<float>* pMonoSafeAmt= nullptr;   // knob Mono Safe propio (≠ "monoSafe"/IN PHASE del chasis)
    std::atomic<float>* pDuck       = nullptr;
    std::atomic<float>* pMix        = nullptr;
    std::atomic<float>* pMotionSync = nullptr;   // bool: MOTION libre / enganchado al tempo (SYNC core)
    std::atomic<float>* pMotionDiv  = nullptr;   // índice de la división (beats por ciclo, choice)
    std::atomic<float>* pMotionRate = nullptr;   // float Hz: velocidad del MOTION en FREE (perilla RATE)
    std::atomic<float>* pLowCut     = nullptr;   // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz)
    std::atomic<float>* pHiCut      = nullptr;   // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz)

    // ── BYPASS COMPENSADO (review 2026-06-10) ──────────────────────────────────────────
    std::atomic<float>* pBypass     = nullptr;   // bool "bypass" (el mismo que lee el power del chasis)
    std::atomic<float>* pInGainUtil = nullptr;   // "inGain"/"output" del chasis: el wet manual del
    std::atomic<float>* pOutputUtil = nullptr;   //   fade-out los aplica estáticos (ver .cpp)
    std::vector<float>  bypassRing[2];           // dry crudo, retrasado getLatencySamples()
    int   bypassWrite  = 0;
    int   bypassRingSz = 0;
    juce::AudioBuffer<float> bypassDly;          // scratch: dry retrasado del bloque
    juce::AudioBuffer<float> bypassWet;          // scratch: wet manual (motor caliente en bypass)
    float bypassXf     = 0.0f;                   // 0 = activo · 1 = bypass (rampa por-sample)
    float bypassXfStep = 0.0f;                   // paso del micro-fade (~12 ms)

    // Alimenta el ring del dry crudo y (si dly != nullptr) lee el dry RETRASADO N.
    void feedBypassRing (const juce::AudioBuffer<float>& in, int n,
                         juce::AudioBuffer<float>* dly) noexcept;

    // Rate efectivo del MOTION (Hz) según SYNC/FREE. RT-safe. Una sola fuente de verdad:
    // la usa processAudio (audio) y el getter de diagnóstico (tests).
    float computeMotionRateHz (double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraProcessor)
};

} // namespace aurora
