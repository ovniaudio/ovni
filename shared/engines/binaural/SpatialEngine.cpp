#include "engines/binaural/SpatialEngine.h"
#include "engines/binaural/HrirRing.h"
#include <cmath>

namespace ovni::engines {

static constexpr double kTwoPi = 6.283185307179586;
static constexpr double kMaxAzVel = 650.0; // pasos/seg máx del azimut (~9 vueltas/seg): slew anti-crackle (capa el pico errático del Caos, deja pasar el Speed base)
static constexpr float  kReflTrim = 0.25f; // trim de seguridad del bus de reflexiones (anti-clip)
static constexpr float  kILD      = 0.72f; // corte del oído lejano a width=100% (paneo dramático)
// Distancia (cercanía-lejanía): mapeo exponencial de radius01 a metros.
static constexpr double kDMin = 0.15, kDMax = 15.0, kDRef = 1.5; // m: al-oído, lejos, crítica (DRR=0)
// Near-field ("al oído", d<1 m, Duda-Martens 1998): cues de cerca sobre el directo (low-shelf).
// Las magnitudes viven en SpatialEngine::NearFieldTuning (parámetros de diseño, configurables).
static constexpr float  kNearShelfMinDB = -26.0f, kNearShelfMaxDB = 12.0f; // clamp por oído (cercano sube poco, lejano cae)

static inline double distanceFromRadius (float radius01) noexcept
{
    return kDMin * std::pow (kDMax / kDMin, (double) juce::jlimit (0.0f, 1.0f, radius01));
}

static inline int modN (long s) noexcept
{
    long r = s % (long) kNumDirs;
    if (r < 0) r += (long) kNumDirs;
    return (int) r;
}

// Catmull-Rom cúbico (4 puntos), zero-delay: resamplea un IR a otra cantidad de taps.
// ratio = SR_sesión / SR_anillo. Offline (en prepare), nunca en el audio thread.
static void resampleIR (const float* in, int inLen, float* out, int outLen, double ratio)
{
    // Compensación de ganancia: un IR remuestreado por factor r tiene ~r× más taps, así que
    // su respuesta escala por r. Multiplicar por 1/r mantiene la ganancia del filtro constante.
    const float invR = (float) (1.0 / ratio);
    auto at = [&] (int i) { return (i >= 0 && i < inLen) ? in[i] : 0.0f; };
    for (int j = 0; j < outLen; ++j)
    {
        const double srcPos = (double) j / ratio;
        const int    i  = (int) std::floor (srcPos);
        const float  t  = (float) (srcPos - (double) i);
        const float  xm = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        out[j] = invR * (x0 + 0.5f * t * (x1 - xm
               + t * (2.0f * xm - 5.0f * x0 + 4.0f * x1 - x2
               + t * (3.0f * (x0 - x1) + x2 - xm))));
    }
}

void SpatialEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    // Robustez de sample rate: el anillo se horneó a kRingSampleRate; si la sesión corre
    // a otro rate, resamplear los IRs y escalar el ITD (offline, una sola vez en prepare).
    const double ratio    = spec.sampleRate / (double) kRingSampleRate;
    const bool   resample = std::abs (ratio - 1.0) > 1.0e-4;
    const int    outTaps  = resample ? juce::jmax (1, (int) std::lround ((double) kRingTaps * ratio))
                                     : kRingTaps;

    // Preasignar TODOS los coeficientes una sola vez (recargar luego = copia de puntero
    // ref-contado -> sin new/delete en el audio thread).
    coefL.resize ((size_t) kNumDirs);
    coefR.resize ((size_t) kNumDirs);
    dlyL .resize ((size_t) kNumDirs);
    dlyR .resize ((size_t) kNumDirs);

    std::vector<float> tmp ((size_t) outTaps);
    float maxDelay = 1.0f;
    for (int d = 0; d < kNumDirs; ++d)
    {
        if (resample)
        {
            resampleIR (&kRingL[(size_t) d * kRingTaps], kRingTaps, tmp.data(), outTaps, ratio);
            coefL[(size_t) d] = new Coefs (tmp.data(), (size_t) outTaps);
            resampleIR (&kRingR[(size_t) d * kRingTaps], kRingTaps, tmp.data(), outTaps, ratio);
            coefR[(size_t) d] = new Coefs (tmp.data(), (size_t) outTaps);
        }
        else
        {
            coefL[(size_t) d] = new Coefs (&kRingL[(size_t) d * kRingTaps], (size_t) kRingTaps);
            coefR[(size_t) d] = new Coefs (&kRingR[(size_t) d * kRingTaps], (size_t) kRingTaps);
        }
        dlyL[(size_t) d] = (float) (kRingDelayL[(size_t) d] * ratio);
        dlyR[(size_t) d] = (float) (kRingDelayR[(size_t) d] * ratio);
        maxDelay = juce::jmax (maxDelay, dlyL[(size_t) d], dlyR[(size_t) d]);
    }

    const int maxDelaySamples = (int) std::ceil (maxDelay) + 4;
    const juce::dsp::ProcessSpec monoSpec { spec.sampleRate, spec.maximumBlockSize, 1 };
    for (auto& v : voice)
    {
        v.firL.coefficients = coefL[0];   // orden fijo antes de prepare (todas igual de largas)
        v.firR.coefficients = coefR[0];
        v.firL.prepare (monoSpec);
        v.firR.prepare (monoSpec);
        v.itdL.setMaximumDelayInSamples (maxDelaySamples);
        v.itdR.setMaximumDelayInSamples (maxDelaySamples);
        v.itdL.prepare (monoSpec);
        v.itdR.prepare (monoSpec);
        v.dir = -1;
    }

    const int maxBlock = (int) spec.maximumBlockSize;
    for (auto* b : { &mono, &w0L, &w0R, &w1L, &w1R })
        b->setSize (1, maxBlock, false, false, true);
    dryBuf.setSize (2, maxBlock, false, false, true);
    space.prepare (spec.sampleRate, maxBlock);
    xtalk.prepare (spec.sampleRate);
    bassCoef = 1.0f - (float) std::exp (-kTwoPi * 250.0 / spec.sampleRate); // crossover bass-mono ~250 Hz
    nearCoef = 1.0f - (float) std::exp (-kTwoPi * (double) nearTune.cornerHz / spec.sampleRate); // shelf near-field
    sampleRate = spec.sampleRate;
    limRelCoef = 1.0f - (float) std::exp (-1.0 / (0.001 * (double) limTune.releaseMs * spec.sampleRate)); // release del limiter

    // Doppler: dimensionar la linea de delay modulado. center = headroom (maxAmp + piso); maxDelay = 2*center.
    dopplerMaxAmpSamples = (float) (dopplerTune.maxAmpMeters * spec.sampleRate / kSoundC);
    dopplerCenterSamples = dopplerMaxAmpSamples + dopplerTune.minSafeSamples;
    dopplerLine.setMaximumDelayInSamples (juce::jmax (4, (int) std::ceil (2.0f * dopplerCenterSamples) + 4));
    dopplerLine.prepare (monoSpec);

    reset();
}

void SpatialEngine::reset()
{
    for (auto& v : voice) { v.firL.reset(); v.firR.reset(); v.itdL.reset(); v.itdR.reset(); }
    mono.clear(); w0L.clear(); w0R.clear(); w1L.clear(); w1R.clear(); dryBuf.clear();
    space.reset();
    xtalk.reset();
    bassLpL = bassLpR = 0.0f;
    airLpL = airLpR = 0.0f;
    directGainSm = 1.0f;
    gFarLsm = gFarRsm = 1.0f;
    nearLpL = nearLpR = 0.0f;
    gNearLsm = gNearRsm = 1.0f;
    dopplerLine.reset();
    dopplerDelayPrev = 0.0f;
    limGain = 1.0f;
    lo = 0;
    posSteps = 0.0;
    primed = false;
}

void SpatialEngine::loadVoice (int slot, int dir)
{
    auto& v = voice[slot];
    v.firL.coefficients = coefL[(size_t) dir];   // copia de puntero (RT-safe)
    v.firR.coefficients = coefR[(size_t) dir];
    v.itdL.setDelay (dlyL[(size_t) dir]);        // ITD ya escalado al SR de la sesión
    v.itdR.setDelay (dlyR[(size_t) dir]);
    v.dir = dir;
}

// Garantiza que voice[lo] tenga la dir baja del bracket (d0) y voice[1-lo] la alta (d1).
// El ping-pong recarga sólo la voz que en el borde de segmento tiene peso ~0 -> sin click.
void SpatialEngine::ensureBracket (long segK)
{
    const int d0 = modN (segK);
    const int d1 = modN (segK + 1);
    const int hi = 1 - lo;

    if (! primed)
    {
        loadVoice (lo, d0);
        loadVoice (hi, d1);
        primed = true;
        return;
    }

    if (voice[lo].dir == d0 && voice[hi].dir == d1)
        return; // mismo segmento

    if (voice[hi].dir == d0)      // avanzó +1 (la vieja alta es la nueva baja)
    {
        lo = hi;                  // voice[lo] ya tiene d0
        loadVoice (1 - lo, d1);   // nueva alta: peso ~0 en el borde
        return;
    }

    if (voice[lo].dir == d1)      // retrocedió -1 (la vieja baja es la nueva alta)
    {
        lo = 1 - lo;              // voice[1-lo] (vieja baja) queda como alta = d1
        loadVoice (lo, d0);       // nueva baja: peso ~0 en el borde
        return;
    }

    // Salto > 1 (loop/automatización brusca): recargar ambas. Raro y breve.
    loadVoice (lo, d0);
    loadVoice (1 - lo, d1);
}

void SpatialEngine::renderVoice (Voice& v, const float* monoIn,
                                 juce::AudioBuffer<float>& wetL,
                                 juce::AudioBuffer<float>& wetR, int len)
{
    auto* l = wetL.getWritePointer (0);
    auto* r = wetR.getWritePointer (0);
    juce::FloatVectorOperations::copy (l, monoIn, len);
    juce::FloatVectorOperations::copy (r, monoIn, len);

    auto blkL = juce::dsp::AudioBlock<float> (wetL).getSubBlock (0, (size_t) len);
    juce::dsp::ProcessContextReplacing<float> cl (blkL);
    v.firL.process (cl);

    auto blkR = juce::dsp::AudioBlock<float> (wetR).getSubBlock (0, (size_t) len);
    juce::dsp::ProcessContextReplacing<float> cr (blkR);
    v.firR.process (cr);

    for (int i = 0; i < len; ++i)
    {
        v.itdL.pushSample (0, l[i]); l[i] = v.itdL.popSample (0);
        v.itdR.pushSample (0, r[i]); r[i] = v.itdR.popSample (0);
    }
}

void SpatialEngine::process (juce::AudioBuffer<float>& buffer, int numInputChannels, const EngineParams& p)
{
    const float azimuthRad  = p.azimuthRad;
    const float mix01       = p.mix01;
    const float room01      = p.room01;
    const float width01     = p.width01;
    const bool  monoSafe    = p.monoSafe;
    const float radius01    = p.radius01;
    const bool  speakerMode = p.speakerMode;

    const int n     = buffer.getNumSamples();
    const int numIn = juce::jmax (1, numInputChannels);
    const int bufCh = buffer.getNumChannels();
    if (n <= 0) return;

    // Defensa: bloque mayor al preparado -> agrandar scratch.
    if (n > mono.getNumSamples())
    {
        for (auto* b : { &mono, &w0L, &w0R, &w1L, &w1R })
            b->setSize (1, n, false, false, true);
        dryBuf.setSize (2, n, false, false, true);
    }

    // 0) capturar el SECO = entrada original (preserva el estéreo; mono -> centrado).
    //    Se usa en el mix seco/efectado; así bajar Mix recupera la señal original.
    auto* dryL = dryBuf.getWritePointer (0);
    auto* dryR = dryBuf.getWritePointer (1);
    juce::FloatVectorOperations::copy (dryL, buffer.getReadPointer (0), n);
    juce::FloatVectorOperations::copy (dryR, buffer.getReadPointer (juce::jmin (numIn > 1 ? 1 : 0, bufCh - 1)), n);

    // 1) suma a mono (punto sonoro) -> alimenta el espacializador
    auto* m = mono.getWritePointer (0);
    juce::FloatVectorOperations::clear (m, n);
    for (int ch = 0; ch < numIn; ++ch)
        juce::FloatVectorOperations::add (m, buffer.getReadPointer (juce::jmin (ch, bufCh - 1)), n);
    if (numIn > 1)
        juce::FloatVectorOperations::multiply (m, 1.0f / (float) numIn, n);

    // 1b) DOPPLER: delay de propagacion mono modulado por la velocidad radial del fly-by. El pitch
    //     emerge de variar el delay (sin pitch-shifter -> latencia 0). Centro = headroom; sólo la
    //     DERIVADA del delay produce pitch, así el offset no agrega latencia perceptible. Bypass a
    //     doppler=0 (distance==radius) -> idéntico a hoy. Rampa por-sample = continuidad C0 (anti-click).
    const float distInst01 = (p.distance01 < 0.0f) ? radius01 : p.distance01;
    const bool  dopActive  = p.doppler01 > 1.0e-4f;
    if (dopActive || dopplerDelayPrev > 1.0e-4f)
    {
        const double dBase = distanceFromRadius (radius01);
        const double dInst = distanceFromRadius (distInst01);
        float modSamp = (float) ((dBase - dInst) * (sampleRate / kSoundC)); // + al acercarse (delay se acorta -> pitch sube)
        modSamp = juce::jlimit (-dopplerMaxAmpSamples, dopplerMaxAmpSamples, modSamp); // anti-aliasing (clamp v_radial)
        const float center   = dopActive ? dopplerCenterSamples : 0.0f;     // se rampea a 0 al desactivar
        const float delayTgt = juce::jmax (dopplerTune.minSafeSamples, center - modSamp);
        // slew-limit del delay = limita el pitch shift máximo -> anti-aliasing y suaviza el extremo:
        // a Speed × Doppler muy altos el delay no alcanza el target, el fly-by se redondea sin raspar.
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

    // 2) azimut -> posición en pasos del anillo [0, kNumDirs)
    double target = (double) azimuthRad / kTwoPi * (double) kNumDirs;
    target -= (double) kNumDirs * std::floor (target / (double) kNumDirs);

    if (! primed)
        posSteps = target; // primer bloque: sin barrido desde 0

    // delta por arco más corto (maneja el wrap del anillo)
    double delta = target - posSteps;
    delta -= (double) kNumDirs * std::round (delta / (double) kNumDirs);
    // slew-limit de la velocidad del azimut: capa el movimiento extremo (Caos/Speed al tope) para que
    // el motor no recargue los HRIR a saltos bruscos -> sin crackle. A movimiento normal no actúa.
    const double maxDelta = kMaxAzVel * (double) n / sampleRate;
    delta = juce::jlimit (-maxDelta, maxDelta, delta);
    const double newPos = posSteps + delta;

    const float dry = 1.0f - mix01;
    const float wet = mix01;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = bufCh > 1 ? buffer.getWritePointer (1) : outL;

    const double invN = 1.0 / (double) n;
    const double fInc = delta * invN; // Δfrac por sample

    // 3) recorrer el bloque por runs de segmento constante (sin cruce interno)
    int k = 0;
    while (k < n)
    {
        const double Pk   = posSteps + delta * (double) k * invN;
        const long   segK = (long) std::floor (Pk);
        ensureBracket (segK);

        // fin del run: primer sample que cae en otro segmento (o n)
        int k2 = n;
        if (delta > 0.0)
        {
            const double kb = ((double) (segK + 1) - posSteps) / fInc; // P(kb) = segK+1
            k2 = (int) std::ceil (kb);
        }
        else if (delta < 0.0)
        {
            const double kb = ((double) segK - posSteps) / fInc;       // P(kb) = segK
            k2 = (int) std::floor (kb) + 1;
        }
        k2 = juce::jlimit (k + 1, n, k2);

        const int len = k2 - k;

        // las dos voces sobre el mismo mono del run
        renderVoice (voice[0], m + k, w0L, w0R, len);
        renderVoice (voice[1], m + k, w1L, w1R, len);

        const auto* loL = (lo == 0 ? w0L : w1L).getReadPointer (0);
        const auto* loR = (lo == 0 ? w0R : w1R).getReadPointer (0);
        const auto* hiL = (lo == 0 ? w1L : w0L).getReadPointer (0);
        const auto* hiR = (lo == 0 ? w1R : w0R).getReadPointer (0);

        double f = (Pk - (double) segK); // frac al inicio del run, en [0,1)
        for (int i = 0; i < len; ++i)
        {
            const float ff  = (float) juce::jlimit (0.0, 1.0, f);
            const float gLo = 1.0f - ff;
            const float gHi = ff;
            // wet PURO (directo binaural); el seco y el Width/mix se aplican al final
            outL[k + i] = gLo * loL[i] + gHi * hiL[i];
            outR[k + i] = gLo * loR[i] + gHi * hiR[i];
            f += fInc;
        }

        k = k2;
    }

    posSteps = newPos - (double) kNumDirs * std::floor (newPos / (double) kNumDirs);

    // 3b) DISTANCIA (cercanía-lejanía) sobre el DIRECTO: ganancia 1/r + air-absorption LP.
    //     Las reflexiones (paso 4) quedan CONSTANTES -> el DRR (directo/reverberado) cae al
    //     alejarse = la pista dominante de profundidad. Cerca: directo fuerte y abierto; lejos:
    //     directo bajo y oscuro bajo un lecho de reflexiones constante.
    const double d = distanceFromRadius (distInst01);   // distancia INSTANTANEA (fly-by): 1/r + near-field siguen el acercamiento
    {
        const float  directGainTgt = juce::jlimit (0.06f, 1.1f, (float) (kDRef / d)); // 1/r (~ -24..+0.8 dB)
        const float  fc = 20000.0f * (float) std::sqrt (kDRef / std::max (d, kDRef));
        const float  airA = (float) std::exp (-kTwoPi * (double) juce::jmin (fc, (float) (0.45 * sampleRate)) / sampleRate);
        const float  airB = 1.0f - airA;
        // rampa por-sample del 1/r: a Doppler alto la distancia oscila rápido; sin rampa esta ganancia
        // saltaría cada bloque -> tren de clicks (el crackle "de vinilo"). Igual patrón que near-field/width.
        const float  dgStep = (directGainTgt - directGainSm) / (float) n;
        float dg = directGainSm;
        for (int i = 0; i < n; ++i)
        {
            airLpL = airB * (outL[i] * dg) + airA * airLpL; outL[i] = airLpL;
            airLpR = airB * (outR[i] * dg) + airA * airLpR; outR[i] = airLpR;
            dg += dgStep;
        }
        directGainSm = directGainTgt;
    }

    // 3c) NEAR-FIELD ("al oído", d<1 m): el remate del 3D de cerca. Dos cues que el HRTF de campo
    //     lejano NO trae (Duda-Martens 1998), ambos sobre el DIRECTO, latencia 0, derivados del Radio:
    //       (a) boost de graves de PROXIMIDAD (low-shelf, +0 dB a >=1 m -> ~+5 dB al oído, ambos oídos).
    //       (b) ILD de GRAVES near-field (el cue clave): a 90° el ILD de graves crece de ~4 dB (lejos)
    //           a ~20 dB (al oído). Low-shelf DIFERENCIAL: oído cercano +ild/2, lejano -ild/2.
    //     NO se toca el ITD (Duda-Martens: el ITD es independiente del rango). Shelf de 1 polo
    //     (alto + gLow·grave) = mismo patrón que air/bass-mono. Rampeado por-sample como Width: el ILD
    //     sigue al azimut -> a Speed alto sin rampa clickearía (mismo origen que el "crack" de Width).
    {
        // nearAmt: 0 a >=1 m, crece al acercar. normalize -> llega a 1 en la distancia mínima (Radio al fondo).
        const float nf      = nearTune.normalize ? 1.0f / (1.0f - (float) kDMin) : 1.0f;
        const float nearAmt = juce::jlimit (0.0f, 1.0f, (1.0f - (float) d) * nf);
        const float s       = std::sin (azimuthRad);                       // + = fuente a la izquierda
        const float bassDB  = nearAmt * nearTune.bassDB;                   // boost de proximidad (oído cercano)
        // ILD near-field ASIMÉTRICO (Duda-Martens): el oído cercano sube sólo el boost; el lejano CAE
        // por la sombra de cabeza near-field. El ILD (diferencia L/R) = ildDB·|sin| igual que el simétrico,
        // pero el nivel absoluto del cercano NO se dispara -> headroom controlado y más fiel a la física.
        const float farCutL = nearAmt * nearTune.ildDB * juce::jmax (0.0f, -s); // L es el lejano si s<0 (fuente a la der)
        const float farCutR = nearAmt * nearTune.ildDB * juce::jmax (0.0f,  s); // R es el lejano si s>0 (fuente a la izq)
        const float gNearL  = std::pow (10.0f, juce::jlimit (kNearShelfMinDB, kNearShelfMaxDB, bassDB - farCutL) / 20.0f);
        const float gNearR  = std::pow (10.0f, juce::jlimit (kNearShelfMinDB, kNearShelfMaxDB, bassDB - farCutR) / 20.0f);
        const float dGL = (gNearL - gNearLsm) / (float) n;
        const float dGR = (gNearR - gNearRsm) / (float) n;
        float gl = gNearLsm, gr = gNearRsm;
        for (int i = 0; i < n; ++i)
        {
            nearLpL += nearCoef * (outL[i] - nearLpL);    // banda grave (LP 1-polo)
            nearLpR += nearCoef * (outR[i] - nearLpR);
            outL[i] = (outL[i] - nearLpL) + gl * nearLpL; // low-shelf: alto + gLow·grave
            outR[i] = (outR[i] - nearLpR) + gr * nearLpR;
            gl += dGL; gr += dGR;
        }
        gNearLsm = gNearL; gNearRsm = gNearR;
    }

    // 4) reflexiones tempranas decorreladas (externalización + lecho del DRR) -> bus wet.
    //    NO se escalan con la distancia: ése es justamente el mecanismo del DRR.
    space.process (m, outL, outR, n, room01 * kReflTrim);

    // 5) Width = eje espacial sobre el wet + mezcla final seco/wet.
    //    0 = mono (reservado, fase preservada) · 0.5 = neutro · 1 = "volador":
    //    paneo DRAMÁTICO (corta el oído lejano -> ILD fuerte que sigue la órbita) + algo de ancho.
    //    El corte sólo atenúa -> clip-safe. Sigue al azimut: + = izquierda, - = derecha.
    const float w        = width01;
    const float sideGain = (w <= 0.5f) ? (w * 2.0f) : (1.0f + (w - 0.5f) * 0.6f);
    const float e        = juce::jmax (0.0f, (w - 0.5f) * 2.0f) * kILD;
    const float s        = std::sin (azimuthRad);
    const float tgtGFarL = 1.0f - e * juce::jmax (0.0f, -s); // corta L si la fuente va a la derecha
    const float tgtGFarR = 1.0f - e * juce::jmax (0.0f,  s); // corta R si la fuente va a la izquierda
    const float sideMono = monoSafe ? 0.45f : 1.0f;          // EN FASE: angosta el Side (audible + mono-safe)
    // Rampa por-sample del corte de oído lejano desde el bloque anterior -> sin salto en el
    // borde del bloque = anti-click a Speed/Caos alto (donde el azimut cambia mucho por bloque).
    const float dGL = (tgtGFarL - gFarLsm) / (float) n;
    const float dGR = (tgtGFarR - gFarRsm) / (float) n;
    float gl = gFarLsm, gr = gFarRsm;
    for (int i = 0; i < n; ++i)
    {
        const float L = outL[i], R = outR[i];      // wet: directo binaural + reflexiones
        const float mid  = 0.5f * (L + R);
        const float side = 0.5f * (L - R) * sideGain * sideMono;
        outL[i] = dryL[i] * dry + (mid + side) * gl * wet;
        outR[i] = dryR[i] * dry + (mid - side) * gr * wet;
        gl += dGL; gr += dGR;
    }
    gFarLsm = tgtGFarL; gFarRsm = tgtGFarR;

    // 6) EN FASE (mono-safe): fuerza los graves (<160 Hz) a mono — donde la fase realmente
    //    cancela al sumar. El filtro corre siempre (toggle sin click); sólo se aplica si está on.
    for (int i = 0; i < n; ++i)
    {
        bassLpL += bassCoef * (outL[i] - bassLpL);
        bassLpR += bassCoef * (outR[i] - bassLpR);
        if (monoSafe)
        {
            const float lowMono = 0.5f * (bassLpL + bassLpR);
            outL[i] = (outL[i] - bassLpL) + lowMono; // alto(L) + grave mono
            outR[i] = (outR[i] - bassLpR) + lowMono;
        }
    }

    // 7) Modo Parlantes: cancelación de crosstalk RACE (binaural -> traduce a parlantes).
    //    Sólo con salida estéreo real. EN FASE lo APAGA: el RACE reintroduce diferencias L/R
    //    (anti-coherente) y pelearía con la compatibilidad mono -> EN FASE gana.
    if (bufCh > 1)
        xtalk.process (outL, outR, n, speakerMode && ! monoSafe);

    // 8) Limiter de salida ESTÉREO-LINKED. Attack instantáneo (la ganancia baja al pico al instante ->
    //    nunca pasa el techo, latencia 0) + release suave (recupera gradual -> contiene sin armónicos
    //    duros). La MISMA ganancia va a L y R -> el balance/imagen estéreo queda exacto. Transparente
    //    mientras la señal no toca el techo.
    const float ceil = limTune.ceiling;
    if (bufCh > 1)
        for (int i = 0; i < n; ++i)
        {
            const float pk  = juce::jmax (std::abs (outL[i]), std::abs (outR[i]));
            const float tgt = (pk > ceil) ? ceil / pk : 1.0f;
            if (tgt < limGain) limGain = tgt;                            // attack instantáneo
            else               limGain += (tgt - limGain) * limRelCoef;  // release suave
            outL[i] *= limGain; outR[i] *= limGain;
        }
    else
        for (int i = 0; i < n; ++i)
        {
            const float pk  = std::abs (outL[i]);
            const float tgt = (pk > ceil) ? ceil / pk : 1.0f;
            if (tgt < limGain) limGain = tgt; else limGain += (tgt - limGain) * limRelCoef;
            outL[i] *= limGain;
        }
}

} // namespace ovni::engines
