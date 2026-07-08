#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace halo
{

namespace pid = halo::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout HaloProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // Las 6 macros del shimmer espacial como % (0..100). El processor las pasa a 0..1 → HaloParams.
    // Defaults (base técnica §7/§9, preset "VASTEDAD"): wet ~40 %, espacio medio-grande, cola larga,
    // shimmer presente, TONE medio (≈7 kHz), órbita media.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 40.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SIZE, 1 }, "Size",
        Range { 0.f, 100.f, 0.01f }, 65.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DECAY, 1 }, "Decay",
        Range { 0.f, 100.f, 0.01f }, 75.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SHIMMER, 1 }, "Shimmer",
        Range { 0.f, 100.f, 0.01f }, 55.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::TONE, 1 }, "Tone",
        Range { 0.f, 100.f, 0.01f }, 45.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ORBIT, 1 }, "Orbit",
        Range { 0.f, 100.f, 0.01f }, 40.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // LOW CUT (high-pass de la SALIDA del plugin, dry+wet): default 0 % = apagado (20 Hz → transparente, no
    // cambia el sonido ni los presets). Sube hasta 100 % = 500 Hz → corta los graves de TODO lo que sale del
    // plugin (se oye a cualquier MIX; filtrar solo el wet era imperceptible a MIX bajo, el dry lo tapaba). El
    // mapeo log 0..1 → Hz vive en ovni::dsp::LowCut.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::LOWCUT, 1 }, "Low Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // HI CUT (low-pass de la SALIDA del plugin, dry+wet): el PAR del LOW CUT. Default 0 % = apagado (20 kHz →
    // transparente, no cambia el sonido ni los presets). Sube hasta 100 % = 1.5 kHz → oscurece TODO lo que sale
    // del plugin cortando los agudos (se oye a cualquier MIX). El mapeo log 0..1 → Hz (invertido) vive en ovni::dsp::HiCut.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HICUT, 1 }, "Hi Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // FREEZE (base técnica §6): captura la nube y permite tocar encima. Default off.
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::FREEZE, 1 }, "Freeze", false));

    // SYNC de la ÓRBITA (control CORE del sello — va AL FINAL, append-only → no rompe recall). orbitSync:
    // órbita libre (0.08 Hz orgánico) vs enganchada al tempo del host. orbitDiv: compases por vuelta (las
    // etiquetas salen de la MISMA tabla sync::divs → una sola fuente de verdad con el processor y los tests).
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::ORBITSYNC, 1 }, "Orbit Sync", false));

    juce::StringArray divNames;
    for (int i = 0; i < params::sync::kCount; ++i) divNames.add (params::sync::divs[i].name);
    layout.add (std::make_unique<APC> (juce::ParameterID { pid::ORBITDIV, 1 }, "Orbit Div",
                                       divNames, params::sync::kDefaultIndex));

    // RATE de la órbita en FREE: velocidad ELEGIBLE por el usuario. Joaquín pidió que llegue MUCHO más lento y
    // que el knob SE SIENTA (el intento anterior con skew power-law dejaba el CUARTO INFERIOR del knob PLANO
    // —no cambiaba nada— por eso "no cambia nada"). Fix: mapeo LOGARÍTMICO (equal-ratio por paso → percepción
    // pareja de la velocidad, como un knob de frecuencia; CERO zonas muertas). Rango 0.004–0.5 Hz = 250 s … 2 s
    // por vuelta. Se muestra como SEGUNDOS POR VUELTA (intuitivo, consistente con los compases del SYNC).
    // Default glacial (0.01 Hz ≈ 100 s/vuelta, ~19 % del recorrido). Va AL FINAL (append-only → no rompe recall).
    juce::NormalisableRange<float> orbitHz {
        0.004f, 0.5f,
        [] (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); },              // 0..1 → Hz (log)
        [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },  // Hz → 0..1
        [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ORBITRATE, 1 }, "Orbit Rate",
        orbitHz, params::kOrbitFreeDefaultHz,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float hz, int) {
                const float secs = (hz > 1.0e-4f) ? 1.0f / hz : 9999.0f;   // segundos por vuelta de la órbita
                return secs >= 10.0f ? juce::String (juce::roundToInt (secs)) + " s"
                                     : juce::String (secs, 1) + " s"; })));
    return layout;
}

// ----------------------------------------------------------------------------- ctor
HaloProcessor::HaloProcessor()
    : ovni::PluginProcessorBase ("HALO", createParameterLayout())
{
    pMix       = apvts.getRawParameterValue (pid::MIX);
    pSize      = apvts.getRawParameterValue (pid::SIZE);
    pDecay     = apvts.getRawParameterValue (pid::DECAY);
    pShimmer   = apvts.getRawParameterValue (pid::SHIMMER);
    pTone      = apvts.getRawParameterValue (pid::TONE);
    pOrbit     = apvts.getRawParameterValue (pid::ORBIT);
    pLowCut    = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut     = apvts.getRawParameterValue (pid::HICUT);
    pFreeze    = apvts.getRawParameterValue (pid::FREEZE);
    pOrbitSync = apvts.getRawParameterValue (pid::ORBITSYNC);
    pOrbitDiv  = apvts.getRawParameterValue (pid::ORBITDIV);
    pOrbitRate = apvts.getRawParameterValue (pid::ORBITRATE);
}

juce::AudioProcessorEditor* HaloProcessor::createEditor()
{
    return new HaloEditor (*this);
}

void HaloProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    engine.prepare (spec);
    // El motor introduce latencia DENTRO del lazo (medio grano del granular + el OS). En un FX de cola es
    // parte del carácter; el dry es nulo → NO reportamos PDC del dry (latencySamples() = 0). Ver base técnica.
}

void HaloProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // --- macros (RT-safe); % -> 0..1 ------------------------------------------------
    const float mix01     = juce::jlimit (0.f, 1.f, (pMix     ? pMix->load()     : 40.f) * 0.01f);
    const float size01    = juce::jlimit (0.f, 1.f, (pSize    ? pSize->load()    : 65.f) * 0.01f);
    const float decay01   = juce::jlimit (0.f, 1.f, (pDecay   ? pDecay->load()   : 75.f) * 0.01f);
    const float shimmer01 = juce::jlimit (0.f, 1.f, (pShimmer ? pShimmer->load() : 55.f) * 0.01f);
    const float tone01    = juce::jlimit (0.f, 1.f, (pTone    ? pTone->load()    : 45.f) * 0.01f);
    const float orbit01   = juce::jlimit (0.f, 1.f, (pOrbit   ? pOrbit->load()   : 40.f) * 0.01f);
    const float lowCut01  = juce::jlimit (0.f, 1.f, (pLowCut  ? pLowCut->load()  : 0.f)  * 0.01f);
    const float hiCut01   = juce::jlimit (0.f, 1.f, (pHiCut   ? pHiCut->load()   : 0.f)  * 0.01f);
    const bool  freeze    = (pFreeze && pFreeze->load() >= 0.5f);

    // --- transporte del host (conduce la trayectoria orbital + el SYNC). ------------
    ovni::engines::TransportInfo transport;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())          bpm = *b;
            transport.isPlaying   = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition())  transport.ppqPosition = *q;
        }
    transport.bpm = bpm;

    // --- RATE de la órbita (control CORE del sello): FREE → la perilla RATE elige la velocidad; SYNC →
    // enganchada al tempo (BPM·división). Una sola fuente de verdad (computeOrbitRateHz), compartida con el
    // getter de diagnóstico de los tests. SIEMPRE > 0 → el motor lo usa como su freeHz orbital.
    const float orbitRateHz = computeOrbitRateHz (bpm);

    // --- motor SHIMMER espacial glacial ---------------------------------------------
    engine.process (buffer, HaloParams {
        .shimmer01   = shimmer01,
        .decay01     = decay01,
        .size01      = size01,
        .tone01      = tone01,
        .orbit01     = orbit01,
        .lowCut01    = lowCut01,
        .hiCut01     = hiCut01,
        .mix01       = mix01,
        .freeze      = freeze,
        .monoSafe    = isMonoSafe(),     // IN PHASE → colapsa el wet a mono-compatible antes del mix
        .orbitRateHz = orbitRateHz },    // velocidad orbital efectiva (Hz): FREE=perilla RATE, SYNC=división
        transport);

    // --- telemetría del visual (lock-free) ------------------------------------------
    uiShimmer.store (shimmer01, std::memory_order_relaxed);
    uiDecay.store   (decay01,   std::memory_order_relaxed);
    uiSize.store    (size01,    std::memory_order_relaxed);
    uiTone.store    (tone01,    std::memory_order_relaxed);
    uiOrbit.store   (orbit01,   std::memory_order_relaxed);
    uiMix.store     (mix01,     std::memory_order_relaxed);
    uiFreeze.store  (freeze ? 1.0f : 0.0f, std::memory_order_relaxed);
    uiLoopRms.store (juce::jlimit (0.0f, 1.0f, engine.loopEnergy()), std::memory_order_relaxed);
    // Velocidad normalizada (0..1) de la órbita/RATE para el visualizador → con qué rapidez nacen/suben/orbitan
    // los anillos. Mapeo LOG del rate efectivo (mismo rango que la perilla: 0.004–0.5 Hz). Así el knob RATE
    // (FREE) y la división (SYNC) controlan la velocidad VISIBLE de los circulitos (no solo el pan de audio).
    {
        const float hz = juce::jlimit (0.004f, 0.5f, orbitRateHz);
        const float rn = (std::log (hz) - std::log (0.004f)) / (std::log (0.5f) - std::log (0.004f));
        uiRateNorm.store (juce::jlimit (0.0f, 1.0f, rn), std::memory_order_relaxed);
    }
}

float HaloProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = conteniendo. Lo mapeamos a 0..1 (empuje del LED).
    return juce::jlimit (0.0f, 1.0f, 1.0f - engine.lastLimiterGain());
}

float HaloProcessor::computeOrbitRateHz (double bpm) const
{
    // SYNC → la división manda (BPM·división). FREE → la velocidad la elige la perilla RATE (orbitRate).
    if (pOrbitSync && pOrbitSync->load() >= 0.5f)
    {
        const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                         (int) std::round (pOrbitDiv ? pOrbitDiv->load() : (float) params::sync::kDefaultIndex));
        return params::sync::orbitRateHz (bpm, divIdx);
    }
    return pOrbitRate ? pOrbitRate->load() : params::kOrbitFreeDefaultHz;
}

float HaloProcessor::debugOrbitRateHz (double bpm) const
{
    const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                     (int) std::round (pOrbitDiv ? pOrbitDiv->load() : (float) params::sync::kDefaultIndex));
    return params::sync::orbitRateHz (bpm, divIdx);
}

} // namespace halo

// ===================================================================== entry point de JUCE
// Los wrappers de cada formato (AU / VST3 / Standalone) referencian este símbolo.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new halo::HaloProcessor();
}
