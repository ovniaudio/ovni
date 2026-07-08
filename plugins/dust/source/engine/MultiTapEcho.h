#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace dust::engine
{

// ── Constantes del motor de ecos (DUST, MOV·03) ─────────────────────────────────────────────────
inline constexpr int   kMaxTaps        = 24;      // techo de burbujas simultáneas (voice management)
inline constexpr float kMinRateMs      = 20.0f;   // FREE: 20 ms (curaduría)
inline constexpr float kMaxRateMs      = 2000.0f; // FREE: 2 s (curaduría)
inline constexpr float kMaxSpanSeconds = 8.0f;    // largo del buffer circular (cap de memoria; a RATE
                                                  // largo entran menos taps directos y el feedback
                                                  // continúa el tren más allá del span)
inline constexpr float kFadeMs         = 5.0f;    // micro-fade raised-cosine de nacimiento/muerte
inline constexpr float kFbMax          = 0.93f;   // piso de estabilidad: la "nube infinita" NUNCA llega a 1
inline constexpr float kFbClamp        = 0.95f;   // clamp duro de seguridad (por encima de kFbMax)
inline constexpr float kLoopDampHz     = 3500.0f; // damping progresivo INTERNO (one-pole LP en la
                                                  // reinyección): cada rebote más oscuro, como una
                                                  // reflexión real. Valor musical fijo, NO expuesto
                                                  // (CORTE de la curaduría: sin knob TONE/DAMP).
inline constexpr float kDriftMaxRad    = 0.9f;    // deriva angular máx de VIDA (~52°, LFO por tap)

// Un tap renderizado: el bloque mono de la burbuja (envolvente ya aplicada) + su azimut absoluto
// en los extremos del bloque. El BubbleField interpola el azimut y RAMPEA POR-SAMPLE las ganancias
// de bus (anti-click estructural: nunca se conmuta un filtro).
struct TapRender
{
    const float* samples = nullptr;  // bloque mono del tap (env de nacimiento/muerte + gain aplicados)
    float azStart = 0.0f;            // azimut (rad, 0 = frente, + = CCW/izquierda) al inicio del bloque
    float azEnd   = 0.0f;            // azimut al final del bloque (continuo, sin wrap brusco)
    float gain    = 0.0f;            // ganancia objetivo del slot este bloque (telemetría: energía de la burbuja)
    int   slot    = 0;               // identidad estable 0..kMaxTaps-1 (el BubbleField guarda ganancias por slot)
    bool  born    = false;           // nació en este bloque -> resetear las ganancias de bus al target
};

// =================================================================================================
// MultiTapEcho — buffer circular mono con hasta kMaxTaps lecturas interpoladas (Lagrange3rd) +
// reinyección de feedback (DENSIDAD) con damping interno y piso de estabilidad.
//
//  · Espaciado: tap del slot s lee a (s+1)·RATE + jitter (jitter fijado al nacer, escala con VIDA).
//  · DENSIDAD: cuenta de taps activos + decaimiento de ganancia por slot + feedback del lazo
//    (curva cuadrática ~log en tiempo de decay, kFbMax como piso de estabilidad, kFbClamp duro).
//  · Cambios de RATE: rateSm se desliza POR-SAMPLE y las lecturas son fraccionales (Lagrange3rd)
//    -> los offsets se MUEVEN sin flutter áspero ni clicks (resampleo continuo, tipo cinta).
//  · Nacimiento/muerte: micro-fade raised-cosine de kFadeMs; voice-stealing del más viejo/débil
//    (el slot más alto = el de menor ganancia) con fade-out, nunca un drop duro.
//  · Acumuladores del lazo en double (decay > 1 s a DENSIDAD alta; house-standard §1).
// =================================================================================================
class MultiTapEcho
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Setters suavizables (target; el suavizado vive ADENTRO, en el dominio del destino, post-mapeo).
    void setRateMs    (float ms)  noexcept { rateTargetMs = juce::jlimit (kMinRateMs, kMaxRateMs, ms); }
    void setDensity01 (float d)   noexcept { densityTarget = juce::jlimit (0.0f, 1.0f, d); }
    void setSpread01  (float s)   noexcept { spreadTarget  = juce::jlimit (0.0f, 1.0f, s); }
    void setVida01    (float v)   noexcept { vidaTarget    = juce::jlimit (0.0f, 1.0f, v); }

    // Procesa un bloque mono. originAzStart/End = azimut del ORIGIN en los extremos del bloque
    // (continuo, ya des-wrapeado por el caller). Llena 'renders' y devuelve cuántos taps hay.
    int process (const float* monoIn, int numSamples,
                 float originAzStart, float originAzEnd,
                 std::array<TapRender, (size_t) kMaxTaps>& renders);

    // ── test-only ────────────────────────────────────────────────────────────────────────────────
    int   dbgLiveTaps()  const noexcept;
    float dbgRateMsSm()  const noexcept { return (float) rateSmMs; }
    float dbgFbGain()    const noexcept { return (float) fbGainSm; }

private:
    struct Tap
    {
        bool   active     = false;
        bool   dying      = false;   // en fade-out (voice-stealing / target bajó)
        float  jitterFrac   = 0.0f;  // offset extra como fracción de RATE (fijado al nacer, ∝ VIDA)
        float  azOffsetUnit = 0.0f;  // offset angular UNITARIO [-1,1] (al nacer; el ángulo real lo
                                     // escala el SPREAD vivo -> girar SPREAD mueve la nube existente).
                                     // Signo = contrapeso del momento lateral del campo (anti-sesgo)
        float  driftUnit  = 0.0f;    // amplitud unitaria de deriva [0.5..1] (al nacer; se escala por VIDA viva)
        float  driftHz    = 0.0f;    // frecuencia del LFO de deriva (lenta, al nacer)
        float  driftPhase = 0.0f;    // fase del LFO de deriva (rad)
        double ageSamples = 0.0;
        float  env        = 0.0f;    // envolvente de nacimiento/muerte (raised-cosine)
        float  fadePhase  = 0.0f;    // 0..1 dentro del micro-fade
        float  gainSm     = 0.0f;    // ganancia del tap suavizada (rampa por-bloque -> por-sample)
    };

    float readLagrange (double delaySamples) const noexcept;  // lectura fraccional del buffer
    void  updateVoices();                                     // births/steals (una vez por bloque)
    float tapAzimuth (const Tap& t, float originAz, double extraAgeSamples) const noexcept;

    // Buffer circular mono (pow2 + máscara).
    std::vector<float> buffer;
    int     mask     = 0;
    long    writePos = 0;            // posición absoluta de escritura (se enmascara al indexar)

    // Estado suavizado (dominio del destino, post-mapeo).
    double rateSmMs     = 250.0;     // RATE en ms, deslizado por-sample
    float  rateTargetMs = 250.0f;
    double rateCoef     = 0.0;       // one-pole por-sample del RATE (τ ~80 ms)
    float  densityTarget = 0.4f, densitySm = 0.4f;
    float  spreadTarget  = 0.6f, spreadSm  = 0.6f;
    float  vidaTarget    = 0.25f, vidaSm   = 0.25f;

    // Lazo de feedback (DENSIDAD): acumuladores en double (decay > 1 s; house-standard §1).
    double fbGainSm   = 0.0;         // ganancia de reinyección suavizada por-sample
    double fbGainTgt  = 0.0;
    double fbCoef     = 0.0;         // one-pole por-sample de la ganancia de feedback (τ ~50 ms)
    double loopSm     = 0.0;         // offset de reinyección (samples), deslizado por-sample
    double loopCoef   = 0.0;
    double dampState  = 0.0;         // one-pole LP del damping interno
    double dampCoef   = 0.0;

    double sampleRate  = 48000.0;
    int    maxSpanSamp = 0;
    int    fadeSamples = 240;
    float  fadeStep    = 1.0f / 240.0f;

    std::array<Tap, (size_t) kMaxTaps> taps;
    std::vector<float> tapScratch;   // kMaxTaps bloques mono contiguos [slot*maxBlock + i]
    int    maxBlock   = 0;
    int    spawnHold  = 0;           // escalona nacimientos/robos (1 por bloque como máximo)

    juce::Random rng { 0xD057 };     // semilla fija -> nube reproducible (tests determinísticos)
};

} // namespace dust::engine
