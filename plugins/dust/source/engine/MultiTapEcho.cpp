// MultiTapEcho.cpp — buffer circular multi-tap con feedback estable (motor de DUST, MOV·03).
// Regla de oro anti-click: TODO lo modulado (rate, ganancias de tap, feedback) se desliza
// POR-SAMPLE; las lecturas son fraccionales (Lagrange3rd) -> barrer RATE no chasquea.
#include "MultiTapEcho.h"
#include <cmath>

namespace dust::engine
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586476925286766559;

    // Mapeos de DENSIDAD (curaduría: feedback log con piso de estabilidad + estallido↔nube).
    inline int tapCountForDensity (float d) noexcept
    {
        return 3 + (int) std::lround ((double) d * 21.0);            // 3..24 burbujas objetivo
    }
    inline float tapDecayForDensity (float d) noexcept
    {
        return 0.30f + 0.65f * d;                                    // rho por slot: 0.30..0.95
    }
    inline double fbForDensity (float d) noexcept
    {
        // Curva cuadrática (~log en tiempo de decay): default 40 -> regeneración sutil (0.15),
        // 100 -> nube casi infinita (kFbMax) SIN diverger. Clamp duro por encima (seguridad).
        const double fb = (double) kFbMax * (double) d * (double) d;
        return std::min (fb, (double) kFbClamp);
    }
}

void MultiTapEcho::prepare (double sr, int maxBlockSize)
{
    sampleRate  = sr;
    maxBlock    = maxBlockSize;
    maxSpanSamp = (int) std::lround ((double) kMaxSpanSeconds * sr);

    // Buffer pow2 que cubre el span + margen de interpolación.
    int size = 1;
    while (size < maxSpanSamp + 8) size <<= 1;
    buffer.assign ((size_t) size, 0.0f);
    mask = size - 1;

    fadeSamples = juce::jmax (8, (int) std::lround (kFadeMs * 0.001 * sr));
    fadeStep    = 1.0f / (float) fadeSamples;

    // Coeficientes one-pole por-sample (dominio del destino, post-mapeo).
    rateCoef = 1.0 - std::exp (-1.0 / (0.080 * sr));   // τ=80 ms: el barrido de RATE re-pitchea suave (cinta)
    loopCoef = 1.0 - std::exp (-1.0 / (0.120 * sr));   // el punto de reinyección se desliza aún más lento
    fbCoef   = 1.0 - std::exp (-1.0 / (0.050 * sr));   // τ=50 ms: la ganancia de feedback nunca salta
    dampCoef = 1.0 - std::exp (-kTwoPi * (double) kLoopDampHz / sr);

    tapScratch.assign ((size_t) kMaxTaps * (size_t) maxBlockSize, 0.0f);
    reset();
}

void MultiTapEcho::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos  = 0;
    dampState = 0.0;
    fbGainSm  = 0.0;
    rateSmMs  = (double) rateTargetMs;
    densitySm = densityTarget;
    spreadSm  = spreadTarget;
    vidaSm    = vidaTarget;
    loopSm    = 0.0;
    spawnHold = 0;
    for (auto& t : taps) t = Tap {};
}

int MultiTapEcho::dbgLiveTaps() const noexcept
{
    int n = 0;
    for (const auto& t : taps) n += (t.active && ! t.dying) ? 1 : 0;
    return n;
}

// Lectura fraccional Lagrange 3er orden (4 puntos) a 'delaySamples' detrás de la escritura actual.
float MultiTapEcho::readLagrange (double delaySamples) const noexcept
{
    const double pos  = (double) writePos - delaySamples;   // posición absoluta fraccional
    const long   base = (long) std::floor (pos);
    const float  t    = (float) (pos - (double) base);

    const float xm = buffer[(size_t) ((base - 1) & mask)];
    const float x0 = buffer[(size_t) ( base      & mask)];
    const float x1 = buffer[(size_t) ((base + 1) & mask)];
    const float x2 = buffer[(size_t) ((base + 2) & mask)];

    // Lagrange 3er orden en forma Catmull-Rom-like (misma forma que resampleIR de SpatialEngine).
    return x0 + 0.5f * t * (x1 - xm
         + t * (2.0f * xm - 5.0f * x0 + 4.0f * x1 - x2
         + t * (3.0f * (x0 - x1) + x2 - xm)));
}

// Azimut absoluto del tap: ORIGIN + offset de nacimiento ESCALADO por el SPREAD vivo + deriva de
// VIDA (LFO lento por tap, amplitud escalada por la VIDA viva). Las dos perillas mueven la nube
// EXISTENTE (honestidad: girar SPREAD abre/cierra el campo ya sonando, no sólo los nacimientos);
// aguas abajo todo es rampa por-sample de ganancias de bus -> sin clicks.
float MultiTapEcho::tapAzimuth (const Tap& t, float originAz, double extraAgeSamples) const noexcept
{
    const double age   = (t.ageSamples + extraAgeSamples) / sampleRate;
    const float  drift = vidaSm * kDriftMaxRad * t.driftUnit
                       * std::sin (t.driftPhase + (float) (kTwoPi * (double) t.driftHz * age));
    return originAz + t.azOffsetUnit * juce::MathConstants<float>::pi * spreadSm + drift;
}

// Voice management (una vez por bloque): nacimientos y voice-stealing escalonados (1 por bloque)
// para que los micro-fades nunca se apilen. El robado es el slot MÁS ALTO vivo = el más débil
// (la ganancia decae con el slot) y el más viejo del tren (offset más largo).
void MultiTapEcho::updateVoices()
{
    const double rateSamp  = rateSmMs * 0.001 * sampleRate;
    const int    maxBySpan = juce::jlimit (1, kMaxTaps, (int) std::floor ((double) maxSpanSamp / juce::jmax (1.0, rateSamp)) - 1);
    const int    nTarget   = juce::jmin (tapCountForDensity (densitySm), maxBySpan);

    int live = 0;
    for (const auto& t : taps) live += (t.active && ! t.dying) ? 1 : 0;

    if (spawnHold > 0) { --spawnHold; return; }

    if (live < nTarget)
    {
        // Nace en el slot libre MÁS BAJO (ecos tempranos primero).
        for (int s = 0; s < kMaxTaps; ++s)
        {
            auto& t = taps[(size_t) s];
            if (t.active) continue;

            // Momento lateral del campo vivo (Σ g²·offset, con el MISMO rho del decay por slot):
            // el recién nacido toma el signo que lo CONTRARRESTA → el wet queda balanceado L/R.
            // (Hallazgo de review: con signo librado a la semilla fija, los slots tempranos —los
            // más fuertes— quedaban clavados de un lado: BAL −3.4 dB sostenido, idéntico en TODA
            // instancia. El contrapeso por energía es determinístico y auto-corrige también los
            // respawns del voice-stealing.)
            const float rhoNow = tapDecayForDensity (densitySm);
            float moment = 0.0f;
            for (int q = 0; q < kMaxTaps; ++q)
            {
                const auto& v = taps[(size_t) q];
                if (! v.active || v.dying) continue;
                const float g = std::pow (rhoNow, (float) q);
                moment += g * g * v.azOffsetUnit;
            }

            t = Tap {};
            t.active     = true;
            t.jitterFrac = 0.30f * vidaSm * (2.0f * rng.nextFloat() - 1.0f);  // jitter acotado, ∝ VIDA
            // SPREAD = varianza angular: MAGNITUD triangular (cargada al centro, |2 uniformes − 1|)
            // y signo de contrapeso; el ángulo real = unit · π · spreadSm VIVO (ver tapAzimuth).
            const float mag = std::abs ((rng.nextFloat() + rng.nextFloat()) - 1.0f);   // [0,1]
            t.azOffsetUnit = (moment > 0.0f) ? -mag : mag;
            t.driftUnit  = 0.5f + 0.5f * rng.nextFloat();
            t.driftHz    = 0.05f + 0.20f * rng.nextFloat();                   // deriva LENTA (flotar, no vibrar)
            t.driftPhase = kTwoPi * rng.nextFloat();
            t.env        = 0.0f;
            t.fadePhase  = 0.0f;
            t.gainSm     = 0.0f;
            spawnHold    = 1;   // escalona el próximo nacimiento/robo
            break;
        }
    }
    else if (live > nTarget)
    {
        // Voice-stealing: fade-out del slot vivo más alto (más viejo/débil). Nunca un drop duro.
        for (int s = kMaxTaps - 1; s >= 0; --s)
        {
            auto& t = taps[(size_t) s];
            if (t.active && ! t.dying)
            {
                t.dying     = true;
                t.fadePhase = 0.0f;
                spawnHold   = 1;
                break;
            }
        }
    }
}

int MultiTapEcho::process (const float* monoIn, int n,
                           float originAzStart, float originAzEnd,
                           std::array<TapRender, (size_t) kMaxTaps>& renders)
{
    // Control rate (una vez por bloque): suavizado de macros + voice management + targets de gain.
    const float ctrlCoef = 1.0f - std::exp (-(float) n / (0.050f * (float) sampleRate));  // τ=50 ms
    densitySm += (densityTarget - densitySm) * ctrlCoef;
    spreadSm  += (spreadTarget  - spreadSm)  * ctrlCoef;
    vidaSm    += (vidaTarget    - vidaSm)    * ctrlCoef;
    updateVoices();

    fbGainTgt = fbForDensity (densitySm);

    // Targets de ganancia por tap, con normalización de energía del lazo (curaduría): si la suma
    // de energías supera 1, se escala -> muchas burbujas no acumulan más energía que la entrada.
    const float rho = tapDecayForDensity (densitySm);
    float gTarget[(size_t) kMaxTaps];
    double sumE = 0.0;
    for (int s = 0; s < kMaxTaps; ++s)
    {
        const auto& t = taps[(size_t) s];
        gTarget[(size_t) s] = (t.active && ! t.dying) ? 0.85f * std::pow (rho, (float) s) : 0.0f;
        sumE += (double) gTarget[(size_t) s] * (double) gTarget[(size_t) s];
    }
    if (sumE > 1.0)
    {
        const float norm = (float) (1.0 / std::sqrt (sumE));
        for (auto& g : gTarget) g *= norm;
    }

    // Render: marca los taps activos ANTES del lazo por-sample (born/azStart con el estado actual).
    int numRenders = 0;
    int renderSlot[(size_t) kMaxTaps];
    const double rateSampStart = rateSmMs * 0.001 * sampleRate;
    for (int s = 0; s < kMaxTaps; ++s)
    {
        auto& t = taps[(size_t) s];
        if (! t.active) continue;
        auto& r   = renders[(size_t) numRenders];
        r.slot    = s;
        r.born    = (t.ageSamples <= 0.0);
        r.azStart = tapAzimuth (t, originAzStart, 0.0);
        r.gain    = gTarget[(size_t) s];   // energía de la burbuja (telemetría: el FIFO de eventos del processor)
        r.samples = tapScratch.data() + (size_t) s * (size_t) maxBlock;
        renderSlot[(size_t) numRenders] = s;
        ++numRenders;
    }

    // Pasos por-bloque -> rampas por-sample (gain del tap; el rate/feedback van con one-pole).
    float gStep[(size_t) kMaxTaps];
    for (int s = 0; s < kMaxTaps; ++s)
        gStep[(size_t) s] = (gTarget[(size_t) s] - taps[(size_t) s].gainSm) / (float) n;

    const double loopTgtMul = (double) juce::jlimit (1, kMaxTaps,
                                  juce::jmin (tapCountForDensity (densitySm),
                                              (int) std::floor ((double) maxSpanSamp / juce::jmax (1.0, rateSampStart)) - 1)) + 1.0;

    // ── Lazo por-sample ──────────────────────────────────────────────────────────────────────────
    for (int i = 0; i < n; ++i)
    {
        // RATE deslizado por-sample (dominio ms, post-mapeo log del knob).
        rateSmMs += ((double) rateTargetMs - rateSmMs) * rateCoef;
        const double rateSamp = rateSmMs * 0.001 * sampleRate;

        // Punto de reinyección: un período más allá del último tap directo, deslizado por-sample.
        const double loopTgt = juce::jlimit (4.0, (double) maxSpanSamp - 4.0, loopTgtMul * rateSamp);
        loopSm += (loopTgt - loopSm) * loopCoef;
        if (loopSm < 4.0) loopSm = loopTgt;   // primer bloque tras reset

        // Feedback (DENSIDAD): lectura del lazo -> damping interno (cada rebote más oscuro) ->
        // ganancia suavizada por-sample con clamp duro. Acumuladores en double.
        fbGainSm += (fbGainTgt - fbGainSm) * fbCoef;
        const double fbRaw = (double) readLagrange (loopSm);
        dampState += dampCoef * (fbRaw - dampState);
        const double fb = std::clamp (fbGainSm, 0.0, (double) kFbClamp) * dampState;

        // Normalización de energía del lazo (curaduría): la inyección se escala por sqrt(1-fb²)
        // -> la energía estacionaria del lazo ≈ la de la entrada (la nube crece en DENSIDAD y
        // duración, no en volumen) y el limiter no queda clavado enmascarando el decay.
        const double inScale = std::sqrt (std::max (0.0, 1.0 - fbGainSm * fbGainSm));

        // Escritura: entrada normalizada + reinyección.
        buffer[(size_t) (writePos & mask)] = monoIn[i] * (float) inScale + (float) fb;
        ++writePos;

        // Lecturas de los taps (fraccionales, offsets en movimiento -> sin clicks al barrer RATE).
        for (int k = 0; k < numRenders; ++k)
        {
            const int s = renderSlot[(size_t) k];
            auto&     t = taps[(size_t) s];

            // Offset clampeado al SPAN del buffer (techo, no sólo piso): en un sweep rápido de
            // RATE hacia arriba con DENSIDAD alta, los slots altos excederían el span antes de que
            // el voice-stealing (1 robo cada 2 bloques) los baje → la lectura enmascarada wrapearía
            // a un delay corto incorrecto (eco fantasma transitorio). El clamp lo cierra gratis;
            // los slots clampeados son los más débiles (rho^s) y el stealing los recoge enseguida.
            const double offset = juce::jlimit (4.0, (double) maxSpanSamp - 4.0,
                                                ((double) (s + 1) + (double) t.jitterFrac) * rateSamp);
            float y = readLagrange (offset);

            // Micro-fade raised-cosine de nacimiento/muerte.
            if (t.dying)
            {
                t.fadePhase = juce::jmin (1.0f, t.fadePhase + fadeStep);
                t.env = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * t.fadePhase);
            }
            else if (t.fadePhase < 1.0f)
            {
                t.fadePhase = juce::jmin (1.0f, t.fadePhase + fadeStep);
                t.env = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * t.fadePhase);
            }

            t.gainSm += gStep[(size_t) s];
            tapScratch[(size_t) s * (size_t) maxBlock + (size_t) i] = y * t.env * t.gainSm;
            t.ageSamples += 1.0;
        }
    }

    // Cierre del bloque: azimut final + libera los taps que terminaron su fade-out.
    for (int k = 0; k < numRenders; ++k)
    {
        const int s = renderSlot[(size_t) k];
        auto&     t = taps[(size_t) s];
        renders[(size_t) k].azEnd = tapAzimuth (t, originAzEnd, 0.0);
        if (t.dying && t.fadePhase >= 1.0f)
            t.active = false;   // el render de este bloque ya salió en silencio; el slot queda libre
    }

    return numRenders;
}

} // namespace dust::engine
