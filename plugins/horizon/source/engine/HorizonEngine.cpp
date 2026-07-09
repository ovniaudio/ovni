#include "engine/HorizonEngine.h"
#include <cmath>

namespace horizon
{

// =====================================================================================
// Constantes de la física del freeze (ver header). Nombradas acá (no mágicas).
// =====================================================================================
namespace
{
    // COTA DURA del re-trigger (curaduría): más rápido que 8 Hz = AM/zumbido, no groove.
    constexpr float kRateCapHz = 8.0f;

    // DUCK: ballistics del envelope-follower del dry + referencia de normalización.
    // Una pegada a −12 dBFS (0.25) agacha el wet del todo con DUCK=100.
    constexpr float kDuckAttackS  = 0.005f;
    constexpr float kDuckReleaseS = 0.150f;
    constexpr float kDuckRefAmp   = 0.25f;

    // GATE: fracción del ciclo que el gate pasa "abierto" (sostiene). El resto es el
    // valle (silencio del gate). attack/decay = mitad de la transición raised-cosine.
    // attack+sustain+decay+valley = 1 ciclo. Con 0.5 de sostén y ~0.18 de subida/bajada
    // el ataque/decay caen en ~5–15 ms a las divisiones del rango (raised-cosine suave).
    constexpr double kGateRiseFrac = 0.18;   // subida (raised-cosine)
    constexpr double kGateHoldFrac = 0.45;   // sostén abierto
    constexpr double kGateFallFrac = 0.18;   // bajada (raised-cosine)
    // valle = 1 − rise − hold − fall = 0.19 (el gate cierra entre disparos)

    inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }

    // IDENTITY PHASE LOCKING (Laroche & Dolson 1999, "identity/region phase locking"): un
    // componente cae en UN bin pico pero la ventana Hann reparte su energía en el lóbulo vecino
    // (±2 bins). El freeze ingenuo resintetiza CADA bin a SU PROPIA frecuencia de bin (k·sr/N) →
    // los bins del lóbulo avanzan a ritmos DISTINTOS y laten unos contra otros: el lóbulo "rota"
    // contra sí mismo cada hop → la suma OLA de los 4 frames solapados se cancela parcialmente y
    // RE-CONSTRUYE → el congelado PULSA (modulación de amplitud al ritmo del frame, ~47 Hz a
    // 48k/512) y PIERDE NIVEL. Medido: 10–17 dB de modulación pico-a-valle en banda ancha y cama
    // de firma — ESTO es lo que se oye como "raro / no hace nada útil", no una caída de nivel.
    //
    // La cura honesta (100% FASE, magnitud/timbre INTACTOS, mono-recuperable): cada bin avanza la
    // fase a la frecuencia del PICO espectral que lo DOMINA (la región de influencia llega hasta
    // el punto medio con el pico vecino). Así CADA lóbulo se mueve RÍGIDO (todos sus bins al mismo
    // ritmo) → cero batido inter-bin → el congelado queda QUIETO (modulación residual <2 dB) y a
    // nivel pleno. Es identity locking: a diferencia del enganche por-lóbulo-prominente anterior
    // (que dejaba TODO el broadband sin enganchar → latía), cubre TODO el espectro asignando cada
    // bin a su pico más cercano. No toca |X[k]| → el timbre/alias floor quedan bit-idénticos y el
    // SPREAD (offset de fase opuesto L/R, aplicado DESPUÉS) sigue abriendo la imagen igual.
    constexpr float kPeakFloorRel = 1.0e-4f;   // piso relativo al pico máximo (~−80 dB): debajo, ruido de fondo

    // CURVA PERCEPTUAL del WHISPER (curaduría: 0–30 casi coherente; default 12 vidrioso,
    // NO ruidoso). Mapea 0..1 del knob → cantidad EFECTIVA de fase random 0..1 con un
    // codo: el primer tercio queda muy comprimido (12 % → ~1.7 % de random; 30 % → ~9 %).
    // Cúbica: w³ es casi plana cerca de 0 y abre fuerte arriba (whisperization a 100).
    inline float whisperCurve (float w01) noexcept
    {
        const float w = clamp01 (w01);
        return w * w * w;   // 0.12³ ≈ 0.0017 · 0.30³ = 0.027 · 1.0 = 1.0
    }

    // Envolvente del GATE en la fase 0..1 del ciclo (raised-cosine en subida y bajada).
    // Devuelve 0 (cerrado) .. 1 (abierto). Suave en los bordes → sin click metálico.
    inline float gateEnvelope (double phase01) noexcept
    {
        const double rise = kGateRiseFrac;
        const double hold = kGateHoldFrac;
        const double fall = kGateFallFrac;
        if (phase01 < rise)
            // subida raised-cosine: 0 → 1
            return (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (phase01 / rise)));
        if (phase01 < rise + hold)
            return 1.0f;   // sostén abierto
        if (phase01 < rise + hold + fall)
            // bajada raised-cosine: 1 → 0
            return (float) (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi
                                                   * ((phase01 - rise - hold) / fall)));
        return 0.0f;   // valle: cerrado entre disparos
    }

    // ════════════════════════════════════════════════════════════════════════════════════
    // SHIMMER ESPECTRAL MONO-AUDIBLE — el fix de "no se oye en mono" (2026-06-13).
    // ════════════════════════════════════════════════════════════════════════════════════
    // El SPREAD/WHISPER del vivo vivían en el SIDE / fase opuesta L/R → L+R cancela el ancho, y
    // con whisper³ ≈ 0 al default (0.12³ ≈ 0.0017) el vivo era ~identity en mono → "no hace nada"
    // (Joaquín, por oído, 2026-06-13). HORIZON es DIFUSO/glassy: acá lo proyectamos a la SUMA MONO
    // con un SHIMMER ESPECTRAL = cada bin respira en MAGNITUD a una fase DISTINTA (binDecorr, random
    // por bin) bajo un LFO lento común → la energía se MUEVE por el espectro = wash glassy que se
    // OYE sumado a mono. Aplicado IGUAL en L y R (mono-compatible: no toca CORR/WIDTH; IN PHASE lo
    // conserva, que es el punto). Distinto del peine ORDENADO de AURORA: acá es RANDOM por bin =
    // difusión, no movimiento tonal.
    //   gShim[k] = max( kShimFloor, 1 + depth·wHi(f_k)·sin(shimmerPhase + 2π·binDecorr[k]) )
    //   depth    = kShimWhisperDep · √WHISPER
    // Lo MANEJA el WHISPER (no un piso): WHISPER 0 → depth 0 → shimmer APAGADO → el vivo vuelve a
    // identity (el piso de alias [identity] del STFT queda intacto). WHISPER arriba → wash difuso
    // glassy mono-audible. La curva √ lo deja YA PRESENTE en el default 12 (√0.12≈0.35) → "carga
    // sonando" sin tocar la LEY del default 12. (El whisper de FASE sigue su curva cúbica propia,
    // glassy; este es el eje de MAGNITUD que mueve la energía y se oye en mono.) wHi(f) deja los
    // graves sólidos. El shimmer vive en el VIVO (liveGain): a FREEZE pleno se apaga (liveGain→0)
    // → el [alias] del whisper-toll [freeze ON] no lo ve.
    constexpr float kShimWhisperDep = 1.0f;    // escala del wash (× √WHISPER); a 100 satura al floor (muy difuso)
    constexpr float kShimFloor      = 0.25f;   // piso de gShim (sin nulls totales ni flip)
    constexpr float kShimRateHz     = 0.18f;   // LFO lento (sub-audio: difuso, NO AM)
    constexpr float kShimLoHz       = 160.0f;  // bajo esto: graves SÓLIDOS (sin wobble de bajo)
    constexpr float kShimFullHz     = 700.0f;  // arriba: shimmer pleno

    // HEADROOM del WIDENER VIVO (FREEZE off): el espectro vivo reconstruye a UNIDAD (full-scale
    // in → wet full-scale) y con MIX 100 + SPREAD (des-correlación de fase por bin) el crest del
    // wet roza ~1.0 ANTES del limiter → el limiter del sello (0.85) queda comprimiendo estado-
    // estacionario en material caliente. Este trim baja el pico pre-limiter a ~0.90 (margen) y
    // devuelve el limiter a RED DE SEGURIDAD. Escala L y R POR IGUAL → CORR/WIDTH intactos (el
    // ancho audible NO cambia; sólo el nivel absoluto del wet vivo, −0.9 dB). Sólo el VIVO
    // (va en liveGain); el FREEZE reconstruye pleno (freezeMag·frameWet, sin tocar).
    constexpr float kLiveHeadroom = 0.90f;   // −0.9 dB de margen pre-limiter del widener vivo
}

// ------------------------------------------------------------------------------ prepare
void HorizonEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sr       = spec.sampleRate;
    maxBlock = (int) spec.maximumBlockSize;

    // N por sample-rate: 2048 @44.1/48k; a 96k se duplica para mantener ~el mismo TIEMPO
    // de frame (~43 ms) → misma resolución espectral percibida (igual que AURORA).
    const int fftN = (sr >= 88200.0) ? 4096 : 2048;

    // 2 canales con el MISMO mid: cada frame ve el espectro X[k] duplicado y el
    // FrameProcessor lo reparte L/R (des-correlación del Spread). El FrameProcessor se
    // setea ACÁ (setup-time: asignar un std::function puede alocar — nunca en el audio thread).
    stft.prepare (sr, maxBlock, 2, fftN);
    stft.setFrameProcessor ([this] (const ovni::engines::StftEngine::FrameView& f) { processFrame (f); });

    limiter.setTuning ({ 0.85f, 60.0f });   // techo del sello (margen true-peak ~−1.4 dB)
    limiter.prepare (sr);

    // Dry delay = latencia del STFT (alineación dry↔wet del MIX, cero comb).
    dryRingSz = stft.latencySamples() + maxBlock;
    for (auto& ring : dryRing) ring.assign ((size_t) dryRingSz, 0.0f);
    dryWrite = 0;

    dryScratch.setSize (2, maxBlock);
    dryScratch.clear();

    // Tablas por bin (el frame sólo las LEE; el delta de fase coherente del hop se
    // precalcula: φ_inc[k] = 2π·k·hop/N → avance de fase de un seno en el bin k tras un hop).
    const int nb  = stft.numBins();
    const int N   = stft.fftSize();
    const int hop = stft.hopSize();
    freezeMag.assign     ((size_t) nb, 0.0f);
    freezePhase.assign   ((size_t) nb, 0.0f);
    coherentPhase.assign ((size_t) nb, 0.0f);
    binPhaseInc.assign   ((size_t) nb, 0.0f);
    lockPhaseInc.assign  ((size_t) nb, 0.0f);   // se llena en cada captura (peak-lock)
    binDecorr.assign     ((size_t) nb, 0.0f);
    binVizBand.assign    ((size_t) nb, 0);
    // Offset de des-correlación FIJO por bin (semilla fija → reproducible). Signo/magnitud
    // pseudo-random por bin: SPREAD lo escala. RANDOM por bin (no un offset constante) → la
    // correlación L/R cae MONÓTONA al subir spread (un offset igual en todos los bins sólo
    // rota la fase global = sigue correlado). DC (k=0) y Nyquist quedan al centro (offset 0).
    {
        juce::Random decorrRng (0x48524E44);   // "HRND"
        for (int k = 0; k < nb; ++k)
            binDecorr[(size_t) k] = (k == 0 || k == nb - 1) ? 0.0f
                                                            : (decorrRng.nextFloat() * 2.0f - 1.0f);
    }
    for (int k = 0; k < nb; ++k)
    {
        // delta nominal de fase del hop por bin, REDUCIDO mod 2π: el valor crudo (2π·k·hop/N) para
        // bins altos llega a ~1600 rad y perdería bits de mantisa en float32 al acumularse; como la
        // fase es periódica, reducir mod 2π es idéntico y preserva la precisión (house-standard §1).
        {
            const double raw = juce::MathConstants<double>::twoPi * (double) k * (double) hop / (double) N;
            const double wrapped = raw - juce::MathConstants<double>::twoPi * std::floor (raw / juce::MathConstants<double>::twoPi);
            binPhaseInc[(size_t) k] = (float) wrapped;
        }
        lockPhaseInc[(size_t) k] = binPhaseInc[(size_t) k];   // identity hasta la 1ª captura
        const float frac = (float) k / (float) juce::jmax (1, nb - 1);   // 0..1 lineal en bins (visual)
        binVizBand[(size_t) k] = juce::jlimit (0, kVizBands - 1, (int) (frac * (float) kVizBands));
    }

    // Ballistics del duck + coeficientes de suavizado por frame (post-mapeo).
    envAtkCoef = 1.0f - std::exp (-1.0f / (kDuckAttackS  * (float) sr));
    envRelCoef = 1.0f - std::exp (-1.0f / (kDuckReleaseS * (float) sr));
    const float hopDur = (float) hop / (float) sr;
    smoothCoef = 1.0f - std::exp (-hopDur / 0.030f);   // macros: τ ≈ 30 ms
    gammaCoef  = 1.0f - std::exp (-hopDur / 0.008f);   // duck: τ ≈ 8 ms (sigue la pegada sin zipper)

    // Cross-fade del freeze: ~4 frames (≈ N samples = un overlap completo) → empalme suave.
    freezeXfStep = 1.0f / 4.0f;

    reset();
}

void HorizonEngine::reset()
{
    stft.reset();
    limiter.reset();
    for (auto& ring : dryRing) std::fill (ring.begin(), ring.end(), 0.0f);
    dryWrite = 0;
    dryScratch.clear();

    std::fill (freezeMag.begin(),     freezeMag.end(),     0.0f);
    std::fill (freezePhase.begin(),   freezePhase.end(),   0.0f);
    std::fill (coherentPhase.begin(), coherentPhase.end(), 0.0f);

    frozen      = false;
    captureNext = false;
    prevFreeze  = false;
    freezeXf    = 0.0f;

    gatePhase01 = 0.0;
    gatePhaseInc = 0.0;
    gateAmpSm   = 1.0f;
    gateActive  = false;

    env = envHopMax = envNorm = envSm = 0.0f;
    hopPhase = 0;
    duckSm = 0.0f;
    monoSafeSm = 1.0f;

    vizSpectrum.fill (0.0f);
    gateAmpTele = 1.0f;
    gatePhaseTele = 0.0f;
    wetEnergyTele = 0.0f;
    primeSmoothing = true;
}

// ------------------------------------------------------------------------------ process
void HorizonEngine::process (juce::AudioBuffer<float>& buffer, const HorizonParams& p)
{
    const int n = buffer.getNumSamples();
    if (n <= 0 || dryRingSz <= 0) return;

    // Defensa contra hosts FUERA DE CONTRATO (n > maximumBlockSize): procesar en sub-bloques.
    if (n > maxBlock)
    {
        jassertfalse;
        int done = 0;
        while (done < n)
        {
            const int len = juce::jmin (maxBlock, n - done);
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(),
                                          buffer.getNumChannels(), done, len);
            process (sub, p);
            done += len;
        }
        return;
    }

    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // --- detección de flanco del gesto FREEZE -----------------------------------------
    // Flanco de subida (off→on): pedir captura en el próximo frame + arrancar cross-fade
    // hacia el frame congelado. Flanco de bajada (on→off): cross-fade de vuelta al audio
    // vivo (el frozen real se apaga cuando el cross-fade llega a 0, abajo).
    if (p.freeze && ! prevFreeze) { captureNext = true; }
    prevFreeze = p.freeze;

    // --- objetivos de los suavizadores (mapeo ACÁ; el suavizado corre por frame) ------
    whisperTgt  = clamp01 (p.whisper01);
    spreadTgt   = clamp01 (p.spread01);
    duckTgt     = clamp01 (p.duck01);
    monoSafeTgt = p.monoSafe ? 0.0f : 1.0f;   // IN PHASE → la des-correlación rampa a 0 (mono-safe)

    // --- GATE: rate efectivo CAPADO a 8 Hz (cota dura). 0 = sostenido (pad plano) ----
    const float rateHz = juce::jlimit (0.0f, kRateCapHz, p.rateHz);
    gateActive = (rateHz > 1.0e-4f);
    gatePhaseInc = (double) rateHz * (double) stft.hopSize() / sr;
    // SHIMMER: LFO lento del wash difuso (independiente del rate del usuario; siempre deriva).
    shimPhaseInc = juce::MathConstants<double>::twoPi * (double) kShimRateHz * (double) stft.hopSize() / sr;
    if (gateActive && p.rateSync && p.isPlaying)
    {
        // SYNC: la fase del latido se ENGANCHA a ppq (el gate dispara en el beat — late
        // con el track, no deriva libre). Lock al inicio del bloque; los frames internos
        // avanzan con gatePhaseInc. El frame que dispara este bloque se OYE (hop−hopPhase)
        // samples después del feed y la salida corre N atrás por el PDC → se compensa el
        // offset para que el latido caiga EN la grilla del host.
        // El one-pole gateAmpSm (coef 0.5/frame) retrasa la amplitud OÍDA del gate:
        // group delay ≈ hop·(1−c)/c samples. Se compensa para que el latido caiga EN la grilla.
        const double gateLagSamples = (double) stft.hopSize() * (1.0 - (double) kGateSmCoef) / (double) kGateSmCoef;
        const double beats = (double) juce::jmax (0.01f, p.beatsPerCycle);
        const double heardOffset = (double) (stft.hopSize() - hopPhase)        // feed → 1er frame
                                 - 0.5 * (double) stft.latencySamples()         // centroide OLA tras PDC
                                 + gateLagSamples;                              // lag del suavizado del gate
        double cyc = p.ppqPosition / beats + (double) rateHz * heardOffset / sr;
        cyc -= std::floor (cyc);
        gatePhase01 = cyc;
    }

    if (primeSmoothing)
    {
        whisperSm = whisperTgt; spreadSm = spreadTgt; duckSm = duckTgt;
        monoSafeSm = monoSafeTgt;
        envSm = envNorm;
        const float phi0 = clamp01 (p.mix01) * juce::MathConstants<float>::halfPi;
        gDry = std::cos (phi0); gWet = std::sin (phi0);
        primeSmoothing = false;
    }

    // --- 1)+2) preservar el DRY + construir el MID + STFT, en CHUNKS alineados al HOP.
    // El envNorm del DUCK se fija en cada FRONTERA DE HOP con el max del período recién
    // completado → cada frame ve la MISMA envolvente sin importar el block-size del host.
    for (int ch = 0; ch < numCh; ++ch)
        dryScratch.copyFrom (ch, 0, buffer, ch, 0, n);

    const bool monoIn = (p.numInputChannels <= 1);
    {
        const float* inL = dryScratch.getReadPointer (0);
        const float* inR = dryScratch.getReadPointer (numCh > 1 ? 1 : 0);
        float* w0 = buffer.getWritePointer (0);
        float* w1 = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;
        const int hop = stft.hopSize();

        int done = 0;
        while (done < n)
        {
            const int len = juce::jmin (n - done, hop - hopPhase);

            for (int i = done; i < done + len; ++i)
            {
                // Mono de entrada: el mid ES el canal único (sin el −6 dB de promediar con un R vacío).
                const float mid = monoIn ? inL[i] : 0.5f * (inL[i] + inR[i]);
                w0[i] = mid;
                if (w1 != nullptr) w1[i] = mid;

                // Envelope follower del DRY (attack 5 ms / release 150 ms) → el DUCK lo consume por frame.
                const float a = std::abs (mid);
                env += (a > env ? envAtkCoef : envRelCoef) * (a - env);
                envHopMax = juce::jmax (envHopMax, env);
            }

            hopPhase += len;
            if (hopPhase >= hop)
            {
                envNorm   = clamp01 (envHopMax / kDuckRefAmp);
                envHopMax = env;
                hopPhase  = 0;
            }

            // Feed del chunk al STFT/OLA (dispara processFrame en la frontera).
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), numCh, done, len);
            stft.process (sub);
            done += len;
        }
    }

    // --- 3) dry delay (latencySamples) + 4) MIX por ley de potencia (rampa por-sample)
    // La ganancia del WET (duck·freeze cross-fade) ya quedó horneada por frame en el
    // espectro resintetizado; acá sólo cruzamos dry/wet. (El wet "vivo" cuando no congela
    // es identity → MIX 100 transparente.)
    const float phi     = clamp01 (p.mix01) * juce::MathConstants<float>::halfPi;
    const float gDryTgt = std::cos (phi);
    const float gWetTgt = std::sin (phi);
    const int   delay   = stft.latencySamples();
    {
        const float stepD = (gDryTgt - gDry) / (float) n;
        const float stepW = (gWetTgt - gWet) / (float) n;
        float gd = gDry, gw = gWet;
        int w = dryWrite;
        const float* dSrc0 = dryScratch.getReadPointer (0);
        const float* dSrc1 = dryScratch.getReadPointer (numCh > 1 ? 1 : 0);
        float* out0 = buffer.getWritePointer (0);
        float* out1 = (numCh > 1) ? buffer.getWritePointer (1) : nullptr;
        float* ring0 = dryRing[0].data();
        float* ring1 = dryRing[1].data();
        for (int i = 0; i < n; ++i)
        {
            const float d0 = dSrc0[i];
            const float d1 = monoIn ? dSrc0[i] : dSrc1[i];
            ring0[w] = d0;
            ring1[w] = d1;
            int r = w - delay; if (r < 0) r += dryRingSz;
            const float dly0 = ring0[r];
            const float dly1 = ring1[r];
            out0[i] = gd * dly0 + gw * out0[i];
            if (out1 != nullptr) out1[i] = gd * dly1 + gw * out1[i];
            if (++w == dryRingSz) w = 0;
            gd += stepD; gw += stepW;
        }
        dryWrite = w;
        gDry = gDryTgt;
        gWet = gWetTgt;
    }

    // --- 5) limiter de salida estéreo-linked (techo 0.85, red de seguridad del sello)
    if (numCh > 1)
        limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n);
    else
        limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (0), n);
}

// ------------------------------------------------------------------------- buildPeakLock
// Construye lockPhaseInc[] del frame RECIÉN capturado (Laroche-Dolson IDENTITY phase locking):
// cada bin avanza la fase a la frecuencia del PICO espectral que lo domina (no a la suya), donde
// la "región de influencia" de un pico llega hasta el PUNTO MEDIO con el pico vecino. Resultado:
// CADA lóbulo se mueve RÍGIDO (todos sus bins al mismo ritmo) → cero batido inter-bin → el freeze
// queda QUIETO, a nivel pleno (sin la modulación/pulso ni la pérdida del avance por-bin). NO toca
// la magnitud (timbre/alias floor bit-idénticos; el SPREAD abre la imagen DESPUÉS, igual que antes).
// Determinista, O(nb), sin allocations (escribe sobre lockPhaseInc preasignado). nb ≤ numBins.
void HorizonEngine::buildPeakLock (float maxMag, int nb) noexcept
{
    // Default: cada bin con su propio incremento nominal (fallback si no hay ningún pico).
    for (int k = 0; k < nb; ++k)
        lockPhaseInc[(size_t) k] = binPhaseInc[(size_t) k];

    // Frame sin energía significativa: nada que enganchar (queda en identity por-bin arriba).
    if (maxMag <= 0.0f) return;

    const float floorMag = maxMag * kPeakFloorRel;   // bins por debajo: ruido de fondo (no son picos)

    // Recorrido único de izquierda a derecha. Mantenemos el ÚLTIMO pico visto (prevPeak) y, al
    // hallar el SIGUIENTE pico (curPeak), repartimos la franja [prevPeak..curPeak] en el punto
    // medio: la mitad izquierda hereda el ritmo de prevPeak, la derecha el de curPeak. Así TODO
    // el espectro queda cubierto y cada bin sigue a su pico más cercano (identity locking).
    int prevPeak = -1;
    for (int k = 1; k < nb - 1; ++k)
    {
        const float m = freezeMag[(size_t) k];
        if (m < floorMag) continue;
        // pico = máximo local estricto-por-un-lado (≥ ambos vecinos, > al menos uno) → sin mesetas planas.
        if (m < freezeMag[(size_t) (k - 1)] || m < freezeMag[(size_t) (k + 1)]) continue;
        if (m <= freezeMag[(size_t) (k - 1)] && m <= freezeMag[(size_t) (k + 1)]) continue;

        const float curInc = binPhaseInc[(size_t) k];
        if (prevPeak < 0)
        {
            // primer pico: TODO lo anterior (incluido DC) sigue a este pico.
            for (int b = 0; b <= k; ++b) lockPhaseInc[(size_t) b] = curInc;
        }
        else
        {
            const float prevInc = binPhaseInc[(size_t) prevPeak];
            const int   mid     = (prevPeak + k) / 2;   // frontera de regiones (punto medio)
            for (int b = prevPeak + 1; b <= mid; ++b) lockPhaseInc[(size_t) b] = prevInc;
            for (int b = mid + 1;      b <= k;   ++b) lockPhaseInc[(size_t) b] = curInc;
        }
        prevPeak = k;
    }

    // cola: del último pico hasta Nyquist sigue a ese pico.
    if (prevPeak >= 0)
    {
        const float lastInc = binPhaseInc[(size_t) prevPeak];
        for (int b = prevPeak + 1; b < nb; ++b) lockPhaseInc[(size_t) b] = lastInc;
    }
}

// -------------------------------------------------------------------------- processFrame
// El DSP espectral por frame. Corre DENTRO de stft.process() en cada frame (hop = N/4).
// Ambos canales traen el MISMO espectro X[k] del mid. Acá:
//   · SIEMPRE (congele o no): construye un WET VIVO espacializado — cada bin del mid se
//     reparte L/R con offset de fase OPUESTO (SPREAD, des-correlación) + difusión de fase
//     (WHISPER) → el goniómetro abre sin congelar (fix audible 2026-06-10). IN PHASE
//     (monoSafeSm→0) anula el offset → mono-compatible.
//   · si está congelando/congelado: resintetiza ADEMÁS el frame capturado (avance de fase
//     coherente peak-lock + whisper + spread), gateado por el re-trigger, agachado por el duck.
//   · cross-fade de frame (freezeXf) entre el WET VIVO espacializado y el frame congelado.
void HorizonEngine::processFrame (const ovni::engines::StftEngine::FrameView& f)
{
    // — suavizado por frame de las macros (post-mapeo) —
    whisperSm  += (whisperTgt  - whisperSm)  * smoothCoef;
    spreadSm   += (spreadTgt   - spreadSm)   * smoothCoef;
    duckSm     += (duckTgt     - duckSm)     * smoothCoef;
    monoSafeSm += (monoSafeTgt - monoSafeSm) * smoothCoef;   // IN PHASE rampa (anti-click)
    envSm      += (envNorm     - envSm)      * gammaCoef;

    // — SHIMMER mono-audible: avanzar el LFO lento (por frame) + profundidad (piso + whisper) —
    shimmerPhase += shimPhaseInc;
    if (shimmerPhase >= juce::MathConstants<double>::twoPi)
        shimmerPhase -= juce::MathConstants<double>::twoPi;
    // ∝ √WHISPER: 0 en whisper 0 (identity intacta) pero ya PRESENTE en el default 12 (√0.12≈0.35
    // → "carga sonando" sin tocar la LEY del default); satura suave hacia whisper 100.
    const float shimDepth = kShimWhisperDep * std::sqrt (clamp01 (whisperSm));

    float* s0 = f.spectra[0];
    float* s1 = f.spectra[1];
    const int nb = f.numBins;

    // — CAPTURA del frame (gesto FREEZE recién activado): tomar mag+fase del MID actual —
    if (captureNext)
    {
        float maxMag = 0.0f;
        for (int k = 0; k < nb; ++k)
        {
            const float re = s0[2 * k];
            const float im = s0[2 * k + 1];
            const float mag = std::sqrt (re * re + im * im);
            freezeMag[(size_t) k]     = mag;
            freezePhase[(size_t) k]   = std::atan2 (im, re);
            coherentPhase[(size_t) k] = freezePhase[(size_t) k];   // siembra el acumulador coherente
            maxMag = juce::jmax (maxMag, mag);
        }

        // PEAK-LOCK: cada bin avanza la fase a la frecuencia DEL PICO regional (anti-anillado).
        // Sin pico cercano (ruido de fondo) → su propio incremento nominal. Magnitud INTACTA.
        buildPeakLock (maxMag, nb);

        captureNext = false;
        frozen      = true;
    }

    // — cross-fade de frame: hacia 1 si el gesto está ON, hacia 0 si está OFF —
    const float xfTgt = (prevFreeze ? 1.0f : 0.0f);
    if (freezeXf < xfTgt) freezeXf = juce::jmin (xfTgt, freezeXf + freezeXfStep);
    else if (freezeXf > xfTgt) freezeXf = juce::jmax (xfTgt, freezeXf - freezeXfStep);

    // Si el cross-fade llegó a 0 y el gesto está OFF: el motor YA NO congela (apaga el frozen
    // real). NO retorna identity: cae al WET VIVO espacializado de abajo (SPREAD+WHISPER sobre
    // el espectro vivo del mid). Antes acá había un early-return identity que dejaba L=R=mid →
    // con MIX 100 (gDry=0) la salida quedaba MONO. Esa era la causa raíz del bug de Joaquín.
    if (freezeXf <= 0.0f && ! prevFreeze)
        frozen = false;

    // — GATE RÍTMICO: amplitud raised-cosine del ciclo (1 = pad sostenido si RATE off) —
    float gateAmp = 1.0f;
    if (gateActive)
    {
        gateAmp = gateEnvelope (gatePhase01);
        gatePhase01 += gatePhaseInc;
        if (gatePhase01 >= 1.0) gatePhase01 -= std::floor (gatePhase01);
    }
    // suavizado extra de la amplitud del gate (cross-fade por disparo, anti-click sobre la
    // ya-suave raised-cosine — derivada acotada en los bordes).
    gateAmpSm += (gateAmp - gateAmpSm) * kGateSmCoef;   // FUENTE ÚNICA del coef (ver SYNC lag, .h)

    // — WHISPER: cantidad efectiva de fase random por frame (curva perceptual) —
    const float whisperAmt = whisperCurve (whisperSm);

    // — SPREAD: offset de fase RANDOM por bin (binDecorr), OPUESTO en L/R, escalado por spread.
    //   Mantiene la magnitud igual en ambos canales (potencia constante: |L|=|R| por bin) →
    //   ancho envolvente sin romper energía. A spread 0 = mono (offset 0); a spread 100 el
    //   offset máximo por bin llega a ±π·kSpreadMaxScale → des-correlación creciente y MONÓTONA
    //   (es RANDOM por bin con semilla fija: nunca colapsa a mono — un offset CONSTANTE de π sí lo
    //   haría, por eso es random; cada bin sale a un ángulo distinto). Antes el techo era ±π/2 y
    //   abría poco una vez que el freeze dejó de latir (el batido viejo daba ancho-basura). Con el
    //   freeze YA QUIETO subimos el techo a ±0.6π: a spread 100 la imagen abre de verdad (CORR→~0,
    //   WIDTH>1, comparable o mejor que DUST) y la WIDTH crece MONÓTONA todo el recorrido (a ±0.8π+
    //   el WIDTH se da vuelta cerca del tope — deshonesto: el knob narrowearía pasado 75; ±0.6π lo
    //   evita y deja margen true-peak). IN PHASE (bass-mono del chasis) es el escape mono-safe.
    constexpr float kSpreadMaxScale = 0.60f;   // ±0.6π por bin a spread 100 (abre fuerte, WIDTH monótona, margen TP)
    // IN PHASE (monoSafeSm→0) escala el offset a 0 → L=R = mono-compatible (escape mono, rampeado).
    const float spreadMaxOff = spreadSm * monoSafeSm * kSpreadMaxScale * juce::MathConstants<float>::pi;

    // — LIVE WIDENER: ganancia de la textura VIVA espacializada (no congelada). Con freeze off
    //   vale 1 (todo el wet es el vivo); durante el cross-fade hacia el freeze cae a 0. El duck
    //   NO la toca (el vivo sigue al dry naturalmente); el gate tampoco (el latido es del freeze).
    // El gate del re-trigger modula TAMBIÉN el vivo (gateActive = RATE>0) → HORIZON groovea SIN
    // congelar: chop espectral rítmico, mono-audible. Sin RATE = 1 (sin gate). El X-fade lo cierra.
    const float liveGain = kLiveHeadroom * (1.0f - freezeXf) * (gateActive ? clamp01 (gateAmpSm) : 1.0f);

    // — ganancia del WET del frame: gate (latido) × duck (la pegada agacha) × freeze X-fade.
    //   Se hornea acá en la magnitud resintetizada; el MIX la cruza con el dry sin más.
    const float duckGain  = 1.0f - duckSm * envSm;                 // 1 = sin duck · →0 con pegada y duck alto
    const float frameWet  = clamp01 (gateAmpSm) * clamp01 (duckGain) * freezeXf;

    float wetEnergyAcc = 0.0f;
    float specViz[kVizBands] = {};
    int   specCnt[kVizBands] = {};

    for (int k = 0; k < nb; ++k)
    {
        // — LIVE: espectro vivo del mid en este bin (ambos canales lo traen igual: L=R=mid). —
        const float liveRe   = s0[2 * k];
        const float liveIm   = s0[2 * k + 1];
        const float liveMag0 = std::sqrt (liveRe * liveRe + liveIm * liveIm);
        float       livePhase = std::atan2 (liveIm, liveRe);

        // SHIMMER MONO-AUDIBLE: respiración de magnitud por bin a fase RANDOM (binDecorr) bajo el
        // LFO lento → la energía se mueve por el espectro = wash difuso glassy que se OYE sumado a
        // mono. IGUAL en L y R (no toca |L|/|R| → CORR/WIDTH intactos). wHi deja los graves sólidos.
        // Sólo el VIVO (el frozen usa freezeMag, intacto → el freeze a nivel/quieto no cambia).
        const float fkShim  = (float) k * (float) sr / (float) f.fftSize;
        const float wHiShim = clamp01 ((fkShim - kShimLoHz) / (kShimFullHz - kShimLoHz));
        const float gShim   = juce::jmax (kShimFloor,
            1.0f + shimDepth * wHiShim
                 * std::sin ((float) shimmerPhase + juce::MathConstants<float>::twoPi * binDecorr[(size_t) k]));
        const float liveMag = liveMag0 * gShim;

        // Fase coherente acumulada del bin congelado (avance nominal del hop) → vidrioso/limpio.
        float phase = coherentPhase[(size_t) k];

        // WHISPER: interpola hacia una fase RANDOM por frame (whisperization). A 0 amt
        // queda 100 % coherente (vidrioso); a 1 amt = fase totalmente random (difuso). UN SOLO
        // draw por bin (mismo delta aplicado al frozen Y al vivo → reproducible + difusión común).
        if (whisperAmt > 1.0e-4f)
        {
            const float rnd = (rng.nextFloat() * 2.0f - 1.0f) * juce::MathConstants<float>::pi;
            // frozen: interpolación angular suave hacia rnd
            float d = rnd - phase;
            while (d >  juce::MathConstants<float>::pi) d -= juce::MathConstants<float>::twoPi;
            while (d < -juce::MathConstants<float>::pi) d += juce::MathConstants<float>::twoPi;
            phase += whisperAmt * d;
            // vivo: misma whisperization hacia el MISMO ángulo random (difusión del wet vivo)
            float dl = rnd - livePhase;
            while (dl >  juce::MathConstants<float>::pi) dl -= juce::MathConstants<float>::twoPi;
            while (dl < -juce::MathConstants<float>::pi) dl += juce::MathConstants<float>::twoPi;
            livePhase += whisperAmt * dl;
        }

        const float off = spreadMaxOff * binDecorr[(size_t) k];

        // — WET CONGELADO: frame capturado, gateado/agachado por frameWet (0 si no congela). —
        const float mag = freezeMag[(size_t) k] * frameWet;
        const float phL = phase - off;
        const float phR = phase + off;
        const float reLf = mag * std::cos (phL);
        const float imLf = mag * std::sin (phL);
        const float reRf = mag * std::cos (phR);
        const float imRf = mag * std::sin (phR);

        // — WET VIVO ESPACIALIZADO: el mismo bin del mid repartido L/R con offset OPUESTO
        //   (SPREAD, des-correlación) + difusión (WHISPER). MISMA magnitud en L y R → potencia
        //   constante, side generado por el desfase. ESTO abre el goniómetro SIN congelar. A
        //   spread 0 (u IN PHASE) el offset es 0 → L=R = passthrough del mid (mono-compatible). —
        const float lphL = livePhase - off;
        const float lphR = livePhase + off;
        const float reLl = liveGain * liveMag * std::cos (lphL);
        const float imLl = liveGain * liveMag * std::sin (lphL);
        const float reRl = liveGain * liveMag * std::cos (lphR);
        const float imRl = liveGain * liveMag * std::sin (lphR);

        // Salida del bin = WET VIVO espacializado (peso liveGain=1−freezeXf) + WET CONGELADO.
        s0[2 * k]     = reLl + reLf;
        s0[2 * k + 1] = imLl + imLf;
        s1[2 * k]     = reRl + reRf;
        s1[2 * k + 1] = imRl + imRf;

        // avance de fase coherente del bin (peak-lock: a la frecuencia del PICO regional → sin
        // batido inter-bin = vidrioso, no anillado; fuera de un pico = su propio delta nominal).
        float np = coherentPhase[(size_t) k] + lockPhaseInc[(size_t) k];
        while (np >= juce::MathConstants<float>::twoPi) np -= juce::MathConstants<float>::twoPi;
        coherentPhase[(size_t) k] = np;

        // telemetría: energía del wet (congelado + vivo) + espectro por banda. Con freeze off
        // el vivo es el que suena → el visual late con la textura espacializada, no muerto.
        const float liveMagW = liveGain * liveMag;
        wetEnergyAcc += mag * mag + liveMagW * liveMagW;
        const int b = binVizBand[(size_t) k];
        specViz[b] += freezeMag[(size_t) k] * frameWet + liveMagW;
        ++specCnt[b];
    }

    // — telemetría (lock-free; el processor la copia a los atomics) —
    const float ampScale = 2.0f / (float) f.fftSize;
    gateAmpTele   = clamp01 (gateAmpSm);
    gatePhaseTele = (float) gatePhase01;
    wetEnergyTele = clamp01 (std::sqrt (wetEnergyAcc) * ampScale * 4.0f);
    for (int b = 0; b < kVizBands; ++b)
    {
        const float amp = (specCnt[b] > 0) ? (specViz[b] / (float) specCnt[b]) * ampScale : 0.0f;
        vizSpectrum[(size_t) b] += 0.35f * (clamp01 (amp * 4.0f) - vizSpectrum[(size_t) b]);
    }
}

} // namespace horizon
