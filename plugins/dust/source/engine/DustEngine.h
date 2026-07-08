#pragma once
#include <juce_dsp/juce_dsp.h>
#include "MultiTapEcho.h"
#include "BubbleField.h"
#include "dsp/StereoLimiter.h"

namespace dust::engine
{

// =================================================================================================
// DustEngine — fachada del motor de DUST (MOV·03): mono-sum de entrada -> MultiTapEcho (burbujas)
// -> BubbleField (banco binaural de 16 direcciones) -> StereoLimiter (techo 0.85, estéreo-linked).
//
// Salida = 100% WET (reemplaza el buffer). El MIX por ley de potencia y el DUCK viven en el
// processor (etapa siguiente); acá sólo el campo de ecos.
//
// ORIGIN: par (x,y) del campo (x + = derecha de pantalla, y + = frente), RAMPEADO POR-SAMPLE.
// El azimut del origen (0 = frente, + = CCW/izquierda, convención de los motores del sello:
// physics-measurement-and-pitfalls.md §4 -> az = atan2(-x, y)) es el CENTRO de la distribución
// de azimuts de las burbujas; moverlo arrastra la nube entera sin clicks (las ganancias de bus
// se rampean por-sample en el BubbleField).
// =================================================================================================
class DustEngine
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    // Setters suavizables (la rampa vive en el dominio del destino, adentro de cada etapa).
    void setRateMs    (float ms) noexcept { echo.setRateMs (ms); }
    void setDensity01 (float d)  noexcept { echo.setDensity01 (d); }
    void setSpread01  (float s)  noexcept { echo.setSpread01 (s); }
    void setVida01    (float v)  noexcept { echo.setVida01 (v); }
    void setMonoSafe  (bool on)  noexcept { field.setMonoSafe (on); }
    void setOrigin    (float x, float y) noexcept
    {
        originXTgt = juce::jlimit (-1.0f, 1.0f, x);
        originYTgt = juce::jlimit (-1.0f, 1.0f, y);
    }

    // Procesa in-place: reemplaza el contenido por el WET binaural (numInputChannels se suman a mono).
    void process (juce::AudioBuffer<float>& buffer, int numInputChannels);

    float lastLimiterGain() const noexcept { return limiter.lastGain(); }

    // ── telemetría (audio thread; el processor la drena tras process() y la publica al FIFO) ─────
    // Nacimientos de burbuja del ÚLTIMO bloque: azimut absoluto al nacer + energía (ganancia objetivo
    // del slot). El visualizador (etapa UI) los consume vía el FIFO lock-free del processor.
    struct BubbleBirth { float azimuthRad = 0.0f; float energy = 0.0f; };
    int lastBirthCount() const noexcept                  { return numBirths; }
    const BubbleBirth& lastBirth (int i) const noexcept  { return births[(size_t) i]; }

    // ── test-only ────────────────────────────────────────────────────────────────────────────────
    int   dbgLiveTaps()           const noexcept { return echo.dbgLiveTaps(); }
    float dbgBusEnergy (int b)    const noexcept { return field.dbgBusEnergy (b); }
    void  dbgResetBusEnergy()           noexcept { field.dbgResetBusEnergy(); }
    float dbgBusAzimuthRad (int b) const noexcept { return field.dbgBusAzimuthRad (b); }
    int   dbgBusItdSamples (int b) const noexcept { return field.dbgBusItdSamples (b); }

private:
    float originAzimuth (float x, float y) const noexcept;

    MultiTapEcho            echo;
    BubbleField             field;
    ovni::dsp::StereoLimiter limiter;

    std::vector<float> monoBuf;
    std::array<TapRender, (size_t) kMaxTaps> renders;

    // Nacimientos del último bloque (telemetría, se reescribe por bloque).
    int numBirths = 0;
    std::array<BubbleBirth, (size_t) kMaxTaps> births {};

    // ORIGIN (x,y) con rampa por-sample (LinearRamp manual: el paso se recorre dentro del bloque).
    float originX = 0.0f,  originY = 0.35f;     // default: centro-frente (curaduría: x 0, y 0.35)
    float originXTgt = 0.0f, originYTgt = 0.35f;
    float originAzCont = 0.0f;                  // azimut continuo (des-wrapeado entre bloques)

    double sampleRate = 48000.0;
    int    maxBlock   = 0;
};

} // namespace dust::engine
