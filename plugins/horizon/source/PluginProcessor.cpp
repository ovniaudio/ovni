#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace horizon
{

namespace pid = horizon::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

namespace
{
    // Micro-fade del bypass compensado (anti-click-clip-truepeak: "micro-fades en
    // conmutaciones"). Cruce LINEAL: wet y dry-retrasado están ALINEADOS en el tiempo.
    constexpr float kBypassFadeMs = 12.0f;
}

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout HorizonProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // FREEZE — el gesto central (latch, bool AUTOMATABLE). Default OFF: carga transparente;
    // apretás FREEZE y el instante queda suspendido (curaduría: Freeze off).
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::FREEZE, 1 }, "Freeze", false));

    // WHISPER — coherencia↔randomización de fase. Default 12 (curaduría ES LEY: vivo pero
    // cristalino; la curva perceptual del motor garantiza que 12 se oye vidrioso, no ruidoso).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::WHISPER, 1 }, "Whisper",
        Range { 0.f, 100.f, 0.01f }, 12.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // SPREAD — des-correlación L/R + paneo de potencia constante. Default 50 (curaduría).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SPREAD, 1 }, "Spread",
        Range { 0.f, 100.f, 0.01f }, 50.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // DUCK — el freeze se aparta cuando pega el dry. Default 0 (off — ADN musical opt-in).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DUCK, 1 }, "Duck",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // MIX — dry/wet (ley de potencia; el dry viaja retrasado N para alinear). Default 100
    // (curaduría: carga transparente — wet = identity hasta que congelás).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 100.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // SYNC del re-trigger (control CORE del sello). rateSync: latido libre / enganchado al
    // tempo. rateDivision: beats por ciclo (etiquetas de la MISMA tabla sync::divs → una
    // sola fuente de verdad con el processor y los tests). Subset HORIZON 1/16…2 bar.
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::RATESYNC, 1 }, "Rate Sync", false));

    juce::StringArray divNames;
    for (int i = 0; i < params::sync::kCount; ++i) divNames.add (params::sync::divs[i].name);
    layout.add (std::make_unique<APC> (juce::ParameterID { pid::RATEDIV, 1 }, "Rate Div",
                                       divNames, params::sync::kDefaultIndex));

    // RATE del re-trigger en FREE (Hz). 0 = OFF (frame sostenido = pad clásico); 0–8 Hz
    // (cota dura de groove). Rango LINEAL con 0 incluido (default 0 = sostenido). Display:
    // "OFF" en 0, "X.X Hz" arriba. La cota se aplica en el motor (física cableada).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::RATE, 1 }, "Rate",
        Range { params::kRateMinHz, params::kRateMaxHz, 0.001f }, params::kRateFreeDefaultHz,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float hz, int) {
                return hz < 1.0e-3f ? juce::String ("OFF")
                     : hz >= 1.0f   ? juce::String (hz, 1) + " Hz"
                                    : juce::String (hz, 2) + " Hz"; })
            .withLabel ("Hz")));

    // LOW CUT + HI CUT del PLUGIN (AL FINAL → append-only, no rompe el recall de los presets ya shippeados).
    // High-pass + low-pass sobre la SALIDA COMPLETA (dry+wet ya mezclados): el corte se OYE a CUALQUIER MIX.
    // Default 0 % = off (20 Hz / 20 kHz) → transparente. El processor mapea %→0..1 y de ahí a Hz con
    // ovni::dsp::LowCut::hzFor01 (20..500 Hz) / HiCut::hzFor01 (20 kHz..1.5 kHz, invertida: más perilla = más oscuro).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::LOWCUT, 1 }, "Low Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HICUT, 1 }, "Hi Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    return layout;
}

// ----------------------------------------------------------------------------- ctor
HorizonProcessor::HorizonProcessor()
    : ovni::PluginProcessorBase ("HORIZON", createParameterLayout())
{
    pFreeze   = apvts.getRawParameterValue (pid::FREEZE);
    pWhisper  = apvts.getRawParameterValue (pid::WHISPER);
    pSpread   = apvts.getRawParameterValue (pid::SPREAD);
    pDuck     = apvts.getRawParameterValue (pid::DUCK);
    pMix      = apvts.getRawParameterValue (pid::MIX);
    pRateSync = apvts.getRawParameterValue (pid::RATESYNC);
    pRateDiv  = apvts.getRawParameterValue (pid::RATEDIV);
    pRate     = apvts.getRawParameterValue (pid::RATE);
    pLowCut   = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut    = apvts.getRawParameterValue (pid::HICUT);

    // Bypass compensado: el mismo bool que lee el power del chasis + los gains de utilidad.
    pBypass      = apvts.getRawParameterValue (pid::BYPASS);
    pInGainUtil  = apvts.getRawParameterValue ("inGain");
    pOutputUtil  = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorEditor* HorizonProcessor::createEditor()
{
    return new HorizonEditor (*this);
}

void HorizonProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    // LOW CUT + HI CUT del PLUGIN: filtros TPT estéreo que esculpen la SALIDA COMPLETA (dry+wet) DESPUÉS
    // del motor (ver processAudio). Se preparan al peor bloque (RT-safe, sin alloc en process).
    lowCut.prepare (spec);
    hiCut.prepare (spec);

    engine.prepare (spec);

    // LATENCIA HONESTA (gate [latency]): el pipeline OLA retarda N samples EXACTOS
    // (2048 @44.1/48k; 4096 @96k). Se declara al host Y el dry viaja retrasado ese mismo
    // N dentro del motor → reportada == real y el MIX cruza señales alineadas.
    setLatencySamples (engine.latencySamples());

    // Bypass compensado: ring del dry crudo (lat + maxBlock) + scratches + micro-fade.
    const int maxBlock = (int) spec.maximumBlockSize;
    bypassRingSz = engine.latencySamples() + maxBlock;
    for (auto& ring : bypassRing) ring.assign ((size_t) bypassRingSz, 0.0f);
    bypassWrite = 0;
    bypassDly.setSize (2, maxBlock);
    bypassWet.setSize (2, maxBlock);
    bypassDly.clear();
    bypassWet.clear();
    bypassXfStep = 1.0f / juce::jmax (1.0f, (float) (kBypassFadeMs * 0.001f * spec.sampleRate));
    bypassXf = (pBypass != nullptr && pBypass->load() >= 0.5f) ? 1.0f : 0.0f;
}

// ------------------------------------------------------------------- bypass compensado
void HorizonProcessor::feedBypassRing (const juce::AudioBuffer<float>& in, int n,
                                       juce::AudioBuffer<float>* dly) noexcept
{
    const int numIn = juce::jmax (1, getTotalNumInputChannels());
    const float* in0 = in.getReadPointer (0);
    const float* in1 = in.getReadPointer (numIn > 1 && in.getNumChannels() > 1 ? 1 : 0);
    float* r0 = bypassRing[0].data();
    float* r1 = bypassRing[1].data();
    float* d0 = dly != nullptr ? dly->getWritePointer (0) : nullptr;
    float* d1 = dly != nullptr ? dly->getWritePointer (1) : nullptr;
    const int delay = getLatencySamples();
    int w = bypassWrite;
    for (int i = 0; i < n; ++i)
    {
        r0[w] = in0[i];
        r1[w] = in1[i];   // input mono: duplicado a R (idéntico al pass-through del chasis)
        if (d0 != nullptr)
        {
            int r = w - delay; if (r < 0) r += bypassRingSz;
            d0[i] = r0[r];
            d1[i] = r1[r];
        }
        if (++w == bypassRingSz) w = 0;
    }
    bypassWrite = w;
}

void HorizonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const bool wantBypass = (pBypass != nullptr && pBypass->load() >= 0.5f);

    if (bypassRingSz <= 0 || n > bypassDly.getNumSamples())
    {
        ovni::PluginProcessorBase::processBlock (buffer, midi);
        return;
    }

    // Camino ACTIVO puro (lo normal): sólo mantener el ring del dry al día.
    if (! wantBypass && bypassXf <= 0.0f)
    {
        feedBypassRing (buffer, n, nullptr);
        ovni::PluginProcessorBase::processBlock (buffer, midi);
        return;
    }

    juce::ScopedNoDenormals noDenormals;

    // Bypass o transición: dry crudo → ring → dry RETRASADO N en bypassDly (alineado al PDC).
    feedBypassRing (buffer, n, &bypassDly);

    if (wantBypass)
    {
        ovni::PluginProcessorBase::processBlock (buffer, midi);   // chasis: PC MIDI + pass-through

        // Motor CALIENTE: el camino wet corre a mano sobre una copia (gains in/out estáticos).
        for (int ch = 0; ch < 2; ++ch)
            bypassWet.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);
        const float inG  = pInGainUtil != nullptr ? juce::Decibels::decibelsToGain (pInGainUtil->load()) : 1.0f;
        const float outG = pOutputUtil != nullptr ? juce::Decibels::decibelsToGain (pOutputUtil->load()) : 1.0f;
        bypassWet.applyGain (0, n, inG);
        {
            juce::AudioBuffer<float> wetView (bypassWet.getArrayOfWritePointers(), 2, 0, n);
            juce::MidiBuffer noMidi;
            processAudio (wetView, noMidi);
        }
        bypassWet.applyGain (0, n, outG);

        float* out0 = buffer.getWritePointer (0);
        float* out1 = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const float* w0 = bypassWet.getReadPointer (0);
        const float* w1 = bypassWet.getReadPointer (1);
        const float* dl0 = bypassDly.getReadPointer (0);
        const float* dl1 = bypassDly.getReadPointer (1);
        float xf = bypassXf;
        for (int i = 0; i < n; ++i)
        {
            xf = juce::jmin (1.0f, xf + bypassXfStep);
            out0[i] = w0[i] * (1.0f - xf) + dl0[i] * xf;
            if (out1 != nullptr) out1[i] = w1[i] * (1.0f - xf) + dl1[i] * xf;
        }
        bypassXf = xf;
        uiOutPeak.store (buffer.getMagnitude (0, n), std::memory_order_relaxed);
    }
    else
    {
        // Cola del fade-in (power recién encendido): chasis corre el activo + cruce hacia 0.
        ovni::PluginProcessorBase::processBlock (buffer, midi);

        float* out0 = buffer.getWritePointer (0);
        float* out1 = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        const float* dl0 = bypassDly.getReadPointer (0);
        const float* dl1 = bypassDly.getReadPointer (1);
        float xf = bypassXf;
        for (int i = 0; i < n; ++i)
        {
            xf = juce::jmax (0.0f, xf - bypassXfStep);
            out0[i] = out0[i] * (1.0f - xf) + dl0[i] * xf;
            if (out1 != nullptr) out1[i] = out1[i] * (1.0f - xf) + dl1[i] * xf;
        }
        bypassXf = xf;
        uiOutPeak.store (buffer.getMagnitude (0, n), std::memory_order_relaxed);
    }
}

void HorizonProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // --- macros (RT-safe); % → 0..1 / unidades del motor -----------------------------
    const bool  freezeOn = (pFreeze  ? pFreeze->load()  >= 0.5f : false);
    const float whisper01= juce::jlimit (0.f, 1.f, (pWhisper ? pWhisper->load() : 12.f)  * 0.01f);
    const float spread01 = juce::jlimit (0.f, 1.f, (pSpread  ? pSpread->load()  : 50.f)  * 0.01f);
    const float duck01   = juce::jlimit (0.f, 1.f, (pDuck    ? pDuck->load()    : 0.f)   * 0.01f);
    const float mix01    = juce::jlimit (0.f, 1.f, (pMix     ? pMix->load()     : 100.f) * 0.01f);

    // --- transporte del host (conduce el SYNC del re-trigger; fallback 120 BPM) -------
    double bpm = 120.0;
    bool   isPlaying = false;
    double ppq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())          bpm = *b;
            isPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition())  ppq = *q;
        }

    // --- RATE del re-trigger (CORE): FREE → perilla RATE; SYNC → BPM·división. Una sola
    // fuente de verdad (computeRateHz), compartida con el getter de diagnóstico.
    const float rateHz = computeRateHz (bpm);
    const bool  syncOn = (pRateSync && pRateSync->load() >= 0.5f);
    const int   divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                       (int) std::round (pRateDiv ? pRateDiv->load()
                                                                  : (float) params::sync::kDefaultIndex));

    // --- motor de freeze espectral rítmico --------------------------------------------
    engine.process (buffer, HorizonParams {
        .freeze           = freezeOn,
        .whisper01        = whisper01,
        .spread01         = spread01,
        .duck01           = duck01,
        .mix01            = mix01,
        .rateHz           = rateHz,
        .rateSync         = syncOn,
        .isPlaying        = isPlaying,
        .ppqPosition      = ppq,
        .beatsPerCycle    = params::sync::beatsForDiv (divIdx),
        .numInputChannels = getTotalNumInputChannels(),
        .monoSafe         = isMonoSafe() });   // IN PHASE → el motor anula la des-correlación del vivo

    // --- LOW CUT + HI CUT del PLUGIN sobre la SALIDA COMPLETA (dry+wet ya mezclados) -----------------------
    // Pasa-altos / pasa-bajos del plugin: cortan los graves/agudos de TODO lo que sale → el corte se OYE a
    // CUALQUIER MIX. Cadena low-cut → hi-cut. TPT modulación-safe (de-zipper interno, sin clicks). Lineales →
    // no generan alias. BYPASS POR FILTRO (lc01>0 / hc01>0): en default (0/0) NO corre NINGÚN filtro → la
    // salida es la del motor intacta (limiter-safe, idéntica a antes).
    {
        const float lc01 = juce::jlimit (0.0f, 1.0f, (pLowCut ? pLowCut->load() : 0.f) * 0.01f);
        const float hc01 = juce::jlimit (0.0f, 1.0f, (pHiCut  ? pHiCut->load()  : 0.f) * 0.01f);
        if (lc01 > 0.0f || hc01 > 0.0f)
        {
            const int chN = buffer.getNumChannels();
            float* oPtrs[2] = { buffer.getWritePointer (0),
                                (chN > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0)) };
            juce::AudioBuffer<float> oView (oPtrs, juce::jmin (chN, 2), n);
            if (lc01 > 0.0f) { lowCut.setCutoffHz (ovni::dsp::LowCut::hzFor01 (lc01)); lowCut.process (oView); }
            if (hc01 > 0.0f) { hiCut .setCutoffHz (ovni::dsp::HiCut ::hzFor01 (hc01)); hiCut .process (oView); }
        }
    }

    // --- telemetría del visual (lock-free) --------------------------------------------
    uiWhisper.store   (whisper01, std::memory_order_relaxed);
    uiSpread.store    (spread01,  std::memory_order_relaxed);
    uiDuck.store      (duck01,    std::memory_order_relaxed);
    uiMix.store       (mix01,     std::memory_order_relaxed);
    uiFreeze.store    (engine.isFrozen() ? 1.0f : 0.0f, std::memory_order_relaxed);
    uiGateAmp.store   (juce::jlimit (0.0f, 1.0f, engine.gateAmplitude()), std::memory_order_relaxed);
    uiGatePhase.store (juce::jlimit (0.0f, 1.0f, engine.gatePhase()),     std::memory_order_relaxed);
    uiWetEnergy.store (juce::jlimit (0.0f, 1.0f, engine.wetEnergy()),     std::memory_order_relaxed);
    uiDuckEnv.store   (juce::jlimit (0.0f, 1.0f, engine.duckEnvelope()),  std::memory_order_relaxed);
    // Velocidad normalizada del latido (mapeo lineal del rate sobre 0–8 Hz) → el visual
    // late a la velocidad REAL (FREE o SYNC).
    uiRateNorm.store  (juce::jlimit (0.0f, 1.0f, rateHz / params::kRateMaxHz), std::memory_order_relaxed);
    {
        const auto& sp = engine.frozenSpectrum();
        for (int b = 0; b < kVizBands; ++b)
            uiSpectrum[(size_t) b].store (sp[(size_t) b], std::memory_order_relaxed);
    }
}

float HorizonProcessor::extraClipPush() const
{
    return juce::jlimit (0.0f, 1.0f, 1.0f - engine.lastLimiterGain());
}

float HorizonProcessor::computeRateHz (double bpm) const
{
    // SYNC → la división manda (BPM·beats por ciclo). FREE → la perilla RATE (0 = sostenido).
    // La COTA DURA (8 Hz) la aplica el motor (física cableada, no opción).
    if (pRateSync && pRateSync->load() >= 0.5f)
    {
        const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                         (int) std::round (pRateDiv ? pRateDiv->load()
                                                                    : (float) params::sync::kDefaultIndex));
        return params::sync::syncRateHz (bpm, divIdx);
    }
    return pRate ? pRate->load() : params::kRateFreeDefaultHz;
}

float HorizonProcessor::debugSyncRateHz (double bpm) const
{
    const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                     (int) std::round (pRateDiv ? pRateDiv->load()
                                                                : (float) params::sync::kDefaultIndex));
    return params::sync::syncRateHz (bpm, divIdx);
}

} // namespace horizon

// ===================================================================== entry point de JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new horizon::HorizonProcessor();
}
