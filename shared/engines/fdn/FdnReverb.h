#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include "dsp/StereoLimiter.h"
#include "dsp/LookaheadLimiter.h"
#include "dsp/Smoothing.h"

namespace ovni::engines {

// Parámetros perceptuales del reverb FDN (designated-init en el call site). Todos 0..1 salvo freeze.
struct FdnParams {
    float size01   = 0.5f;   // escala las longitudes de delay (espacio chico .. ~2 s)
    float decay01  = 0.5f;   // T60 (mapeo interno a segundos); 1.0 + freeze = cola infinita
    float tone01   = 0.4f;   // damping HF en el lazo (T60(ω): agudos decaen antes)
    float breath01 = 0.25f;  // profundidad de la respiración (modulación lenta de Size)
    float mix01    = 0.35f;  // dry/wet (ley de potencia)
    bool  freeze   = false;  // decay al tope -> cola congelada (g_i clampeado al borde)
    // IN PHASE (lo inyecta el chasis): colapsa la cola WET a mono-compatible (in phase) ANTES del mix.
    // La cola del FDN está MUY decorrelada (CORR≈0.14) → al ponerla in-phase se nota y queda mono-safe.
    // El dry NO se toca (sólo el wet). El bass-mono del chasis sigue corriendo aparte (inofensivo).
    bool  monoSafe = false;
    // SYNC de la respiración: >0 = respira sincronizado a esa frecuencia (Hz, ciclos/seg); ≤0 = libre
    // (las frecuencias orgánicas internas del BreathLFO). El processor lo calcula desde el BPM del host.
    float breathRateHz = -1.0f;
    // Difusión de ENTRADA (4 allpass Schroeder sobre la inyección al lazo): ON para reverbs "a secas"
    // (NEBULA — densidad de eco pro en <150 ms). OFF cuando el FDN vive DENTRO de un lazo regenerativo
    // más grande (HALO/shimmer): ahí re-difundir el RETORNO en cada vuelta cambia la subida de la
    // escalera de octavas (medido: rompía el gate HF/fundamental) — el dueño difunde su excitación.
    bool inputDiffusion = true;
};

// Reverb FDN (Jot & Chaigne): N líneas realimentadas por matriz Householder lossless, T60(ω) por
// filtros de absorción, early reflections FIR, respiración por LFO lento propio. Estéreo in/out.
class FdnReverb {
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const FdnParams& p);   // in-place, estéreo
    // Ganancia para el LED de clip: el MENOR de los dos limiters (wet + lookahead de salida) → el LED toma
    // el que más esté conteniendo (el lookahead final suele ser el que gobierna la suma dry+wet).
    float lastLimiterGain() const noexcept { return juce::jmin (limiter.lastGain(), outLimiter.lastGain()); }

    // Latencia (samples) que el motor introduce → el lookahead limiter de salida. El processor la reporta al
    // host (setLatencySamples). 0 si aún no se preparó (el lookahead se dimensiona en prepare).
    int latencySamples() const noexcept { return outLimiter.latencySamples(); }

    // ── Bypass del limiter (sólo para DIAGNÓSTICO / clip-scan). En producción SIEMPRE on. Permite medir
    //    el WET crudo antes de que el limiter actúe (variante "sin limiter" del mapa de etapas). NO toca
    //    el camino de audio normal del usuario. ────────────────────────────────────────────────────────
    void setLimiterEnabled (bool on) noexcept { limiterEnabled = on; }

    // Difusores de ENTRADA (4 allpass Schroeder en serie, retardos primos ~5-15 ms, g=0.62): suben la
    // densidad de eco de la cola (impulso → wash gaussiano en <150 ms, medido Abel-Huang; antes ~400 ms
    // = ataque "granulado" en transitorios). Allpass = lossless → NO cambia el T60 ni el tono del lazo.
    // Sólo difunden la INYECCIÓN al lazo FDN; las early reflections siguen leyendo el input CRUDO
    // (el patrón temprano discreto de Moorer se preserva — es la palanca de externalización).
    struct InputDiffuser
    {
        void  prepare (double sr);
        void  reset() noexcept;
        float process (float x) noexcept;
        static constexpr int   kStages = 4;
        static constexpr float kG      = 0.62f;
        std::array<std::vector<float>, (size_t) kStages> buf;
        std::array<int, (size_t) kStages> len {};
        std::array<int, (size_t) kStages> wr  {};
    };

    // ── Probe de medición (clip-scan): cuando hay un MeterProbe enganchado, process() acumula los picos
    //    sample-peak de cada etapa INTERNA de la cadena del reverb (lo que NO se puede medir desde afuera
    //    porque viven dentro del loop) + el trabajo del limiter. Sin probe, costo cero (un puntero nulo).
    //    El true-peak (inter-sample) lo calcula el test desde la salida final; acá damos los sample-peak
    //    internos y las estadísticas del limiter (cuánto reduce y qué % de samples toca → proxy de
    //    aspereza, ver references/anti-click-clip-truepeak.md §4/§8). ──────────────────────────────────
    struct MeterProbe
    {
        float dryPeak       = 0.0f;   // (1) |dry| post-inGain que ENTRA al mix (la señal directa del usuario)
        float wetPrePeak    = 0.0f;   // (2) |wet del FDN| (wetG·full) ANTES del limiter
        float totalPrePeak  = 0.0f;   // (3) |dry+wet| del mix ANTES del limiter
        float postLimPeak   = 0.0f;   // (4) |salida| del motor POST-limiter (lo que devuelve process)
        float limMaxRedDb   = 0.0f;   // reducción MÁXIMA de ganancia del limiter (dB, ≥0; 0 = no tocó)
        double limWorkFrac  = 0.0;    // % de samples con reducción (gain<~1) → cuánto "trabaja" en transientes
        long   samplesSeen  = 0;      // total de samples procesados (denominador del limWorkFrac)
        long   samplesWork  = 0;      // samples con reducción del limiter (numerador)
        void reset() noexcept { *this = MeterProbe{}; }
    };
    void setProbe (MeterProbe* pr) noexcept { probe = pr; }

    static constexpr int kN = 8;   // líneas de la FDN

    // ── Mapeos perceptuales públicos (única fuente de verdad: el motor y los tests comparten estas
    //    fórmulas para no derivar). Estáticos y puros.
    // decay01 -> T60 en segundos (0.1 .. 12 s, skew logarítmico → la mayor parte del recorrido es cola corta-media).
    static float t60ForDecay (float decay01) noexcept;
    // tone01 -> frecuencia de corte del LP de absorción en Hz (18 kHz @ tone=0 .. 1.5 kHz @ tone=1, log).
    static float cutoffForTone (float tone01) noexcept;
    // size01 -> factor de escala de las longitudes base (0.15 .. 1.0).
    static float sizeScaleForSize (float size01) noexcept;

private:
    using DelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
    using ToneFilter = juce::dsp::FirstOrderTPTFilter<float>;

    // Modulador de respiración PROPIO (no se importa nada de PULSAR): suma de 3 senos incoherentes
    // (0.05–0.3 Hz) → salida orgánica y lenta en [−1,1]. Su único trabajo es modular sizeScale.
    //
    // SYNC: next() acepta un baseHz opcional. baseHz ≤ 0 → frecuencias orgánicas libres (las de prepare).
    // baseHz > 0 → el oscilador PRINCIPAL corre a baseHz y los secundarios se escalan PROPORCIONAL (misma
    // razón que en libre → mantiene el carácter orgánico, sólo que enganchado al tempo). El cambio de
    // frecuencia se de-zippea (rampa interna de los incrementos) → la respiración NO salta al togglear SYNC.
    struct BreathLFO {
        void prepare (double sr) noexcept;
        void reset() noexcept;
        float next (double baseHz = -1.0) noexcept;   // avanza 1 sample, devuelve [−1,1]
    private:
        static constexpr int kOsc = 3;
        // Frecuencias libres (Hz) de cada oscilador y la base libre (la del oscilador principal). Las
        // razones freeHz[k]/freeBaseHz definen el "carácter": en sync reescalamos por (baseHz/freeBaseHz).
        static constexpr double freeHz[kOsc]  = { 0.061, 0.103, 0.179 };
        static constexpr double freeBaseHz    = 0.061;   // = freeHz[0] (el principal en modo libre)
        double sampleRate = 48000.0;
        double phase[kOsc] {};
        double inc[kOsc]   {};            // rad/sample ACTUAL de cada seno (rampeado hacia el objetivo)
        double norm = 1.0;                // normaliza la suma a [−1,1]
    };

    // Recalcula las longitudes (base prima escalada por sizeScale) y prepara las líneas/filtros.
    void rebuildLines (float sizeScale);
    // g_i para cada línea dado un T60 (s): g = 10^(−3·M_i/(T60·fs)), clampeado < 1.
    void updateFeedbackGains (float t60Seconds) noexcept;

    double sampleRate = 48000.0;
    int    maxBlock   = 512;

    // Las kN líneas de delay realimentadas + sus longitudes en samples (mutuamente primas; escaladas
    // por Size; longitud objetivo vs longitud actual rampeada → cross-fade anti-click).
    std::array<DelayLine, (size_t) kN> lines;
    std::array<float, (size_t) kN>     baseSamples {};    // longitud "base" (sizeScale=1) por línea, prima
    std::array<float, (size_t) kN>     delaySamples {};   // longitud objetivo actual (base·sizeScale)
    std::array<float, (size_t) kN>     delaySamplesSm {}; // longitud rampeada (la que se aplica de verdad)
    std::array<float, (size_t) kN>     feedback {};       // g_i por línea (absorción aparte la da el filtro)
    int   maxLenAlloc = 0;                                 // capacidad reservada en cada línea

    // Filtros de absorción (T60(ω)): un LP TPT por línea dentro del lazo → los agudos decaen antes.
    std::array<ToneFilter, (size_t) kN> absorb;

    InputDiffuser diffuser;   // difusión de la inyección (p.inputDiffusion)

    // Early reflections: FIR corto en paralelo (tiempos primos crecientes, ganancias decrecientes) →
    // sensación de "fuera de la cabeza" en auriculares. Buffer circular mono propio.
    static constexpr int kErTaps = 24;
    std::array<int,   (size_t) kErTaps> erDelay {};       // retardo de cada tap (samples)
    std::array<float, (size_t) kErTaps> erGainL {};       // ganancia del tap hacia L
    std::array<float, (size_t) kErTaps> erGainR {};       // ganancia del tap hacia R
    std::vector<float>                  erBuf;            // línea de retardo circular (mono input)
    int                                 erWrite = 0;
    int                                 erLen   = 0;

    // De-zipper: SmoothedValue para las macros DESPUÉS del mapeo (spec §6). El dominio de cada una sigue
    // su naturaleza perceptual: T60 (s) y corte (Hz) MULTIPLICATIVO (uniforme en dB/octavas); sizeScale,
    // mix y breath LINEAL (la curva de potencia del mix la aplica el por-sample). Todos > 0 (requisito
    // del smoothing multiplicativo): T60 ≥ 0.1 s, corte ≥ 1.5 kHz.
    using SmLin = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
    using SmMul = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
    SmLin sizeScaleSm;   // escala de longitudes suavizada (centro de la respiración)
    SmMul t60Sm;         // T60 (s) suavizado → recalcula g_i (multiplicativo: dB-uniforme)
    SmMul cutoffSm;      // corte del LP de absorción (Hz) suavizado (multiplicativo: octava-uniforme)
    SmLin mixSm;         // dry/wet 0..1 suavizado (a power-law por-sample)

    BreathLFO breath;
    SmLin     breathDepthSm; // profundidad de respiración suavizada (anti-zipper)

    bool firstBlock = true;                   // para snappear los smoothers al primer process tras prepare

    // Limiter de la COLA WET (red intermedia: contiene la cola difusa ANTES del mix; transparente, §2/§4).
    ovni::dsp::StereoLimiter limiter;
    // Limiter de SALIDA con LOOKAHEAD sobre la SUMA dry+wet (la señal que sale al DAW). Es el que GOBIERNA el
    // anti-clip final: con graves/transientes fuertes la cola + el dry full-scale pasaban 0 dBFS y el attack
    // instantáneo del limiter de wet no alcanzaba a la suma. El lookahead mira el pico que viene y baja la
    // ganancia con rampa suave (sin escalón → sin crackle) ANTES de que el pico salga. Introduce ~3 ms de
    // latencia (reportada al host vía latencySamples()). Ver references/anti-click-clip-truepeak.md §3/§6.
    ovni::dsp::LookaheadLimiter outLimiter;
    bool limiterEnabled = true;          // diagnóstico: off = medir el wet crudo (clip-scan). Prod = true.

    // Scratch del WET (sin dry) para limitar SÓLO la cola difusa: el dry se suma DESPUÉS, intacto. Se
    // dimensiona en prepare al maxBlock. Ver process() y references/anti-click-clip-truepeak.md §4/§6.
    std::vector<float> wetScratchL, wetScratchR;

    MeterProbe* probe = nullptr;         // clip-scan: si != null, process() llena las etapas (costo cero si null).
    float limiterProbeGain = 1.0f;       // estado de la réplica del limiter del probe (continuidad entre bloques).
};

} // namespace ovni::engines
