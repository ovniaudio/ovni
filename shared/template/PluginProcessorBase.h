#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "presets/PresetManager.h"
#include "presets/ABState.h"
#include "presets/PresetTypes.h"

// ========================================================================================================
// PluginProcessorBase — chasis del plugin del sello OVNI (heredado de ÓRBITA M5). Encapsula TODO lo que
// comparten los plugins del sello y que NO depende del DSP concreto:
//   · APVTS (el layout lo provee el plugin),
//   · bypass pass-through,
//   · gain in/out (rampeados, anti-zipper) + IN PHASE / mono-safe (bass-mono) inyectados al layout
//     por el chasis -> TODOS los plugins del catálogo los tienen sin declararlos,
//   · intercepción de Program Change por MIDI -> preset de fábrica (AsyncUpdater, RT-safe),
//   · getStateInformation/setStateInformation (APVTS XML + stateVersion),
//   · atomics uiOutPeak/uiClip (lock-free) para el meter / LED de clip del editor,
//   · PresetManager + ABState ya cableados,
//   · getNumPrograms()=1 (NO exponer presets como programs: rompe la restauración de Bool en VST3).
//
// El plugin concreto deriva de esta clase y SÓLO define su DSP (processAudio) y su motor (prepareEngine).
// Ver `shared/template/_README.md`.
// ========================================================================================================
namespace ovni
{
class PluginProcessorBase : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    // `pluginName`  -> getName() + carpeta de User presets (OVNI <pluginName>).
    // `layout`      -> lo construye el plugin con su static createParameterLayout().
    // `buses`       -> por defecto estéreo->estéreo; pasá otro si tu plugin difiere.
    PluginProcessorBase (juce::String pluginName,
                         juce::AudioProcessorValueTreeState::ParameterLayout layout,
                         BusesProperties buses = stereoBuses());

    ~PluginProcessorBase() override = default;

    // APVTS público: lo usan el editor (attachments) y los presets, igual que en ÓRBITA.
    juce::AudioProcessorValueTreeState apvts;

    // Estado para el visualizador / meter (lock-free: el audio escribe, la UI lee).
    std::atomic<float> uiOutPeak { 0.0f };   // pico de salida final (post-DSP) -> meter
    std::atomic<float> uiClip    { 0.0f };   // "estás empujando" 0..1 -> LED de clip

    presets::PresetManager& presets() { return presetManager; }   // browser de presets (lo usa el editor)
    presets::ABState&       ab()      { return abState; }         // comparador A/B

    // PLUGIN (opcional): id del parámetro Bool de bypass. Default "bypass". Público: lo lee el editor base
    // (toggle del power + estado del LED). Sobreescribilo si tu layout usa otro id.
    virtual juce::String bypassParamID() const { return "bypass"; }

    //==================================================================================== AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    // createEditor() lo define el plugin (devuelve SU editor).

    const juce::String getName() const override { return pluginName; }
    bool acceptsMidi() const override;
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // NO exponemos presets como programs del host (rompía la restauración de Bool en VST3 / pluginval).
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

protected:
    // ======== PUNTOS DE EXTENSIÓN (el plugin concreto los define) ========

    // PLUGIN: define acá tu DSP. Se llama por bloque SÓLO cuando NO está en bypass. Los canales de salida
    // sin entrada ya vienen limpios; escribí el resultado (estéreo) en `buffer`.
    virtual void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) = 0;

    // PLUGIN (opcional): prepará tu motor con sample rate / block size. Llamado desde prepareToPlay.
    virtual void prepareEngine (const juce::dsp::ProcessSpec&) {}

    // PLUGIN (opcional): aporte extra al LED de clip 0..1 (p.ej. la reducción de tu limiter). La base ya
    // computa el empuje por pico de salida; devolvé acá el de tu limiter para que el LED tome el mayor.
    virtual float extraClipPush() const { return 0.0f; }

    // PLUGIN (opcional): ¿IN PHASE / mono-safe activo? (el chasis inyecta el param "monoSafe"). Para que
    // tu motor ajuste su comportamiento — p.ej. MovementEngine apaga el ITD -> paneo puro, en fase.
    bool isMonoSafe() const noexcept { return pMonoSafe != nullptr && pMonoSafe->load() >= 0.5f; }

    static BusesProperties stereoBuses();

    juce::String pluginName;

private:
    // Ctor delegado: inyecta los params de utilidad del sello (inGain/output/monoSafe) en el layout
    // ANTES de construir el APVTS, así TODOS los plugins los tienen sin declararlos. `bypass` lo
    // declara cada plugin (ya estándar) y la base sólo lo LEE; no se inyecta (evita duplicado).
    struct PrivateTag {};
    PluginProcessorBase (PrivateTag, juce::String pluginName,
                         juce::AudioProcessorValueTreeState::ParameterLayout layout, BusesProperties buses);
    static juce::AudioProcessorValueTreeState::ParameterLayout withUtilityParams (
        juce::AudioProcessorValueTreeState::ParameterLayout layout);

    void handleAsyncUpdate() override;   // Program Change diferido -> applyFactory (message thread)

    presets::PresetManager presetManager;
    presets::ABState       abState;
    std::atomic<int>       pendingProgram { -1 };

    // Gain staging del sello (ÓRBITA): IN drive antes del DSP, OUT después; ambos rampeados por-bloque
    // (anti-zipper). + IN PHASE (Mono Safe): bass-mono genérico (one-pole LP -> suma graves a mono).
    std::atomic<float>* pInGain   = nullptr;
    std::atomic<float>* pOutput   = nullptr;
    std::atomic<float>* pMonoSafe = nullptr;
    float prevInGain  = 1.0f;
    float prevOutGain = 1.0f;
    float bassMonoL = 0.0f, bassMonoR = 0.0f, bassMonoCoef = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessorBase)
};
}
