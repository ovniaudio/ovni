#pragma once
// AppAudioEngine — hostea el SupernovaProcessor y le entrega audio por processBlock (mismo camino que el
// DAW), desde UNA de dos fuentes por vez:
//   · System Audio  : SystemCapture (Core Audio process taps en macOS 14.2+, ScreenCaptureKit antes)
//                     empuja samples desde su cola → processBlock.
//   · Input Device  : AudioDeviceManager abre mic/interfaz; ESTE objeto es el AudioIODeviceCallback.
// Ambos caminos pasan por pushAudio(): mide el pico (medidor de la barra), tira el MIDI del collector
// (nota→explosión/rayo ya existen) y llama processBlock. La SENSIBILIDAD VISUAL (fader IN de la barra)
// es el param "visGain": escala SOLO la mezcla de análisis dentro del processor (la TopBar lo escribe por
// attachment; el inGain del chasis queda en 0 — la salida se descarta igual, es un visualizador).
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

#include "PluginProcessor.h"
#include "AudioSourceModel.h"
#include "audio/SystemCapture.h"
#include "SystemAudioPermission.h"

namespace supernova {

class AppAudioEngine final : private juce::AudioIODeviceCallback,
                             private juce::Timer   // heartbeat de silencio: mantiene vivo el reloj sin audio
{
public:
    // capture = el backend a usar. nullptr (el default de producción) ⇒ makeSystemCapture() elige por la
    // versión de macOS. Se inyecta para poder testear la reconciliación del gate sin Cocoa, TCC ni HAL.
    AppAudioEngine (SupernovaProcessor& processor, juce::PropertiesFile& settings,
                    std::unique_ptr<SystemCapture> capture = nullptr);
    ~AppAudioEngine() override;

    // Arranca con lo persistido (o el default = System Audio). Llamar en el message thread.
    void begin();

    void useSystemAudio();
    void requestSystemAudioPermission();   // el click de ALLOW: ÚNICO camino al cartel del sistema (D-33)
    void pollSystemAudio();                // la barra lo llama a 30Hz: reconcilia el gate con el backend
    void useInputDevice (const juce::String& deviceName);   // "" = device actual del manager
    void setMidiInput (const juce::String& identifierOrName);

    float meterLevel() const noexcept                    { return meter.load(); }
    SystemCaptureStatus systemStatus() const noexcept    { return sysAudio->status(); }
    SystemAudioBackend  captureBackend() const noexcept  { return sysAudio->backend(); }
    SystemAudioPermissionGate::State permissionState() const noexcept { return gate.state(); }
    const AudioSourceModel& model() const noexcept       { return model_; }
    juce::AudioDeviceManager& deviceManager() noexcept   { return devMgr; }   // para el diálogo de settings

    void save();

private:
    // ---- AudioIODeviceCallback (camino Input Device) ----
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // ---- sink común ----
    // fromRealSource=false lo usa SOLO el heartbeat (audio silencioso): no estampa lastRealAudioMs.
    void pushAudio (const float* const* chans, int numCh, int numSamples, double sr, bool fromRealSource = true);
    void stopEverything();     // deja el processor sin fuente (ni SCK ni device drivean processBlock)
    void prepareProcessor (double sr, int block);
    void load();

    // ---- HEARTBEAT (fix 2): el BeatClock/tap/auto-BPM avanzan SOLO en processAudio, que corre SOLO cuando
    // fluye audio. Sin permiso de System Audio / sin sonido → el tap quedaba muerto (BPM clavado en 120). Un
    // timer empuja bloques de SILENCIO (del tamaño del tiempo real transcurrido) cuando NO llegó audio real
    // hace >kAudioGapMs → processBlock corre igual → el reloj tickea con 0 audio. El audio real lo PISA (el
    // gate lo suprime en cuanto una fuente entrega bloques). ----
    void timerCallback() override;

    void startSystemCapture();                 // arranca el backend con el sink de siempre
    static constexpr int    kPollRetryTicks = 75;    // ~2.5s a 30Hz: reintento lento mientras esté denegado
    static constexpr int    kRestartTicks   = 15;    // ~0.5s a 30Hz: rearme de una captura que se cayó sola
    static constexpr int    kRequestTicks   = 30;    // ~1s a 30Hz: re-pedido de un intento que no prendió

    static constexpr int    kPrepBlock    = 4096;
    static constexpr int    kHeartbeatHz  = 90;      // cadencia del tick (buena precisión de tap, barato)
    static constexpr double kAudioGapMs   = 120.0;   // audio real dentro de esta ventana → heartbeat suprimido
    static constexpr double kMaxSilenceMs = 50.0;    // tope del dt por tick → bloque acotado a kPrepBlock

    SupernovaProcessor&   proc;
    juce::PropertiesFile& settings;
    juce::AudioDeviceManager devMgr;
    std::unique_ptr<SystemCapture> sysAudio;   // backend elegido por la versión de macOS (D-33)
    SystemAudioPermissionGate      gate;       // cuándo se intenta capturar (y por lo tanto cuándo sale el cartel)
    juce::MidiMessageCollector midiCollector;
    std::unique_ptr<juce::MidiInput> midiIn;
    AudioSourceModel model_;

    juce::CriticalSection  processLock;   // handoff SCK-queue ↔ message-thread en los switches
    juce::AudioBuffer<float> scratch;     // buffer que ve processBlock
    juce::MidiBuffer         midiScratch;
    std::atomic<float>  meter { 0.0f };
    std::atomic<double> preparedSr { 0.0 };
    std::atomic<double> lastRealAudioMs { 0.0 };   // marca de tiempo del último bloque de audio REAL
    double lastHeartbeatMs = 0.0;                   // (message thread) para el dt del heartbeat
    int  preparedBlock = 0;
    int  retryTicks = 0;
    int  restartTicks = 0;
    int  requestTicks = 0;      // ticks desde el último intento de captura (rearme de `requesting`)
    bool deviceCallbackAdded = false;
    bool loggedLive = false;                   // el "[supernova] live: …" sale una vez por captura

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppAudioEngine)
};

} // namespace supernova
