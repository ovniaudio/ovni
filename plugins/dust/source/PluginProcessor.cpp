#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace dust
{

namespace pid = dust::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using APC   = juce::AudioParameterChoice;
using Range = juce::NormalisableRange<float>;

namespace
{
    constexpr float kHalfPi = juce::MathConstants<float>::halfPi;

    // DUCK: profundidad máxima de atenuación del wet (0.9 ≈ −20 dB a DUCK 100 con dry pegando
    // fuerte) y sensibilidad del envelope (env ≥ kDuckEnvFull => profundidad plena). Más fuerte el
    // dry => más se aparta el wet (honesto: "se aparta cuando pega", no un gate fijo).
    constexpr float kDuckMaxDepth = 0.9f;
    constexpr float kDuckEnvFull  = 0.25f;   // ≈ −12 dBFS de envelope => duck pleno

    // SPREAD perceptual (re-curva del QA, barrido [honestidad]): la apertura LATERAL audible (side)
    // satura arriba — medido: con la curva lineal, SPREAD 25 ya entregaba ~55 % del side máximo y el
    // tramo 75→100 apenas +8 %. El exponente 1.5 reparte el recorrido del knob parejo (cada cuarto
    // de vuelta abre el campo de forma audible, sin tramo chato). Es mapeo del MACRO (el "foso"),
    // no del motor: el motor sigue recibiendo varianza angular 0..1. El visual usa el MISMO valor
    // efectivo (uiSpread) → la apertura que se VE es la que SUENA.
    inline float spreadPerceptual (float s01) noexcept { return std::pow (s01, 1.5f); }
}

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout DustProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };

    // Defaults de la curaduría ("Burbujas"): Mix 35 · Rate FREE 250 ms · Densidad 40 · Spread 60 ·
    // Vida 25 · Duck 0 · Origin centro-frente (0, 0.35).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 35.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // RATE en FREE: espaciado entre ecos en ms, mapeo LOGARÍTMICO (equal-ratio por paso → percepción
    // pareja, CERO zonas muertas — lección del knob de HALO). Rango 20 ms – 2 s (curaduría). Display
    // en ms (< 1 s) o segundos.
    juce::NormalisableRange<float> rateMs {
        params::kRateMinMs, params::kRateMaxMs,
        [] (float lo, float hi, float t) { return lo * std::pow (hi / lo, t); },              // 0..1 → ms (log)
        [] (float lo, float hi, float v) { return std::log (v / lo) / std::log (hi / lo); },  // ms → 0..1
        [] (float lo, float hi, float v) { return juce::jlimit (lo, hi, v); } };
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::RATE, 1 }, "Rate",
        rateMs, params::kRateFreeDefaultMs,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float ms, int) {
                return ms >= 1000.0f ? juce::String (ms / 1000.0f, 2) + " s"
                                     : juce::String (juce::roundToInt (ms)) + " ms"; })));

    // SYNC del RATE (control CORE del sello): el espaciado se engancha al tempo del host. La
    // división sale de la MISMA tabla sync::divs (una sola fuente de verdad con tests y editor).
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::RATESYNC, 1 }, "Rate Sync", false));

    juce::StringArray divNames;
    for (int i = 0; i < params::sync::kCount; ++i) divNames.add (params::sync::divs[i].name);
    layout.add (std::make_unique<APC> (juce::ParameterID { pid::RATEDIV, 1 }, "Rate Div",
                                       divNames, params::sync::kDefaultIndex));

    // DENSIDAD (display "Densidad" — identidad del plugin): estallido finito → nube que crece.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DENSITY, 1 }, "Densidad",
        Range { 0.f, 100.f, 0.01f }, 40.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SPREAD, 1 }, "Spread",
        Range { 0.f, 100.f, 0.01f }, 60.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::VIDA, 1 }, "Vida",
        Range { 0.f, 100.f, 0.01f }, 25.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // DUCK (ADN musical del diccionario): el wet se aparta cuando pega el dry. Default 0 (off).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::DUCK, 1 }, "Duck",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // ORIGIN (x, y): lo arrastra el visualizador (superficie de control). Default centro-frente.
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ORIGINX, 1 }, "Origin X",
        Range { -1.f, 1.f, 0.0001f }, params::kOriginXDefault,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2); })));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::ORIGINY, 1 }, "Origin Y",
        Range { -1.f, 1.f, 0.0001f }, params::kOriginYDefault,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String (v, 2); })));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

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
DustProcessor::DustProcessor()
    : ovni::PluginProcessorBase ("DUST", createParameterLayout())
{
    pMix      = apvts.getRawParameterValue (pid::MIX);
    pRate     = apvts.getRawParameterValue (pid::RATE);
    pRateSync = apvts.getRawParameterValue (pid::RATESYNC);
    pRateDiv  = apvts.getRawParameterValue (pid::RATEDIV);
    pDensity  = apvts.getRawParameterValue (pid::DENSITY);
    pSpread   = apvts.getRawParameterValue (pid::SPREAD);
    pVida     = apvts.getRawParameterValue (pid::VIDA);
    pDuck     = apvts.getRawParameterValue (pid::DUCK);
    pOriginX  = apvts.getRawParameterValue (pid::ORIGINX);
    pOriginY  = apvts.getRawParameterValue (pid::ORIGINY);
    pLowCut   = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut    = apvts.getRawParameterValue (pid::HICUT);
}

juce::AudioProcessorEditor* DustProcessor::createEditor()
{
    // El editor real del sello: campo de burbujas (drag del ORIGIN) + rail + utilidad (MOV·03).
    return new DustEditor (*this);
}

void DustProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    // LOW CUT + HI CUT del PLUGIN: filtros TPT estéreo que esculpen la SALIDA COMPLETA (dry+wet) DESPUÉS
    // del motor (ver processAudio). Se preparan al peor bloque (RT-safe, sin alloc en process).
    lowCut.prepare (spec);
    hiCut.prepare (spec);

    // Targets ANTES de prepare: el reset interno del motor arranca cada suavizado EN el valor del
    // parámetro (sin barridos fantasma al abrir la sesión).
    engine.setRateMs    (computeRateMs (120.0));
    engine.setDensity01 ((pDensity ? pDensity->load() : 40.f) * 0.01f);
    engine.setSpread01  (spreadPerceptual ((pSpread ? pSpread->load() : 60.f) * 0.01f));
    engine.setVida01    ((pVida    ? pVida->load()    : 25.f) * 0.01f);
    engine.setMonoSafe  (isMonoSafe());
    engine.setOrigin    (pOriginX ? pOriginX->load() : params::kOriginXDefault,
                         pOriginY ? pOriginY->load() : params::kOriginYDefault);
    engine.prepare (spec);

    dryBuf.setSize (2, (int) spec.maximumBlockSize);

    // DUCK: attack ~5 ms (agacha rápido cuando pega el dry), release ~150 ms (vuelve en el hueco).
    duckAttCoef = 1.0f - std::exp (-1.0f / (0.005f * (float) spec.sampleRate));
    duckRelCoef = 1.0f - std::exp (-1.0f / (0.150f * (float) spec.sampleRate));
    duckEnv     = 0.0f;
    duckDepthSm = kDuckMaxDepth * (pDuck ? pDuck->load() : 0.f) * 0.01f;

    // MIX: ganancias mapeadas (ley de potencia) arrancan en el valor del parámetro.
    const float mix01 = juce::jlimit (0.f, 1.f, (pMix ? pMix->load() : 35.f) * 0.01f);
    gDrySm = std::cos (mix01 * kHalfPi);
    gWetSm = std::sin (mix01 * kHalfPi);

    // Latencia HONESTA: 0 declarada == 0 real (el dry no se retarda; el wet son ecos ≥ 20 ms por
    // diseño, no PDC). El gate [latency] lo verifica con impulso real.
    setLatencySamples (0);
}

void DustProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    const int numIn = juce::jmin (2, juce::jmax (1, getTotalNumInputChannels()));

    // --- macros (RT-safe); % -> 0..1 ------------------------------------------------
    const float mix01     = juce::jlimit (0.f, 1.f, (pMix     ? pMix->load()     : 35.f) * 0.01f);
    const float density01 = juce::jlimit (0.f, 1.f, (pDensity ? pDensity->load() : 40.f) * 0.01f);
    const float spread01  = juce::jlimit (0.f, 1.f, (pSpread  ? pSpread->load()  : 60.f) * 0.01f);
    const float vida01    = juce::jlimit (0.f, 1.f, (pVida    ? pVida->load()    : 25.f) * 0.01f);
    const float duck01    = juce::jlimit (0.f, 1.f, (pDuck    ? pDuck->load()    : 0.f)  * 0.01f);
    const float originX   = juce::jlimit (-1.f, 1.f, pOriginX ? pOriginX->load() : params::kOriginXDefault);
    const float originY   = juce::jlimit (-1.f, 1.f, pOriginY ? pOriginY->load() : params::kOriginYDefault);

    // --- transporte del host (sólo BPM: el SYNC fija el espaciado, no hay fase que enganchar) ----
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = *b;

    // --- RATE efectivo (control CORE): FREE = perilla en ms; SYNC = división al tempo ------------
    const float rateMs = computeRateMs (bpm);

    // Targets al motor (el suavizado vive ADENTRO, en el dominio del destino, post-mapeo).
    // SPREAD pasa por la re-curva perceptual (ver spreadPerceptual: recorrido del knob parejo).
    const float spreadEff = spreadPerceptual (spread01);
    engine.setRateMs    (rateMs);
    engine.setDensity01 (density01);
    engine.setSpread01  (spreadEff);
    engine.setVida01    (vida01);
    engine.setMonoSafe  (isMonoSafe());     // IN PHASE: el banco cae a paneo de potencia constante
    engine.setOrigin    (originX, originY); // rampa por-sample aguas abajo (slew 25 ms + buses)

    // --- copia del dry (para MIX por ley de potencia + envelope del DUCK) ------------------------
    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (juce::jmin (1, numIn - 1));
    dryBuf.copyFrom (0, 0, inL, n);
    dryBuf.copyFrom (1, 0, inR, n);

    // --- motor: buffer pasa a ser el WET binaural (limiter 0.85 estéreo-linked adentro) ----------
    engine.process (buffer, numIn);

    // --- telemetría: energía del wet (flare de la nube) ------------------------------------------
    {
        double acc = 0.0;
        const float* wL = buffer.getReadPointer (0);
        const float* wR = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i) acc += (double) wL[i] * wL[i] + (double) wR[i] * wR[i];
        uiWetRms.store (juce::jlimit (0.0f, 1.0f, (float) std::sqrt (acc / (2.0 * (double) n))),
                        std::memory_order_relaxed);
    }

    // --- DUCK + MIX en un solo lazo por-sample ----------------------------------------------------
    // DUCK: envelope del dry (attack 5 ms / release 150 ms) agacha el wet multiplicativamente; la
    // ganancia resultante es continua por construcción (one-pole por-sample). MIX: ley de potencia
    // con las ganancias MAPEADAS rampeadas por-sample (suavizado DESPUÉS del mapeo, regla de oro).
    const float gDryTgt = std::cos (mix01 * kHalfPi);
    const float gWetTgt = std::sin (mix01 * kHalfPi);
    const float ddTgt   = kDuckMaxDepth * duck01;

    const float invN     = 1.0f / (float) n;
    const float gDryStep = (gDryTgt - gDrySm) * invN;
    const float gWetStep = (gWetTgt - gWetSm) * invN;
    const float ddStep   = (ddTgt   - duckDepthSm) * invN;

    const float* dL = dryBuf.getReadPointer (0);
    const float* dR = dryBuf.getReadPointer (1);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    float gD = gDrySm, gW = gWetSm, dd = duckDepthSm, env = duckEnv;
    float grMax = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float a = 0.5f * (std::abs (dL[i]) + std::abs (dR[i]));
        env += (a > env ? duckAttCoef : duckRelCoef) * (a - env);
        const float duckGain = 1.0f - dd * juce::jmin (1.0f, env * (1.0f / kDuckEnvFull));
        grMax = juce::jmax (grMax, 1.0f - duckGain);

        L[i] = dL[i] * gD + L[i] * gW * duckGain;
        R[i] = dR[i] * gD + R[i] * gW * duckGain;
        gD += gDryStep; gW += gWetStep; dd += ddStep;
    }
    gDrySm = gDryTgt; gWetSm = gWetTgt; duckDepthSm = ddTgt; duckEnv = env;

    // --- LOW CUT + HI CUT del PLUGIN sobre la SALIDA COMPLETA (dry+wet ya mezclados) ---------------------
    // Pasa-altos / pasa-bajos del plugin: cortan los graves/agudos de TODO lo que sale → el corte se OYE a
    // CUALQUIER MIX. Cadena low-cut → hi-cut. TPT modulación-safe (de-zipper interno, sin clicks). Lineales →
    // no generan alias. BYPASS POR FILTRO (lc01>0 / hc01>0): en default (0/0) NO corre NINGÚN filtro → la
    // salida es la del motor intacta (idéntica a antes, limiter-safe).
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

    // --- telemetría del visual (lock-free) --------------------------------------------------------
    uiMix.store     (mix01,     std::memory_order_relaxed);
    uiDensity.store (density01, std::memory_order_relaxed);
    uiSpread.store  (spreadEff, std::memory_order_relaxed);   // apertura EFECTIVA: lo que se ve = lo que suena
    uiVida.store    (vida01,    std::memory_order_relaxed);
    uiDuck.store    (duck01,    std::memory_order_relaxed);
    uiDuckGr.store  (juce::jlimit (0.0f, 1.0f, grMax), std::memory_order_relaxed);
    uiOriginX.store (originX,   std::memory_order_relaxed);
    uiOriginY.store (originY,   std::memory_order_relaxed);
    {
        // Espaciado efectivo normalizado (log 20..2000 ms): el RATE (FREE) o la división (SYNC)
        // mueven la cadencia VISIBLE de nacimientos del campo (el knob nunca es inerte en el visual).
        const float ms = juce::jlimit (params::kRateMinMs, params::kRateMaxMs, rateMs);
        const float rn = std::log (ms / params::kRateMinMs)
                       / std::log (params::kRateMaxMs / params::kRateMinMs);
        uiRateNorm.store (juce::jlimit (0.0f, 1.0f, rn), std::memory_order_relaxed);
    }

    // FIFO de eventos burbuja: un evento por nacimiento (azimut, energía, vida) para que el
    // visualizador haga nacer cada burbuja EXACTAMENTE donde suena.
    const int nb = engine.lastBirthCount();
    for (int i = 0; i < nb; ++i)
    {
        const auto& b = engine.lastBirth (i);
        bubbleEvents.push ({ b.azimuthRad, b.energy, vida01 });
    }
}

float DustProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = conteniendo. Mapeado a 0..1 (empuje del LED de clip).
    return juce::jlimit (0.0f, 1.0f, 1.0f - engine.lastLimiterGain());
}

float DustProcessor::computeRateMs (double bpm) const
{
    // SYNC → la división manda (60000·beats/bpm, clampeado al rango FREE). FREE → la perilla RATE.
    namespace sd = params::sync;
    if (pRateSync && pRateSync->load() >= 0.5f)
    {
        const int idx = juce::jlimit (0, sd::kCount - 1,
                                      (int) std::round (pRateDiv ? pRateDiv->load()
                                                                 : (float) sd::kDefaultIndex));
        return sd::rateMsForDiv (bpm, idx);
    }
    return juce::jlimit (params::kRateMinMs, params::kRateMaxMs,
                         pRate ? pRate->load() : params::kRateFreeDefaultMs);
}

} // namespace dust

// ===================================================================== entry point de JUCE
// Los wrappers de cada formato (AU / VST3 / Standalone) referencian este símbolo.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new dust::DustProcessor();
}
