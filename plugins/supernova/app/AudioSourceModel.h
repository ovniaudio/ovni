#pragma once
// Estado PURO de selección de fuente de audio de la app (sin Cocoa, sin GPU, sin JUCE audio I/O).
// AppAudioEngine decide con esto qué camino arma. Recuerda el device aunque estés en System Audio,
// así volver a la interfaz es un solo click.
#include <juce_data_structures/juce_data_structures.h>

namespace supernova {

class AudioSourceModel
{
public:
    enum class Kind { systemAudio, inputDevice };

    Kind kind() const noexcept                      { return kind_; }
    const juce::String& deviceName() const noexcept { return deviceName_; }
    const juce::String& midiInput()  const noexcept { return midiInput_; }

    void selectSystemAudio() noexcept               { kind_ = Kind::systemAudio; }
    void selectInputDevice (juce::String name)      { kind_ = Kind::inputDevice; deviceName_ = std::move (name); }
    void setMidiInput (juce::String name)           { midiInput_ = std::move (name); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("appAudio");
        t.setProperty ("kind",   kind_ == Kind::inputDevice ? "device" : "system", nullptr);
        t.setProperty ("device", deviceName_, nullptr);
        t.setProperty ("midi",   midiInput_, nullptr);
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        if (! t.isValid()) return;
        kind_       = (t.getProperty ("kind").toString() == "device") ? Kind::inputDevice : Kind::systemAudio;
        deviceName_ = t.getProperty ("device").toString();
        midiInput_  = t.getProperty ("midi").toString();
    }

private:
    Kind kind_ = Kind::systemAudio;
    juce::String deviceName_, midiInput_;
};

} // namespace supernova
