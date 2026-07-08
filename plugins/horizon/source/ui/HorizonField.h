#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include <array>
#include <atomic>

// =============================================================================
// HorizonField — el gancho visual de HORIZON (SPL·02): el ESPECTRO SUSPENDIDO que
// PULSA al ritmo. Subclase de la base del sello (ovni::ui::VisualizerBase): cache
// px-físico + animación a fps + pausa-CPU GRATIS. Lee telemetría lock-free del
// processor — NO conoce el processor concreto, sólo atomics (desacoplado del DSP,
// patrón HaloRings/AuroraField).
//
// Identidad visual ≠ HALO (anillos que orbitan) ≠ DUST (burbujas) ≠ AURORA (cortinas
// por azimut): HORIZON es el ESPECTRO CRISTALIZADO Y SUSPENDIDO sobre el horizonte —
// eje X = FRECUENCIA (graves→aire, log), cada banda dibujada como una LÁMINA DE
// CRISTAL espejada arriba/abajo del horizonte de eventos. La visualización ES la
// explicación: al FREEZE el espectro se cristaliza (se detiene, suspendido); con
// RATE late/gatea (el cristal pulsa); en SYNC, al pulso del track.
//
// Reacciona a TODAS las macros (regla anti-bug PULSAR — ninguna macro sin efecto
// visual):
//   · Freeze  → el espectro se CRISTALIZA (las láminas aparecen nítidas, con piso
//               de luz; sin freeze quedan fantasmales/tenues)
//   · Rate    → el cristal LATE (gateAmp modula la altura/bloom; gatePhase barre la
//               luz a lo largo del espectro). RATE off = quieto (pad suspendido)
//   · Whisper → el cristal TIEMBLA/se difumina (jitter horizontal + parpadeo de alpha
//               por lámina, semillas fijas → snapshot reproducible)
//   · Spread  → el espectro se ABRE a los lados (las láminas se abanican L/R)
//   · Duck    → el cristal se ENCOGE/atenúa cuando entra el dry (duck·duckEnv)
//   · Mix     → intensidad global (piso de luz: nunca "apagado", legible a 320 px)
//
// BLOOM REAL (patrón HaloRings/AuroraField): sprites blancos horneados una vez,
// bliteados teñidos + escalados + con alpha → floración barata (sólo drawImage por
// lámina). Motion sólo en transform/opacity/alpha (compositor-friendly);
// reduced-motion → un frame estático coherente (lo maneja la base).
// =============================================================================
namespace horizon::ui
{

class HorizonField : public ovni::ui::VisualizerBase
{
public:
    static constexpr int kBands = 24;   // debe casar con HorizonProcessor::kVizBands

    HorizonField (std::atomic<float>& whisper,
                  std::atomic<float>& spread,
                  std::atomic<float>& duck,
                  std::atomic<float>& mix,
                  std::atomic<float>& freeze,
                  std::atomic<float>& gateAmp,
                  std::atomic<float>& gatePhase,
                  std::atomic<float>& wetEnergy,
                  std::atomic<float>& duckEnv,
                  std::atomic<float>& rateNorm,
                  std::array<std::atomic<float>, kBands>& spectrum);
    ~HorizonField() override = default;

    // Test-only [diccionario]: avanzar N frames sin ventana visible + leer la geometría
    // dibujada de una banda (verificar que TODAS las macros mueven el visual — anti-bug PULSAR).
    void  dbgAdvanceFrames (int frames) noexcept { for (int i = 0; i < frames; ++i) advanceFrame(); }
    float dbgBandHeight (int band) const noexcept
    { return drawLvl[(size_t) juce::jlimit (0, kBands - 1, band)]; }
    // Alpha efectivo de una lámina (captura freeze·gate·whisper·duck·mix — la MISMA fórmula que
    // paintLive). Sube con freeze/mix, baja con duck activo, parpadea con whisper.
    float dbgBandAlpha (int band) const noexcept;
    // X dibujado de una lámina, en [0..1] del ancho útil (captura SPREAD = abanico + WHISPER =
    // jitter). u central (0.5) no se mueve; las bandas extremas se abren con SPREAD.
    float dbgBandX (int band) const noexcept;

protected:
    void renderStatic (juce::Graphics& g, int width, int height) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

private:
    // — fuentes de telemetría (el audio escribe, acá sólo se lee) —
    std::atomic<float>& whisperA;
    std::atomic<float>& spreadA;
    std::atomic<float>& duckA;
    std::atomic<float>& mixA;
    std::atomic<float>& freezeA;
    std::atomic<float>& gateAmpA;
    std::atomic<float>& gatePhaseA;
    std::atomic<float>& wetEnergyA;
    std::atomic<float>& duckEnvA;
    std::atomic<float>& rateNormA;
    std::array<std::atomic<float>, kBands>& spectrumA;

    // — estado suavizado (de-zipper visual one-pole, POST-mapeo) —
    float whisperSm = 0.12f, spreadSm = 0.5f, duckSm = 0.0f, mixSm = 1.0f;
    float freezeSm = 0.0f, gateAmpSm = 1.0f, duckEnvSm = 0.0f, rateSm = 0.0f;
    float gatePhaseSm = 0.0f;                 // fase del latido (para el barrido de luz)
    std::array<float, kBands> eSm {};         // energía/altura del espectro por banda suavizada
    std::array<float, kBands> drawLvl {};     // nivel FINAL dibujado por banda (0..1, con gate/freeze/piso)
    std::array<float, kBands> bandSeed {};    // fase de tiembleo propia (semilla fija → snapshot reproducible)
    float twinklePhase = 0.0f;                // tiembleo global del cristal (sólo alpha, ∝ whisper/rate)

    // BLOOM cacheado (patrón HaloRings/AuroraField): sprites blancos horneados una vez,
    // bliteados teñidos + escalados + con alpha → floración real sin gradientes por frame.
    void        ensureSprites();
    juce::Image glowSprite;     // disco radial (cabeza de la lámina / barrido de luz)
    juce::Image shardSprite;    // tira vertical (la lámina de cristal: núcleo brillante, borde suave)
    void blitGlow  (juce::Graphics& g, float cx, float cy, float radius, juce::Colour tint) const;
    void blitShard (juce::Graphics& g, float cx, float yTop, float width, float height, juce::Colour tint) const;

    // Telemetría HONESTA en las 4 esquinas (mockup §tele): strings cacheadas, refrescadas
    // ~cada 0.2 s, con valores REALES de los atomics (estado FREEZE/LIVE, SPREAD, RATE, láminas,
    // DUCK, balance L/R derivado del abanico real). No interactivo.
    void paintTelemetry (juce::Graphics& g, int w, int h) const;
    void refreshTelemetry();
    int          teleCountdown = 0;
    juce::String teleState, teleSpread, teleRate, teleDuck, teleBalance;
    bool         teleFrozen  = false;

    // Tinte por banda: familia VERDE (Espectral) SIEMPRE. Graves = verde profundo,
    // aire = menta brillante (el degradé del cristal). No cambia de familia.
    juce::Colour bandTint (float u, float alpha) const noexcept;

    // Geometría de una lámina (pura, sin Graphics) — fuente única para paintLive y los dbg
    // accessors (DRY). bandAlpha01 = freeze·gate·whisper·duck·mix; bandXFrac = log-f + abanico
    // del SPREAD + jitter del WHISPER (0..1 del ancho útil).
    float bandAlpha01 (int band) const noexcept;
    float bandXFrac   (int band) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HorizonField)
};

} // namespace horizon::ui
