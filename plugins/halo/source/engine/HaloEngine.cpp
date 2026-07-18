#include "engine/HaloEngine.h"
#include <cmath>

namespace halo {

namespace {

// ── Rango del feedback del lazo (DECAY). Piso > 0 (siempre hay algo de cola) y techo < 1 (kRegenMax → el
//    round-trip NUNCA llega a ganancia unidad → el shimmer no diverge). Mapeo LOG: control fino arriba
//    (donde vive el "casi infinito"). decay01=0 → 0.30 (cola corta); decay01=1 → kRegenMax.
constexpr float kRegenMin = 0.30f;

// ── Rango del LP de banda del lazo (TONE). El shimmer EMPUJA energía hacia arriba en frecuencia; este LP
//    la drena cada vuelta (la clave anti-divergencia, base técnica §6). Más TONE = corte MÁS BAJO = más
//    oscuro y más estable. tone01=0 → 9 kHz (brillante); tone01=1 → 1.5 kHz (oscuro). Default 0.45 ≈ 7 kHz.
constexpr float kToneHiHz = 9000.0f;
constexpr float kToneLoHz = 1500.0f;

// ── HP fijo del lazo (DC blocker): mata el sub-rumble que el feedback acumularía en graves. Bajo, no
//    adelgaza el cuerpo. Fijo (no es control de usuario). Base técnica §6: HPF 1-polo @ 80–100 Hz.
constexpr float kLoopHpHz = 90.0f;

// ── Pre-delay (ms) a la entrada del wet — el "vacío" inicial (base técnica §4). Deriva de SIZE: 60→120 ms.
constexpr float kPreDelayMinMs = 60.0f;
constexpr float kPreDelayMaxMs = 120.0f;

// ── Grano del PitchShifter: 90 ms (base técnica §5/§9) — sidebands más suaves ("organ/pad"), latencia
//    ~45 ms/vuelta (ayuda al bloom). Vía el arg aditivo de prepare() (compat con los demás callers).
constexpr float kGrainMs = 90.0f;

// ── Voces FIJAS del shimmer (base técnica §1/§5): octava (+12.00, gain implícito 1.0) + quinta (+7.05,
//    micro-detune +5c → batimientos lentos que hacen "vivir" el shimmer). HONESTO: NO scale-aware. La
//    quinta NO es invariante bajo recursión pero el LP del lazo la drena → "intervalo de quinta fija"
//    (lo que hacen Eventide ShimmerVerb/BigSky), nunca vendida como escala.
constexpr float kVoiceOctave = 12.00f;
constexpr float kVoiceFifth  =  7.05f;   // quinta + 5 cents

// ── Parámetros INTERNOS del difusor FDN (no son controles). Decay LARGO (difusor casi-lossless dentro de
//    UNA vuelta del lazo → la cola la gobierna el DECAY del lazo, no el difusor). Tone casi abierto (el
//    TONE del lazo ya oscurece). Base técnica §4: el FDN difunde + respira; el shimmer da la vida.
constexpr float kFdnDecay01 = 0.95f;
constexpr float kFdnTone01  = 0.18f;
// SHIMMER de salida: valor del knob a partir del cual la capa pitched entra COMPLETA al wet.
// == DEFAULT del param (55%) → los proyectos guardados al default suenan BIT-idéntico; por debajo
// el primer paso pitched se desvanece linealmente hasta "reverb a secas" en 0 (QA 2026-07-16).
constexpr float kShimmerFullOut = 0.55f;

// ── Trayectoria orbital (base técnica §3/§9): Ellipse, freeHz=0.08 (≈1 vuelta/12 s; sólo fallback — el rate
//    real lo elige RATE/SYNC), spread=0.35, radio orbital 0.70.
//    CHAOS = 0 a propósito: el "bamboleo OVNI" del chaos agrega un jitter del azimut de ±~11° suavizado a
//    ~0.2 s (≈5 Hz) cuyo timescale NO depende del RATE → a órbita lenta ese bamboleo rápido DOMINA lo que se
//    oye ("pongo despacio y sigue rápido", lo cazó Joaquín; medido: a RATE mín el bamboleo era 1.55 dB rms vs
//    1.06 de la órbita). HALO quiere una órbita LISA y glacial mandada sólo por el RATE — el dardo errático es
//    el carácter de PULSAR, no de HALO. La vida orgánica la da el breath del FDN (que SÍ escala con el rate).
constexpr float kTrajFreeHz   = 0.08f;
constexpr float kTrajSpread01 = 0.35f;
constexpr float kTrajChaos01  = 0.0f;
constexpr float kSpatialRadius01 = 0.70f;

// ── Sombra de cabeza (ILD dependiente de frecuencia): corner del high-shelf del oído LEJANO. Una cabeza
//    humana empieza a sombrear notoriamente arriba de ~1.5–2 kHz (longitud de onda ≲ ancho de la cabeza);
//    debajo, el sonido la rodea sin atenuarse. 1.6 kHz = corner del LP 1-polo que separa "graves intactos"
//    (la parte LP, a unidad) del "residuo HF" (x − LP, que el shelf atenúa según el azimut).
constexpr float kHeadShadowHz = 1600.0f;

constexpr double kSmoothMs = 50.0;    // de-zipper de las macros del lazo. FREEZE: el input→0 se rampea
                                      // por-sample sobre el bloque (click-free; la captura engancha en ~10 ms).

// Interpolación geométrica (log) entre a y b según t∈[0,1].
float logLerp (float a, float b, float t) noexcept
{
    return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, t));
}

} // namespace

// ── Filtros 1-polo float64 del lazo (regla house §1: el feedback con decay > 1 s va en double) ──────────
void HaloEngine::OnePoleLP64::setCutoff (double fc, double sr) noexcept
{
    // LP 1-polo (TPT-equivalente en forma directa): y += a·(x − y), a = 1 − exp(−2π·fc/sr).
    const double w = juce::jlimit (1.0, sr * 0.49, fc);
    a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * w / sr);
}
double HaloEngine::OnePoleLP64::process (double x) noexcept
{
    z += a * (x - z);
    return z;
}
void HaloEngine::OnePoleHP64::setCutoff (double fc, double sr) noexcept
{
    const double w = juce::jlimit (1.0, sr * 0.49, fc);
    a = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * w / sr);
}
double HaloEngine::OnePoleHP64::process (double x) noexcept
{
    // HP = x − LP(x).
    z += a * (x - z);
    return x - z;
}

// ── Mapeos perceptuales públicos (única fuente de verdad con los tests) ─────────────────────────────────
float HaloEngine::feedbackForRegen (float decay01) noexcept
{
    return logLerp (kRegenMin, kRegenMax, juce::jlimit (0.0f, 1.0f, decay01));
}

float HaloEngine::loopCutoffForTone (float tone01) noexcept
{
    return logLerp (kToneHiHz, kToneLoHz, juce::jlimit (0.0f, 1.0f, tone01));
}

float HaloEngine::estimatedRt60Seconds (float decay01, float shimmer01) noexcept
{
    // Piso REAL: el difusor glacial corre con decay interno fijo (una pasada dura t60ForDecay(kFdnDecay01)
    // ≈ 9.4 s aunque DECAY esté a 0). El lazo (regen·shimmer) lo estira por encima. Término de bloom
    // (1 + 1.9·(d·s)²) CALIBRADO contra el T60 medido por [honestidad][halo] (ver HaloEngine.h).
    const float bed = ovni::engines::FdnReverb::t60ForDecay (kFdnDecay01);
    const float ds  = juce::jlimit (0.0f, 1.0f, decay01) * juce::jlimit (0.0f, 1.0f, shimmer01);
    return bed * (1.0f + 1.9f * ds * ds);
}

// ── prepare / reset ─────────────────────────────────────────────────────────────────────────────────
void HaloEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = (spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0);
    maxBlock   = (int) juce::jmax<juce::uint32> (1, spec.maximumBlockSize);
    numCh      = (int) juce::jmax<juce::uint32> (1, spec.numChannels);

    fdn.prepare (spec);

    // OS 4× (factor log2 = 2 → 2^2 = 4×) FIR equiripple → stopband profundo (anti-alias del pitch,
    // house-standard §1). Envuelve SOLO el pitch (mono-sumado a estéreo: 2 canales). Su latencia queda
    // DENTRO del lazo (carácter). processSamplesUp devuelve kOsRatio·maxBlock samples → el pitch se prepara
    // al RATE/BLOQUE sobremuestreados (si no, escribe fuera de sus buffers → crash).
    constexpr size_t kOsLog2  = 2;
    constexpr int    kOsRatio = 4;            // 2^kOsLog2
    os = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) 2, kOsLog2,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        /*maxQuality*/ true, /*integerLatency*/ false);
    os->initProcessing ((size_t) maxBlock);
    os->reset();

    // Pitch con grano 90 ms (sidebands suaves), preparado al dominio SOBREMUESTREADO (corre DENTRO del OS).
    // 2 voces (octava + quinta) FIJAS. El grano en ms es invariante al SR → 90 ms a 4× SR = 4× samples (ok).
    const juce::dsp::ProcessSpec osSpec { sampleRate * (double) kOsRatio,
                                          (juce::uint32) (maxBlock * kOsRatio), 2 };
    pitch.prepare (osSpec, 2, kGrainMs);
    {
        float voices[2] = { kVoiceOctave, kVoiceFifth };
        pitch.setVoices (voices, 2);
    }

    // Pre-delay: capacidad para el máximo (120 ms) + holgura.
    const int preMax = (int) std::ceil (kPreDelayMaxMs * 0.001 * sampleRate) + 4;
    preDelayL.setMaximumDelayInSamples (preMax);
    preDelayR.setMaximumDelayInSamples (preMax);
    const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlock, 1 };
    preDelayL.prepare (monoSpec);
    preDelayR.prepare (monoSpec);
    preDelaySmL.reset (0.0f);
    preDelaySmR.reset (0.0f);

    bloomL.prepare (sampleRate);
    bloomR.prepare (sampleRate);
    bloomL.setCoefficient (0.55f);
    bloomR.setCoefficient (0.55f);

    // Difusión de la excitación (por canal): densifica la entrada al lazo sin tocar el retorno.
    exDiffL.prepare (sampleRate);
    exDiffR.prepare (sampleRate);

    // LOW CUT (HP del wet de entrada a la cola): estéreo (2 canales), bloque-procesa el wetBuf. Default = 20 Hz
    // (≈apagado → transparente). El de-zipper del cutoff vive adentro del LowCut (TPT, sin clicks al modular).
    {
        const juce::dsp::ProcessSpec lcSpec { sampleRate, (juce::uint32) maxBlock, 2 };
        lowCut.prepare (lcSpec);
        // HI CUT (LP del wet de la cola): mismo spec estéreo, encadenado DESPUÉS del LowCut sobre el mismo wet.
        // Default = 20 kHz (≈off → transparente). De-zipper del cutoff interno (TPT, sin clicks al modular).
        hiCut.prepare (lcSpec);
    }

    // Trajectory: conduce el azimut de la órbita (lenta/glacial). El SYNC engancha su freeHz al tempo por
    // bloque (ver process()); acá fijamos la geometría base (Ellipse/spread/chaos/CCW, freeHz orgánico).
    traj.prepare (sampleRate);
    {
        ovni::engines::TrajectoryParams tp;
        tp.shape    = ovni::engines::Ellipse;
        tp.rate     = ovni::engines::Free;
        tp.radius01 = kSpatialRadius01;
        tp.spread01 = kTrajSpread01;
        tp.chaos01  = kTrajChaos01;
        tp.dir      = ovni::engines::CCW;
        tp.freeHz   = kTrajFreeHz;
        traj.setParams (tp);
    }

    // ÓRBITA por ITD+ILD sobre el wash estéreo (FIX envolvimiento). Líneas de delay interaural (Lagrange3rd,
    // continuo) + crossover bass-mono para IN PHASE. ITD máx = 0.7 ms (rango interaural humano).
    {
        const juce::dsp::ProcessSpec monoSpecOrbit { sampleRate, (juce::uint32) maxBlock, 1 };
        orbitItdMaxSamp = (float) (0.0007 * sampleRate);                 // 0.7 ms
        const int itdCap = (int) std::ceil (orbitItdMaxSamp) + 8;
        orbitItdL.setMaximumDelayInSamples (itdCap);
        orbitItdR.setMaximumDelayInSamples (itdCap);
        orbitItdL.prepare (monoSpecOrbit);
        orbitItdR.prepare (monoSpecOrbit);
        orbitBassCoef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 250.0 / sampleRate);
        orbitBassLpL = orbitBassLpR = 0.0f;
        // Coef del LP 1-polo del corner del shelf de sombra de cabeza (fijo; el SHELF GAIN es lo que se modula).
        orbitShelfCoef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * (double) kHeadShadowHz / sampleRate);
        orbitShelfLpL = orbitShelfLpR = 0.0f;
        orbitGLsm = orbitGRsm = 1.0f;
        orbitDLsm = orbitDRsm = 0.0f;
        orbitHLsm = orbitHRsm = 1.0f;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        loopLP[(size_t) ch].setCutoff (loopCutoffForTone (0.45f), sampleRate);
        loopLP[(size_t) ch].reset();
        loopHP[(size_t) ch].setCutoff (kLoopHpHz, sampleRate);
        loopHP[(size_t) ch].reset();
        outHP[(size_t) ch].setCutoff (kLoopHpHz, sampleRate);   // EQ del camino pre-pitch (blend SHIMMER)
        outHP[(size_t) ch].reset();
        outLP[(size_t) ch].reset();                             // su cutoff sigue a TONE por-sample en (d)
        shimmerReturn[(size_t) ch].assign ((size_t) juce::jmax (1, maxBlock), 0.0);
    }

    wetBuf.setSize (2, juce::jmax (1, maxBlock));
    wetBuf.clear();
    spatialMix.setSize (2, juce::jmax (1, maxBlock));
    spatialMix.clear();
    prePitch.setSize (2, juce::jmax (1, maxBlock));
    prePitch.clear();

    const double sr = sampleRate;
    shimmerSm.reset (sr, kSmoothMs * 0.001);
    regenSm  .reset (sr, kSmoothMs * 0.001);
    mixSm    .reset (sr, kSmoothMs * 0.001);
    inputSm  .reset (1.0f);   // LinearRamp: estado inicial = entrada plena (no FREEZE)
    loopCutSm.reset (sr, kSmoothMs * 0.001);

    outLimiter.prepare (sr);
    // Techo true-peak-safe del sello (anti-clip inter-sample; ver references/anti-click-clip-truepeak.md §3).
    outLimiter.setTuning ({ /*ceiling*/ 0.85f, /*releaseMs*/ 80.0f });

    firstBlock  = true;
    lastLoopRms = 0.0f;
    reset();
}

void HaloEngine::reset() noexcept
{
    fdn.reset();
    pitch.reset();
    if (os) os->reset();
    preDelayL.reset();
    preDelayR.reset();
    bloomL.reset();
    bloomR.reset();
    exDiffL.reset();
    exDiffR.reset();
    lowCut.reset();
    hiCut.reset();
    traj.reset();
    orbitItdL.reset();
    orbitItdR.reset();
    orbitBassLpL = orbitBassLpR = 0.0f;
    orbitShelfLpL = orbitShelfLpR = 0.0f;
    orbitGLsm = orbitGRsm = 1.0f;
    orbitDLsm = orbitDRsm = 0.0f;
    orbitHLsm = orbitHRsm = 1.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        loopLP[(size_t) ch].reset();
        loopHP[(size_t) ch].reset();
        outLP[(size_t) ch].reset();
        outHP[(size_t) ch].reset();
        std::fill (shimmerReturn[(size_t) ch].begin(), shimmerReturn[(size_t) ch].end(), 0.0);
    }
    wetBuf.clear();
    spatialMix.clear();
    prePitch.clear();
    outLimiter.reset();
    firstBlock  = true;
    lastLoopRms = 0.0f;
}

// ── process ───────────────────────────────────────────────────────────────────────────────────────
void HaloEngine::process (juce::AudioBuffer<float>& buffer, const HaloParams& p,
                          const ovni::engines::TransportInfo& transport) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    const int n     = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();
    if (n <= 0 || bufCh <= 0) return;

    // ── Destinos de los smoothers (de-zipper DESPUÉS del mapeo). En FREEZE: feedback=1.0, input→0. ─────
    const float shimmerTgt = juce::jlimit (0.0f, 1.0f, p.shimmer01);
    const float regenTgt   = p.freeze ? 1.0f : feedbackForRegen (p.decay01);
    const float mixTgt     = juce::jlimit (0.0f, 1.0f, p.mix01);
    const float inputTgt   = p.freeze ? 0.0f : 1.0f;             // FREEZE: corta la entrada al lazo (xfade)
    const float cutTgt     = p.freeze ? kToneHiHz                // FREEZE: damping=0 (LP abierto, no drena)
                                      : loopCutoffForTone (p.tone01);

    // Pre-delay objetivo (samples) derivado de SIZE.
    const float preMs   = logLerp (kPreDelayMinMs, kPreDelayMaxMs, juce::jlimit (0.0f, 1.0f, p.size01));
    const float preSamp = preMs * 0.001f * (float) sampleRate;

    if (firstBlock)
    {
        shimmerSm.setCurrentAndTargetValue (shimmerTgt);
        regenSm  .setCurrentAndTargetValue (regenTgt);
        mixSm    .setCurrentAndTargetValue (mixTgt);
        inputSm  .reset (inputTgt);
        loopCutSm.setCurrentAndTargetValue (cutTgt);
        preDelaySmL.reset (preSamp);
        preDelaySmR.reset (preSamp);
        firstBlock = false;
    }
    shimmerSm.setTargetValue (shimmerTgt);
    regenSm  .setTargetValue (regenTgt);
    mixSm    .setTargetValue (mixTgt);
    loopCutSm.setTargetValue (cutTgt);

    // Guardas si el bloque viniera más grande que el reservado (no debería en RT).
    if ((int) shimmerReturn[0].size() < n)
        for (int ch = 0; ch < 2; ++ch) shimmerReturn[(size_t) ch].assign ((size_t) n, 0.0);
    if (wetBuf.getNumSamples()    < n) wetBuf.setSize    (2, n, false, false, true);
    if (spatialMix.getNumSamples()< n) spatialMix.setSize(2, n, false, false, true);
    if (prePitch.getNumSamples()  < n) prePitch.setSize  (2, n, false, false, true);

    auto* inL  = buffer.getReadPointer (0);
    auto* inR  = bufCh > 1 ? buffer.getReadPointer (1) : inL;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = bufCh > 1 ? buffer.getWritePointer (1) : nullptr;

    // Pasos del input-gain del FREEZE (xfade por-sample del input al lazo).
    const float inputStep = inputSm.stepTo (inputTgt, n);

    // (a) Excitación del difusor en wetBuf = inputGain·(PRE-DELAY+BLOOM del dry) + DECAY·shimmerReturn(prev).
    //     El pre-delay + bloom crean el vacío inicial y el ataque lento (base técnica §4). float64 en el
    //     término de feedback (lazo). En FREEZE el inputGain→0 → la nube circula sola.
    {
        float* exL = wetBuf.getWritePointer (0);
        float* exR = wetBuf.getWritePointer (1);
        const double* retL = shimmerReturn[0].data();
        const double* retR = shimmerReturn[1].data();

        // Pre-delay rampeado (de-zipper del largo) + bloom, por canal, en buffers temporales (los exX).
        const float preStepL = preDelaySmL.stepTo (preSamp, n);
        const float preStepR = preDelaySmR.stepTo (preSamp, n);
        for (int i = 0; i < n; ++i)
        {
            const float dL = inL[i];
            const float dR = (outR != nullptr ? inR[i] : inL[i]);
            preDelayL.pushSample (0, dL);
            preDelayR.pushSample (0, dR);
            exL[i] = preDelayL.popSample (0, preDelaySmL.value(), true);
            exR[i] = preDelayR.popSample (0, preDelaySmR.value(), true);
            preDelaySmL.advance (preStepL);
            preDelaySmR.advance (preStepR);
        }
        bloomL.processMono (exL, n);
        bloomR.processMono (exR, n);

        // Difusión de la EXCITACIÓN (fix densidad 2026-07-02): 4 allpass Schroeder por canal sobre el dry
        // pre-delayed+bloom, ANTES de sumar el retorno del shimmer → la cola arranca densa (sin el "flutter"
        // de ecos discretos) SIN re-difundir el lazo en cada vuelta (la escalera de octavas queda intacta;
        // por eso el FDN de abajo corre con inputDiffusion=false).
        for (int i = 0; i < n; ++i)
        {
            exL[i] = exDiffL.process (exL[i]);
            exR[i] = exDiffR.process (exR[i]);
        }

        // LOW CUT + HI CUT: NO van acá (entrada del lazo). El pitch-shifter (octava+quinta UP, sección c) REGENERA
        // agudos DENTRO del lazo en cada vuelta → filtrar la ENTRADA no se oye (la regeneración lo enmascara;
        // medido: HI CUT en la entrada con SHIMMER=0.6 sólo daba −1.6 dB en la banda alta). Se aplican en la
        // SALIDA del wet, post-pitch, en la sección (d2) — ahí esculpen la cola que realmente sale.

        // Sumar el input (con xfade de FREEZE) + el retorno del lazo (DECAY·shimmerReturn).
        for (int i = 0; i < n; ++i)
        {
            const float regen = regenSm.getNextValue();
            const float ig    = inputSm.value();
            exL[i] = ig * exL[i] + (float) (regen * retL[i]);
            exR[i] = ig * exR[i] + (float) (regen * retR[i]);
            inputSm.advance (inputStep);
        }
    }

    // (b) Difusor FDN al 100% wet. BREATH interno fijo (base técnica §7); en FREEZE modDepth=0. monoSafe lo
    //     maneja HALO al final (sobre el wet total). breathRateHz: en SYNC lo deriva el processor de orbitRate.
    //     FREEZE (base técnica §6): el difusor TAMBIÉN se congela (freeze=true + decay01=1.0 → g_i=1.0 exacto,
    //     absorción en bypass) → junto con el feedback=1.0 del lazo, la nube sostiene de verdad (no decae).
    fdn.process (wetBuf, ovni::engines::FdnParams {
        .size01       = juce::jlimit (0.0f, 1.0f, p.size01),
        .decay01      = p.freeze ? 1.0f : kFdnDecay01,
        .tone01       = kFdnTone01,
        .breath01     = p.freeze ? 0.0f : juce::jlimit (0.0f, 1.0f, p.breath01),
        .mix01        = 1.0f,
        .freeze       = p.freeze,
        .monoSafe     = false,
        .breathRateHz = p.orbitRateHz,
        .inputDiffusion = false });   // el lazo NO se re-difunde por vuelta: HALO difunde su excitación (a)

    // snapshot PRE-pitch del wet (para el blend honesto de SHIMMER en (d): a shimmer bajo la salida
    // se acerca a esta reverb "a secas"; el lazo no lo usa — sigue 100% post-pitch).
    for (int ch = 0; ch < 2; ++ch)
        prePitch.copyFrom (ch, 0, wetBuf, ch, 0, n);

    // (c) Pitch-shift granular poly DENTRO del lazo (octava + quinta FIJAS), con OS 4× LOCAL (anti-alias del
    //     pitch, house-standard §1). El OS sube×4 → pitch.process → baja×4. Su latencia queda DENTRO del
    //     lazo (carácter). Diagnóstico [alias]: osEnabled=false saltea el OS para medir el alias crudo.
    if (osEnabled && os != nullptr)
    {
        juce::dsp::AudioBlock<float> block (wetBuf.getArrayOfWritePointers(), (size_t) 2, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int upN = (int) up.getNumSamples();
        // Empaquetar el bloque sobremuestreado en un AudioBuffer para el pitch (sin copia: usa los punteros).
        float* upPtrs[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
        juce::AudioBuffer<float> upBuf (upPtrs, 2, upN);
        pitch.process (upBuf);
        os->processSamplesDown (block);
    }
    else
    {
        pitch.process (wetBuf);
    }

    // (d) Band EQ del lazo (LP por TONE + HP DC blocker) por-sample sobre wetBuf, en float64 (lazo). Construir
    //     shimmerReturn(next) = SHIMMER · (cola pitched + EQ). Acá medimos la RMS del lazo (telemetría). El LP
    //     es la clave anti-divergencia (drena la energía HF que el pitch-up acumula). En FREEZE el LP queda
    //     abierto (damping=0) → la nube no se oscurece.
    {
        float* wL = wetBuf.getWritePointer (0);
        float* wR = wetBuf.getWritePointer (1);
        double* retL = shimmerReturn[0].data();
        double* retR = shimmerReturn[1].data();
        const float* preL = prePitch.getReadPointer (0);
        const float* preR = prePitch.getReadPointer (1);
        double acc = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double cutoff  = (double) loopCutSm.getNextValue();
            const float  shimmer = shimmerSm.getNextValue();
            loopLP[0].setCutoff (cutoff, sampleRate);
            loopLP[1].setCutoff (cutoff, sampleRate);
            outLP[0].setCutoff (cutoff, sampleRate);   // el camino pre-pitch sigue el MISMO TONE
            outLP[1].setCutoff (cutoff, sampleRate);

            const double eL = loopHP[0].process (loopLP[0].process ((double) wL[i]));
            const double eR = loopHP[1].process (loopLP[1].process ((double) wR[i]));

            // Camino PRE-pitch EQ'd (reverb a secas) — filtros PROPIOS (estado aparte del lazo).
            const double pL = outHP[0].process (outLP[0].process ((double) preL[i]));
            const double pR = outHP[1].process (outLP[1].process ((double) preR[i]));

            // wetBuf pasa a contener la cola EQ que va al spatializer/MIX: blend HONESTO por SHIMMER
            // (w=1 desde el default hacia arriba → idéntico a la salida histórica; w→0 = a secas).
            // shimmerReturn = SHIMMER·EQ(post-pitch) SIN cambios (el lazo/estabilidad no se tocan).
            const double w = (double) juce::jmin (1.0f, shimmer * (1.0f / kShimmerFullOut));
            wL[i] = (float) ((1.0 - w) * pL + w * eL);
            wR[i] = (float) ((1.0 - w) * pR + w * eR);
            retL[i] = (double) shimmer * eL;
            retR[i] = (double) shimmer * eR;

            acc += eL * eL + eR * eR;
        }
        lastLoopRms = (float) std::sqrt (acc / juce::jmax (1, 2 * n));
    }

    // (LOW CUT + HI CUT se aplican al FINAL, sobre la salida COMPLETA dry+wet — ver sección (g) antes del limiter.
    //  Razón medida: filtrar solo el wet es imperceptible a MIX bajo porque el dry full-range tapa el filtrado.
    //  Como "filtro" del plugin, cortan TODO lo que sale → se oye a cualquier MIX.)

    // (e) ORBITAR el wet EQ — FUERA del lazo (clave anti-divergencia R1). El shimmerReturn ya quedó armado del
    //     wet EQ pre-orbit en (d). ORBIT = cuánto te ENVUELVE + orbita (NUNCA un punto que angosta):
    //
    //     El viejo camino sumaba el wash a mono (SpatialEngine) → más ORBIT = más ANGOSTO (bug que Joaquín
    //     cazó con Insight: goniómetro casi mono al centro con ORBIT=100%). Acá NO sumamos a mono: paneamos el
    //     wash ANCHO COMPLETO alrededor de la cabeza con los DOS cues binaurales reales —ITD (delay
    //     interaural) + ILD (nivel interaural DEPENDIENTE DE FRECUENCIA: sombra de cabeza HF @1.6 kHz + pizca
    //     plana)— modulados por el azimut θ de la trayectoria. Como el wash es
    //     decorrelado (L/R del FDN), atenuar/retrasar UN canal NO lo angosta (los términos cruzados ≈0 →
    //     side/mid se mantiene): conserva el ancho/IACC bajo y ADEMÁS orbita. ORBIT = profundidad del pan:
    //     0 = wash quieto y ancho; 1 = órbita plena (el wash gira alrededor de la cabeza). NUNCA mono. IN
    //     PHASE (monoSafe) apaga el pan + fuerza graves a mono → colapsa seguro.
    //
    //     FIX SYNC: la órbita es el elemento rítmico natural de HALO. orbitRateHz>0 (SYNC on) → la velocidad
    //     orbital se engancha al tempo (freeHz = BPM·división); ≤0 (FREE) → 0.08 Hz orgánico. Mismo patrón que
    //     PULSAR (computar Hz del BPM·división y alimentar la trayectoria). Cambiar la división cambia
    //     AUDIBLEMENTE la velocidad de rotación.
    {
        // Rate de la órbita: SYNC engancha freeHz al tempo; FREE = orgánico. Se re-setea por bloque (banca
        // cambios de BPM/división en vivo) manteniendo el resto de la geometría (Ellipse/spread/chaos/CCW).
        ovni::engines::TrajectoryParams tp;
        tp.shape    = ovni::engines::Ellipse;
        tp.rate     = ovni::engines::Free;
        tp.radius01 = kSpatialRadius01;
        tp.spread01 = kTrajSpread01;
        tp.chaos01  = kTrajChaos01;
        tp.dir      = ovni::engines::CCW;
        tp.freeHz   = (p.orbitRateHz > 0.0f) ? p.orbitRateHz : kTrajFreeHz;
        traj.setParams (tp);

        const auto  tgt   = traj.advance (n, transport);
        const float orbit = juce::jlimit (0.0f, 1.0f, p.orbit01);
        const float s     = std::sin (tgt.azimuth);   // +1 = fuente a la IZQUIERDA, -1 = a la DERECHA

        // Profundidad del pan ∝ ORBIT (en IN PHASE el pan se apaga → wash sin tocar, mono-safe).
        // ILD = SOMBRA DE CABEZA DEPENDIENTE DE FRECUENCIA (upgrade binaural honesto, reemplaza la atenuación
        // PLANA de banda ancha original). Una cabeza real casi no atenúa graves (los rodean) pero sombrea
        // fuerte los agudos. Por eso el oído LEJANO se trata en DOS términos:
        //   (1) sombra HF DOMINANTE: la transmisión HF del shelf (1 = sin sombra) baja con el azimut hasta
        //       1−kHeadShadowDepth (≈ −14 dB de HF en el extremo). Sólo toca el residuo HF (x − LP@1.6kHz);
        //       los graves pasan a UNIDAD → el cuerpo del wash no se adelgaza ni se angosta en graves.
        //   (2) atenuación PLANA residual chica (cue de nivel global real, no descompensa): hasta ≈ −1.6 dB.
        // ITD igual que antes (hasta 0.7 ms). Atenúa-sólo (gains/transmisión ≤ 1) → clip-safe.
        constexpr float kIldFlatDepth   = 0.18f;       // atenuación plana residual del oído lejano (≈ −1.6 dB máx)
        constexpr float kHeadShadowDepth = 0.80f;      // sombra HF máx del oído lejano (transmisión HF baja a 0.20 ≈ −14 dB)
        const float depth   = p.monoSafe ? 0.0f : orbit;
        const float flatAmt = depth * kIldFlatDepth;
        const float shadAmt = depth * kHeadShadowDepth;
        const float itdAmt  = depth * orbitItdMaxSamp;
        // Ipsilateral = sin tocar; contralateral = atenuado (plano + sombra HF) + retrasado. s>0 (izquierda) →
        // el contralateral es la DERECHA. Destinos del bloque (de-zipper por-sample de gains, delays y shelf).
        const float farL  = juce::jmax (0.0f, -s);    // cuánto es L el oído LEJANO (fuente a la derecha)
        const float farR  = juce::jmax (0.0f,  s);    // cuánto es R el oído LEJANO (fuente a la izquierda)
        const float gLtgt = 1.0f - flatAmt * farL;    // ganancia plana residual del L
        const float gRtgt = 1.0f - flatAmt * farR;    // ganancia plana residual del R
        const float hLtgt = 1.0f - shadAmt * farL;    // transmisión HF del L (1 = sin sombra)
        const float hRtgt = 1.0f - shadAmt * farR;    // transmisión HF del R (1 = sin sombra)
        const float dLtgt = itdAmt * farL;            // retrasa L si la fuente va a la derecha
        const float dRtgt = itdAmt * farR;            // retrasa R si la fuente va a la izquierda

        const float* wL = wetBuf.getReadPointer (0);
        const float* wR = wetBuf.getReadPointer (1);
        float* mxL = spatialMix.getWritePointer (0);
        float* mxR = spatialMix.getWritePointer (1);

        // Rampa por-sample de ganancias, transmisión HF y delays (anti-click: el azimut puede moverse rápido).
        const float dgL = (gLtgt - orbitGLsm) / (float) n;
        const float dgR = (gRtgt - orbitGRsm) / (float) n;
        const float dhL = (hLtgt - orbitHLsm) / (float) n;
        const float dhR = (hRtgt - orbitHRsm) / (float) n;
        const float ddL = (dLtgt - orbitDLsm) / (float) n;
        const float ddR = (dRtgt - orbitDRsm) / (float) n;
        float gL = orbitGLsm, gR = orbitGRsm, hL = orbitHLsm, hR = orbitHRsm, dL = orbitDLsm, dR = orbitDRsm;
        for (int i = 0; i < n; ++i)
        {
            orbitItdL.pushSample (0, wL[i]);
            orbitItdR.pushSample (0, wR[i]);
            orbitItdL.setDelay (juce::jlimit (0.0f, orbitItdMaxSamp, dL));
            orbitItdR.setDelay (juce::jlimit (0.0f, orbitItdMaxSamp, dR));
            float oL = orbitItdL.popSample (0);
            float oR = orbitItdR.popSample (0);

            // SOMBRA DE CABEZA (high-shelf 1-polo): LP@1.6kHz separa graves (intactos) del residuo HF. El LP
            // corre SIEMPRE (continuidad/toggle sin click); el residuo HF se atenúa por la transmisión hL/hR.
            // hL/hR=1 → identidad (graves + HF intactos). Atenúa-sólo el HF del oído lejano.
            orbitShelfLpL += orbitShelfCoef * (oL - orbitShelfLpL);
            orbitShelfLpR += orbitShelfCoef * (oR - orbitShelfLpR);
            oL = orbitShelfLpL + hL * (oL - orbitShelfLpL);
            oR = orbitShelfLpR + hR * (oR - orbitShelfLpR);

            // Atenuación PLANA residual (cue de nivel global).
            oL *= gL;
            oR *= gR;

            // IN PHASE (mono-safe): colapsa el wet de BANDA COMPLETA a mono (CORR→1, mono-compatible real).
            // El bass-mono solo (graves <250 Hz) dejaba medios/agudos decorrelados → al sumar a mono cancelaba
            // y la CORR de banda completa se quedaba en ~0.36 (NO mono-safe). El gate del sello exige CORR≥0.95:
            // con IN PHASE el wash deja de orbitar (pan ya apagado arriba) y L=R en TODA la banda → seguro al
            // sumar a mono (club/vinilo). El 1-polo de graves se mantiene de estado (continuidad/sin click).
            orbitBassLpL += orbitBassCoef * (oL - orbitBassLpL);
            orbitBassLpR += orbitBassCoef * (oR - orbitBassLpR);
            if (p.monoSafe)
            {
                const float mono = 0.5f * (oL + oR);
                oL = mono;
                oR = mono;
            }

            mxL[i] = oL;
            mxR[i] = oR;
            gL += dgL; gR += dgR; hL += dhL; hR += dhR; dL += ddL; dR += ddR;
        }
        orbitGLsm = gLtgt; orbitGRsm = gRtgt; orbitHLsm = hLtgt; orbitHRsm = hRtgt;
        orbitDLsm = dLtgt; orbitDRsm = dRtgt;
    }

    // (f) Mezcla dry/wet por ley de potencia. El DRY sale de 'buffer' (inL/inR siguen apuntando a él); el WET
    //     es spatialMix (la cola pitched+EQ: wash ancho + par binaural decorrelado orbitando). El dry NO se
    //     espacializa (azimut 0, al frente). Luego limiter de salida estéreo-linked.
    {
        const float* wL = spatialMix.getReadPointer (0);
        const float* wR = spatialMix.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mix  = mixSm.getNextValue();
            const float wetG = std::sin (mix * juce::MathConstants<float>::halfPi);
            const float dryG = std::cos (mix * juce::MathConstants<float>::halfPi);

            const float wetL = wL[i];
            const float wetR = (outR != nullptr) ? wR[i] : wL[i];

            const float dL = inL[i];
            const float dR = (outR != nullptr) ? inR[i] : inL[i];

            outL[i] = dryG * dL + wetG * wetL;
            if (outR != nullptr) outR[i] = dryG * dR + wetG * wetR;
        }
    }

    // (g) LOW CUT + HI CUT sobre la SALIDA COMPLETA (dry+wet ya mezclados), como un filtro pasa-altos/pasa-bajos
    //     del plugin: cortan los graves/agudos de TODO lo que sale, no solo el wet → el corte se OYE a cualquier
    //     MIX. (Medido: filtrar solo el wet daba −0.5 dB en la salida a MIX 40% = imperceptible; el dry full-range
    //     lo tapaba.) Default off → transparente. TPT modulación-safe. VAN ANTES del limiter (caza cualquier pico
    //     que el filtro pudiera introducir).
    {
        const int chs = (outR != nullptr) ? 2 : 1;
        float* oPtrs[2] = { outL, (outR != nullptr ? outR : outL) };
        juce::AudioBuffer<float> oView (oPtrs, chs, n);
        // BYPASS-POR-FILTRO cuando ==0 (patrón NEBULA; FIX del null 2026-07-02): el LowCut corría SIEMPRE
        // (a 20 Hz en default) → rotaba la fase del sub del DRY+WET a cualquier MIX (el null a MIX=0 medía
        // −25.7 dB, dominado por 20-150 Hz, en vez de dry intacto). Con el guard, default 0 % = NO corre
        // ningún filtro = salida bit-intacta (misma semántica que el HI CUT de abajo y que NEBULA/PULSAR).
        const float lowCutAmt = juce::jlimit (0.0f, 1.0f, p.lowCut01);
        if (lowCutAmt > 0.0f)
        {
            lowCut.setCutoffHz (ovni::dsp::LowCut::hzFor01 (lowCutAmt));
            lowCut.process (oView);
        }
        const float hiCutAmt = juce::jlimit (0.0f, 1.0f, p.hiCut01);
        if (hiCutAmt > 0.0f)
        {
            hiCut.setCutoffHz (ovni::dsp::HiCut::hzFor01 (hiCutAmt));
            hiCut.process (oView);
        }
    }

    // Limiter de salida estéreo-linked (anti-clip de la suma dry+wet; techo del sello, latencia 0).
    if (outR != nullptr) outLimiter.process (outL, outR, n);
    else                 outLimiter.process (outL, outL, n);
}

} // namespace halo
