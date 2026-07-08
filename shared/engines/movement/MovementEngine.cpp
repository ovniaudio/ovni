#include "engines/movement/MovementEngine.h"
#include <cmath>
#include <algorithm>

namespace ovni::engines {

static constexpr double kTwoPi  = 6.283185307179586;
static constexpr double kSoundC = 343.0;                 // m/s
// Distancia (cercanía-lejanía): mapeo exponencial de distance01 a metros (igual que ÓRBITA).
static constexpr double kDMin = 0.15, kDMax = 15.0, kDRef = 1.5; // m: al-oído, lejos, crítica (1/r=1)

static inline double distanceFromNorm (float d01) noexcept
{
    const float c = d01 < 0.0f ? 0.0f : (d01 > 1.0f ? 1.0f : d01);
    return kDMin * std::pow (kDMax / kDMin, (double) c);
}

void MovementEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = (spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0);
    const int maxBlock = juce::jmax (1, (int) spec.maximumBlockSize);
    mono.setSize (1, maxBlock, false, false, true);

    // Doppler: dimensionar la línea de delay modulado. center = headroom (maxAmp + piso); max = 2*center.
    dopplerMaxAmpSamples = (float) (dopplerTune.maxAmpMeters * sampleRate / kSoundC);
    dopplerCenterSamples = dopplerMaxAmpSamples + dopplerTune.minSafeSamples;
    const juce::dsp::ProcessSpec monoSpec { sampleRate, spec.maximumBlockSize, 1 };
    dopplerLine.setMaximumDelayInSamples (juce::jmax (4, (int) std::ceil (2.0f * dopplerCenterSamples) + 4));
    dopplerLine.prepare (monoSpec);

    // ITD: escala (a/c)*sr (a = radio de cabeza ~8.75 cm) y dimensión de las líneas (piso + ITD máx a π/2).
    itdScaleSamples = (float) (0.0875 / kSoundC) * (float) sampleRate;
    const int itdMaxInt = juce::jmax (8, (int) std::ceil (kItdBaseSamples + itdScaleSamples * (1.57079633f + 1.0f)) + 4);
    itdLineL.setMaximumDelayInSamples (itdMaxInt);
    itdLineR.setMaximumDelayInSamples (itdMaxInt);
    itdLineL.prepare (monoSpec);
    itdLineR.prepare (monoSpec);

    limiter.prepare (sampleRate);
    reset();
}

void MovementEngine::reset()
{
    mono.clear();
    dopplerLine.reset();
    dopplerDelayPrev = 0.0f;
    directGainSm = 1.0f;
    airLp        = 0.0f;
    gLsm = gRsm  = 0.70710678f;
    itdLineL.reset();
    itdLineR.reset();
    itdLPrev = itdRPrev = kItdBaseSamples;
    hsL = hsR = 0.0f;
    limiter.reset();
}

void MovementEngine::process (juce::AudioBuffer<float>& buffer, const MovementParams& p)
{
    const int n     = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();
    if (n <= 0 || bufCh <= 0) return;

    // Defensa: bloque mayor al preparado -> agrandar scratch.
    if (n > mono.getNumSamples())
        mono.setSize (1, n, false, false, true);

    // 1) sumar a mono (el punto sonoro que se mueve).
    auto* m = mono.getWritePointer (0);
    juce::FloatVectorOperations::clear (m, n);
    for (int ch = 0; ch < bufCh; ++ch)
        juce::FloatVectorOperations::add (m, buffer.getReadPointer (ch), n);
    if (bufCh > 1)
        juce::FloatVectorOperations::multiply (m, 1.0f / (float) bufCh, n);

    const double dInstMeters = distanceFromNorm (p.distance01);

    // 1b) DOPPLER: delay de propagación mono modulado por la velocidad radial (derivada de la distancia).
    //     El pitch emerge de variar el delay (sin pitch-shifter -> latencia 0). Centro = headroom; sólo
    //     la DERIVADA del delay produce pitch. Bypass a doppler=0 (rampa el centro a 0 al desactivar ->
    //     sin click). Continuidad C0 entre bloques + slew-limit del delay (anti-aliasing).
    const bool dopActive = p.doppler01 > 1.0e-4f;
    if (dopActive || dopplerDelayPrev > 1.0e-4f)
    {
        const double dRefMeters = distanceFromNorm (0.5f);  // referencia de centro (no hay radius base)
        float modSamp = (float) ((dRefMeters - dInstMeters) * (sampleRate / kSoundC)) * p.doppler01;
        modSamp = juce::jlimit (-dopplerMaxAmpSamples, dopplerMaxAmpSamples, modSamp); // clamp v_radial
        const float center   = dopActive ? dopplerCenterSamples : 0.0f;  // se rampea a 0 al desactivar
        const float delayTgt = juce::jmax (dopplerTune.minSafeSamples, center - modSamp);
        float dStep = (delayTgt - dopplerDelayPrev) / (float) n;
        dStep = juce::jlimit (-dopplerTune.maxSlew, dopplerTune.maxSlew, dStep);
        float delay = dopplerDelayPrev;
        for (int i = 0; i < n; ++i)
        {
            dopplerLine.setDelay (juce::jmax (dopplerTune.minSafeSamples, delay));
            dopplerLine.pushSample (0, m[i]);
            m[i] = dopplerLine.popSample (0);
            delay += dStep;
        }
        dopplerDelayPrev = delay; // el delay ALCANZADO (no el target), por el slew-limit
    }

    // 2) PANEO de potencia constante desde el azimut + spread (width). leftness = sin(az) (+ = izquierda);
    //    pan01 0=izq..1=der; width escala la profundidad del recorrido (0 = centro, 1 = barrido completo).
    const float leftness = std::sin (p.azimuthRad);
    const float pan01    = 0.5f - 0.5f * leftness;
    const float width    = juce::jlimit (0.0f, 1.0f, p.width01);
    const float panEff   = 0.5f + (pan01 - 0.5f) * width;
    const auto  pg       = ovni::dsp::constantPowerPan (panEff);

    // 2b) ITD (Woodworth): el oído lejano llega después -> decorrelación L/R = ancho/3D real. El azimut ya
    //     trae el width adentro (az = tx·width·π/2), así que el ITD escala con width solo. Piso COMÚN en
    //     ambos oídos -> con ITD=0 no hay diferencia de fase (idéntico al paneo puro). monoSafe -> ITD=0.
    const float az        = juce::jlimit (-1.57079633f, 1.57079633f, p.azimuthRad);
    // ITD escalado por WIDTH más allá del head-natural (ancho creativo, no clínico). monoSafe -> 0.
    const float itdSigned = p.monoSafe ? 0.0f : itdScaleSamples * (az + std::sin (az)) * (1.0f + 0.8f * width);
    const float dLtgt     = kItdBaseSamples + juce::jmax (0.0f, -itdSigned);   // fuente a la izq -> R espera
    const float dRtgt     = kItdBaseSamples + juce::jmax (0.0f,  itdSigned);

    // 2c) HEAD-SHADOW: el oído lejano además llega más OSCURO (la cabeza tapa agudos). LP 1-polo en el oído
    //     lejano, profundidad ~ |azimut|·width. 2da pista binaural (espectral) = lo que MÁS abre. monoSafe -> 0.
    const float shadow  = p.monoSafe ? 0.0f : juce::jlimit (0.0f, 1.0f, std::abs (leftness) * width);
    const float fcFar   = 20000.0f - 15000.0f * shadow;   // 20 kHz (centro) -> 5 kHz (full side)
    const float coefFar = 1.0f - (float) std::exp (-kTwoPi * (double) juce::jmin (fcFar, (float) (0.45 * sampleRate)) / sampleRate);
    const float coefL   = (leftness < 0.0f) ? coefFar : 1.0f;   // L lejano cuando la fuente va a la derecha
    const float coefR   = (leftness > 0.0f) ? coefFar : 1.0f;

    // 3) DISTANCIA sobre el mono: ganancia 1/r + air-absorption LP (1-polo; corte cae con la distancia).
    //    Ambos rampeados por-sample (a Doppler alto la distancia oscila rápido -> sin rampa = crackle).
    const float directGainTgt = juce::jlimit (0.06f, 1.1f, (float) (kDRef / std::max (dInstMeters, 1.0e-3)));
    const float fc   = 20000.0f * (float) std::sqrt (kDRef / std::max (dInstMeters, kDRef));
    const float airA = (float) std::exp (-kTwoPi * (double) juce::jmin (fc, (float) (0.45 * sampleRate)) / sampleRate);
    const float airB = 1.0f - airA;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = bufCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const float dgStep = (directGainTgt - directGainSm) / (float) n;
    const float dGL    = (pg.left  - gLsm) / (float) n;
    const float dGR    = (pg.right - gRsm) / (float) n;
    const float dLStep = (dLtgt - itdLPrev) / (float) n;   // ITD rampeado por-sample (anti-zipper/click)
    const float dRStep = (dRtgt - itdRPrev) / (float) n;
    float dg = directGainSm, gl = gLsm, gr = gRsm, dL = itdLPrev, dR = itdRPrev;
    for (int i = 0; i < n; ++i)
    {
        airLp = airB * (m[i] * dg) + airA * airLp;   // mono distante (1/r + aire), pre-paneo
        const float src = airLp;
        if (outR != nullptr)
        {
            // ITD: cada oído lee el mono con su propio delay (decorrelación L/R). Luego head-shadow + paneo.
            itdLineL.setDelay (dL); itdLineL.pushSample (0, src); float sL = itdLineL.popSample (0);
            itdLineR.setDelay (dR); itdLineR.pushSample (0, src); float sR = itdLineR.popSample (0);
            hsL += coefL * (sL - hsL); sL = hsL;   // sombra de cabeza: oído lejano más oscuro
            hsR += coefR * (sR - hsR); sR = hsR;
            outL[i] = sL * gl;
            outR[i] = sR * gr;
        }
        else
        {
            outL[i] = src * (gl + gr) * 0.70710678f;  // mono fold (sin paneo ni ITD)
        }
        dg += dgStep; gl += dGL; gr += dGR; dL += dLStep; dR += dRStep;
    }
    directGainSm = directGainTgt; gLsm = pg.left; gRsm = pg.right;
    itdLPrev = dLtgt; itdRPrev = dRtgt;

    // 4) limiter de salida estéreo-linked (red de seguridad; alimenta lastLimiterGain).
    if (outR != nullptr)
    {
        limiter.process (outL, outR, n);
    }
    else
    {
        juce::FloatVectorOperations::copy (m, outL, n);   // reusa el scratch mono (ya consumido) como R
        limiter.process (outL, m, n);                     // misma ganancia; outL queda limitado correctamente
    }
}

} // namespace ovni::engines
