#pragma once

namespace ovni::dsp {

// Afinables del limiter de salida (red de seguridad anti-clip).
//   ceiling  ~0.85 (~-1.4 dB): margen para inter-sample peaks (true-peak) -> ni el medidor
//            true-peak de un DAW marca clip (ver references/anti-click-clip-truepeak.md §3).
//   releaseMs: recuperación suave de la ganancia tras contener un pico (contiene sin armónicos duros).
struct LimiterTuning
{
    float ceiling   = 0.85f;
    float releaseMs = 60.0f;
};

// Limiter de salida ESTÉREO-LINKED, latencia 0. Attack instantáneo (la ganancia baja al pico al
// instante -> nunca pasa el techo, sin lookahead) + release suave (contiene sin armónicos duros).
// La MISMA ganancia va a L y R -> la imagen estéreo (balance/ITD/ILD) queda exacta. Transparente
// mientras la señal no toca el techo.
//
// Extraído del paso 8 de ÓRBITA SpatialEngine; ver references/anti-click-clip-truepeak.md §2.
// Por qué NO un soft-clip/waveshaper: comprimir excesos grandes con tanh genera distorsión dura
// (armónicos) -> suena a clip. La reducción de ganancia es mucho más transparente.
class StereoLimiter
{
public:
    void prepare (double sampleRate);            // dimensiona el release; resetea la ganancia
    void reset() noexcept;                        // ganancia -> 1 (sin reducción)
    void setTuning (LimiterTuning t) noexcept;    // recalcula el release si ya está preparado

    // Procesa in-place L/R (estéreo-linked). RT-safe, sin asignaciones.
    void process (float* left, float* right, int numSamples) noexcept;

    // Última ganancia aplicada: 1 = sin reducción, <1 = conteniendo picos. Para el LED de clip.
    float lastGain() const noexcept { return gain; }

private:
    void updateRelease() noexcept;

    double        sampleRate = 48000.0;
    LimiterTuning tuning {};
    float         gain    = 1.0f;   // ganancia actual (1 = transparente)
    float         relCoef = 0.0f;   // coef de release (1-polo), computado en prepare/setTuning
};

} // namespace ovni::dsp
