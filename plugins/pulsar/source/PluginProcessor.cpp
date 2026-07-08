#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include <cmath>

namespace pulsar
{

namespace pid = pulsar::params::id;

using APF   = juce::AudioParameterFloat;
using APB   = juce::AudioParameterBool;
using Range = juce::NormalisableRange<float>;

// ----------------------------------------------------------------------------- layout
juce::AudioProcessorValueTreeState::ParameterLayout PulsarProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static auto pct = [] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; };
    static auto hz  = [] (float v, int) { return juce::String (v, v < 1.0f ? 2 : 1) + " Hz"; };   // "0.50 Hz" / "10.0 Hz"

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MOTION, 1 }, "Motion",
        Range { 0.f, 100.f, 0.01f }, 45.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // RATE: free = Hz (skew). El editor muestra Hz o la división según SYNC.
    Range rateHz { 0.01f, 10.f, 0.0001f }; rateHz.setSkewForCentre (0.5f);
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::RATE, 1 }, "Rate", rateHz, 0.5f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (hz).withLabel ("Hz")));
    layout.add (std::make_unique<APB> (juce::ParameterID { pid::SYNC, 1 }, "Sync", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::DIVISION, 1 }, "Division", pulsar::params::sync::labels(), 2)); // def "1 bar"

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SHAPE, 1 }, "Shape",
        Range { 0.f, 100.f, 0.01f }, 66.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::SMEAR, 1 }, "Smear",
        Range { 0.f, 100.f, 0.01f }, 25.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::WIDTH, 1 }, "Width",
        Range { 0.f, 100.f, 0.01f }, 70.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    // Campo (lo setea el visual; rango simétrico, default centro).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::FIELDX, 1 }, "Field X",
        Range { -1.f, 1.f, 0.001f }, 0.f));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::FIELDY, 1 }, "Field Y",
        Range { -1.f, 1.f, 0.001f }, 0.f));

    layout.add (std::make_unique<APF> (juce::ParameterID { pid::MIX, 1 }, "Mix",
        Range { 0.f, 100.f, 0.01f }, 100.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));

    layout.add (std::make_unique<APB> (juce::ParameterID { pid::BYPASS, 1 }, "Bypass", false));

    // PRO: filtros de tono de SALIDA (append-only). Default 0 % = apagado → transparente (bit-exact: en 0
    // no corre ningún filtro). LOW CUT = HPF (limpia graves), HI CUT = LPF (oscurece). %→0..1→Hz (log).
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::LOWCUT, 1 }, "Low Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    layout.add (std::make_unique<APF> (juce::ParameterID { pid::HICUT, 1 }, "Hi Cut",
        Range { 0.f, 100.f, 0.01f }, 0.f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct).withLabel ("%")));
    return layout;
}

// ----------------------------------------------------------------------------- ctor
PulsarProcessor::PulsarProcessor()
    : ovni::PluginProcessorBase ("PULSAR", createParameterLayout())
{
    pMotion   = apvts.getRawParameterValue (pid::MOTION);
    pRate     = apvts.getRawParameterValue (pid::RATE);
    pSync     = apvts.getRawParameterValue (pid::SYNC);
    pDivision = apvts.getRawParameterValue (pid::DIVISION);
    pMix      = apvts.getRawParameterValue (pid::MIX);
    pShape    = apvts.getRawParameterValue (pid::SHAPE);
    pSmear    = apvts.getRawParameterValue (pid::SMEAR);
    pWidth    = apvts.getRawParameterValue (pid::WIDTH);
    pLowCut   = apvts.getRawParameterValue (pid::LOWCUT);
    pHiCut    = apvts.getRawParameterValue (pid::HICUT);
    pFieldX   = apvts.getRawParameterValue (pid::FIELDX);
    pFieldY   = apvts.getRawParameterValue (pid::FIELDY);

    // Tuning del motor BINAURAL (heredado de ÓRBITA, validado anti-clip). Llamar ANTES de prepare:
    // estos setters recalculan coeficientes (shelf near-field, dimensiones de las líneas de delay).
    //   Doppler: misma geometría que ÓRBITA (el fly-by produce pitch al variar el delay, latencia 0).
    //   NearField: perfil "Pro" por defecto (cue físico real, timbre limpio, headroom seguro).
    //   Limiter: ceiling 0.85 (~-1.4 dB) -> margen true-peak; red de seguridad, casi no trabaja.
    engine.setDoppler   ({ /*maxAmpMeters*/ 3.0f, /*minSafeSamples*/ 2.0f, /*maxSlew*/ 0.20f });
    engine.setNearField ({});                               // default Pro (bass+ILD near-field, normalize=on)
    engine.setLimiter   ({ /*ceiling*/ 0.85f, /*releaseMs*/ 60.0f });
}

juce::AudioProcessorEditor* PulsarProcessor::createEditor()
{
    return new PulsarEditor (*this);
}

void PulsarProcessor::prepareEngine (const juce::dsp::ProcessSpec& spec)
{
    engine.prepare (spec);
    traj.prepare (spec.sampleRate);
    smear.prepare (spec.sampleRate, (int) spec.maximumBlockSize);
    lowCut.prepare (spec);
    hiCut.prepare (spec);
}

void PulsarProcessor::processAudio (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    // --- macros (RT-safe); % -> 0..1 ------------------------------------------------
    const float motion  = juce::jlimit (0.f, 1.f, (pMotion ? pMotion->load() : 45.f) * 0.01f);
    const float shape   = juce::jlimit (0.f, 1.f, (pShape  ? pShape->load()  : 66.f) * 0.01f);
    const float smr     = juce::jlimit (0.f, 1.f, (pSmear  ? pSmear->load()  : 25.f) * 0.01f);
    const float width01 = juce::jlimit (0.f, 1.f, (pWidth  ? pWidth->load()  : 70.f) * 0.01f);
    const float fX = pFieldX ? pFieldX->load() : 0.f;
    const float fY = pFieldY ? pFieldY->load() : 0.f;

    // --- RATE: free (Hz) o sync (division choice * BPM del host) --------------------
    float rateHz = pRate ? pRate->load() : 0.5f;
    if (pSync && pSync->load() >= 0.5f)
    {
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm()) bpm = *b;
        rateHz = syncRateHz (bpm);
    }

    // --- trayectoria del bloque (carácter de PULSAR) --------------------------------
    traj.setShape (shape);
    traj.setCenter (fX, fY);
    traj.advanceBlock (n, rateHz, motion);
    const float tx = traj.x();   // [-1,1] azimut
    const float ty = traj.y();   // [-1,1] frente-atrás

    // --- mapeo al motor BINAURAL HRIR (el que ABRE el estéreo de verdad, externalizado) ----------
    // L/R (crítico): el visual dibuja tx>0 a la DERECHA (cx + x); el SpatialEngine usa az>0 = IZQUIERDA
    // (s = sin(az), + = fuente a la izquierda). Negamos el azimut -> pantalla-derecha = canal derecho.
    // El HRIR coloca la fuente en el azimut GEOMÉTRICO completo (no escalado por WIDTH): WIDTH controla
    // el EJE espacial del wet por separado (width01 del engine), no el ángulo del HRIR.
    const float azimuthRad = -tx * juce::MathConstants<float>::halfPi;

    // WIDTH (PRIORIDAD: abrir fuerte, NO colapsar). El SpatialEngine width01: 0=mono, 0.5=natural,
    // 1=paneo "volador". Mapeamos WIDTH 0–100% -> 0.5–1.0 (de "natural" a "volador"): a 0% ya abre
    // (natural binaural), a 100% paneo dramático fuera de fase (ancho real). Nunca colapsa a mono.
    const float engWidth01 = 0.5f + 0.5f * width01;

    // ROOM (reflexiones tempranas = EXTERNALIZACIÓN, gran parte de la apertura): piso firme + algo de
    // MOTION. Siempre > 0 -> siempre externaliza ("fuera de la cabeza"), aún con MOTION=0. Moderado
    // (el bus de reflexiones ya trae kReflTrim interno) -> abre sin ensuciar ni bombear el limiter.
    const float room01 = juce::jlimit (0.f, 1.f, 0.30f + 0.25f * motion);

    // Distancia + Doppler ACOPLADOS a MOTION (el SpatialEngine hace 1/r + air + near-field + Doppler
    // internamente): radius01 = base estable (referencia del Doppler); distance01 = posición frente-atrás
    // VIVA de la trayectoria (su variación -> Doppler, su valor -> profundidad por DRR/1-r/near-field).
    const float baseDist01 = juce::jlimit (0.f, 1.f, 0.5f - 0.45f * ty * (0.3f + 0.7f * motion)); // ty arriba = cerca
    const float dop01      = juce::jlimit (0.f, 1.f, motion * (0.4f + 0.6f * std::abs (ty)));

    const float mix01 = juce::jlimit (0.f, 1.f, (pMix ? pMix->load() : 100.f) * 0.01f);

    engine.process (buffer, juce::jmax (1, getTotalNumInputChannels()),
        ovni::engines::SpatialEngine::EngineParams {
            .azimuthRad = azimuthRad, .mix01 = mix01, .room01 = room01,
            .width01 = engWidth01, .monoSafe = isMonoSafe(), .radius01 = 0.5f,
            .speakerMode = false, .distance01 = baseDist01, .doppler01 = dop01 });

    // --- ENSANCHADOR M/S (escalado por WIDTH): amplifica el SIDE que el HRIR ya creó -> abre DRAMÁTICO
    //     a WIDTH alto (fuera de fase a propósito = ancho real, medido). monoSafe -> k=1 (sin ensanchar).
    //     El limiter estéreo-linked del SMEAR (techo 0.85) que sigue contiene el pico extra del boost. ---
    if (! isMonoSafe() && buffer.getNumChannels() >= 2)
    {
        const float k = 1.0f + width01 * 4.0f;   // side x1 (WIDTH 0) .. x5 (WIDTH 100) — abre MÁS fuerte
        auto* wL = buffer.getWritePointer (0);
        auto* wR = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mid  = 0.5f * (wL[i] + wR[i]);
            const float side = 0.5f * (wL[i] - wR[i]) * k;
            wL[i] = mid + side;
            wR[i] = mid - side;
        }
    }

    // --- SMEAR: la estela sigue al azimut (post-binaural; tiene su propio anti-clip) --------------
    // FIX del null 2026-07-02: el SMEAR corre POST-mix sobre el buffer entero → a MIX=0 difuminaba el
    // DRY (null medía −14 dB en vez de dry intacto; el diccionario decía "mix=0 ~ dry", aproximado).
    // La estela es parte del WET: su cantidad escala por la MISMA ley de potencia del mix (sin(mix·π/2)).
    // A MIX=100 (el caso calibrado y el default de fábrica) sin(π/2)=1 → carácter EXACTAMENTE igual;
    // a MIX=0 el dry sale bit-intacto; en el medio la estela sigue al wet (continuo, sin escalón).
    const float smearWetG = std::sin (mix01 * juce::MathConstants<float>::halfPi);
    smear.process (buffer, smr * smearWetG, tx * width01);

    // --- PRO: filtros de tono de SALIDA (LOW CUT / HI CUT). Default 0 => skip (transparente, bit-exact) ---
    const float lowCut01 = juce::jlimit (0.f, 1.f, (pLowCut ? pLowCut->load() : 0.f) * 0.01f);
    const float hiCut01  = juce::jlimit (0.f, 1.f, (pHiCut  ? pHiCut->load()  : 0.f) * 0.01f);
    if (lowCut01 > 0.0001f) { lowCut.setCutoffHz (ovni::dsp::LowCut::hzFor01 (lowCut01)); lowCut.process (buffer); }
    if (hiCut01  > 0.0001f) { hiCut.setCutoffHz  (ovni::dsp::HiCut::hzFor01  (hiCut01));  hiCut.process  (buffer); }

    // --- telemetría del visual (lock-free) ------------------------------------------
    uiAzimuth.store  (tx * width01,  std::memory_order_relaxed);  // posición azimutal aparente (con WIDTH)
    uiDepth.store    (ty * width01,  std::memory_order_relaxed);
    uiHeat.store     (dop01,         std::memory_order_relaxed);
    uiDistance.store (baseDist01,    std::memory_order_relaxed);
    uiMotion.store   (motion,        std::memory_order_relaxed);
    uiShape.store    (shape,         std::memory_order_relaxed);
    uiSmear.store    (smr,           std::memory_order_relaxed);
}

float PulsarProcessor::extraClipPush() const
{
    // 1 = sin reducción; <1 = conteniendo. Lo mapeamos a 0..1 (empuje del LED).
    const float g = engine.lastLimiterGain();
    return juce::jlimit (0.0f, 1.0f, 1.0f - g);
}

float PulsarProcessor::debugSyncRateHz (double bpm) const { return syncRateHz (bpm); }

float PulsarProcessor::syncRateHz (double bpm) const
{
    namespace sd = pulsar::params::sync;
    const int idx = pDivision
        ? juce::jlimit (0, sd::kCount - 1, (int) std::round (pDivision->load()))
        : 2;
    const float beats = sd::divs[idx].beatsPerCycle;
    return (float) (bpm / 60.0) / juce::jmax (0.01f, beats);
}

} // namespace pulsar

// ===================================================================== entry point de JUCE
// Los wrappers de cada formato (AU / VST3 / Standalone) referencian este símbolo. Su ausencia
// era la causa del "ld: symbol(s) not found for architecture arm64" (3x, uno por formato).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new pulsar::PulsarProcessor();
}
