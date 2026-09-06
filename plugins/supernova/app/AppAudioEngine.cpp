#include "AppAudioEngine.h"
#include "AudioConvert.h"
#include <cstdio>

namespace supernova {

namespace {
constexpr const char* kSettingSource = "audioSource";      // XML del AudioSourceModel
constexpr const char* kSettingDevice = "audioDeviceState"; // XML del AudioDeviceManager
constexpr const char* kSettingAsked  = "sysAudioAsked";    // ¿ya salió el cartel de permiso alguna vez? (D-33)
}

AppAudioEngine::AppAudioEngine (SupernovaProcessor& processor, juce::PropertiesFile& s,
                                std::unique_ptr<SystemCapture> capture)
    : proc (processor), settings (s),
      sysAudio (capture != nullptr ? std::move (capture) : makeSystemCapture())
{
    scratch.setSize (2, kPrepBlock, false, true, false);   // preasignado; jamás realoca en callback
}

AppAudioEngine::~AppAudioEngine()
{
    stopTimer();               // parar el heartbeat ANTES de soltar la fuente (no toca processBlock tras esto)
    stopEverything();
    if (midiIn) midiIn->stop();
    midiIn = nullptr;
}

// ---------------------------------------------------------------------------- ciclo de vida
void AppAudioEngine::begin()
{
    load();
    if (model_.midiInput().isNotEmpty())
        setMidiInput (model_.midiInput());

    if (model_.kind() == AudioSourceModel::Kind::inputDevice)
        useInputDevice (model_.deviceName());
    else
        useSystemAudio();

    // Heartbeat de silencio (fix 2): el reloj musical (tap/auto-BPM/LFOs) tickea aunque no fluya audio.
    lastHeartbeatMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (kHeartbeatHz);
}

void AppAudioEngine::useSystemAudio()
{
    stopEverything();
    model_.selectSystemAudio();

    prepareProcessor (48000.0, kPrepBlock);
    midiCollector.reset (48000.0);

    // D-33: acá NO se arranca a ciegas. El gate decide — sin permiso todavía, la app queda viva (heartbeat,
    // MIC/MIDI) y la barra muestra el aviso; el cartel del sistema espera al click de ALLOW.
    gate.begin (settings.getBoolValue (kSettingAsked, false), sysAudio->isAuthorized());
    if (gate.shouldStartCapture())
        startSystemCapture();
}

void AppAudioEngine::startSystemCapture()
{
    loggedLive = false;
    sysAudio->start (48000, 2, [this] (const float* const* ch, int nc, int nf, double sr)
    {
        pushAudio (ch, nc, nf, sr);
    });
}

// El click de ALLOW de la barra: el ÚNICO camino por el que la app deja salir el cartel de macOS.
void AppAudioEngine::requestSystemAudioPermission()
{
    if (model_.kind() != AudioSourceModel::Kind::systemAudio) return;
    if (! gate.allowClicked()) return;

    settings.setValue (kSettingAsked, true);   // se pide UNA vez: los próximos arranques reintentan callados
    settings.saveIfNeeded();
    prepareProcessor (48000.0, kPrepBlock);    // idempotente
    startSystemCapture();
}

// Reconcilia el gate con lo que de verdad hizo el backend. La barra lo llama a 30Hz.
void AppAudioEngine::pollSystemAudio()
{
    if (model_.kind() != AudioSourceModel::Kind::systemAudio) return;

    const auto st = sysAudio->status();

    if (gate.state() == SystemAudioPermissionGate::State::requesting)
    {
        if (st == SystemCaptureStatus::capturing)             gate.captureStarted();
        else if (st == SystemCaptureStatus::permissionDenied) { gate.captureDenied(); sysAudio->stop(); }
        else if (st == SystemCaptureStatus::error || st == SystemCaptureStatus::unsupported)
        {
            // NO es un permiso faltante (HAL sin salida default, aggregate caído, API que no existe en
            // esta versión de macOS): sería mentira mandar al usuario a Ajustes. Pero hasta 0.3.0 esto
            // no hacía NADA y el gate quedaba clavado en `requesting` PARA SIEMPRE — sin aviso, sin
            // botón, sin audio, y con lo que el intento hubiera creado colgando del HAL. Soltamos la
            // captura y volvemos al aviso con ALLOW, que es lo honesto: "no pude, probá de nuevo".
            gate.captureFailed();
            sysAudio->stop();
        }
        return;
    }

    // Capturando (o eso creíamos): el backend puede haberse caído SOLO. El caso de todos los días son los
    // auriculares — el aggregate queda atado al UID de la salida de ese momento y el formato del tap se lee
    // una sola vez —, pero vale para cualquier muerte del HAL.
    if (gate.state() == SystemAudioPermissionGate::State::granted)
    {
        if (st == SystemCaptureStatus::capturing) { restartTicks = 0; return; }

        // El usuario apagó el permiso en Ajustes con la app abierta.
        if (st == SystemCaptureStatus::permissionDenied) { gate.captureDenied(); sysAudio->stop(); return; }

        // Rearme CALLADO: TCC ya contestó, así que reintentar no levanta ningún cartel. Con throttle, para
        // no martillar el HAL si la salida se fue del todo. El start() relee el formato del tap nuevo.
        if ((++restartTicks % kRestartTicks) != 0) return;
        prepareProcessor (48000.0, kPrepBlock);
        startSystemCapture();
        return;
    }

    if (gate.state() != SystemAudioPermissionGate::State::denied) return;

    // Denegado: reintento LENTO (~2.5s). No levanta cartel — TCC ya tiene respuesta guardada —, así que si
    // el usuario prende el permiso en Ajustes el aviso se va solo, sin tener que tocar REOPEN.
    if ((++retryTicks % kPollRetryTicks) != 0) return;
    gate.authorizationSeen();
    prepareProcessor (48000.0, kPrepBlock);
    startSystemCapture();
}

void AppAudioEngine::useInputDevice (const juce::String& deviceName)
{
    stopEverything();
    model_.selectInputDevice (deviceName.isNotEmpty() ? deviceName : model_.deviceName());

    // Abrir/seleccionar el device pedido (stereo in/out; el default si el nombre está vacío).
    if (deviceName.isNotEmpty())
    {
        auto setup = devMgr.getAudioDeviceSetup();
        setup.inputDeviceName = deviceName;
        setup.useDefaultInputChannels = true;
        setup.useDefaultOutputChannels = true;
        devMgr.setAudioDeviceSetup (setup, true);
    }
    else if (devMgr.getCurrentAudioDevice() == nullptr)
    {
        devMgr.initialise (2, 2, nullptr, true);
    }

    devMgr.addAudioCallback (this);   // audioDeviceAboutToStart prepara el processor
    deviceCallbackAdded = true;
}

void AppAudioEngine::stopEverything()
{
    sysAudio->stop();
    if (deviceCallbackAdded)
    {
        devMgr.removeAudioCallback (this);   // bloquea hasta que el callback del device termine
        deviceCallbackAdded = false;
    }
    const juce::ScopedLock sl (processLock);  // espera cualquier pushAudio de la cola SCK en vuelo
    juce::ignoreUnused (sl);
}

// ---------------------------------------------------------------------------- MIDI / gain
void AppAudioEngine::setMidiInput (const juce::String& idOrName)
{
    if (midiIn) { midiIn->stop(); midiIn = nullptr; }
    model_.setMidiInput (idOrName);
    if (idOrName.isEmpty()) return;

    for (const auto& d : juce::MidiInput::getAvailableDevices())
    {
        if (d.name == idOrName || d.identifier == idOrName)
        {
            midiIn = juce::MidiInput::openDevice (d.identifier, &midiCollector);
            if (midiIn) midiIn->start();
            break;
        }
    }
}

// ---------------------------------------------------------------------------- sink común
void AppAudioEngine::prepareProcessor (double sr, int block)
{
    if (juce::exactlyEqual (preparedSr.load(), sr) && preparedBlock == block) return;
    proc.prepareToPlay (sr, block);
    preparedSr.store (sr);
    preparedBlock = block;
}

void AppAudioEngine::pushAudio (const float* const* chans, int numCh, int numSamples, double sr, bool fromRealSource)
{
    if (fromRealSource)
        lastRealAudioMs.store (juce::Time::getMillisecondCounterHiRes());   // marca: hay fuente viva

    const juce::ScopedTryLock sl (processLock);
    if (! sl.isLocked()) return;              // durante un switch: dropeá este bloque

    const int ch = juce::jlimit (1, 2, numCh);

    // Procesar en tajadas de <= preparedBlock (monoScratch del processor está dimensionado a ese block).
    for (int off = 0; off < numSamples; off += kPrepBlock)
    {
        const int n = juce::jmin (kPrepBlock, numSamples - off);
        scratch.clear();
        for (int c = 0; c < ch; ++c)
            if (chans[c] != nullptr)
                scratch.copyFrom (c, 0, chans[c] + off, n);

        // Vista de n samples sobre el scratch preasignado (sin realocar).
        juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(), scratch.getNumChannels(), n);
        const float pk = bufferPeak (block);
        meter.store (pk);

        // Prueba de vida para el smoke: una línea, la primera vez que entra audio REAL con señal.
        if (fromRealSource && ! loggedLive && pk > 0.0f)
        {
            loggedLive = true;
            std::fprintf (stderr, "[supernova] live: system-audio %s %dch @%.0fHz rms>0 (peak %.4f)\n",
                          sysAudio->backend() == SystemAudioBackend::processTap ? "process-tap" : "screencapturekit",
                          numCh, sr, (double) pk);
        }

        midiScratch.clear();
        midiCollector.removeNextBlockOfMessages (midiScratch, n);
        proc.processBlock (block, midiScratch);
    }
}

// ---------------------------------------------------------------------------- heartbeat (fix 2)
void AppAudioEngine::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    // ¿Llegó audio real hace poco? Entonces la fuente ya drivea processBlock → no dupliques el reloj.
    if (now - lastRealAudioMs.load() < kAudioGapMs) { lastHeartbeatMs = now; return; }

    double dtMs = now - lastHeartbeatMs;
    lastHeartbeatMs = now;
    if (dtMs <= 0.0) return;
    dtMs = juce::jmin (dtMs, kMaxSilenceMs);   // acota el bloque (evita saltos si el timer se atrasó)

    const double sr = preparedSr.load() > 0.0 ? preparedSr.load() : 48000.0;
    const int n = juce::jlimit (1, kPrepBlock, (int) std::llround (dtMs * 0.001 * sr));

    // Empujá SILENCIO del tamaño del tiempo real transcurrido → processAudio avanza el BeatClock (blockDt =
    // n/sr) y consume el tap. Canales nulos → pushAudio deja el scratch en cero (silencio). El medidor cae a 0.
    static const float* const kSilent[2] = { nullptr, nullptr };
    pushAudio (kSilent, 2, n, sr, /*fromRealSource=*/false);
}

// ---------------------------------------------------------------------------- AudioIODeviceCallback
void AppAudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 48000.0;
    prepareProcessor (sr, kPrepBlock);
    midiCollector.reset (sr);
}

void AppAudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn,
                                                       float* const* out, int numOut, int numSamples,
                                                       const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)                 // visualizador: no sale audio
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);

    if (numIn > 0)
        pushAudio (in, numIn, numSamples, preparedSr.load());
}

void AppAudioEngine::audioDeviceStopped() {}

// ---------------------------------------------------------------------------- persistencia
void AppAudioEngine::save()
{
    settings.setValue (kSettingSource, model_.toValueTree().toXmlString());
    if (auto xml = devMgr.createStateXml())
        settings.setValue (kSettingDevice, xml->toString());
    settings.saveIfNeeded();
}

void AppAudioEngine::load()
{
    if (auto xml = juce::parseXML (settings.getValue (kSettingSource)))
        model_.fromValueTree (juce::ValueTree::fromXml (*xml));

    std::unique_ptr<juce::XmlElement> devXml;
    if (auto x = juce::parseXML (settings.getValue (kSettingDevice)))
        devXml = std::move (x);
    // initialise con el estado guardado (o default) — sólo si vamos a device; SCK no lo necesita.
    if (model_.kind() == AudioSourceModel::Kind::inputDevice)
        devMgr.initialise (2, 2, devXml.get(), true);
}

} // namespace supernova
