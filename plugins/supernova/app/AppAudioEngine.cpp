#include "AppAudioEngine.h"
#include "AudioConvert.h"

namespace supernova {

namespace {
constexpr const char* kSettingSource = "audioSource";      // XML del AudioSourceModel
constexpr const char* kSettingDevice = "audioDeviceState"; // XML del AudioDeviceManager
}

AppAudioEngine::AppAudioEngine (SupernovaProcessor& processor, juce::PropertiesFile& s)
    : proc (processor), settings (s)
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

    sysAudio.start (48000, 2, [this] (const float* const* ch, int nc, int nf, double sr)
    {
        pushAudio (ch, nc, nf, sr);
    });
}

void AppAudioEngine::retrySystemAudio()
{
    // Solo si estamos en System Audio y NO capturando (típicamente permiso recién concedido en Ajustes).
    if (model_.kind() != AudioSourceModel::Kind::systemAudio) return;
    const auto st = sysAudio.status();
    if (st == SystemAudioSource::Status::capturing) return;

    prepareProcessor (48000.0, kPrepBlock);   // idempotente
    sysAudio.start (48000, 2, [this] (const float* const* ch, int nc, int nf, double sr)
    {
        pushAudio (ch, nc, nf, sr);
    });
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
    sysAudio.stop();
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

    juce::ignoreUnused (sr);
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
        meter.store (bufferPeak (block));

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
