#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include "dsp/StereoLimiter.h"
#include "dsp/Smoothing.h"

namespace ovni::engines {

// =====================================================================================
// PitchShifter — pitch-shift GRANULAR / SPLICE-OVERLAP, POLIFÓNICO, multi-voz (spec §3.1).
//
// Técnica: overlap-add con crossfade. Un buffer circular guarda el input reciente; por cada VOZ y
// canal hay DOS cabezales de lectura ("splice heads") separados medio grano, que se alejan del cabezal
// de escritura a razón (1−ratio) samples/sample. Cuando un cabezal recorre un grano entero, salta de
// vuelta cerca de la escritura; una ventana de Hann cruza-funde los dos cabezales de modo que el salto
// (discontinuidad de fase) queda enmascarado por el otro cabezal a amplitud plena. Esto transpone por
// un RATIO sin asumir UNA sola nota → anda en pads, acordes, voz, percusión (poly).
//
// Las *sidebands* del splice dan la textura "coro/orquesta" deseada en un shimmer (spec §3.1, HALO).
//
// MULTI-VOZ: N voces simultáneas, cada una con su transposición en semitonos; se SUMAN. Para el shimmer
// alcanzan 1–3 voces. El ratio de cada voz se RAMPEA por-sample (anti-zipper) → cambiar de voicing en
// caliente no mete clicks. La suma pasa por el StereoLimiter compartido (anti-clip, techo del sello).
//
// LATENCIA: intrínseca al tamaño de grano. En un lazo de feedback (HALO) es parte del carácter (cada
// pasada llega corrida). El dry del MIX no necesita PDC en un FX de cola. Ver spec §3.1.
//
// API (spec §3.1): prepare(spec, maxVoices) · setVoices(span<const float semitones>) · process(buffer).
// Pura en el sentido de RT-safe: process() no asigna; setVoices() no asigna (sólo copia a estado).
// =====================================================================================

class PitchShifter
{
public:
    // Dimensiona buffers para 'maxVoices' voces y el sampleRate/maxBlock del spec. Resetea el estado.
    // grainMs: largo del grano del splice (ms). Default 40 ms = compromiso clásico (compat: los callers de
    // 2 args lo conservan idéntico). Granos más largos (HALO 90 ms) → sidebands más suaves ("organ/pad") +
    // más latencia por vuelta (ayuda al bloom). ringSize escala SOLO de grainSamples → auto-dimensiona.
    void prepare (const juce::dsp::ProcessSpec& spec, int maxVoices, float grainMs = 40.0f);

    // Limpia los buffers de grano y la ganancia del limiter (silencio limpio).
    void reset() noexcept;

    // Fija las voces ACTIVAS: 'semitones[0..count)' transposiciones (puede ser negativa = baja).
    // count se clampa a maxVoices. RT-safe (sólo copia). Los ratios objetivo se actualizan; el valor
    // aplicado los persigue rampeado por-sample (no salta). count=0 → sin voces (silencio del wet).
    void setVoices (const float* semitones, int count) noexcept;

    // Procesa in-place L/R: reemplaza el buffer por la SUMA de las voces transpuestas (wet puro; el mix
    // dry/wet lo hace quien llama). Estéreo. RT-safe, sin asignaciones. Pasa por el limiter compartido.
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    // Voces activas actuales (≤ maxVoices).
    int numVoices() const noexcept { return activeVoices; }

    // Latencia (samples) que introduce el granular = medio grano (el centro del crossfade). El processor
    // la puede reportar al host; en un FX de cola (HALO) es parte del carácter. 0 si no se preparó.
    int latencySamples() const noexcept { return grainSamples / 2; }

    // Ganancia del limiter (1 = sin reducción) → para un LED de clip si el plugin lo quiere.
    float limiterGain() const noexcept { return limiter.lastGain(); }

private:
    // Una VOZ = su ratio de transposición (rampeado por-sample) + el estado de fase del grano por canal.
    // El estado de fase es COMPARTIDO entre canales para que L y R queden coherentes (misma imagen).
    struct Voice
    {
        ovni::dsp::LinearRamp ratio { 1.0f };   // ratio actual (persigue 'ratioTarget' rampeado)
        float ratioTarget = 1.0f;               // 2^(semitones/12) objetivo
        double phase = 0.0;                      // fase del grano en [0,1) (0 = un cabezal recién saltó)
        bool   active = false;
    };

    // Lee el buffer circular del canal 'ch' a 'delaySamples' muestras detrás de la escritura, con
    // interpolación lineal (delaySamples fraccional). delaySamples ≥ 0.
    float readDelayed (int ch, float delaySamples) const noexcept;

    double sampleRate = 48000.0;
    int    maxBlock   = 512;
    int    numCh      = 2;

    int    grainSamples = 0;     // largo del grano (muestras) — fija la latencia y la densidad del splice
    int    maxVoicesCap = 3;
    int    activeVoices = 0;

    // Buffer circular de entrada por canal (capacidad = grano + maxBlock + guarda). El cabezal de
    // escritura avanza 1/sample; los cabezales de lectura van por detrás a (1−ratio)·grano.
    std::array<std::vector<float>, 2> ring;
    int   ringSize  = 0;
    int   writePos  = 0;

    std::array<Voice, 3> voices {};   // hasta 3 (kMax del shimmer; maxVoicesCap ≤ 3)

    // Ventana de Hann precomputada (largo del grano) para el crossfade de los dos cabezales. w(0)=w(1)=0,
    // w(0.5)=1 → un cabezal está a amplitud plena justo cuando el otro salta (enmascara la discontinuidad).
    std::vector<float> hann;

    ovni::dsp::StereoLimiter limiter;   // anti-clip de la suma (techo del sello, ver dsp/StereoLimiter.h)

    // Scratch de salida por canal (se acumulan las voces antes de copiar al buffer). Dimensionado a maxBlock.
    std::array<std::vector<float>, 2> outScratch;
};

} // namespace ovni::engines
