#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace nebula
{

namespace pid = nebula::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

// Umbral de freeze: por encima de este % la cola se congela (decay infinito). El motor FDN
// clampa los g_i al borde y corta la inyección de input → sostiene sin diverger.
static constexpr float kFreezeThreshold = 0.99f;   // 0..1 (≈99% del recorrido de DECAY)

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout NebulaProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // Las 5 macros del reverb FDN como % (0..100). El processor las pasa a 0..1 → FdnParams.
    // Defaults sensatos (spec): espacio medio, cola media, algo de damping, respiración suave, wet ~1/3.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SIZE, 1 }, "Size",
        Range { 0.f, 100.f, 0.01f }, 50.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DECAY, 1 }, "Decay",
        Range { 0.f, 100.f, 0.01f }, 50.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::TONE, 1 }, "Tone",
        Range { 0.f, 100.f, 0.01f }, 40.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::BREATH, 1 }, "Breath",
        Range { 0.f, 100.f, 0.01f }, 25.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 35.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // SYNC de la respiración (AL FINAL → append-only, no rompe el recall de los presets ya shippeados).
    // breathSync: respiración libre (orgánica) vs enganchada al tempo del host. breathDiv: división del
    // ciclo (las etiquetas salen de la MISMA tabla sync::divs → una sola fuente de verdad con el motor).
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BREATHSYNC, 1 }, "Breath Sync", false));

    juce::StringArray divNames;
    for (int i = 0; i < params::sync::kCount; ++i) divNames.add (params::sync::divs[i].name);
    layout.add (std::make_unique<APC> (juce::ParameterID { pid::BREATHDIV, 1 }, "Breath Div",
                                       divNames, params::sync::kDefaultIndex));

    // LOW CUT del PLUGIN (AL FINAL → append-only, no rompe el recall de los presets ya shippeados). High-pass
    // sobre la SALIDA COMPLETA (dry+wet ya mezclados): es el pasa-altos del plugin → corta los graves de TODO lo
    // que sale, no sólo el wet, así el corte se OYE a CUALQUIER MIX (a MIX bajo el dry full-range ya no lo tapa).
    // Default 0 % = 20 Hz = apagado → transparente. El processor mapea %→0..1 y de ahí a Hz con
    // ovni::dsp::LowCut::hzFor01 (20..500 Hz, log).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::LOWCUT, 1 }, "Low Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // HI CUT del PLUGIN (AL FINAL → append-only, no rompe el recall de los presets ya shippeados). El PAR del LOW
    // CUT: low-pass sobre la SALIDA COMPLETA (dry+wet) → oscurece/suaviza TODO lo que sale cortando los agudos,
    // no sólo el wet (se oye a cualquier MIX). Default 0 % = 20 kHz = apagado → transparente. El processor mapea
    // %→0..1 y de ahí a Hz con ovni::dsp::HiCut::hzFor01 (20 kHz..1.5 kHz, log, invertida: más perilla = más oscuro).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HICUT, 1 }, "Hi Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    return layout;
}

// ----------------------------------------------------------------------------- ctor
NebulaProcessor::NebulaProcessor()
    : ovni::PluginProcessorBase ("NEBULA", createParameterLayout())
{
    pSize       = apvts.getRawParameterValue (pid::SIZE);
    pDecay      = apvts.getRawParameterValue (pid::DECAY);
    pTone       = apvts.getRawParameterValue (pid::TONE);
    pBreath     = apvts.getRawParameterValue (pid::BREATH);
    pMix        = apvts.getRawParameterValue (pid::MIX);
    pLowCut     = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut      = apvts.getRawParameterValue (pid::HICUT);
    pBreathSync = apvts.getRawParameterValue (pid::BREATHSYNC);
    pBreathDiv  = apvts.getRawParameterValue (pid::BREATHDIV);   // índice de la división (float)
}

juce::AudioProcessorEditor* NebulaProcessor::createEditor()
{
    return new NebulaEditor (*this);
}

void NebulaProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    // LOW CUT + HI CUT del PLUGIN: filtros TPT estéreo que esculpen la SALIDA COMPLETA (dry+wet) DESPUÉS del
    // motor (ver processAudio). Se preparan acá al peor bloque (RT-safe, sin alloc en process). El de-zipper
    // del cutoff vive dentro de cada filtro (TPT modulación-safe, sin clicks al modular).
    lowCut.prepare (spec);
    hiCut.prepare (spec);

    fdn.prepare (spec);
    // El motor introduce latencia: el limiter de SALIDA con lookahead (~3 ms) retrasa la suma dry+wet para
    // poder mirar el pico que viene y contenerlo sin crackle (Joaquín aceptó la latencia a cambio de cero
    // clip). Se la reportamos al host para que compense (PDC). Va DESPUÉS de fdn.prepare (ya dimensionó el
    // lookahead). El chasis (prepareToPlay) llama a prepareEngine al final y NO toca la latencia → no la pisa.
    setLatencySamples (fdn.latencySamples());
}

void NebulaProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // --- macros (RT-safe); % -> 0..1 ------------------------------------------------
    const float size01   = juce::jlimit (0.f, 1.f, (pSize   ? pSize->load()   : 50.f) * 0.01f);
    const float decay01  = juce::jlimit (0.f, 1.f, (pDecay  ? pDecay->load()  : 50.f) * 0.01f);
    const float tone01   = juce::jlimit (0.f, 1.f, (pTone   ? pTone->load()   : 40.f) * 0.01f);
    const float breath01 = juce::jlimit (0.f, 1.f, (pBreath ? pBreath->load() : 25.f) * 0.01f);
    const float mix01    = juce::jlimit (0.f, 1.f, (pMix    ? pMix->load()    : 35.f) * 0.01f);
    const float lowCut01 = juce::jlimit (0.f, 1.f, (pLowCut ? pLowCut->load() : 0.f)  * 0.01f);
    const float hiCut01  = juce::jlimit (0.f, 1.f, (pHiCut  ? pHiCut->load()  : 0.f)  * 0.01f);

    // DECAY al tope -> freeze: cola congelada (el motor clampa g_i al borde y corta la inyección).
    const bool freeze = decay01 >= kFreezeThreshold;

    // --- SYNC de la respiración: libre (orgánica) o enganchada al tempo del host ------
    // breathSync on → leemos el BPM del playhead (default 120 si el host no lo da) y derivamos la
    // frecuencia de respiración (ciclos/seg). breathSync off → -1 (el motor respira libre).
    // La división del ciclo es FIJA = "1 bar" (4 beats por ciclo): el editor ya NO expone el selector
    // (decisión: "con el SYNC solo estamos"). El param breathDiv sigue en el layout (append-only → no
    // rompe el recall de presets ya shippeados) pero NO se lee; SYNC engancha SIEMPRE a 1 compás.
    float breathRateHz = -1.0f;   // ≤0 = libre (frecuencias orgánicas internas del BreathLFO)
    if (pBreathSync && pBreathSync->load() >= 0.5f)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    bpm = *b;
        const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                         (int) std::round (pBreathDiv ? pBreathDiv->load() : (float) params::sync::kDefaultIndex));
        breathRateHz = params::sync::breathRateHz (bpm, divIdx);
    }

    // --- motor REVERB FDN (la cola difusa que respira) — in-place sobre buffer (deriva dry+wet del input) ----
    // El FDN procesa in-place: toma el dry del usuario, calcula la mezcla dry/wet (ley de potencia), aplica IN
    // PHASE (monoSafe) a la cola y el limiter de salida con lookahead — todo adentro. Al volver, buffer contiene
    // la SALIDA FINAL completa (dry+wet ya mezclados y limitados).
    fdn.process (buffer, ovni::engines::FdnParams {
        .size01       = size01,
        .decay01      = decay01,
        .tone01       = tone01,
        .breath01     = breath01,
        .mix01        = mix01,
        .freeze       = freeze,
        .monoSafe     = isMonoSafe(),     // IN PHASE → colapsa la cola WET a mono-compatible (in phase)
        .breathRateHz = breathRateHz });  // >0 = respiración sincronizada; ≤0 = libre

    // --- LOW CUT + HI CUT del PLUGIN sobre la SALIDA COMPLETA (dry+wet ya mezclados) -----------------------
    // El filtro pasa-altos / pasa-bajos del plugin: corta los graves/agudos de TODO lo que sale, no sólo el wet
    // → el corte se OYE a CUALQUIER MIX (a MIX bajo el dry full-range ya no lo tapa). Cadena low-cut → hi-cut.
    // TPT modulación-safe (de-zipper interno del cutoff, sin clicks). Lineales → no generan alias.
    //
    // BYPASS POR FILTRO (lc01>0 / hc01>0): cada filtro corre SÓLO si su perilla está engaged. Crítico: van
    // DESPUÉS del limiter de salida del motor (el limiter vive DENTRO de fdn.process), así que NO los protege.
    // Un HP, aunque atenúe en banda, desfasa → su respuesta transitoria puede SOBREPASAR el pico que el limiter
    // dejó al borde del techo (medido: el HP de 20 Hz "off" subía la salida de 0.95 a ~1.08 → clip). Con el
    // bypass, en default (lowCut01=hiCut01=0) NO corre NINGÚN filtro → la salida es la del motor intacta (idéntica
    // a antes, limiter-safe; clip-scan verde). Cuando el usuario engancha el filtro, el corte baja el nivel de la
    // banda filtrada (net peak típicamente ≤ el del motor); el techo del motor (0.95 con margen inter-sample) da
    // colchón de sobra para el overshoot residual de fase del filtro. No cambia el sonido ni los presets en off.
    {
        const int   chN  = buffer.getNumChannels();
        const float lc01 = juce::jlimit (0.0f, 1.0f, lowCut01);
        const float hc01 = juce::jlimit (0.0f, 1.0f, hiCut01);
        if (lc01 > 0.0f || hc01 > 0.0f)
        {
            float* oPtrs[2] = { buffer.getWritePointer (0),
                                (chN > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0)) };
            juce::AudioBuffer<float> oView (oPtrs, juce::jmin (chN, 2), n);
            if (lc01 > 0.0f)   // 0 → bypass (el HP de 20 Hz ya es ~transparente; saltarlo evita el overshoot post-limiter)
            {
                lowCut.setCutoffHz (ovni::dsp::LowCut::hzFor01 (lc01));
                lowCut.process (oView);
            }
            if (hc01 > 0.0f)   // 0 → bypass (LP en 20 kHz ya es transparente)
            {
                hiCut.setCutoffHz (ovni::dsp::HiCut::hzFor01 (hc01));
                hiCut.process (oView);
            }
        }
    }

    // --- telemetría del visual (lock-free) ------------------------------------------
    uiSize.store   (size01,   std::memory_order_relaxed);
    uiDecay.store  (decay01,  std::memory_order_relaxed);
    uiTone.store   (tone01,   std::memory_order_relaxed);
    uiBreath.store (breath01, std::memory_order_relaxed);
    uiMix.store    (mix01,    std::memory_order_relaxed);
}

float NebulaProcessor::debugBreathRateHz (double bpm) const
{
    const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                     (int) std::round (pBreathDiv ? pBreathDiv->load() : (float) params::sync::kDefaultIndex));
    return params::sync::breathRateHz (bpm, divIdx);
}

float NebulaProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = conteniendo. Lo mapeamos a 0..1 (empuje del LED).
    return juce::jlimit (0.0f, 1.0f, 1.0f - fdn.lastLimiterGain());
}

} // namespace nebula

// ===================================================================== entry point de JUCE
// Los wrappers de cada formato (AU / VST3 / Standalone) referencian este símbolo.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nebula::NebulaProcessor();
}
