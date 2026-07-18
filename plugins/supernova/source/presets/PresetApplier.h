#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

// PresetApplier — contraparte ANGOSTA y a-prueba-de-feedback de PresetManager::applyFactory a nivel plugin. Al
// cambiar el param 'preset' (choice) escribe SÓLO los 11 continuos a su destino de fábrica (default→overlay);
// NUNCA resetToDefaults, NUNCA escribe 'preset'/'bypass'/'explode'. Diferido al message thread (AsyncUpdater),
// coalesce ráfagas y lee el índice ACTUAL al disparar → el reset('preset'→0) que emite applyFactory queda
// coalescido e idempotente (no pelea con PresetManager). Hace converger el APVTS al destino → round-trip
// correcto + wiring de la automatización de 'preset' (RF8) + headless.
namespace supernova
{
class PresetApplier : private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater
{
public:
    explicit PresetApplier (juce::AudioProcessorValueTreeState& s);
    ~PresetApplier() override;

    // Bracket alrededor del restore de estado del chasis: suspende para que replaceState() NO re-aplique el
    // preset sobre los continuos recién restaurados (posiblemente tocados a mano), y adopta el índice.
    void beginStateRestore() noexcept { suspended.store (true); cancelPendingUpdate(); }
    void endStateRestore()   noexcept;

private:
    void parameterChanged (const juce::String& id, float) override;
    void handleAsyncUpdate() override;
    int  currentIndex() const noexcept;

    juce::AudioProcessorValueTreeState& apvts;
    std::atomic<bool> suspended { false };
    int lastIndex = -1;
};
}
