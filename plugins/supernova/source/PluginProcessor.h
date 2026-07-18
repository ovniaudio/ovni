#pragma once
#include "template/PluginProcessorBase.h"
#include "analysis/LockFreeAudioFifo.h"
#include "analysis/TripleBuffer.h"
#include "analysis/AnalysisFrame.h"
#include "analysis/MidiTriggerQueue.h"
#include "analysis/MidiCcQueue.h"
#include "image/PhotoSequence.h"
#include "midi/MidiMapper.h"
#include "midi/MidiCcMap.h"
#include "tempo/BeatClock.h"
#include "tempo/LfoBank.h"
#include "presets/SceneBank.h"
#include "presets/PresetApplier.h"
#include <atomic>
#include <memory>
#include <vector>

namespace supernova
{
class AnalysisThread;

// SUPERNOVA — sintetizador visual. NO procesa audio (RNF1): processAudio SOLO copia (read-only) una mezcla
// mono de la entrada a un FIFO lock-free para el AnalysisThread; el buffer sale bit-exacto. El chasis, en
// default (inGain/output 0 dB, monoSafe off), no altera el audio (gain ramps identidad, bass-mono salteado).
class SupernovaProcessor : public ovni::PluginProcessorBase
{
public:
    SupernovaProcessor();
    ~SupernovaProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorEditor* createEditor() override;
    void releaseResources() override;
    void setStateInformation (const void* data, int sizeInBytes) override;   // bracket para el PresetApplier

    // Consumidor ÚNICO (el MetalViewComponent del editor) lee el último frame de análisis de acá.
    TripleBuffer<AnalysisFrame>& analysis() noexcept { return analysisFrames; }
    // Cola de triggers MIDI visuales (explosión/rayo); el editor la conecta a la vista.
    MidiTriggerQueue& midiTriggers() noexcept { return midiTriggerQueue; }

    // TEMPO (Phase B · BeatClock). El BeatClock avanza en processAudio con el playhead del host; el editor lee
    // estos snapshots atómicos (lock-free) para LFOs sync / cuantizar eventos / mostrar el BPM.
    double tempoBpm()        const noexcept { return pubBpm.load(); }
    double beatPhase01()     const noexcept { return pubBeatPhase.load(); }   // 0..1 dentro de la negra
    double phaseInBeats()    const noexcept { return pubPhaseBeats.load(); }
    bool   transportPlaying() const noexcept { return pubPlaying.load(); }
    // TAP TEMPO (message thread, app standalone sin host): marca un golpe; processAudio lo consume con su reloj.
    void   tapTempo() noexcept { tapPending.store (true); }

    // MIDI-LEARN (Phase B). La cola trae los CC crudos (audio→editor); el mapa (message thread) los rutea a
    // params. El editor: dcha-click en un knob → armLearn(paramId); drena la cola cada tick → feed → APVTS.
    MidiCcQueue& midiCcQueue() noexcept { return ccQueue; }
    MidiCcMap&   midiCcMap()   noexcept { return ccMap; }
    // LFOs sync al tempo + banco de ESCENAS (Phase B). El editor los configura/aplica; viven acá para que el
    // getState del chasis (apvts.state) los serialice gratis, como el ccMap y la secuencia.
    LfoBank&   lfoBank()   noexcept { return lfos; }
    SceneBank& sceneBank() noexcept { return scenes; }

    // Persistencia (message thread): sube los mapeos/LFOs/escenas serializados al ValueTree del APVTS.
    void syncCcMapToState()  { apvts.state.setProperty ("midiCcMap", juce::String (ccMap.serialize()), nullptr); }
    void syncLfosToState()   { apvts.state.setProperty ("lfoBank",   juce::String (lfos.serialize()), nullptr); }
    void syncScenesToState() { apvts.state.setProperty ("sceneBank", juce::String (scenes.serialize()), nullptr); }

    // EXPORT CON SONIDO: snapshot desenrollado (viejo→nuevo, interleaved L/R) de los últimos ~12s del
    // MISMO audio que alimenta el análisis — el export lo muxea al MP4 en sync con recentFrames.
    std::vector<float> audioRingSnapshot (double& srOut) const;

    // PHOTO SEQUENCE (spec §D): fuente de verdad; el editor la maneja SOLO en el message thread.
    PhotoSequence& photoSequence() noexcept { return photoSeq; }
    // Sube el estado de la secuencia al ValueTree del APVTS (child "sequence") → getStateInformation
    // del chasis lo serializa gratis (copyState lleva los children). Llamar tras cada cambio (msg thread).
    void syncSequenceToState()
    {
        auto st = apvts.state;
        st.removeChild (st.getChildWithName ("sequence"), nullptr);
        if (photoSeq.size() > 0) st.appendChild (photoSeq.toValueTree(), nullptr);
    }
    // Sello de restauraciones: setStateInformation lo incrementa → el editor abierto re-hidrata la
    // secuencia en su próximo tick (el host cargó un proyecto/preset con el editor en pantalla).
    int stateStamp() const noexcept { return stateStampCount.load(); }

protected:
    void prepareEngine (const juce::dsp::ProcessSpec& spec) override;
    void processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

private:
    LockFreeAudioFifo            audioFifo;
    TripleBuffer<AnalysisFrame>  analysisFrames;
    std::unique_ptr<AnalysisThread> analysisThread;
    std::vector<float>           monoScratch;   // pre-alocado en prepareEngine (sin alloc en el audio thread)

    // Anillo de audio del export-con-sonido (~12s, interleaved L/R). RT-safe: escritura memcpy-style a
    // buffer pre-alocado + índice de frame atómico (release); el snapshot (consumer) copia y desenrolla.
    static constexpr int kAudioRingSeconds = 12;
    std::vector<float>   audioRing;
    std::atomic<int>     audioRingWriteFrame { 0 };
    int                  audioRingFrames = 0;
    double               audioRingSr     = 48000.0;

    MidiMapper                   midiMapper;    // nota/PC → evento tipado (C++ puro, RF5)
    MidiTriggerQueue             midiTriggerQueue;   // triggers visuales → render tick (SPSC lock-free)
    MidiCcQueue                  ccQueue;            // CC crudos → editor (MIDI-learn, SPSC lock-free)
    MidiCcMap                    ccMap;              // CC → param (message thread; persistido al state)
    LfoBank                      lfos;               // moduladores sync al tempo (message thread; persistido)
    SceneBank                    scenes;             // snapshots del usuario (message thread; persistido)

    std::atomic<float>*          pVisGain = nullptr; // sensibilidad visual (escala SOLO el análisis, no el insert)
    BeatClock                    beatClock;          // tempo sync (host/tap/auto) — avanza en processAudio
    double                       audioTimeSec = 0.0; // reloj de sample para tap-tempo (audio thread)
    std::atomic<bool>            tapPending { false };
    std::atomic<double>          pubBpm { 120.0 };   // snapshots publicados al editor (lock-free)
    std::atomic<double>          pubBeatPhase { 0.0 };
    std::atomic<double>          pubPhaseBeats { 0.0 };
    std::atomic<bool>            pubPlaying { false };
    std::unique_ptr<PresetApplier> presetApplier;    // 'preset' choice → continuos del APVTS (RF8 + morph B)
    PhotoSequence                photoSeq;           // rotación de fotos (spec §D; msg thread)
    std::atomic<int>             stateStampCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupernovaProcessor)
};
}
