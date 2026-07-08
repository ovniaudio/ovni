#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Glow.h"
#include <juce_audio_processors/juce_audio_processors.h>   // RangedAudioParameter (campo interactivo)
#include <array>
#include <atomic>

// =============================================================================
// PULSAR — EL POZO ESTELAR (el gancho visual vivo; rediseño mockup pulsar-a).
// Subclase de la base del sello (ovni::ui::VisualizerBase): cache px-físico +
// animación a fps + pausa-CPU GRATIS. Acá sólo va el gancho de PULSAR:
//   · capa ESTÁTICA (StellarPadStatic.h): pozo profundo, polvo estelar baked,
//     anillos con glow, vignette, rim-shadow, scanlines.
//   · capa VIVA: estela heat cian→magenta→ámbar (2 pasadas: difusa por SMEAR +
//     núcleo), bloom de la fuente en 3 capas (sprite cacheado por bucket de heat,
//     piso de luminosidad 0.62) + núcleo blanco, cruz/orbe del campo (FIELD X/Y),
//     telemetría mono en las 4 esquinas (valores REALES de los atomics).
// Lee telemetría lock-free del processor — NO conoce el processor concreto.
//
// Honestidad visual (regla de marca): muestra MOVIMIENTO/trayectoria; jamás una
// cabeza/HRTF ni una grilla 3D clínica.
// =============================================================================
namespace pulsar::ui
{

class StellarPad : public ovni::ui::VisualizerBase
{
public:
    // El pad es CONTROLADOR: el drag setea el centro del atractor (fieldX/fieldY) por
    // seteo directo del APVTS (entra en automatización/recall). `motion` dimensiona el orbe del centro;
    // `smear` gobierna la capa difusa de la estela (espejo visual del knob SMEAR).
    StellarPad (std::atomic<float>& azimuth,
                std::atomic<float>& depth,
                std::atomic<float>& heat,
                std::atomic<float>& distance,
                std::atomic<float>& motion,
                std::atomic<float>& smear,
                std::atomic<float>& shape,
                juce::RangedAudioParameter* fieldX,
                juce::RangedAudioParameter* fieldY);
    ~StellarPad() override = default;

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    std::atomic<float>& azSrc;
    std::atomic<float>& depthSrc;
    std::atomic<float>& heatSrc;
    std::atomic<float>& distSrc;
    std::atomic<float>& motionSrc;
    std::atomic<float>& smearSrc;
    std::atomic<float>& shapeSrc;
    juce::RangedAudioParameter* fxP = nullptr;
    juce::RangedAudioParameter* fyP = nullptr;

    // Centro del campo (lo refleja el visual): cacheado de los params para dibujar la cruz/orbe.
    float fieldXNow = 0.0f, fieldYNow = 0.0f, motionNow = 0.0f, smearNow = 0.0f;
    float shapeNow  = 0.0f;          // SHAPE [0,1] cacheado → posición del marcador del medidor de morph

    // (x,y) escalados por distancia: cerca (0) = la órbita se contrae al oyente, lejos (1) = al borde.
    juce::Point<float> cvToPad (float x, float y, float dist01, int w, int h) const noexcept;
    juce::Colour heatColour (float heat, float alpha) const noexcept;

    // --- bloom ADDITIVO (rediseño 2026-06): la estela + la fuente se ACUMULAN en una capa premultiplicada
    //     (ovni::ui::Bloom) que reproduce el `lighter` del mockup §draw → la estela cometaria y la cabeza
    //     REVIENTAN, en vez de quedar lavadas por el alpha-over. Cacheada por tamaño. ----------------------
    ovni::ui::Bloom bloom;

    void paintFieldCross (juce::Graphics& g, int w, int h) const;
    void paintSource (ovni::ui::Bloom& bl, int w, int h);       // halo additivo de la cabeza (glow)
    void paintTrailCore (juce::Graphics& g, int w, int h);      // estela reciente NÍTIDA (corta, alpha-over)
    void paintSourceCore (juce::Graphics& g, int w, int h);     // núcleo OPACO de la cabeza (alpha-over, arriba)
    void paintMorphMeter (juce::Graphics& g, int w, int h) const;  // medidor de morph T3 (reemplaza el label falso)
    void paintTelemetry (juce::Graphics& g, int w, int h) const;
    void refreshTelemetry();

    // FÓSFORO (larga exposición, rediseño 2026-06): capa persistente que se desvanece de a poco por frame +
    // traza nueva (alpha-over heat) → la forma del atractor se DIBUJA SOLA como un osciloscopio. Preset-
    // agnóstica (un solo mecanismo para cualquier preset). Se compone aditiva sobre el pozo oscuro.
    void updatePhosphor (int w, int h);   // corre en advanceFrame (NO en paint): desvanece + dibuja la traza
    juce::Image phosphor;                 // buffer del fósforo (supersample fijo, independiente del paint)
    int   rebuildFrames = 0;              // frames restantes del "reset suave" (fade fuerte tras un salto/preset)
    bool  phosSkipSeg   = true;           // no unir la traza a través de un salto (preset/jump) ni recién creado
    float shapeLast     = 1.0e9f;         // SHAPE del frame previo (detección morph continuo vs salto)
    static constexpr float kPhosScale     = 2.0f;    // supersample del fósforo (glow suave; no necesita 4K nítido)
    static constexpr float kKeepNormal    = 0.972f;  // persistencia "Media" (vida media ~0.8 s a 30 fps)
    static constexpr float kKeepReset     = 0.55f;   // fade fuerte durante el reset suave
    static constexpr int   kRebuildFrames = 14;      // duración del reset suave (~0.5 s a 30 fps)
    static constexpr float kShapeJump     = 0.045f;  // umbral de salto de SHAPE/frame: > ⇒ reset; ≤ ⇒ morph
    static constexpr int   kCoreTrail     = 30;      // largo de la estela reciente nítida (sobre el fósforo)

    static constexpr int kTrailMax = 180;   // cometa LARGO (mockup pulsar-a): el loop entero se llena de estela
    struct TrailPt { float x = 0.0f, y = 0.0f, heat = 0.0f, dist = 0.5f; };
    TrailPt trail[kTrailMax] = {};
    int     trailHead = 0;
    int     trailLen  = 0;

    float lastX = 1.0e9f, lastY = 1.0e9f, lastDist = -1.0f;

    // telemetría (strings cacheadas; se refrescan cada ~0.2 s, mockup §tele)
    int teleCountdown = 0;
    juce::String teleTheta, teleAz, teleDst, teleVel, teleHeat, teleField;
    float teleHeatVal = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StellarPad)
};

} // namespace pulsar::ui
