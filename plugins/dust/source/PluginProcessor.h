#pragma once

#include "template/PluginProcessorBase.h"
#include "engine/DustEngine.h"
#include "ui/BubbleEvents.h"
#include "dsp/LowCut.h"
#include "dsp/HiCut.h"
#include <atomic>

// =============================================================================
// DUST — processor concreto (MOV·03). Deriva del chasis del sello
// (ovni::PluginProcessorBase: APVTS, bypass, Program Change->preset, estado,
// meter atomics, PresetManager+A/B). Aporta lo PROPIO de DUST: el motor de ECOS
// BINAURALES tipo burbujas (DustEngine = multi-tap con feedback estable + banco
// de 16 direcciones HRIR fijas + limiter 0.85) y la capa de macro-mapping
// perceptual (el foso): RATE FREE/SYNC en ms, DENSIDAD = feedback log con piso
// de estabilidad + nº de taps, SPREAD = varianza angular alrededor del ORIGIN,
// VIDA = deriva por tap, DUCK = sidechain interno del dry sobre el wet, MIX por
// ley de potencia. Latencia 0 declarada == real (dry sin retardo).
//
// Honestidad (curaduría): sin TONE/DAMP (interno fijo), sin órbita del ORIGIN.
// El entry point de JUCE (createPluginFilter) se define en el .cpp.
// =============================================================================
namespace dust
{

class DustProcessor : public ovni::PluginProcessorBase
{
public:
    DustProcessor();
    ~DustProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorEditor* createEditor() override;

    // Acceso al motor SÓLO para tests (estabilidad/medición del DustProcessor real).
    engine::DustEngine& engineForTest() noexcept { return engine; }

    // Diagnóstico [real]/[measure]: espaciado efectivo (ms) que recibe el motor con el BPM dado,
    // honrando FREE/SYNC. Una sola fuente de verdad con processAudio (prueba que el SYNC no es inerte).
    float debugEffectiveRateMs (double bpm) const { return computeRateMs (bpm); }

    // Telemetría lock-free para el visualizador (el audio escribe, la UI lee). Una por macro (regla
    // anti-bug PULSAR: ninguna macro sin efecto visual). Todas 0..1 salvo origin (-1..1).
    std::atomic<float> uiMix      { 0.35f };   // presencia del wet
    std::atomic<float> uiRateNorm { 0.55f };   // espaciado efectivo (log 20..2000 ms -> 0..1): cadencia de nacimientos
    std::atomic<float> uiDensity  { 0.40f };   // cuántas burbujas / cuánto regenera la nube
    std::atomic<float> uiSpread   { 0.60f };   // apertura angular del campo
    std::atomic<float> uiVida     { 0.25f };   // cuánto derivan/flotan las burbujas
    std::atomic<float> uiDuck     { 0.0f };    // profundidad del sidechain (knob)
    std::atomic<float> uiDuckGr   { 0.0f };    // reducción de ganancia REAL del duck este bloque (el campo "respira")
    std::atomic<float> uiOriginX  { 0.0f };    // ORIGIN (el visualizador lo dibuja y lo arrastra)
    std::atomic<float> uiOriginY  { 0.35f };
    std::atomic<float> uiWetRms   { 0.0f };    // energía del wet (flare/luminosidad de la nube)

    // FIFO lock-free de eventos burbuja (nacimientos: azimut, energía, vida) — lo drena el visualizador.
    ui::BubbleEventFifo bubbleEvents;

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    float extraClipPush() const override;   // el LED de clip toma la reducción del limiter del motor

private:
    engine::DustEngine engine;   // multi-tap + banco binaural de 16 direcciones + limiter 0.85

    // LOW CUT (high-pass TPT) + HI CUT (low-pass TPT) del PLUGIN: filtran la SALIDA COMPLETA (dry+wet ya
    // mezclados) DESPUÉS del motor — el corte se OYE a cualquier MIX. Bloques compartidos shared/dsp/.
    // Default 0 → 20 Hz / 20 kHz → transparente (no cambia el sonido ni los presets). Lineales → sin alias.
    ovni::dsp::LowCut lowCut;
    ovni::dsp::HiCut  hiCut;

    // RATE efectivo (ms) según SYNC/FREE. RT-safe (solo lecturas + aritmética). La usa processAudio
    // (audio) y el getter de diagnóstico (tests).
    float computeRateMs (double bpm) const;

    // Copia del dry (pre-motor) para MIX por ley de potencia + envelope del DUCK. Preasignada.
    juce::AudioBuffer<float> dryBuf;

    // MIX ley de potencia: las ganancias MAPEADAS (cos/sin) se rampean por-sample (suavizado
    // POST-mapeo, regla anti-click) con continuidad C0 entre bloques.
    float gDrySm = 1.0f, gWetSm = 0.0f;

    // DUCK: envelope-follower del dry (attack ~5 ms, release ~150 ms) + profundidad rampeada.
    float duckEnv = 0.0f, duckDepthSm = 0.0f;
    float duckAttCoef = 0.0f, duckRelCoef = 0.0f;

    // Punteros atómicos cacheados del APVTS (RT-safe; nada de getParameter por string en audio).
    std::atomic<float>* pMix      = nullptr;
    std::atomic<float>* pRate     = nullptr;   // float ms: espaciado en FREE (perilla RATE)
    std::atomic<float>* pRateSync = nullptr;   // bool: FREE / enganchado al tempo (SYNC core)
    std::atomic<float>* pRateDiv  = nullptr;   // índice de la división (choice)
    std::atomic<float>* pDensity  = nullptr;
    std::atomic<float>* pSpread   = nullptr;
    std::atomic<float>* pVida     = nullptr;
    std::atomic<float>* pDuck     = nullptr;
    std::atomic<float>* pOriginX  = nullptr;
    std::atomic<float>* pOriginY  = nullptr;
    std::atomic<float>* pLowCut   = nullptr;   // 0..100 % high-pass de la SALIDA dry+wet (20 Hz=off → 500 Hz)
    std::atomic<float>* pHiCut    = nullptr;   // 0..100 % low-pass de la SALIDA dry+wet (20 kHz=off → 1.5 kHz)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustProcessor)
};

} // namespace dust
