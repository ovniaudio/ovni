#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include <array>
#include <atomic>

// =============================================================================
// AURORA — LA AURORA DESPLEGADA (el gancho visual vivo; rediseño mockup aurora-a).
// Subclase de la base del sello (ovni::ui::VisualizerBase): cache px-físico +
// animación a fps + pausa-CPU GRATIS. Lee telemetría lock-free del processor — NO
// conoce el processor concreto, sólo atomics (desacoplado del DSP).
//
// Identidad visual ≠ HALO (anillos) ≠ NÉBULA (nube): AURORA es el ESPECTRO
// DESPLEGADO EN POSICIÓN sobre un HORIZONTE — 24 CORTINAS DE LUZ verticales, cada
// banda en su azimut (uiBandPos) con su energía (uiBandEnergy), naciendo de la
// línea de horizonte (y≈0.70) como una aurora boreal. La visualización ES la
// explicación: cada frecuencia vive en un lugar del campo y el campo respira.
//
// Capas (mockup aurora-a):
//   · ESTÁTICA (AuroraFieldStatic.h, baked 1×): cielo, resplandor del horizonte,
//     banda de luz, polvo estelar LCG, eje L↔R + rótulos, scanlines/vignette/rim.
//   · VIVA (cada frame): las cortinas (cuerpo translúcido + filamento + pie con
//     bloom por SPRITE cacheado), el charco-reflejo bajo el horizonte, el faro del
//     MONO SAFE, y la TELEMETRÍA honesta en las 4 esquinas (strings ~cada 0.2 s).
//
// Reacciona a TODAS las macros (regla anti-bug PULSAR — ninguna macro sin efecto):
//   · Spread/γ  → apertura del despliegue (las cortinas se separan del centro)
//   · Tilt      → la curva se reordena (graves-centro ⇄ agudos-centro), con SIGNO
//   · Motion    → el abanico LATE (γ oscila; el titileo acelera ∝ RATE)
//   · MonoSafe  → los graves amarrados al centro (faro + colapso de las bajas)
//   · Duck      → el abanico se CIERRA en la pegada del dry (duckEnv → dim + cierre)
//   · Mix       → presencia global (piso de luz: nunca apagado, legible a 320 px)
//
// POSICIONES HONESTAS: con señal cada cortina se dibuja donde el MOTOR la puso
// (uiBandPos real); sin energía cae a la MISMA ley del motor replicada en UI
// (idlePosFor) → el despliegue coincide con lo que va a sonar. Motion sólo en
// alpha/transform (compositor-friendly); reduced-motion = frame estático coherente.
// =============================================================================
namespace aurora::ui
{

class AuroraField : public ovni::ui::VisualizerBase
{
public:
    // Debe casar con AuroraEngine/AuroraProcessor::kVizBands (static_assert en PluginEditor.h).
    static constexpr int kBands = 24;

    AuroraField (std::atomic<float>& spread,
                 std::atomic<float>& tilt,
                 std::atomic<float>& motion,
                 std::atomic<float>& monoSafe,
                 std::atomic<float>& duck,
                 std::atomic<float>& mix,
                 std::atomic<float>& gamma,      // γ EFECTIVO (spread·motion·duck): el abanico late/respira
                 std::atomic<float>& duckEnv,    // envolvente del dry (la "pegada" que cierra el abanico)
                 std::atomic<float>& rateNorm,   // velocidad del MOTION 0..1 (FREE o SYNC) → ritmo del titileo
                 std::array<std::atomic<float>, kBands>& bandEnergy,
                 std::array<std::atomic<float>, kBands>& bandPos);
    ~AuroraField() override = default;

    //== Test-only [diccionario][aurora] ========================================
    // Avanza N frames de animación (converge los one-pole) y expone la posición
    // DIBUJADA (-1..+1) y el nivel (0..1) por banda → prueba determinística de que
    // SPREAD abre, TILT reordena y MONO SAFE amarra los graves (macro → visual).
    void  dbgAdvanceFrames (int frames) noexcept { for (int i = 0; i < frames; ++i) advanceFrame(); }
    float dbgDrawPos (int band) const noexcept   { return drawPos[(size_t) juce::jlimit (0, kBands - 1, band)]; }
    float dbgDrawLvl (int band) const noexcept   { return drawLvl[(size_t) juce::jlimit (0, kBands - 1, band)]; }

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

private:
    // — fuentes de telemetría (el audio escribe, acá sólo se lee) —
    std::atomic<float>& spreadSrc;
    std::atomic<float>& tiltSrc;
    std::atomic<float>& motionSrc;
    std::atomic<float>& monoSafeSrc;
    std::atomic<float>& duckSrc;
    std::atomic<float>& mixSrc;
    std::atomic<float>& gammaSrc;
    std::atomic<float>& duckEnvSrc;
    std::atomic<float>& rateNormSrc;
    std::array<std::atomic<float>, kBands>& bandEnergySrc;
    std::array<std::atomic<float>, kBands>& bandPosSrc;

    // — estado suavizado (de-zipper visual one-pole, POST-mapeo) —
    float spreadSm = 0.55f, tiltSm = 0.0f, motionSm = 0.0f, msSm = 0.5f;
    float duckSm = 0.0f, mixSm = 1.0f, gammaSm = 0.55f, duckEnvSm = 0.0f, rateSm = 0.45f;
    std::array<float, kBands> eSm {};      // energía por banda suavizada
    std::array<float, kBands> pSm {};      // posición del MOTOR por banda suavizada
    std::array<float, kBands> drawPos {};  // posición FINAL dibujada (-1..+1; idle⇄motor por energía)
    std::array<float, kBands> drawLvl {};  // nivel FINAL dibujado (0..1, con piso de luz)
    std::array<float, kBands> bandSeed {}; // fase de titileo propia (semilla fija → snapshot reproducible)
    float twinklePhase = 0.0f;             // titileo/serpenteo global de las cortinas (sólo alpha/ondulación)

    // Ley del motor replicada en UI (idle fallback): γ·e(u,tilt)·weave(u) + colapso
    // Mono Safe. Misma matemática que AuroraEngine (sin la cota de fase por bin, que a
    // resolución de 24 bandas no cambia el dibujo; con señal manda la posición REAL).
    float idlePosFor (float u, float gamma, float tilt, float monoSafe01) const noexcept;

    // BLOOM cacheado (patrón HaloRings/StellarPad): sprites blancos horneados una vez,
    // bliteados teñidos + escalados + con alpha → floración real sin gradientes por frame.
    void        ensureSprites();
    juce::Image glowSprite;      // disco radial (cabeza de la cortina + pie + faros)
    juce::Image curtainSprite;   // tira vertical (la cortina: núcleo brillante, velo ancho, pie que se funde)
    void blitGlow    (juce::Graphics& g, float cx, float cy, float radius, juce::Colour tint) const;
    void blitCurtain (juce::Graphics& g, float cx, float yTop, float width, float height, juce::Colour tint) const;

    // Una cortina: cuerpo translúcido + filamento central + pie con bloom (mockup §drawField).
    void paintCurtain (juce::Graphics& g, float x, float baseY, float colH,
                       float colW, float lvl, float hue, float alpha, float phaseB) const;
    void paintReflection (juce::Graphics& g, int w, int h, float baseY, float presence) const;
    void paintTelemetry  (juce::Graphics& g, int w, int h) const;
    void refreshTelemetry();

    // Telemetría (strings cacheadas; se refrescan cada ~0.2 s, mockup §tele honesta).
    int teleCountdown = 0;
    juce::String teleGamma, teleSpread, teleTilt, teleDuck, teleLR;
    float teleDuckVal = 0.0f;
    float teleEnergyL = 0.5f, teleEnergyR = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraField)
};

} // namespace aurora::ui
