#pragma once
#include <juce_dsp/juce_dsp.h>
#include "dsp/StereoLimiter.h"
#include "dsp/Smoothing.h"

namespace ovni::engines {

// Parámetros de proceso del motor MOVIMIENTO (firma del CONTRACT). Designated-init en el call site.
//   azimuthRad : posición angular objetivo al final del bloque. 0 = frente (centro), + = CCW /
//                izquierda, - = derecha. La produce Trajectory (o un control directo).
//   distance01 : distancia instantánea normalizada. 0 = al oído (fuerte/cerca) .. 1 = lejos (bajo/
//                oscuro). Su VARIACIÓN en el tiempo alimenta el Doppler (la derivada del delay = pitch).
//   doppler01  : cantidad de Doppler. 0 = línea de delay en bypass (latencia 0 real, sin pitch);
//                >0 = el fly-by produce pitch (escala la modulación del delay).
//   width01    : spread estéreo del recorrido. 0 = colapsado al centro (mono) .. 1 = barrido L<->R completo.
//   speakerMode: RESERVADO. La cancelación de crosstalk (binaural -> parlantes) vive en el módulo
//                binaural aparte (ÓRBITA/POLVO); el motor de movimiento base no la aplica.
struct MovementParams
{
    float azimuthRad  = 0.0f;
    float distance01  = 0.5f;
    float doppler01   = 0.0f;
    float width01     = 1.0f;
    bool  speakerMode = false;
    bool  monoSafe    = false;   // IN PHASE: fuerza ITD=0 -> paneo puro (en fase, mono-compatible).
};

// Motor de MOVIMIENTO estéreo (SIN HRTF): toma la fuente como un punto mono y la posiciona en el
// campo estéreo por PANEO DE POTENCIA CONSTANTE (gL^2+gR^2=1), con Doppler (delay modulado por la
// velocidad radial del fly-by) y atenuación de distancia (1/r + air-absorption LP). Latencia 0 (sin
// lookahead; con Doppler activo hay un delay de propagación inherente, que ES el efecto, no se
// reporta al host). Toda ganancia/coef modulada se rampea por-sample (anti-click; ver
// references/anti-click-clip-truepeak.md).
//
// ITD ligero (inter-aural time difference, modelo de Woodworth) SÍ está en el motor base: un delay
// inter-canal dependiente del azimut que decorrelaciona L/R -> ancho/profundidad 3D REAL sin HRTF.
// `monoSafe` (IN PHASE) lo desactiva (vuelve al paneo puro, en fase). El HRTF completo (HRIR-FIR,
// near-field, reflexiones tempranas, cancelación de crosstalk) sigue siendo MÓDULO APARTE (ÓRBITA/POLVO).
class MovementEngine
{
public:
    // Afinables del Doppler (geometría del delay modulado). Llamar ANTES de prepare (dimensiona la línea).
    struct DopplerTuning
    {
        float maxAmpMeters   = 3.0f;   // amplitud máx de la modulación de delay (m)
        float minSafeSamples = 2.0f;   // piso de delay (Lagrange3rd estable)
        float maxSlew        = 0.20f;  // Δdelay máx por sample -> limita el pitch (anti-aliasing)
    };
    void setDoppler (const DopplerTuning& t) noexcept { dopplerTune = t; }
    void setLimiter (ovni::dsp::LimiterTuning t) noexcept { limiter.setTuning (t); }

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Espacializa in-place (estéreo out, latencia 0).
    void process (juce::AudioBuffer<float>& buffer, const MovementParams& p);

    // Última ganancia del limiter de salida (1 = sin reducción, <1 = conteniendo). Para el LED de clip.
    float lastLimiterGain() const noexcept { return limiter.lastGain(); }

private:
    using DelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;

    double sampleRate = 48000.0;

    juce::AudioBuffer<float> mono;              // scratch del punto sonoro (1 canal)

    // Doppler: delay de propagación mono modulado. Centro = headroom; sólo la DERIVADA -> pitch.
    DopplerTuning dopplerTune;
    DelayLine     dopplerLine { 2048 };
    float         dopplerCenterSamples = 0.0f;
    float         dopplerMaxAmpSamples = 0.0f;
    float         dopplerDelayPrev     = 0.0f;

    // Distancia: 1/r rampeado por-sample + air-absorption LP 1-polo (sobre el mono, pre-paneo).
    float directGainSm = 1.0f;
    float airLp        = 0.0f;

    // Paneo: ganancias L/R rampeadas por-sample (anti-click al moverse). Arranque = centro.
    float gLsm = 0.70710678f, gRsm = 0.70710678f;

    // ITD (Woodworth): delay inter-canal dependiente del azimut -> ancho/3D real (decorrelación L/R) sin
    // HRTF. Piso COMÚN (kItdBase) para estabilidad del Lagrange3rd: con ITD=0 ambos oídos comparten el
    // mismo piso -> sin diferencia de fase (idéntico al paneo puro). monoSafe (IN PHASE) -> ITD=0.
    static constexpr float kItdBaseSamples = 2.0f;
    DelayLine itdLineL { 256 }, itdLineR { 256 };
    float     itdScaleSamples = 0.0f;                              // (a/c)*sr: muestras por (az + sin az)
    float     itdLPrev = kItdBaseSamples, itdRPrev = kItdBaseSamples;

    // Head-shadow: LP 1-polo por oído; el lejano se oscurece (2da pista binaural -> más ancho/3D).
    float hsL = 0.0f, hsR = 0.0f;

    ovni::dsp::StereoLimiter limiter;
};

} // namespace ovni::engines
