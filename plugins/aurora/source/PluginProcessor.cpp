#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace aurora
{

namespace pid = aurora::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

namespace
{
    // Micro-fade del bypass compensado (anti-click-clip-truepeak: "micro-fades en
    // conmutaciones"). Cruce LINEAL: wet y dry-retrasado están ALINEADOS en el tiempo y
    // correlacionados (el motor preserva la fase por bin) → la ley lineal no infla el cruce.
    constexpr float kBypassFadeMs = 12.0f;
}

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout AuroraProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // Las 6 macros de la curaduría (display-name = etiqueta UI, diccionario). Defaults
    // de curaduría (ES LEY): Spread 55 · Tilt 0 · Motion 0 · Mono Safe 50 · Duck 0 · Mix 100.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SPREAD, 1 }, "Spread",
        Range { 0.f, 100.f, 0.01f }, 55.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // TILT bipolar: la curva frecuencia→posición. 0 = graves-centro/agudos-bordes;
    // negativo INVIERTE (agudos al centro); positivo abre antes en frecuencia.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::TILT, 1 }, "Tilt",
        Range { -100.f, 100.f, 0.1f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return (v > 0 ? "+" + juce::String (juce::roundToInt (v))
                                              : juce::String (juce::roundToInt (v))); })));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MOTION, 1 }, "Motion",
        Range { 0.f, 100.f, 0.01f }, 30.f,   // default >0: "carga sonando" — la aurora ondula (mono-audible) sin tocar nada; 0 = widener transparente
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // MONO SAFE (id "monoSafeAmt" — el "monoSafe" del chasis es el toggle IN PHASE; cero
    // colisión). 0–100 → corte de colapso-al-centro 50→400 Hz log; 0 = sin red de usuario.
    // DEFAULT 0 (revisado 2026-06-10 con el ENSANCHADOR REAL de fase): con la decorrelación
    // de fase, una red al 50 % se tragaba la masa de energía del beat y dejaba el goniómetro
    // MONO (la queja de Joaquín). A 0 "carga sonando" (principio de curaduría) y el PISO SUB
    // siempre-mono del motor (<~55 Hz, física no-opcional) ya mantiene el kick mono-compatible.
    // El knob es la red PARA SUBIR cuando el material tiene bajo-medio fuerte que proteger.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MONOSAFEAMT, 1 }, "Mono Safe",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DUCK, 1 }, "Duck",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 100.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // SYNC del MOTION (control CORE del sello). motionSync: abanico libre / enganchado al
    // tempo. motionDivision: beats por ciclo (etiquetas de la MISMA tabla sync::divs →
    // una sola fuente de verdad con el processor y los tests).
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::MOTIONSYNC, 1 }, "Motion Sync", false));

    juce::StringArray divNames;
    for (int i = 0; i < params::sync::kCount; ++i) divNames.add (params::sync::divs[i].name);
    layout.add (std::make_unique<APC> (juce::ParameterID { pid::MOTIONDIV, 1 }, "Motion Div",
                                       divNames, params::sync::kDefaultIndex));

    // RATE del MOTION en FREE: mapeo LOGARÍTMICO (equal-ratio por paso → percepción
    // pareja, CERO zonas muertas — lección del RATE de HALO). Rango 0.02–8 Hz, default
    // 0.3 Hz (curaduría: "rate FREE listo en 0.3 Hz"). La cota anti-AM (< 20 Hz) vive en
    // el motor; el rango del knob ya queda por debajo.
    juce::NormalisableRange<float> motionHz {
        params::kMotionRateMinHz, params::kMotionRateMaxHz,
        [] (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); },              // 0..1 → Hz (log)
        [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },  // Hz → 0..1
        [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MOTIONRATE, 1 }, "Motion Rate",
        motionHz, params::kMotionFreeDefaultHz,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float hz, int) {
                return hz >= 1.0f ? juce::String (hz, 1) + " Hz"
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
AuroraProcessor::AuroraProcessor()
    : ovni::PluginProcessorBase ("AURORA", createParameterLayout())
{
    pSpread      = apvts.getRawParameterValue (pid::SPREAD);
    pTilt        = apvts.getRawParameterValue (pid::TILT);
    pMotion      = apvts.getRawParameterValue (pid::MOTION);
    pMonoSafeAmt = apvts.getRawParameterValue (pid::MONOSAFEAMT);
    pDuck        = apvts.getRawParameterValue (pid::DUCK);
    pMix         = apvts.getRawParameterValue (pid::MIX);
    pMotionSync  = apvts.getRawParameterValue (pid::MOTIONSYNC);
    pMotionDiv   = apvts.getRawParameterValue (pid::MOTIONDIV);
    pMotionRate  = apvts.getRawParameterValue (pid::MOTIONRATE);
    pLowCut      = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut       = apvts.getRawParameterValue (pid::HICUT);

    // Bypass compensado: el mismo bool que lee el power del chasis + los gains de utilidad
    // que el chasis inyecta (el wet manual del fade-out los aplica estáticos).
    pBypass      = apvts.getRawParameterValue (pid::BYPASS);
    pInGainUtil  = apvts.getRawParameterValue ("inGain");
    pOutputUtil  = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorEditor* AuroraProcessor::createEditor()
{
    // El editor del sello (chasis verde SPL·01 + la aurora desplegada como gancho visual).
    return new AuroraEditor (*this);
}

void AuroraProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    // LOW CUT + HI CUT del PLUGIN: filtros TPT estéreo que esculpen la SALIDA COMPLETA (dry+wet) DESPUÉS
    // del motor (ver processAudio). Se preparan al peor bloque (RT-safe, sin alloc en process). El de-zipper
    // del cutoff vive dentro de cada filtro (TPT modulación-safe, sin clicks al modular).
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
    // Estado inicial = el del param (una sesión que carga en bypass NO hace fade de entrada).
    bypassXf = (pBypass != nullptr && pBypass->load() >= 0.5f) ? 1.0f : 0.0f;
}

// ------------------------------------------------------------------- bypass compensado
// Alimenta el ring con el dry CRUDO (pre-gains del chasis) y, si dly != nullptr, lee el
// dry RETRASADO getLatencySamples() — la señal que el bypass emite para que el PDC del
// host siga siendo correcto ("latencia declarada == real" también en estado bypass).
void AuroraProcessor::feedBypassRing (const juce::AudioBuffer<float>& in, int n,
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

void AuroraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const bool wantBypass = (pBypass != nullptr && pBypass->load() >= 0.5f);

    // Sin prepare todavía, o host fuera de contrato (n > maxBlock de los scratches):
    // chasis pelado (sin compensación — el motor igual se defiende troceando).
    if (bypassRingSz <= 0 || n > bypassDly.getNumSamples())
    {
        ovni::PluginProcessorBase::processBlock (buffer, midi);
        return;
    }

    // Camino ACTIVO puro (lo normal): sólo mantener el ring del dry al día (una copia
    // estéreo, costo ~0) para que el delay compensado esté LISTO al instante de apagar
    // el power; el resto delega intacto al chasis.
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
        // El chasis maneja el Program Change MIDI y deja buffer = input (pass-through).
        ovni::PluginProcessorBase::processBlock (buffer, midi);

        // Motor CALIENTE: el camino wet corre a mano sobre una copia (gains in/out del
        // chasis aplicados ESTÁTICOS — el wet sólo se OYE durante el micro-fade de
        // salida; mantenerlo corriendo evita el garble de un pipeline OLA frío al volver
        // y mantiene el dry interno del motor alineado). CPU en bypass = la del activo
        // (~0.2%, [bench]) — decisión deliberada: corrección > ahorro.
        for (int ch = 0; ch < 2; ++ch)
            bypassWet.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0, n);
        const float inG  = pInGainUtil != nullptr ? juce::Decibels::decibelsToGain (pInGainUtil->load()) : 1.0f;
        const float outG = pOutputUtil != nullptr ? juce::Decibels::decibelsToGain (pOutputUtil->load()) : 1.0f;
        bypassWet.applyGain (0, n, inG);
        {
            juce::AudioBuffer<float> wetView (bypassWet.getArrayOfWritePointers(), 2, 0, n);
            juce::MidiBuffer noMidi;   // el PC ya lo interceptó el chasis arriba
            processAudio (wetView, noMidi);
        }
        bypassWet.applyGain (0, n, outG);

        // Salida = cruce wet → dry-retrasado con rampa POR-SAMPLE hacia 1. A xf=1 el
        // resultado es EXACTAMENTE el dry retrasado (0·wet + 1·dly): bypass bit-exact
        // contra la entrada retrasada N — lo verifica [null].
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
        // Cola del fade-in (power recién encendido): el chasis corre el camino activo
        // COMPLETO y el cruce vuelve del dry-retrasado al wet con rampa hacia 0.
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

void AuroraProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // --- macros (RT-safe); % → 0..1 / unidades del motor -----------------------------
    const float spread01   = juce::jlimit (0.f, 1.f, (pSpread      ? pSpread->load()      : 55.f) * 0.01f);
    const float tiltBi     = juce::jlimit (-1.f, 1.f, (pTilt        ? pTilt->load()        : 0.f)  * 0.01f);
    const float motion01   = juce::jlimit (0.f, 1.f, (pMotion      ? pMotion->load()      : 0.f)  * 0.01f);
    const float monoSafe01 = juce::jlimit (0.f, 1.f, (pMonoSafeAmt ? pMonoSafeAmt->load() : 50.f) * 0.01f);
    const float duck01     = juce::jlimit (0.f, 1.f, (pDuck        ? pDuck->load()        : 0.f)  * 0.01f);
    const float mix01      = juce::jlimit (0.f, 1.f, (pMix         ? pMix->load()         : 100.f) * 0.01f);

    // --- transporte del host (conduce el SYNC del MOTION; fallback 120 BPM) ----------
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

    // --- RATE del MOTION (CORE): FREE → perilla RATE; SYNC → BPM·división. Una sola
    // fuente de verdad (computeMotionRateHz), compartida con el getter de diagnóstico.
    const float motionRateHz = computeMotionRateHz (bpm);
    const bool  syncOn       = (pMotionSync && pMotionSync->load() >= 0.5f);
    const int   divIdx       = juce::jlimit (0, params::sync::kCount - 1,
                                             (int) std::round (pMotionDiv ? pMotionDiv->load()
                                                                          : (float) params::sync::kDefaultIndex));

    // --- motor de paneo espectral -----------------------------------------------------
    engine.process (buffer, AuroraParams {
        .spread01         = spread01,
        .tiltBi           = tiltBi,
        .motion01         = motion01,
        .monoSafe01       = monoSafe01,
        .duck01           = duck01,
        .mix01            = mix01,
        .motionRateHz     = motionRateHz,
        .motionSync       = syncOn,
        .isPlaying        = isPlaying,
        .ppqPosition      = ppq,
        .beatsPerCycle    = params::sync::beatsForDiv (divIdx),
        .numInputChannels = getTotalNumInputChannels(),
        .inPhase          = isMonoSafe() });   // IN PHASE: el motor colapsa su ensanchador (mono-safe); el chasis suma graves a mono DESPUÉS

    // --- LOW CUT + HI CUT del PLUGIN sobre la SALIDA COMPLETA (dry+wet ya mezclados) -----------------------
    // Pasa-altos / pasa-bajos del plugin: cortan los graves/agudos de TODO lo que sale → el corte se OYE a
    // CUALQUIER MIX. Cadena low-cut → hi-cut. TPT modulación-safe (de-zipper interno, sin clicks). Lineales →
    // no generan alias. BYPASS POR FILTRO (lc01>0 / hc01>0): en default (0/0) NO corre NINGÚN filtro → la
    // salida es la del motor intacta (limiter-safe, idéntica a antes; el HP, aunque atenúe, desfasa y podría
    // sobrepasar el techo post-limiter → por eso se saltea en off).
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
    uiSpread.store   (spread01,   std::memory_order_relaxed);
    uiTilt.store     (tiltBi,     std::memory_order_relaxed);
    uiMotion.store   (motion01,   std::memory_order_relaxed);
    uiMonoSafe.store (monoSafe01, std::memory_order_relaxed);
    uiDuck.store     (duck01,     std::memory_order_relaxed);
    uiMix.store      (mix01,      std::memory_order_relaxed);
    uiGamma.store    (juce::jlimit (0.0f, 1.0f, engine.currentGamma()),  std::memory_order_relaxed);
    uiDuckEnv.store  (juce::jlimit (0.0f, 1.0f, engine.duckEnvelope()),  std::memory_order_relaxed);
    // Velocidad normalizada del MOTION (mapeo log del rate efectivo sobre el rango de la
    // perilla 0.02–8 Hz) → el visual late a la velocidad REAL (FREE o SYNC).
    {
        const float hz = juce::jlimit (params::kMotionRateMinHz, params::kMotionRateMaxHz, motionRateHz);
        const float rn = std::log (hz / params::kMotionRateMinHz)
                       / std::log (params::kMotionRateMaxHz / params::kMotionRateMinHz);
        uiRateNorm.store (juce::jlimit (0.0f, 1.0f, rn), std::memory_order_relaxed);
    }
    // El despliegue por banda (energía + posición): la aurora que pinta la etapa 3.
    {
        const auto& ez = engine.bandEnergies();
        const auto& px = engine.bandPositions();
        for (int b = 0; b < kVizBands; ++b)
        {
            uiBandEnergy[(size_t) b].store (ez[(size_t) b], std::memory_order_relaxed);
            uiBandPos[(size_t) b].store    (px[(size_t) b], std::memory_order_relaxed);
        }
    }
}

float AuroraProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = conteniendo. Lo mapeamos a 0..1 (empuje del LED).
    return juce::jlimit (0.0f, 1.0f, 1.0f - engine.lastLimiterGain());
}

float AuroraProcessor::computeMotionRateHz (double bpm) const
{
    // SYNC → la división manda (BPM·beats por ciclo). FREE → la perilla RATE.
    // La COTA ANTI-AM (< 20 Hz) la aplica el motor (física cableada, no opción).
    if (pMotionSync && pMotionSync->load() >= 0.5f)
    {
        const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                         (int) std::round (pMotionDiv ? pMotionDiv->load()
                                                                      : (float) params::sync::kDefaultIndex));
        return params::sync::motionRateHz (bpm, divIdx);
    }
    return pMotionRate ? pMotionRate->load() : params::kMotionFreeDefaultHz;
}

float AuroraProcessor::debugMotionRateHz (double bpm) const
{
    const int divIdx = juce::jlimit (0, params::sync::kCount - 1,
                                     (int) std::round (pMotionDiv ? pMotionDiv->load()
                                                                  : (float) params::sync::kDefaultIndex));
    return params::sync::motionRateHz (bpm, divIdx);
}

} // namespace aurora

// ===================================================================== entry point de JUCE
// Los wrappers de cada formato (AU / VST3 / Standalone) referencian este símbolo.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new aurora::AuroraProcessor();
}
