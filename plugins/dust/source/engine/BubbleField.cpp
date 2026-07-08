// BubbleField.cpp — banco de 16 direcciones HRIR de costo fijo (espacializador de DUST).
// Las burbujas se reparten entre buses por ley de potencia con RAMPA POR-SAMPLE de las
// ganancias; los filtros HRIR son FIJOS (nunca se conmutan) -> anti-click estructural.
#include "BubbleField.h"
#include "engines/binaural/HrirRing.h"   // anillo SADIE II KU100 (72 dirs × 256 taps, ovni::engines)
#include "dsp/Smoothing.h"               // constantPowerPan (ley de potencia)
#include <cmath>

namespace dust::engine
{

namespace
{
    constexpr float kTwoPiF  = juce::MathConstants<float>::twoPi;
    constexpr float kHalfPiF = juce::MathConstants<float>::halfPi;
    constexpr float kGainEps = 1.0e-4f;
    constexpr float kApGain  = 0.40f;    // coef del all-pass decorrelador por bus (dispersión de fase HF)

    // Resampleo Catmull-Rom de un IR (misma forma que SpatialEngine::prepare: si la sesión no corre
    // a kRingSampleRate, el IR se re-muestrea offline una sola vez y se compensa la energía).
    void resampleIR (const float* in, int inLen, float* out, int outLen, double ratio)
    {
        const float invR = (float) (1.0 / ratio);
        auto at = [in, inLen] (int i) { return (i >= 0 && i < inLen) ? in[i] : 0.0f; };
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
}

void BubbleField::prepare (double sampleRate, int maxBlockSize)
{
    using namespace ovni::engines;
    maxBlock = maxBlockSize;

    // 16 direcciones ~equiespaciadas del anillo de 72 (paso 4.5 -> alterna 20°/25°, todas
    // direcciones REALES medidas: no se interpolan IRs). Azimut del bus = dir·5° (+ = CCW/izquierda,
    // misma convención que SpatialEngine: target = az/2π · kNumDirs).
    const double ratio    = sampleRate / (double) kRingSampleRate;
    const bool   resample = std::abs (ratio - 1.0) > 1.0e-4;
    const int    fullTaps = resample ? juce::jmax (1, (int) std::lround ((double) kRingTaps * ratio))
                                     : kRingTaps;

    // Truncado MEDIDO (no asumido): con los 256 taps el motor medía ~7.6% de CPU (>> presupuesto 3%
    // de la familia Movimiento). Las HRIR del anillo son fase mínima (la energía vive en los primeros
    // taps): 128 taps @48k = 2.7 ms conservan el cue direccional del eco (curaduría: "el eco tolera
    // HRIR más corta que la fuente directa"). Fade raised-cosine en la cola para no cortar en seco.
    const int kBusTaps48k = 128;
    firTaps = juce::jmin (fullTaps, (int) std::lround ((double) kBusTaps48k * ratio));
    // Taper de cola CORTO (8 taps): sólo evita el corte en seco. El raised-cosine sobre firTaps/4=32
    // taps NO bajaba agudos (medido: <0.01 dB sobre 320 Hz; las HRIR son fase mínima), pero un taper
    // largo no aporta nada y oscurece la cola del cue -> se acota a 8 taps (transparente).
    const int fadeLen = juce::jmin (8, firTaps / 4);

    coefL.resize ((size_t) kNumBuses);
    coefR.resize ((size_t) kNumBuses);
    std::vector<float> tmp ((size_t) fullTaps);

    auto truncated = [&] (const float* full) -> Coefs::Ptr
    {
        std::vector<float> cut (full, full + firTaps);
        for (int j = 0; j < fadeLen; ++j)
        {
            const float t = (float) (j + 1) / (float) fadeLen;   // 0..1 hacia el final
            cut[(size_t) (firTaps - fadeLen + j)] *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * t);
        }
        return new Coefs (cut.data(), (size_t) firTaps);
    };

    // ── Air shelf FIJO (DESBOXY): high-shelf de 1er orden que devuelve la presencia/aire que la
    // coloración común del banco KU100 (pabellón + piso del comb) se llevaba. Medido sobre el beat
    // real: el wet caía −5..−9 dB de 2.5 a 16 kHz vs el dry. El shelf levanta esa banda; aplicado
    // IDÉNTICO a L/R no toca la imagen (CORR/WIDTH/colocación binaural intactos). 1er orden bilineal:
    //   H(z) = (b0 + b1 z^-1)/(1 + a1 z^-1),  corte fc, ganancia HF = gHi (lineal).
    {
        const double fcHz = 3000.0;                       // arranque del shelf (~donde empieza el hueco)
        const double gHi  = 2.3;                            // +7.2 dB en HF (compensa el déficit medido)
        // High-shelf de 1er orden, bilineal. H(z) = (b0 + b1 z^-1)/(1 + a1 z^-1). Diseñado para
        // ganancia UNITARIA en DC y gHi (lineal) en Nyquist (verificado: H(z=1)=1, H(z=-1)=gHi).
        const double k   = std::tan (juce::MathConstants<double>::pi * fcHz / sampleRate);
        const double den = k + 1.0;
        airB0 = (float) ((k + gHi) / den);
        airB1 = (float) ((k - gHi) / den);
        airA1 = (float) ((k - 1.0) / den);
    }

    // ── Decorrelador por bus (DESBOXY): delay del all-pass DISTINTO por bus (primos pequeños) para
    // que las copias coherentes que el power-pan mete en 2 buses adyacentes dejen de combar en agudos.
    static constexpr int kApDelay[(size_t) kNumBuses] =
        { 0, 5, 7, 11, 4, 9, 6, 13, 3, 8, 5, 12, 7, 10, 4, 9 };   // bus 0 (frente) sin decorrelar

    const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlockSize, 1 };
    for (int b = 0; b < kNumBuses; ++b)
    {
        auto& bus   = buses[(size_t) b];
        bus.ringDir = (int) std::lround ((double) b * (double) kNumDirs / (double) kNumBuses) % kNumDirs;
        busAzRad[(size_t) b] = (float) bus.ringDir * kRingStepDeg * juce::MathConstants<float>::pi / 180.0f;

        if (resample)
        {
            resampleIR (&kRingL[(size_t) bus.ringDir * kRingTaps], kRingTaps, tmp.data(), fullTaps, ratio);
            coefL[(size_t) b] = truncated (tmp.data());
            resampleIR (&kRingR[(size_t) bus.ringDir * kRingTaps], kRingTaps, tmp.data(), fullTaps, ratio);
            coefR[(size_t) b] = truncated (tmp.data());
        }
        else
        {
            coefL[(size_t) b] = truncated (&kRingL[(size_t) bus.ringDir * kRingTaps]);
            coefR[(size_t) b] = truncated (&kRingR[(size_t) bus.ringDir * kRingTaps]);
        }
        bus.firL.coefficients = coefL[(size_t) b];
        bus.firR.coefficients = coefR[(size_t) b];
        bus.firL.prepare (monoSpec);
        bus.firR.prepare (monoSpec);

        // ── ITD por bus (la otra mitad del cue binaural; curaduría: "min-phase corto + ITD por
        // bus"). El anillo es fase mínima y su delay field (kRingDelayL/R) vino en CERO -> el cue
        // temporal se SINTETIZA con el modelo esférico de Woodworth (a = 8.75 cm, c = 343 m/s):
        //   ITD = a/c · (θ + sin θ),  θ = ángulo respecto del plano medio (simetría frente/espalda)
        // Máx ≈ 0.66 ms a ±90°. El delay es FIJO por bus y va al oído LEJANO, redondeado a sample
        // (error ≤ 10.4 µs @48k, muy por debajo del paso entre buses ~80 µs): nunca se modula ->
        // el anti-click estructural queda intacto (entre buses sólo se rampean GANANCIAS).
        const float lat    = std::sin (busAzRad[(size_t) b]);                       // componente interaural
        const float theta  = std::asin (juce::jlimit (-1.0f, 1.0f, std::abs (lat)));
        const float itdSec = (0.0875f / 343.0f) * (theta + std::abs (lat));
        const int   itd    = (int) std::lround ((double) itdSec * sampleRate);
        bus.itdL = (lat > 0.0f) ? 0 : itd;    // az>0 = izquierda: oído L cerca -> se retrasa R
        bus.itdR = (lat > 0.0f) ? itd : 0;
        bus.dlyMask = 0;
        bus.dlyW    = 0;
        bus.dly.clear();
        if (itd > 0)
        {
            int dlySize = 1;
            while (dlySize < itd + 4) dlySize <<= 1;
            bus.dly.assign ((size_t) dlySize, 0.0f);
            bus.dlyMask = dlySize - 1;
        }

        // Decorrelador del bus (escalado al SR; el delay del all-pass va en samples a cada SR).
        bus.apN = juce::jmin (63, (int) std::lround ((double) kApDelay[(size_t) b] * ratio));
        bus.apL.fill (0.0f);
        bus.apR.fill (0.0f);
        bus.apWL = 0;
        bus.apWR = 0;
    }

    busBuf.assign ((size_t) kNumBuses * (size_t) maxBlockSize, 0.0f);
    scratch.assign ((size_t) maxBlockSize, 0.0f);
    panL.assign ((size_t) maxBlockSize, 0.0f);
    panR.assign ((size_t) maxBlockSize, 0.0f);

    hrirWeightCoefBlock = 1.0f - std::exp (-(float) maxBlockSize / (0.030f * (float) sampleRate));
    reset();
}

void BubbleField::reset()
{
    for (auto& bus : buses)
    {
        bus.firL.reset();
        bus.firR.reset();
        bus.zeroRun = 1 << 24;   // arranca "purgado": sin energía no se convoluciona
        std::fill (bus.dly.begin(), bus.dly.end(), 0.0f);
        bus.dlyW = 0;
        bus.apL.fill (0.0f);
        bus.apR.fill (0.0f);
        bus.apWL = 0;
        bus.apWR = 0;
    }
    for (auto& g : slotGain) g.fill (0.0f);
    slotPanL.fill (0.70710678f);
    slotPanR.fill (0.70710678f);
    hrirWeightSm = hrirWeightTgt;
    airZL = 0.0f;
    airZR = 0.0f;
    busEnergy.fill (0.0);
}

// Ganancias objetivo de los 16 buses para un azimut: los 2 buses ADYACENTES por ley de potencia.
void BubbleField::targetGainsForAzimuth (float azRad, float* g) const noexcept
{
    std::fill (g, g + kNumBuses, 0.0f);
    float az = std::fmod (azRad, kTwoPiF);
    if (az < 0.0f) az += kTwoPiF;

    int lo = kNumBuses - 1;                       // si az >= último bus, el segmento envuelve a 2π
    for (int b = 0; b < kNumBuses - 1; ++b)
        if (az >= busAzRad[(size_t) b] && az < busAzRad[(size_t) b + 1]) { lo = b; break; }

    const int   hi   = (lo + 1) % kNumBuses;
    const float a0   = busAzRad[(size_t) lo];
    const float span = (lo == kNumBuses - 1) ? (kTwoPiF - a0 + busAzRad[0])
                                             : (busAzRad[(size_t) hi] - a0);
    const float frac = juce::jlimit (0.0f, 1.0f, (az - a0) / juce::jmax (1.0e-6f, span));

    g[lo] = std::cos (frac * kHalfPiF);           // ley de potencia: g_lo² + g_hi² = 1
    g[hi] = std::sin (frac * kHalfPiF);
}

void BubbleField::process (const std::array<TapRender, (size_t) kMaxTaps>& taps, int numTaps,
                           int n, float* outL, float* outR)
{
    for (int b = 0; b < kNumBuses; ++b)   // limpiar el PREFIJO de cada stride (cada bus arranca en b·maxBlock)
    {
        float* dst = busBuf.data() + (size_t) b * (size_t) maxBlock;
        std::fill (dst, dst + n, 0.0f);
    }
    std::fill (panL.begin(),   panL.begin()   + (size_t) n, 0.0f);
    std::fill (panR.begin(),   panR.begin()   + (size_t) n, 0.0f);
    bool busActive[(size_t) kNumBuses] = {};

    const float invN = 1.0f / (float) n;
    // Con IN PHASE sostenido (peso HRIR ~0 y sin transición) el banco no aporta: no se scatterea
    // -> los buses purgan su cola y el FIR se saltea solo (costo cae al camino de paneo).
    const bool hrirPathOn = (hrirWeightSm > 1.0e-3f) || (hrirWeightTgt > 1.0e-3f);

    // ── Scatter: cada tap reparte su bloque en los buses con ganancias rampeadas por-sample ─────
    for (int k = 0; k < numTaps; ++k)
    {
        const auto&  r = taps[(size_t) k];
        const int    s = r.slot;
        const float* x = r.samples;

        float gStart[(size_t) kNumBuses];
        float gEnd  [(size_t) kNumBuses];
        if (r.born)
        {
            // Recién nacida: su stream entra desde 0 (micro-fade) -> fijar las ganancias al azimut
            // de nacimiento no chasquea (multiplican silencio) y evita arrastrar el bus del slot viejo.
            targetGainsForAzimuth (r.azStart, gStart);
            auto& pg = slotGain[(size_t) s];
            std::copy (gStart, gStart + kNumBuses, pg.begin());
            const auto pan = ovni::dsp::constantPowerPan (0.5f * (1.0f - std::sin (r.azStart)));
            slotPanL[(size_t) s] = pan.left;
            slotPanR[(size_t) s] = pan.right;
        }
        else
        {
            std::copy (slotGain[(size_t) s].begin(), slotGain[(size_t) s].end(), gStart);
        }
        targetGainsForAzimuth (r.azEnd, gEnd);

        for (int b = 0; b < kNumBuses; ++b)
        {
            const float g0 = gStart[(size_t) b];
            const float g1 = gEnd  [(size_t) b];
            slotGain[(size_t) s][(size_t) b] = g1;
            if (! hrirPathOn) continue;                       // banco apagado (IN PHASE sostenido)
            if (g0 < kGainEps && g1 < kGainEps) continue;     // bus sin energía de este tap

            busActive[(size_t) b] = true;
            float*      dst  = busBuf.data() + (size_t) b * (size_t) maxBlock;
            const float step = (g1 - g0) * invN;
            float       gain = g0;
            for (int i = 0; i < n; ++i) { dst[i] += x[i] * gain; gain += step; }
        }

        // Camino monoSafe: paneo de potencia constante POR TAP, también rampeado por-sample.
        const auto  panTgt = ovni::dsp::constantPowerPan (0.5f * (1.0f - std::sin (r.azEnd)));
        const float plStep = (panTgt.left  - slotPanL[(size_t) s]) * invN;
        const float prStep = (panTgt.right - slotPanR[(size_t) s]) * invN;
        float pl = slotPanL[(size_t) s], pr = slotPanR[(size_t) s];
        for (int i = 0; i < n; ++i)
        {
            panL[(size_t) i] += x[i] * pl;  pl += plStep;
            panR[(size_t) i] += x[i] * pr;  pr += prStep;
        }
        slotPanL[(size_t) s] = panTgt.left;
        slotPanR[(size_t) s] = panTgt.right;
    }

    // ── Banco FIR: una convolución por bus (costo fijo) acumulada en el wet HRIR ─────────────────
    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    for (int b = 0; b < kNumBuses; ++b)
    {
        auto&  bus = buses[(size_t) b];
        float* in  = busBuf.data() + (size_t) b * (size_t) maxBlock;

        if (busActive[(size_t) b])
            bus.zeroRun = 0;
        else
            bus.zeroRun += n;

        // Skip bit-transparente: la entrada fue exactamente cero por más del largo del FIR + el
        // ITD del bus -> el estado interno (FIR y delay del oído lejano) ya es cero y la salida
        // sería cero. No es un gate audible.
        if (bus.zeroRun > firTaps + bus.itdL + bus.itdR + 64) continue;

        // test-only: energía de entrada por bus (mide el reparto angular real de las ganancias).
        double e = 0.0;
        for (int i = 0; i < n; ++i) e += (double) in[i] * (double) in[i];
        busEnergy[(size_t) b] += e;

        std::copy (in, in + n, scratch.begin());
        {
            float* chL[1] = { scratch.data() };
            juce::dsp::AudioBlock<float> abL (chL, 1, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctxL (abL);
            bus.firL.process (ctxL);
        }
        {
            float* chR[1] = { in };
            juce::dsp::AudioBlock<float> abR (chR, 1, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctxR (abR);
            bus.firR.process (ctxR);
        }
        // ITD por bus: el oído LEJANO sale retrasado su delay FIJO (Woodworth, ver prepare).
        // El delay jamás cambia -> sin clicks; entre buses sólo se crossfadean ganancias.
        if (bus.itdL + bus.itdR > 0)
        {
            float*    far = (bus.itdL > 0) ? scratch.data() : in;
            const int d   = bus.itdL + bus.itdR;
            for (int i = 0; i < n; ++i)
            {
                bus.dly[(size_t) (bus.dlyW & (long) bus.dlyMask)] = far[i];
                far[i] = bus.dly[(size_t) ((bus.dlyW - (long) d) & (long) bus.dlyMask)];
                ++bus.dlyW;
            }
        }

        // Decorrelador del bus (DESBOXY): all-pass Schroeder de delay FIJO por bus, IDÉNTICO a L/R
        // -> magnitud unitaria (no colorea) y no toca el ITD/ILD (la dirección queda intacta). Sólo
        // dispersa la FASE de agudos distinto por bus -> las copias coherentes que el power-pan mete
        // en buses adyacentes dejan de cancelarse al sumar = se va el comb "encajonado". Coef fijo.
        if (bus.apN > 0)
        {
            const float g = kApGain;
            const int   d = bus.apN;
            for (int i = 0; i < n; ++i)
            {
                const float xl = scratch[(size_t) i];
                const float dl = bus.apL[(size_t) ((bus.apWL - d) & 63)];
                const float yl = -g * xl + dl;
                bus.apL[(size_t) (bus.apWL & 63)] = xl + g * yl;
                scratch[(size_t) i] = yl;  ++bus.apWL;

                const float xr = in[i];
                const float dr = bus.apR[(size_t) ((bus.apWR - d) & 63)];
                const float yr = -g * xr + dr;
                bus.apR[(size_t) (bus.apWR & 63)] = xr + g * yr;
                in[i] = yr;  ++bus.apWR;
            }
        }

        juce::FloatVectorOperations::add (outL, scratch.data(), n);
        juce::FloatVectorOperations::add (outR, in, n);
    }

    // ── Air shelf FIJO (DESBOXY) sobre el wet HRIR (mismo filtro a L y R -> no toca la imagen) ──
    // High-shelf de 1er orden, transposed direct form II. Devuelve el aire que la coloración común
    // del banco se llevaba. Corre SIEMPRE (estado continuo) sobre el camino HRIR antes del cross-fade.
    for (int i = 0; i < n; ++i)
    {
        const float xl = outL[i];
        outL[i] = airB0 * xl + airZL;
        airZL   = airB1 * xl - airA1 * outL[i];
        const float xr = outR[i];
        outR[i] = airB0 * xr + airZR;
        airZR   = airB1 * xr - airA1 * outR[i];
    }

    // ── Cross-fade HRIR <-> paneo (IN PHASE) por LEY DE POTENCIA, rampeado por-sample ───────────
    // (regla de la casa: los cross-fades van por potencia constante — el peso lineal w/(1−w)
    // metía un dip de nivel breve al togglear IN PHASE; hallazgo de review.)
    const float wEnd  = hrirWeightSm + (hrirWeightTgt - hrirWeightSm) * hrirWeightCoefBlock;
    const float wStep = (wEnd - hrirWeightSm) * invN;
    float w = hrirWeightSm;
    for (int i = 0; i < n; ++i)
    {
        const float gH = std::sin (w * kHalfPiF);   // w=1 -> HRIR pleno · w=0 -> paneo pleno
        const float gP = std::cos (w * kHalfPiF);
        outL[i] = outL[i] * gH + panL[(size_t) i] * gP;
        outR[i] = outR[i] * gH + panR[(size_t) i] * gP;
        w += wStep;
    }
    hrirWeightSm = wEnd;
}

} // namespace dust::engine
