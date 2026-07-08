#include "dsp/LookaheadLimiter.h"
#include <cmath>
#include <algorithm>

namespace ovni::dsp {

void LookaheadLimiter::prepare (double sr, int mb)
{
    sampleRate = (sr > 0.0 ? sr : 48000.0);
    maxBlock   = std::max (1, mb);
    rebuild();
}

void LookaheadLimiter::setTuning (LookaheadTuning t)
{
    tuning = t;
    rebuild();   // el lookahead puede cambiar → re-dimensiona el retardo/ventana y recalcula coefs
}

void LookaheadLimiter::rebuild()
{
    // L = round(lookaheadMs·sr/1000), mínimo 1 sample (sin lookahead no hay nada que mirar adelante).
    const double ms = std::max (0.0f, tuning.lookaheadMs);
    lookahead = std::max (1, (int) std::lround (ms * sampleRate / 1000.0));

    delayL.assign ((size_t) lookahead, 0.0f);
    delayR.assign ((size_t) lookahead, 0.0f);
    // El deque monótono nunca tiene más de L elementos (la ventana mide L). Ring de capacidad L.
    mqVal.assign ((size_t) lookahead, 0.0f);
    mqIdx.assign ((size_t) lookahead, 0);
    updateCoefs();
    reset();
}

void LookaheadLimiter::updateCoefs()
{
    // Attack: la ganancia debe estar EN el target para cuando el pico cruza el retardo (L samples). Con τ=L/4
    // un 1-polo converge a ~98% en L samples → el pico emerge ya atenuado, sin escalón (rampa suave, no salto).
    // El sliding-max SOSTIENE el target mientras el pico esté en la ventana, así que la cota nunca se viola.
    const double tau = std::max (1.0, lookahead / 4.0);
    atkCoef = 1.0f - (float) std::exp (-1.0 / tau);

    // Release: recuperación con cte de tiempo = releaseMs (lento → sin bombeo ni aspereza).
    const double rms = std::max (1.0e-3, (double) tuning.releaseMs);
    relCoef = 1.0f - (float) std::exp (-1.0 / (0.001 * rms * sampleRate));
}

void LookaheadLimiter::reset() noexcept
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
    mqHead = mqTail = mqSize = 0;
    detIdx = 0;
    hL[0] = hL[1] = hL[2] = hL[3] = 0.0f;
    hR[0] = hR[1] = hR[2] = hR[3] = 0.0f;
    primed = 0;
    gain   = 1.0f;
}

// True-peak (inter-sample) del segmento ENTRE las dos muestras del medio (h1,h2) de una historia de 4 (h0..h3),
// vía Catmull-Rom 4× (el método estándar del medidor true-peak, §3). Devuelve el mayor |valor| de las 4
// sub-muestras (incluye h1; las posiciones interiores son las que pueden sobrepasar la muestra).
static inline float interSamplePeak (const float h[4]) noexcept
{
    // OS=16: oversample alto. El 4× estándar (ITU-R BS.1770) SUBESTIMA el pico inter-sample de
    // transientes muy agresivos (ataque bestial, HF cerca de Nyquist) → el limiter no actuaba y la
    // señal real se colaba sobre el techo. 16× cubre el segmento [P1,P2] casi entero (hueco < 1/16)
    // → el detector ve el pico verdadero y limita de verdad. (Catmull-Rom puede sobreestimar un pelín
    // en bordes agudos → más conservador = más seguro, nunca menos.)
    constexpr int OS = 16;
    const float P0 = h[0], P1 = h[1], P2 = h[2], P3 = h[3];
    float tp = std::abs (P1);
    for (int s = 1; s < OS; ++s)   // s=0 es P1 (ya contado); interiores s=1..OS-1
    {
        const float t = (float) s / (float) OS;
        const float v = 0.5f * ((2.0f * P1) + (-P0 + P2) * t
                      + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * t * t
                      + (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * t * t * t);
        tp = std::max (tp, std::abs (v));
    }
    return tp;
}

void LookaheadLimiter::process (float* left, float* right, int n) noexcept
{
    if (n <= 0 || lookahead <= 0) return;

    const float ceiling = tuning.ceiling;
    const int   L       = lookahead;

    for (int i = 0; i < n; ++i)
    {
        // ── 1) Entrada de ESTE sample; la metemos en la historia signed por canal (para el detector TP). ──
        const float inL = left[i];
        const float inR = right[i];
        hL[0] = hL[1]; hL[1] = hL[2]; hL[2] = hL[3]; hL[3] = inL;   // shift: h3 = más nuevo
        hR[0] = hR[1]; hR[1] = hR[2]; hR[2] = hR[3]; hR[3] = inR;
        if (primed < 4) ++primed;

        // ── 2) Detector TRUE-PEAK del segmento [h1,h2] (sub-muestras inter-sample) en AMBOS canales →
        //       estéreo-linked. Este pico pertenece al índice de h2 = (índice de entrada actual − 1): el
        //       detector tiene 1 sample de lag, absorbido por el retardo de L (L≫1). Hasta primar 4
        //       muestras, h0/h1 son ceros → el TP sale conservador (no subestima). ─────────────────────────
        const float tp   = std::max (interSamplePeak (hL), interSamplePeak (hR));
        const long  tpIdx = detIdx;   // índice global de ESTE pico true-peak (h2)
        ++detIdx;

        // ── 3) Sliding-window MAX (deque monótono) sobre los true-peaks: el frente = mayor TP de la ventana
        //       de L muestras que termina en tpIdx = cubre el output que emerge abajo + su entorno hacia
        //       adelante. (a) expirá el frente si salió de la ventana; (b) descartá por la cola lo ≤ tp; (c)
        //       encolá tp. ───────────────────────────────────────────────────────────────────────────────
        const long windowStart = tpIdx - (long) (L - 1);   // índice más viejo aún dentro de la ventana
        if (mqSize > 0 && mqIdx[(size_t) mqHead] < windowStart)
        {
            if (++mqHead >= L) mqHead = 0;                 // expira el frente (salió por la izquierda)
            --mqSize;
        }
        while (mqSize > 0)
        {
            const int back = (mqTail == 0 ? L - 1 : mqTail - 1);   // último elemento del deque
            if (mqVal[(size_t) back] <= tp) { mqTail = back; --mqSize; }   // descartá los ≤ tp
            else break;
        }
        mqVal[(size_t) mqTail] = tp;                       // encolá tp en la cola
        mqIdx[(size_t) mqTail] = tpIdx;
        if (++mqTail >= L) mqTail = 0;
        ++mqSize;
        const float winPeak = mqVal[(size_t) mqHead];      // mayor TRUE-PEAK de la ventana de lookahead

        // ── 4) Ganancia objetivo de la ventana: si el TRUE-PEAK que viene pasa el techo, atenuá ceiling/TP
        //       → la onda CONTINUA (no sólo las muestras) queda bajo el techo. ───────────────────────────
        const float target = (winPeak > ceiling) ? (ceiling / winPeak) : 1.0f;

        // ── 5) Suavizado de la ganancia: attack rampea hacia abajo (converge dentro de L samples → ya está
        //       cuando el pico emerge, SIN escalón → sin crackle); release lento hacia arriba. ───────────
        if (target < gain) gain += (target - gain) * atkCoef;   // attack (converge en ≈L)
        else               gain += (target - gain) * relCoef;   // release suave

        // ── 6) Escribí la entrada en el retardo y leé la salida RETRASADA L samples; aplicá la ganancia.
        //       La ganancia ya está atenuada para cuando este pico viejo emerge → nunca pasa el techo. ──
        const float outLs = delayL[(size_t) writePos];
        const float outRs = delayR[(size_t) writePos];
        delayL[(size_t) writePos] = inL;
        delayR[(size_t) writePos] = inR;
        if (++writePos >= L) writePos = 0;

        left[i]  = outLs * gain;
        right[i] = outRs * gain;
    }
}

} // namespace ovni::dsp
