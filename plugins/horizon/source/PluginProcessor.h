#pragma once

#include "template/PluginProcessorBase.h"
#include "engine/HorizonEngine.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include <array>
#include <atomic>
#include <vector>

// =============================================================================
// HORIZON — processor concreto (SPL·02). Deriva del chasis del sello
// (ovni::PluginProcessorBase: APVTS, bypass, Program Change→preset, estado,
// meter atomics, PresetManager+A/B). Aporta lo PROPIO de HORIZON: el motor de
// FREEZE ESPECTRAL RÍTMICO (HorizonEngine = STFT/OLA compartido + captura de
// frame + scheduler de gate raised-cosine + whisper + spread + duck).
//
// REUSA EL STFT DE AURORA tal cual (ovni::engines::StftEngine). Lo único nuevo
// vive en HorizonEngine: captura mag+fase del frame, resíntesis con avance de
// fase coherente, gate rítmico enganchado al ppq, whisper, duck.
//
// LATENCIA HONESTA (gate [latency], house-standard §1): el pipeline OLA retarda
// N samples (2048 @44.1/48k) → se DECLARA con setLatencySamples() en
// prepareEngine y el DRY pasa por el MISMO retardo dentro del motor (el MIX
// cruza señales ALINEADAS: cero comb a mix intermedio, y reportada == real).
//
// BYPASS COMPENSADO (idéntico a AURORA — HORIZON es latente): el pass-through
// instantáneo del chasis vale para latencia 0, pero HORIZON declara N=2048. En
// bypass el dry sale RETRASADO getLatencySamples() (PDC constante) y la
// conmutación cruza wet↔dry-retrasado con un micro-fade (~12 ms) sobre señales
// ALINEADAS (anti-click). El motor sigue corriendo en bypass para que el
// pipeline OLA esté caliente al volver.
//
// SYNC (control CORE del sello): rateSync + rateDivision enganchan el RATE del
// re-trigger (el latido del freeze) al tempo del host. DUCK = la palanca que
// REACCIONA (ADN musical: el freeze se aparta cuando pega el dry).
//
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace horizon
{

class HorizonProcessor : public ovni::PluginProcessorBase
{
public:
    HorizonProcessor();
    ~HorizonProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;   // HorizonEditor (chasis verde SPL·02 + espectro congelado)

    // BYPASS COMPENSADO (ver cabecera): pisa el pass-through instantáneo del chasis con
    // dry retrasado N + micro-fade. El camino ACTIVO delega intacto al chasis.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // Acceso al motor SÓLO para tests (anti-clip/estabilidad del HorizonProcessor real).
    HorizonEngine& engineForTest() noexcept { return engine; }

    // Diagnóstico [diccionario]/[measure]: rate del re-trigger (Hz) que daría la división
    // actual con el BPM dado (lee pRateDiv, no lo fija). Puro/RT-safe.
    float debugSyncRateHz (double bpm) const;

    // Diagnóstico: rate EFECTIVO (Hz) que recibe el motor honrando el modo — FREE = la
    // perilla RATE (rate); SYNC = la división. Prueba que el RATE no es inerte.
    float debugEffectiveRateHz (double bpm) const { return computeRateHz (bpm); }

    // ── Telemetría lock-free para el visualizador (el audio escribe, la UI lee). Una por
    //    macro (regla anti-bug PULSAR: ninguna macro sin efecto visual) + el estado del
    //    gate (fase/amplitud del latido) + el espectro congelado (para el visualizador).
    std::atomic<float> uiWhisper  { 0.12f };  // 0..1 coherencia↔randomización de fase (tiembla el cristal)
    std::atomic<float> uiSpread   { 0.50f };  // 0..1 el freeze abriéndose a los lados
    std::atomic<float> uiDuck     { 0.0f };   // 0..1 profundidad del sidechain (el freeze se encoge con el dry)
    std::atomic<float> uiMix      { 1.0f };   // 0..1 presencia del wet
    std::atomic<float> uiFreeze   { 0.0f };   // 0/1 el espectro está cristalizado (gesto)
    std::atomic<float> uiGateAmp  { 1.0f };   // 0..1 amplitud del latido (raised-cosine) — el pulso del freeze
    std::atomic<float> uiGatePhase{ 0.0f };   // 0..1 fase del ciclo del gate (dónde está el latido)
    std::atomic<float> uiWetEnergy{ 0.0f };   // 0..1 energía del wet (el freeze "se oye" a mix realista)
    std::atomic<float> uiDuckEnv  { 0.0f };   // 0..1 envolvente del dry (la pegada que agacha el freeze)
    std::atomic<float> uiRateNorm { 0.0f };   // 0..1 velocidad del latido (mapeo lineal 0–8 Hz) — FREE o SYNC

    static constexpr int kVizBands = HorizonEngine::kVizBands;
    std::array<std::atomic<float>, kVizBands> uiSpectrum {};  // el espectro congelado por banda (visualizador)

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    float extraClipPush() const override;   // el LED de clip toma la reducción del limiter del motor

private:
    HorizonEngine engine;   // motor de freeze espectral rítmico (STFT compartido + gate + whisper + duck)

    // LOW CUT (high-pass TPT) + HI CUT (low-pass TPT) del PLUGIN: filtran la SALIDA COMPLETA (dry+wet ya
    // mezclados) DESPUÉS del motor — el corte se OYE a cualquier MIX. Bloques compartidos shared/dsp/.
    // Default 0 → 20 Hz / 20 kHz → transparente (no cambia el sonido ni los presets). Lineales → sin alias.
    ovni::dsp::LowCut lowCut;
    ovni::dsp::HiCut  hiCut;

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pFreeze   = nullptr;
    std::atomic<float>* pWhisper  = nullptr;
    std::atomic<float>* pSpread   = nullptr;
    std::atomic<float>* pDuck     = nullptr;
    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pRateSync = nullptr;   // bool: latido libre / enganchado al tempo (SYNC core)
    std::atomic<float>* pRateDiv  = nullptr;   // índice de la división (beats por ciclo, choice)
    std::atomic<float>* pRate     = nullptr;   // float Hz: velocidad del re-trigger en FREE (perilla RATE)
    std::atomic<float>* pLowCut   = nullptr;   // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz)
    std::atomic<float>* pHiCut    = nullptr;   // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz)

    // ── BYPASS COMPENSADO (idéntico a AURORA — HORIZON es latente) ─────────────────────
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

    void feedBypassRing (const juce::AudioBuffer<float>& in, int n,
                         juce::AudioBuffer<float>* dly) noexcept;

    // Rate efectivo del re-trigger (Hz) según SYNC/FREE. RT-safe. Una sola fuente de verdad:
    // la usa processAudio (audio) y el getter de diagnóstico (tests).
    float computeRateHz (double bpm) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonProcessor)
};

} // namespace horizon
