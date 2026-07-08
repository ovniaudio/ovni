#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Glow.h"
#include <atomic>

// =============================================================================
// HALO — EL FUNNEL DE CORONAS (el gancho visual vivo; rediseño mockup halo-a).
// Subclase de la base del sello (ovni::ui::VisualizerBase): cache px-físico +
// animación a fps + pausa-CPU GRATIS. Lee telemetría lock-free del processor — NO
// conoce el processor concreto, sólo atomics, así queda desacoplado del DSP.
//
// Identidad visual (mockup halo-a): los anillos NACEN en la FUENTE (centro-abajo del
// campo), ASCIENDEN por el eje vertical y ORBITAN alrededor de él — cada uno una
// CORONA en perspectiva (elipse achatada cuyo ancho oscila con la fase orbital) →
// el conjunto lee como un FUNNEL / vórtice de coronas apiladas, el bloom magenta del
// sello reventando. Reacciona a TODAS las macros (regla anti-bug PULSAR):
//   · Radio base de las coronas  = Size
//   · Cantidad / brillo anillos   = Shimmer  (más capa pitched = más coronas en vuelo)
//   · Persistencia / vida         = Decay    (más feedback = las coronas viven/suben más)
//   · Tinte / oscuridad           = Tone     (damping del lazo = magenta más profundo)
//   · Órbita / compresión elíptica= Orbit    (más órbita = coronas más giratorias)
//   · Presencia / opacidad wet    = Mix
//   · Floración del bloom          = loopRms  (la energía del lazo enciende la columna)
//   · Congelado                   = Freeze   (las coronas se sostienen, la nube capturada)
//   · Velocidad nacer/subir/orbitar = rateNorm (RATE/órbita: FREE knob o SYNC división)
//
// NO es un control interactivo: HALO se maneja con los knobs. El funnel es el gancho que reacciona.
// =============================================================================
namespace halo::ui
{

class HaloRings : public ovni::ui::VisualizerBase
{
public:
    HaloRings (std::atomic<float>& shimmer,
               std::atomic<float>& decay,
               std::atomic<float>& size,
               std::atomic<float>& tone,
               std::atomic<float>& orbit,
               std::atomic<float>& mix,
               std::atomic<float>& freeze,
               std::atomic<float>& loopRms,
               std::atomic<float>& rateNorm);   // VELOCIDAD (0..1) de la órbita/RATE → ritmo de las coronas
    ~HaloRings() override = default;

    // Capacidad del banco de coronas en vuelo (las que ascienden). Más que suficiente para el flujo del funnel.
    static constexpr int kRings = 16;

    // Test-only [diccionario][halo]: corre N frames de animación y devuelve cuántas coronas NACIERON →
    // prueba que el RATE controla la velocidad de los circulitos (slow = pocas, fast = muchas).
    int dbgSpawnsOverFrames (int frames) noexcept;

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

private:
    std::atomic<float>& shimmerSrc;
    std::atomic<float>& decaySrc;
    std::atomic<float>& sizeSrc;
    std::atomic<float>& toneSrc;
    std::atomic<float>& orbitSrc;
    std::atomic<float>& mixSrc;
    std::atomic<float>& freezeSrc;
    std::atomic<float>& loopRmsSrc;
    std::atomic<float>& rateNormSrc;   // velocidad (0..1) de la órbita/RATE → ritmo de nacimiento/ascenso/órbita

    // Estado cacheado + suavizado (knob → funnel sin saltos).
    float shimmerNow = 0.55f, decayNow = 0.75f, sizeNow = 0.65f, toneNow = 0.45f, mixNow = 0.40f;
    float orbitNow = 0.40f, freezeNow = 0.0f, loopNow = 0.0f, rateNow = 0.19f;
    float shimmerSm = 0.55f, decaySm = 0.75f, sizeSm = 0.65f, toneSm = 0.45f, mixSm = 0.40f;
    float orbitSm = 0.40f, freezeSm = 0.0f, loopSm = 0.0f, rateSm = 0.19f;
    float orbitPhase = 0.0f;   // fase orbital global (compresión elíptica + deriva lateral de cada corona)
    float coreGlow   = 0.0f;   // pulso de la FUENTE cuando nace una corona (decae cada frame)
    float pulsePhase = 0.0f;   // latido lento global (el funnel "respira" aunque las macros estén quietas)

    // Una corona en vuelo: nace en la FUENTE (life=0) y asciende/crece/orbita mientras se desvanece (life→1).
    // 'seed' = ángulo orbital propio (fase). Sin 'active': life>=1 ⇒ libre (el banco se recicla por nextRing).
    struct Ring { float life = 1.0f; float seed = 0.0f; bool active = false; };
    Ring rings[kRings] = {};
    int   nextRing   = 0;
    float spawnAccum = 0.0f;   // acumulador para temporizar el nacimiento de coronas (∝ Shimmer × RATE)
    int   liveCount  = 0;      // coronas activas (para la telemetría "ANILLOS NN")

    void spawnRing() noexcept;
    long dbgSpawns = 0;   // contador de nacimientos (test-only)

    // Tinte del halo: magenta del sello (familia Espacio/Profundidad). Tone corre claro→magenta profundo
    // (mockup §tintColor). 'alpha' modula la opacidad. No cambia de familia.
    juce::Colour tintColour (float tone01, float alpha) const noexcept;

    // BLOOM ADDITIVO (rediseño 2026-06): toda la luz del funnel (columna + coronas + chispas + fuente) se
    // ACUMULA en una capa premultiplicada (ovni::ui::Bloom) que reproduce el `lighter` del mockup — los
    // solapamientos revientan hacia el blanco — y se vuelca de una sola vez sobre el chasis. Cacheada por tamaño.
    ovni::ui::Bloom bloom;

    // Suma el bloom de UNA corona (nube apilada + trazo elíptico brillante + chispas de flanco) a `bl`.
    // mockup §drawRing, ahora additivo. Devuelve por addSprite/gfx de la capa de luz.
    void drawCorona (ovni::ui::Bloom& bl, const Ring& r, float cx, float baseR,
                     float toneAmt, float orbAmt, float decayAmt, float wetPresence, float flare,
                     float srcY, float topYpos) const;

    // ── telemetría honesta (strings cacheadas, refresco ~cada 0.2 s = 6 frames a 30 fps; mockup §tele) ──
    int          teleCountdown = 0;
    juce::String teleRings, teleRt, teleSize, teleOrbit, teleTone;
    float        teleToneVal = 0.0f;
    void         refreshTelemetry();
    void         paintTelemetry (juce::Graphics& g, int w, int h) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HaloRings)
};

} // namespace halo::ui
