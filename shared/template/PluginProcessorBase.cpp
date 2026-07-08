#include "template/PluginProcessorBase.h"
#include <cmath>

namespace ovni
{
juce::AudioProcessor::BusesProperties PluginProcessorBase::stereoBuses()
{
    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

// Ctor público: delega al privado inyectando los params de utilidad del sello en el layout.
PluginProcessorBase::PluginProcessorBase (juce::String pn,
                                          juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                          BusesProperties buses)
    : PluginProcessorBase (PrivateTag{}, std::move (pn), withUtilityParams (std::move (layout)), buses)
{
}

PluginProcessorBase::PluginProcessorBase (PrivateTag, juce::String pn,
                                          juce::AudioProcessorValueTreeState::ParameterLayout layout,
                                          BusesProperties buses)
    : juce::AudioProcessor (buses),
      apvts (*this, nullptr, "PARAMETERS", std::move (layout)),
      pluginName (std::move (pn)),
      presetManager (apvts, pluginName),
      abState (apvts)
{
    // Punteros atómicos cacheados (RT-safe; nada de lookup por string en el audio thread).
    pInGain   = apvts.getRawParameterValue ("inGain");
    pOutput   = apvts.getRawParameterValue ("output");
    pMonoSafe = apvts.getRawParameterValue ("monoSafe");
}

// Inyecta los params de utilidad del sello — TODOS los plugins los heredan. IDs/rangos = ÓRBITA:
// inGain/output en dB [-24,+24] def 0; monoSafe bool def off. (bypass lo declara cada plugin.)
juce::AudioProcessorValueTreeState::ParameterLayout
PluginProcessorBase::withUtilityParams (juce::AudioProcessorValueTreeState::ParameterLayout layout)
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    const auto db = [] { return juce::AudioParameterFloatAttributes().withLabel ("dB"); };
    layout.add (std::make_unique<APF> (juce::ParameterID { "inGain", 1 }, "In",
                                       juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db()));
    layout.add (std::make_unique<APF> (juce::ParameterID { "output", 1 }, "Out",
                                       juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, db()));
    layout.add (std::make_unique<APB> (juce::ParameterID { "monoSafe", 1 }, "In Phase", false));
    return layout;
}

bool PluginProcessorBase::acceptsMidi() const
{
    // Necesitamos MIDI input para interceptar Program Change. El CMake del plugin debe setear
    // NEEDS_MIDI_INPUT TRUE (-> JucePlugin_WantsMidiInput=1). Ver CMakeLists.txt.in.
   #if defined (JucePlugin_WantsMidiInput) && JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool PluginProcessorBase::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Default del sello (FX espacial): salida SIEMPRE estéreo; entrada mono o estéreo (se suma a mono
    // aguas abajo si el plugin lo necesita). Sobreescribí este método si tu plugin difiere.
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return true;
}

void PluginProcessorBase::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels      = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());

    // Gain staging del chasis: arrancar el suavizado en el valor actual (sin salto en el 1er bloque).
    prevInGain  = pInGain ? juce::Decibels::decibelsToGain (pInGain->load()) : 1.0f;
    prevOutGain = pOutput ? juce::Decibels::decibelsToGain (pOutput->load()) : 1.0f;
    // IN PHASE (bass-mono): one-pole LP a ~120 Hz. coef = 1 - e^(-2π·fc/sr).
    bassMonoCoef = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * 120.0f
                                    / (float) juce::jmax (1.0, sampleRate));
    bassMonoL = bassMonoR = 0.0f;

    prepareEngine (spec);   // PLUGIN
}

void PluginProcessorBase::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Program Change MIDI -> cargar preset de fábrica. Se difiere al message thread (RT-safe): NO tocamos
    // el APVTS desde el audio thread. Acotamos al tamaño de la tabla del plugin.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isProgramChange())
        {
            const int prog = m.getProgramChangeNumber();
            if (prog >= 0 && prog < (int) presets::factoryPresets().size())
            {
                pendingProgram.store (prog, std::memory_order_relaxed);
                triggerAsyncUpdate();
            }
        }
    }

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Limpia canales de salida sin entrada (evita basura antes de escribirlos).
    for (int i = numIn; i < numOut; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Bypass: pass-through (la salida es estéreo; si entró mono, duplicar a R).
    if (auto* bp = apvts.getRawParameterValue (bypassParamID()))
        if (bp->load() >= 0.5f)
        {
            if (numOut > 1 && numIn == 1)
                buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
            uiOutPeak.store (buffer.getMagnitude (0, buffer.getNumSamples()), std::memory_order_relaxed);
            uiClip.store (0.0f, std::memory_order_relaxed);
            return;
        }

    // ===== IN gain (drive) — rampeado por-bloque (anti-zipper); sólo en el camino activo. =====
    {
        const float inG = pInGain ? juce::Decibels::decibelsToGain (pInGain->load()) : 1.0f;
        buffer.applyGainRamp (0, buffer.getNumSamples(), prevInGain, inG);
        prevInGain = inG;
    }

    // ===== PLUGIN: tu DSP =====
    processAudio (buffer, midi);

    // ===== IN PHASE (Mono Safe) — bass-mono: suma los graves a mono (fase coherente en lo bajo /
    // compatibilidad mono). Genérico del chasis: vale para cualquier motor del catálogo. =====
    if (numOut >= 2 && pMonoSafe && pMonoSafe->load() >= 0.5f)
    {
        auto* dataL = buffer.getWritePointer (0);
        auto* dataR = buffer.getWritePointer (1);
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            bassMonoL += bassMonoCoef * (dataL[i] - bassMonoL);
            bassMonoR += bassMonoCoef * (dataR[i] - bassMonoR);
            const float monoLow = 0.5f * (bassMonoL + bassMonoR);
            dataL[i] = (dataL[i] - bassMonoL) + monoLow;   // high (stereo) + low (mono)
            dataR[i] = (dataR[i] - bassMonoR) + monoLow;
        }
    }

    // ===== OUT gain — rampeado por-bloque (anti-zipper). =====
    {
        const float outG = pOutput ? juce::Decibels::decibelsToGain (pOutput->load()) : 1.0f;
        buffer.applyGainRamp (0, buffer.getNumSamples(), prevOutGain, outG);
        prevOutGain = outG;
    }

    // Estado para el meter / LED de clip (POST out-gain). El empuje del LED toma el MAYOR de: (a) pico
    // de salida cerca/encima de 0 dBFS, y (b) lo que aporte el plugin (extraClipPush, p.ej. su limiter).
    const float outPk = buffer.getMagnitude (0, buffer.getNumSamples());
    uiOutPeak.store (outPk, std::memory_order_relaxed);
    const float peakPush = juce::jlimit (0.0f, 1.0f, juce::jmap (outPk, 0.95f, 1.0f, 0.0f, 1.0f));
    uiClip.store (juce::jmax (peakPush, juce::jlimit (0.0f, 1.0f, extraClipPush())), std::memory_order_relaxed);
}

void PluginProcessorBase::handleAsyncUpdate()
{
    const int p = pendingProgram.exchange (-1, std::memory_order_relaxed);
    if (p >= 0) presetManager.applyFactory (p);   // corre en el message thread (RT-safe)
}

void PluginProcessorBase::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", 1, nullptr);   // para migraciones futuras
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessorBase::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            // const int stateVersion = (int) tree.getProperty ("stateVersion", 1); // ramificar al migrar
            apvts.replaceState (tree);
        }
}
}
