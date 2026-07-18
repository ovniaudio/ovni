#include "engines/fdn/FdnReverb.h"
#include <cmath>
#include <numeric>

namespace ovni::engines {

// =====================================================================================
// Motor FDN — Tasks 1-3.
//
// LAZO LOSSLESS (Task 1): N=8 líneas de delay realimentadas por una matriz Householder
// A = I − (2/N)·u·uᵀ con u = 1/√N (ortogonal → conserva energía: con g_i = 1 el lazo es EXACTAMENTE
// lossless). El producto matriz-vector se hace en 2N−1 sumas vía A·s = s − (2/N)·(Σ s_i)·1.
// Longitudes mutuamente primas (primos en samples) → evita resonancias degeneradas / sonido metálico.
//
// DECAY / T60 (Task 2): g_i = 10^(−3·M_i/(T60·f_s)) por línea → toda la red cae a −60 dB en T60 s
// (líneas más largas necesitan g más alto para el MISMO T60 percibido). T60 = t60ForDecay(decay01).
//
// ABSORCIÓN / T60(ω) (Task 2): un FirstOrderTPTFilter LP por línea DENTRO del lazo. Su ganancia en DC
// es 1 (no toca el T60 grave) pero atenúa los agudos → los agudos pierden energía cada vuelta → T60(ω)
// (la cola se oscurece como sala real). Corte = cutoffForTone(tone01).
//
// SIZE (Task 2): M_i = baseM_i · sizeScale(size01). Al cambiar Size recalculamos g_i para CONSERVAR el
// T60 percibido (más espacio = vueltas más largas = menos vueltas por segundo). La longitud aplicada se
// rampea (delaySamplesSm) → el Lagrange3rd interpola sin clicks (cross-fade de la línea).
//
// EARLY REFLECTIONS (Task 2): FIR corto en paralelo (24 taps, tiempos primos crecientes, ganancias
// decrecientes) sobre el input mono → patrón temprano discreto sumado al wet → externalización ("fuera
// de la cabeza") en auriculares. L y R con ganancias ligeramente distintas → ancho.
//
// BREATH (Task 3): BreathLFO PROPIO (suma de 3 senos incoherentes 0.05–0.3 Hz, normalizado a [−1,1]).
// breath01 = profundidad con que modula sizeScale LENTO → el espacio "inhala/exhala". Pasa por
// SmoothedValue → sin zipper.
//
// FREEZE (Task 3): decay01=1 && freeze → g_i = 1.0 exacto, absorción en bypass y la inyección de input
// CORTADA → la red sostiene sin diverger (sub-unidad en el borde). El test lossless de Task 1 depende
// de esto.
//
// DE-ZIPPER (Task 3): juce::SmoothedValue para size/decay(T60)/tone(cutoff)/mix DESPUÉS del mapeo.
// ScopedNoDenormals en process. StereoLimiter de salida (anti-clip) + decorrelación L/R (Task 1).
// =====================================================================================

namespace {

// Longitudes base en ms (primos perceptuales). Una por línea (kN = 8).
constexpr std::array<int, 8> kBaseMs { 23, 29, 37, 43, 53, 61, 71, 83 };

// Tiempos base de las early reflections en ms (primos crecientes; 24 taps). Patrón temprano discreto.
constexpr std::array<int, 24> kErMs {
    7,  11,  13,  17,  19,  23,  29,  31,
    37,  41,  43,  47,  53,  59,  61,  67,
    71,  73,  79,  83,  89,  97, 101, 103
};

// Rango del T60 (segundos) y del corte de absorción (Hz). Mapeos LOG (skew perceptual).
constexpr float kT60Min   = 0.1f;     // decay01 = 0 → cola muy corta
constexpr float kT60Max   = 12.0f;    // decay01 = 1 → cola muy larga (antes del freeze)
constexpr float kToneHiHz  = 18000.0f; // tone01 = 0 → casi sin damping (agudos pasan)
constexpr float kToneLoHz  = 1500.0f;  // tone01 = 1 → mucho damping (cola oscura)
constexpr float kSizeMin   = 0.15f;    // size01 = 0 → cuarto chico
constexpr float kSizeMax   = 1.0f;     // size01 = 1 → espacio grande (~2 s con las bases dadas)

// Profundidad máxima de la respiración como fracción del sizeScale (±). Lento y sutil (no chorus).
constexpr float kBreathMaxFrac = 0.18f;

// Tiempo de de-zipper de las macros (ms). Suficiente para matar el escalón sin enchastrar la respuesta.
constexpr double kSmoothMs       = 60.0;
// La modulación de respiración es MUY lenta → un smoothing más largo la mantiene orgánica.
constexpr double kBreathSmoothMs = 250.0;

bool isPrime (int n) noexcept
{
    if (n < 2) return false;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; (long long) d * d <= n; d += 2)
        if (n % d == 0) return false;
    return true;
}

// Primo más cercano a 'n' (busca hacia afuera desde n). Garantiza longitudes coprimas (dos primos
// distintos no comparten factores → mcd = 1).
int nearestPrime (int n) noexcept
{
    if (n < 2) return 2;
    for (int off = 0; ; ++off)
    {
        if (isPrime (n + off)) return n + off;
        if (n - off >= 2 && isPrime (n - off)) return n - off;
    }
}

// Interpolación geométrica (log) entre a y b según t∈[0,1]. a·(b/a)^t.
float logLerp (float a, float b, float t) noexcept
{
    return a * std::pow (b / a, juce::jlimit (0.0f, 1.0f, t));
}

} // namespace

// ── InputDiffuser: 4 allpass Schroeder en serie sobre la INYECCIÓN al lazo ────────────────────────
// y[n] = −g·x[n] + x[n−M] + g·y[n−M]  (forma directa: el ring guarda v[n] = x[n] + g·y[n]).
// Retardos primos ~5/7.9/11.3/14.7 ms (coprimos con las líneas) → densidad sin coloración nueva.
void FdnReverb::InputDiffuser::prepare (double sr)
{
    constexpr double ms[kStages] = { 5.0, 7.9, 11.3, 14.7 };
    for (int a = 0; a < kStages; ++a)
    {
        len[(size_t) a] = nearestPrime (juce::jmax (2, (int) std::lround (ms[a] * 0.001 * sr)));
        buf[(size_t) a].assign ((size_t) len[(size_t) a], 0.0f);
        wr[(size_t) a] = 0;
    }
}

void FdnReverb::InputDiffuser::reset() noexcept
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    wr.fill (0);
}

float FdnReverb::InputDiffuser::process (float x) noexcept
{
    for (int a = 0; a < kStages; ++a)
    {
        float* b = buf[(size_t) a].data();
        int&   w = wr[(size_t) a];
        const float d = b[w];              // x[n−M] + g·y[n−M]
        const float y = d - kG * x;
        b[w] = x + kG * y;
        if (++w >= len[(size_t) a]) w = 0;
        x = y;
    }
    return x;
}

// ── Mapeos perceptuales públicos ──────────────────────────────────────────────────────────────────
float FdnReverb::t60ForDecay (float decay01) noexcept   { return logLerp (kT60Min,  kT60Max, decay01); }
float FdnReverb::cutoffForTone (float tone01) noexcept   { return logLerp (kToneHiHz, kToneLoHz, tone01); }
float FdnReverb::sizeScaleForSize (float size01) noexcept { return juce::jmap (juce::jlimit (0.0f, 1.0f, size01), kSizeMin, kSizeMax); }

// ── BreathLFO ─────────────────────────────────────────────────────────────────────────────────────
void FdnReverb::BreathLFO::prepare (double sr) noexcept
{
    // Tres senos incoherentes (frecuencias no múltiplos entre sí) en la banda 0.05–0.3 Hz → una
    // envolvente lenta que nunca se repite obvio (≠ chorus periódico). En modo libre arrancan en freeHz.
    sampleRate = (sr > 0.0 ? sr : 48000.0);
    for (int k = 0; k < kOsc; ++k)
        inc[k] = juce::MathConstants<double>::twoPi * freeHz[k] / sampleRate;
    norm = 1.0 / (double) kOsc;   // suma de kOsc senos en [−1,1] → /kOsc mantiene [−1,1]
    reset();
}

void FdnReverb::BreathLFO::reset() noexcept
{
    // Fases iniciales distintas → arranca ya "en movimiento", no todas alineadas en 0.
    constexpr double ph0[kOsc] = { 0.0, 2.1, 4.2 };
    for (int k = 0; k < kOsc; ++k) phase[k] = ph0[k];
}

float FdnReverb::BreathLFO::next (double baseHz) noexcept
{
    // Objetivo de frecuencia de cada oscilador: libre (freeHz) o sincronizado (escala proporcional para
    // preservar el carácter orgánico: el principal a baseHz, los secundarios por la misma razón que libre).
    const bool   synced = (baseHz > 0.0);
    const double ratio  = synced ? (baseHz / freeBaseHz) : 1.0;

    // De-zipper de la frecuencia: rampamos inc[] hacia el objetivo (coef ~ unos cientos de ms) → al
    // togglear SYNC la respiración cambia de ritmo SUAVE, sin salto de fase ni click. La fase sigue
    // siendo continua (sólo cambia su velocidad), así que la envolvente RMS no da un escalón.
    constexpr double kIncCoef = 0.0008;   // glide lento de la frecuencia (anti-zipper del propio rate)
    double s = 0.0;
    for (int k = 0; k < kOsc; ++k)
    {
        const double tgtHz  = freeHz[k] * ratio;
        const double tgtInc = juce::MathConstants<double>::twoPi * tgtHz / sampleRate;
        inc[k] += (tgtInc - inc[k]) * kIncCoef;

        s += std::sin (phase[k]);
        phase[k] += inc[k];
        if (phase[k] >= juce::MathConstants<double>::twoPi) phase[k] -= juce::MathConstants<double>::twoPi;
    }
    return (float) (s * norm);   // [−1,1]
}

// ── prepare / rebuild ───────────────────────────────────────────────────────────────────────────────
void FdnReverb::rebuildLines (float sizeScale)
{
    // Longitudes base (sizeScale=1) en samples desde los ms primos, al primo más cercano (coprimas) y
    // distintas entre sí. Se calculan una vez; Size escala sobre estas.
    maxLenAlloc = 0;
    for (size_t i = 0; i < (size_t) kN; ++i)
    {
        const int raw = (int) std::lround (kBaseMs[i] * sampleRate / 1000.0);
        int len = nearestPrime (juce::jmax (raw, 2));
        for (size_t j = 0; j < i; ++j)
            while (len == (int) std::lround (baseSamples[j])) len = nearestPrime (len + 1);
        baseSamples[i] = (float) len;
        // Capacidad: la longitud base es el máximo (sizeScale ≤ 1) → reservamos con headroom.
        maxLenAlloc = juce::jmax (maxLenAlloc, len);
    }

    // Capacidad por línea: la longitud puede llegar a base · sizeScaleMax · (1 + breathMax) cuando Size
    // está al tope y la respiración infla al máximo. Reservamos para ese peor caso + headroom del
    // Lagrange3rd (si no, modular el delay por encima del máximo desborda la línea).
    const int lineCap = (int) std::ceil (maxLenAlloc * kSizeMax * (1.0f + kBreathMaxFrac)) + 8;

    const juce::dsp::ProcessSpec monoSpec { sampleRate, (juce::uint32) maxBlock, 1 };
    for (size_t i = 0; i < (size_t) kN; ++i)
    {
        lines[i].setMaximumDelayInSamples (lineCap);   // peor caso (Size+breath) + headroom Lagrange3rd
        lines[i].prepare (monoSpec);

        const float target = juce::jmax (2.0f, baseSamples[i] * sizeScale);
        delaySamples[i]    = target;
        delaySamplesSm[i]  = target;
        lines[i].setDelay (target);

        absorb[i].prepare (monoSpec);
        absorb[i].setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
        absorb[i].setCutoffFrequency (kToneHiHz);
        absorb[i].reset();
    }

    // Early reflections: línea circular dimensionada al tap más largo (en samples) con headroom.
    int erMax = 0;
    for (size_t t = 0; t < (size_t) kErTaps; ++t)
    {
        erDelay[t] = juce::jmax (1, (int) std::lround (kErMs[t] * sampleRate / 1000.0));
        erMax      = juce::jmax (erMax, erDelay[t]);
        // Ganancias decrecientes (las reflexiones tardías llegan más débiles) con leve distinción L/R.
        const float g = 0.62f * std::pow (0.86f, (float) t);
        const bool  toL = (t & 1u) == 0u;
        erGainL[t] = toL ? g : g * 0.72f;
        erGainR[t] = toL ? g * 0.72f : g;
    }
    erLen   = erMax + 1;
    erBuf.assign ((size_t) erLen, 0.0f);
    erWrite = 0;
}

void FdnReverb::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = (spec.sampleRate > 0.0 ? spec.sampleRate : 48000.0);
    maxBlock   = (int) juce::jmax<juce::uint32> (1, spec.maximumBlockSize);

    rebuildLines (sizeScaleForSize (0.5f));
    diffuser.prepare (sampleRate);

    // De-zipper de macros (se snappean al primer process según los params reales).
    const double sr = sampleRate;
    sizeScaleSm  .reset (sr, kSmoothMs       * 0.001);
    t60Sm        .reset (sr, kSmoothMs       * 0.001);
    cutoffSm     .reset (sr, kSmoothMs       * 0.001);
    mixSm        .reset (sr, kSmoothMs       * 0.001);
    breathDepthSm.reset (sr, kBreathSmoothMs * 0.001);

    // Scratch del WET (sin dry) para limitar SÓLO la cola: reservamos al peor bloque. El dry NO entra al
    // limiter → se suma DESPUÉS, transparente (la señal directa del usuario no debe distorsionarse, §4/§6).
    wetScratchL.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
    wetScratchR.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);

    breath.prepare (sr);
    limiter.prepare (sr);
    // Limiter de SALIDA con lookahead sobre la SUMA dry+wet (anti-clip DEFINITIVO). Lookahead ~3 ms (la
    // latencia que Joaquín aceptó), ceiling 0.95 true-peak-safe, release suave. Es el que gobierna lo que
    // sale al DAW: contiene la suma dry+wet (cola + dry full-scale) que el limiter de wet, por estar sólo
    // sobre el wet, NO veía. Su latencia se reporta al host (NebulaProcessor::prepareEngine → setLatencySamples).
    outLimiter.prepare (sr, maxBlock);
    outLimiter.setTuning ({ /*ceiling*/ 0.95f, /*lookaheadMs*/ 3.0f, /*releaseMs*/ 80.0f });
    // Techo true-peak-safe del limiter (anti-clip inter-sample, ver references/anti-click-clip-truepeak.md §3).
    // El limiter de sample-peak garantiza |muestra| ≤ ceiling, pero NO el true-peak: con transientes HF
    // agresivas (impulsos/percusión) la onda reconstruida ENTRE muestras sobrepasa el ceiling un factor
    // fijo (~×1.10, medido) → con el 0.85 por defecto el true-peak llegaba a ~0.92–0.94 y el medidor
    // true-peak del DAW marcaba clip ("clipea un poco" — el oído de Joaquín lo cazó, igual que en ÓRBITA).
    // 0.80 deja el true-peak peor caso en ~0.88 (≈ −1.1 dBFS, el margen estándar de mastering) sin
    // sacrificar nivel audible (−0.5 dB de techo) ni agregar latencia. NOTA: es local de NÉBULA (no toca
    // el default 0.85 del StereoLimiter compartido que usan PULSAR/movement).
    limiter.setTuning ({ /*ceiling*/ 0.80f, /*releaseMs*/ 60.0f });
    firstBlock = true;
    reset();
}

void FdnReverb::reset()
{
    for (auto& l : lines)  l.reset();
    for (auto& f : absorb) f.reset();
    diffuser.reset();
    feedback.fill (0.0f);
    std::fill (erBuf.begin(), erBuf.end(), 0.0f);
    erWrite = 0;
    breath.reset();
    limiter.reset();
    outLimiter.reset();        // vacía el retardo del lookahead (sin cola vieja al re-arrancar)
    limiterProbeGain = 1.0f;   // réplica del limiter del probe (clip-scan)
    firstBlock = true;
}

// g_i por línea para un T60 dado (la absorción HF la pone el filtro aparte; en DC el LP no atenúa).
void FdnReverb::updateFeedbackGains (float t60Seconds) noexcept
{
    const float t60 = juce::jmax (0.02f, t60Seconds);
    for (size_t i = 0; i < (size_t) kN; ++i)
    {
        // M_i = longitud ACTUAL aplicada (rampeada) → conserva el T60 percibido al cambiar Size.
        const float m = juce::jmax (1.0f, delaySamplesSm[i]);
        const float g = std::pow (10.0f, -3.0f * m / (t60 * (float) sampleRate));
        feedback[i]   = juce::jmin (g, 0.9995f);   // clamp < 1 (estabilidad)
    }
}

// ── process ───────────────────────────────────────────────────────────────────────────────────────
void FdnReverb::process (juce::AudioBuffer<float>& buffer, const FdnParams& p)
{
    juce::ScopedNoDenormals noDenormals;

    const int n     = buffer.getNumSamples();
    const int bufCh = buffer.getNumChannels();
    if (n <= 0 || bufCh <= 0) return;

    // ── Mapeo de las macros → destinos de los smoothers (de-zipper DESPUÉS del mapeo). ───────────
    const bool  isFrozen   = (p.freeze && p.decay01 >= 0.999f);
    const float t60Target  = t60ForDecay  (p.decay01);
    const float cutTarget  = cutoffForTone (p.tone01);
    const float sizeTarget = sizeScaleForSize (p.size01);
    const float mixTarget  = juce::jlimit (0.0f, 1.0f, p.mix01);
    const float breathTgt  = juce::jlimit (0.0f, 1.0f, p.breath01);

    if (firstBlock)   // arranque: sin rampa desde 0 (evita un swell/zip en el primer bloque)
    {
        sizeScaleSm  .setCurrentAndTargetValue (sizeTarget);
        t60Sm        .setCurrentAndTargetValue (t60Target);
        cutoffSm     .setCurrentAndTargetValue (cutTarget);
        mixSm        .setCurrentAndTargetValue (mixTarget);
        breathDepthSm.setCurrentAndTargetValue (breathTgt);
        firstBlock = false;
    }
    sizeScaleSm  .setTargetValue (sizeTarget);
    t60Sm        .setTargetValue (t60Target);
    cutoffSm     .setTargetValue (cutTarget);
    mixSm        .setTargetValue (mixTarget);
    breathDepthSm.setTargetValue (breathTgt);

    auto* inL = buffer.getReadPointer (0);
    auto* inR = bufCh > 1 ? buffer.getReadPointer (1) : inL;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = bufCh > 1 ? buffer.getWritePointer (1) : nullptr;

    const float twoOverN = 2.0f / (float) kN;             // factor de la reflexión Householder
    const float outScale = 1.0f / std::sqrt ((float) kN); // normaliza la suma de kN líneas a L/R

    // Scratch del WET (sin dry): acá escribimos SÓLO la cola (wetG·full) por-sample. El limiter actúa sobre
    // ESTO (la cola difusa, donde casi no trabaja) y el dry se suma DESPUÉS, intacto → la señal directa del
    // usuario queda transparente (un reverb no debe distorsionar el dry, §4/§6). Reservado en prepare al
    // maxBlock; si por las dudas el bloque viniera más grande, lo agrandamos (no debería pasar en RT).
    if ((int) wetScratchL.size() < n) { wetScratchL.assign ((size_t) n, 0.0f); wetScratchR.assign ((size_t) n, 0.0f); }
    float* wL = wetScratchL.data();
    float* wR = wetScratchR.data();

    // Acumuladores de etapa para el probe de clip-scan (sólo si hay probe; si no, no se tocan).
    float dryPk = 0.0f, wetPk = 0.0f, totalPk = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        // Entrada mono: la fuente que excita el reverb (preservamos el dry aparte para el mix).
        const float dryL = inL[i];
        const float dryR = inR[i];
        const float in   = 0.5f * (dryL + dryR);

        // ── Macros suavizadas de este sample ───────────────────────────────────────────────────
        const float baseScale   = sizeScaleSm.getNextValue();
        const float t60         = t60Sm.getNextValue();
        const float cutoff      = cutoffSm.getNextValue();
        const float mix         = mixSm.getNextValue();
        const float breathDepth = breathDepthSm.getNextValue();

        // ── Respiración: el BreathLFO propio modula sizeScale LENTO alrededor del centro. Funciona
        //    también en freeze (es la firma "Infinito": el espacio congelado igual inhala/exhala); el
        //    glide de las longitudes (kLenCoef) + Lagrange3rd evitan clicks al mover los delays.
        //    SYNC: si breathRateHz>0 respira ENGANCHADO al tempo (esa frecuencia); ≤0 = libre/orgánico.
        //    El de-zipper del rate vive dentro del LFO → togglear SYNC no salta la respiración. ────
        const float lfo   = breath.next ((double) p.breathRateHz);
        lastBreathLfo     = lfo;   // telemetría (la nube de NÉBULA respira en fase con esto)
        const float scale = baseScale * (1.0f + kBreathMaxFrac * breathDepth * lfo);

        // ── Cross-fade de las longitudes (anti-click): rampa suave delaySamplesSm → objetivo. ────
        // Coef ~ tiempo de de-zipper; recalcula g_i con la longitud ACTUAL para conservar el T60.
        constexpr float kLenCoef = 0.0015f;   // ~ unos ms de glide de la longitud
        for (size_t k = 0; k < (size_t) kN; ++k)
        {
            float tgt = juce::jmax (2.0f, baseSamples[k] * scale);
            // FREEZE lossless DE VERDAD: una longitud FRACCIONAL obliga a interpolar (Lagrange) y cada
            // vuelta por el interpolador lowpassea un pelo → el congelado perdía −0.24 dB/s y los agudos
            // se apagaban (T60 HF ~14 s, MEDIDO). Con la longitud SNAPEADA a entero la lectura es exacta
            // (frac 0 = tap directo) → sostén eterno. El salto de convergencia es sub-sample (< 0.5) y el
            // glide kLenCoef lo suaviza; con BREATH > 0 el freeze "que respira" sigue siendo fraccional a
            // propósito (la firma "Infinito" respira — y esa pérdida leve es física, no bug).
            if (isFrozen)
            {
                tgt = std::round (tgt);
                if (std::abs (tgt - delaySamplesSm[k]) < 0.5f) delaySamplesSm[k] = tgt;   // converge YA
            }
            delaySamples[k]   = tgt;
            delaySamplesSm[k] += (tgt - delaySamplesSm[k]) * kLenCoef;
            lines[k].setDelay (delaySamplesSm[k]);
        }

        // ── g_i del decay (o 1.0 exacto en freeze). Absorción: corte del LP (bypass en freeze). ──
        if (isFrozen)
        {
            feedback.fill (1.0f);   // lossless exacto en el borde → sostiene sin diverger
        }
        else
        {
            updateFeedbackGains (t60);
            for (size_t k = 0; k < (size_t) kN; ++k)
                absorb[k].setCutoffFrequency (cutoff);
        }

        // 1) Leer las kN salidas de las líneas (estado de delay actual).
        std::array<float, (size_t) kN> s {};
        float sum = 0.0f;
        for (size_t k = 0; k < (size_t) kN; ++k)
        {
            s[k] = lines[k].popSample (0);
            sum += s[k];
        }

        // 2) Mezcla HOUSEHOLDER lossless: y = s − (2/N)·(Σ s)·1 (ortogonal → conserva energía).
        const float reflect = twoOverN * sum;

        // 3) Reinyectar y_k·g_i (+ absorción LP) + input. En freeze g=1 (lossless en el borde) y la
        //    inyección se ATENÚA fuerte (kFreezeInject): la cola entrante "siembra" la red congelada,
        //    pero una señal sostenida ya no acumula sin control (un lazo lossless integra el input) —
        //    el StereoLimiter de salida cierra la red de seguridad. La absorción se bypassa en freeze
        //    para que el sostén sea EXACTAMENTE lossless (ni se oscurece ni decae).
        constexpr float kFreezeInject = 0.25f;
        // Difusión de ENTRADA: el lazo recibe el input DIFUNDIDO (4 allpass Schroeder → densidad de eco
        // pro en <150 ms); las early reflections (5) siguen leyendo el input CRUDO (patrón discreto).
        // p.inputDiffusion=false cuando el FDN vive dentro de un lazo regenerativo mayor (HALO).
        const float inDiff = p.inputDiffusion ? diffuser.process (in) : in;
        const float inj    = isFrozen ? kFreezeInject * inDiff : inDiff;
        for (size_t k = 0; k < (size_t) kN; ++k)
        {
            float y = s[k] - reflect;                       // componente k tras Householder
            if (! isFrozen) y = absorb[k].processSample (0, y);   // damping HF → T60(ω)
            lines[k].pushSample (0, y * feedback[k] + inj);
        }

        // 4) Cola estéreo decorrelada: L y R suman subconjuntos distintos de las líneas con signos
        //    alternados (mitad/mitad, patrón Hadamard 1-D) → cola ancha (no mono).
        float wetL = 0.0f, wetR = 0.0f;
        for (size_t k = 0; k < (size_t) kN; ++k)
        {
            const float sign = (k & 1u) ? -1.0f : 1.0f;
            if (k < (size_t) (kN / 2)) wetL += sign * s[k];
            else                       wetR += sign * s[k];
        }
        wetL *= outScale;
        wetR *= outScale;

        // 5) Early reflections: FIR corto en paralelo sobre el input mono (externalización). En freeze
        //    la inyección al ER se atenúa igual que al lazo (las reflexiones tempranas de un input
        //    sostenido no deben crecer); la línea sigue drenando lo que ya tenía.
        erBuf[(size_t) erWrite] = inj;
        float erL = 0.0f, erR = 0.0f;
        for (size_t t = 0; t < (size_t) kErTaps; ++t)
        {
            int idx = erWrite - erDelay[t];
            if (idx < 0) idx += erLen;
            const float v = erBuf[(size_t) idx];
            erL += v * erGainL[t];
            erR += v * erGainR[t];
        }
        if (++erWrite >= erLen) erWrite = 0;

        float fullL = wetL + erL;
        float fullR = wetR + erR;

        // 5b) IN PHASE (mono-safe): colapsá la cola WET a mono ANTES del mix. La cola del FDN está muy
        //     decorrelada (CORR≈0.14) → sumar L+R a un solo canal la pone EN FASE (CORR→1, mono-compatible:
        //     sin cancelaciones al monoficar). El DRY no se toca (se mezcla intacto más abajo) → IN PHASE
        //     afecta sólo la reverb, no la señal directa. El bass-mono del chasis corre aparte (inofensivo
        //     sobre una cola ya mono).
        if (p.monoSafe)
        {
            const float m = 0.5f * (fullL + fullR);
            fullL = fullR = m;
        }

        // 6) Mix dry/wet por ley de potencia (sin bache de energía al recorrer Mix). CLAVE del anti-clip
        //    (§4/§6): el DRY (señal directa del usuario) va DIRECTO a la salida; el WET (cola difusa) va al
        //    scratch para limitarse APARTE. Así el limiter NO caza los transientes del dry (su attack
        //    instantáneo los distorsionaría → "clip/vinilo" perceptual aunque el nivel quede ≤ techo). El
        //    dry queda transparente; el wet se contiene; se suman después.
        const float wetG = std::sin (mix * juce::MathConstants<float>::halfPi);
        const float dryG = std::cos (mix * juce::MathConstants<float>::halfPi);

        float wetSL, wetSR;
        if (outR != nullptr)
        {
            const float dL = dryG * dryL, dR = dryG * dryR;   // dry → salida (intacto)
            outL[i] = dL;
            outR[i] = dR;
            wetSL = wetG * fullL;                             // wet (crudo) → scratch (lo limita el paso 7)
            wetSR = wetG * fullR;
            if (probe)
            {
                dryPk = juce::jmax (dryPk, juce::jmax (std::abs (dL), std::abs (dR)));   // (1)
                wetPk = juce::jmax (wetPk, juce::jmax (std::abs (wetSL), std::abs (wetSR)));   // (2) wet PRE
                totalPk = juce::jmax (totalPk, juce::jmax (std::abs (dL + wetSL), std::abs (dR + wetSR)));   // (3) total PRE
            }
        }
        else
        {
            const float wetMono = 0.70710678f * (fullL + fullR);   // fold de la cola (energía constante)
            const float dL = dryG * dryL;
            outL[i] = dL;
            wetSL = wetSR = wetG * wetMono;   // duplicado: el limiter estéreo-linked ve el mismo pico
            if (probe)
            {
                dryPk = juce::jmax (dryPk, std::abs (dL));                       // (1)
                wetPk = juce::jmax (wetPk, std::abs (wetSL));                    // (2) wet PRE
                totalPk = juce::jmax (totalPk, std::abs (dL + wetSL));          // (3) total PRE
            }
        }
        wL[i] = wetSL;
        wR[i] = wetSR;
    }

    // ── Trabajo del limiter (sólo clip-scan): replica EXACTA del StereoLimiter (attack instantáneo +
    //    release 1-polo, mismo ceiling/relCoef) sobre el WET CRUDO, ANTES de que el limiter real lo mute.
    //    Cuenta cuántos samples reducen la ganancia (proxy de aspereza, §8) y la reducción máxima. Lectura
    //    pura (no toca el audio): el limiter REAL corre justo abajo. Sin probe → no se ejecuta. ──────────
    if (probe && limiterEnabled)
    {
        const float ceiling = 0.80f;                              // techo true-peak-safe de NÉBULA (= setTuning)
        const float relCoef = 1.0f - (float) std::exp (-1.0 / (0.001 * 60.0 * sampleRate));   // releaseMs=60
        float g = limiterProbeGain;                               // continúa la trayectoria entre bloques
        for (int i = 0; i < n; ++i)
        {
            const float pk  = juce::jmax (std::abs (wL[i]), std::abs (wR[i]));
            const float tgt = (pk > ceiling) ? ceiling / pk : 1.0f;
            if (tgt < g) g = tgt;                                 // attack instantáneo
            else         g += (tgt - g) * relCoef;               // release suave
            ++probe->samplesSeen;
            if (g < 0.99999f) ++probe->samplesWork;              // "trabajó" = redujo ganancia
            const float redDb = (g < 1.0f && g > 0.0f) ? -20.0f * std::log10 (g) : 0.0f;
            probe->limMaxRedDb = juce::jmax (probe->limMaxRedDb, redDb);
        }
        limiterProbeGain = g;
        probe->limWorkFrac = (probe->samplesSeen > 0)
                           ? (double) probe->samplesWork / (double) probe->samplesSeen : 0.0;
    }

    // 7) Limiter de salida estéreo-linked SÓLO sobre el WET (la cola difusa). El dry NO pasa por acá →
    //    transparente. En la cola, el limiter casi no trabaja (el exceso es chico y suave) → sin bombeo ni
    //    aspereza. Aceptamos el true-peak OCASIONAL de la suma dry+wet (§6: latencia 0 + dry transparente
    //    ⇒ algún inter-sample peak): el LED de clip del chasis avisa (uiClip ← extraClipPush). Diagnóstico:
    //    limiterEnabled=false saltea el limiter para medir el wet crudo (clip-scan).
    if (limiterEnabled)
    {
        if (outR != nullptr) limiter.process (wL, wR, n);
        else                 limiter.process (wL, wL, n);
    }

    // 7b) Sumar el WET (ya limitado) al DRY (intacto) → suma dry+wet.
    for (int i = 0; i < n; ++i)
    {
        outL[i] += wL[i];
        if (outR != nullptr) outR[i] += wR[i];
    }

    // 7c) LIMITER DE SALIDA con LOOKAHEAD sobre la SUMA dry+wet (la señal que sale al DAW). ESTE es el que
    //     gobierna el anti-clip final: con graves/transientes fuertes la cola se infla y, sumada al dry
    //     full-scale, pasaba 0 dBFS — y el limiter de wet (paso 7), por mirar SÓLO el wet, no lo veía. El
    //     lookahead mira el pico que viene (~3 ms) y baja la ganancia con rampa SUAVE (sin escalón → sin el
    //     crackle del attack instantáneo) ANTES de que el pico emerja → la suma nunca pasa el techo (0.95,
    //     true-peak-safe). Introduce la latencia que el processor reporta al host. Diagnóstico:
    //     limiterEnabled=false lo saltea (medir la suma cruda en el clip-scan).
    if (limiterEnabled)
    {
        if (outR != nullptr) outLimiter.process (outL, outR, n);
        else                 outLimiter.process (outL, outL, n);
    }

    // (1)(2)(3) y (4): volcamos los picos de etapa al probe (sólo si hay probe).
    if (probe)
    {
        probe->dryPeak      = juce::jmax (probe->dryPeak,      dryPk);     // (1) dry post-inGain (entra al mix)
        probe->wetPrePeak   = juce::jmax (probe->wetPrePeak,   wetPk);     // (2) wet del FDN ANTES del limiter
        probe->totalPrePeak = juce::jmax (probe->totalPrePeak, totalPk);   // (3) total dry+wet ANTES del limiter

        float totalPostPk = 0.0f;   // (4) salida del motor (dry transparente + wet ya limitado)
        for (int i = 0; i < n; ++i)
        {
            totalPostPk = juce::jmax (totalPostPk, std::abs (outL[i]));
            if (outR != nullptr) totalPostPk = juce::jmax (totalPostPk, std::abs (outR[i]));
        }
        probe->postLimPeak = juce::jmax (probe->postLimPeak, totalPostPk);
    }
}

} // namespace ovni::engines
