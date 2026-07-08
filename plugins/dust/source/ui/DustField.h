#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Glow.h"
#include "ui/BubbleEvents.h"
#include <juce_audio_processors/juce_audio_processors.h>   // RangedAudioParameter (superficie editable)
#include <array>
#include <atomic>

// =============================================================================
// DUST — EL CAMPO DE BURBUJAS (el gancho visual vivo Y la primera superficie
// EDITABLE del sello). Subclase de la base (ovni::ui::VisualizerBase): cache
// px-físico + animación a fps + pausa-CPU + reduced-motion GRATIS.
//
// Qué muestra (honestidad visual — cada macro mueve ALGO):
//   · Burbujas = ecos REALES: nacen donde suenan (FIFO de eventos del motor:
//     azimut + energía + vida), flotan/derivan con VIDA y ESTALLAN al morir.
//   · Cadencia de nacimientos  = RATE  (en SYNC se ve la grilla del tempo)
//   · Cantidad / regeneración  = DENSIDAD
//   · Apertura angular         = SPREAD (del espaciador idle; los eventos reales
//                                 ya la traen puesta en su azimut)
//   · Intensidad / presencia   = MIX (piso de luminosidad: legible a 320 px)
//   · Atenuación al pegar dry  = DUCK (vía la reducción REAL del bloque, duckGr)
//   · Flare de la nube         = wetRms (energía del wet)
//
// ★ ORIGIN ARRASTRABLE: el drag mueve originX/originY por gesto de parámetro
// (beginChangeGesture → setValueNotifyingHost → endChangeGesture: entra en
// automatización/undo del host). El mapeo del drag INVIERTE EXACTAMENTE el
// mapeo con que se DIBUJA el marcador (fieldToScreen/screenToField, una sola
// fuente de verdad → 1:1 bajo el cursor; bug real de PULSAR, briefing §4).
// Convención de signo: pantalla-derecha = canal derecho (la negación vive en el
// MOTOR: DustEngine.originAzimuth = atan2(-x, y); acá x+ = derecha tal cual).
// =============================================================================
namespace dust::ui
{

class DustField : public ovni::ui::VisualizerBase
{
public:
    DustField (std::atomic<float>& mix,
               std::atomic<float>& rateNorm,
               std::atomic<float>& density,
               std::atomic<float>& spread,
               std::atomic<float>& vida,
               std::atomic<float>& duckGr,
               std::atomic<float>& originX,
               std::atomic<float>& originY,
               std::atomic<float>& wetRms,
               BubbleEventFifo& events,
               juce::RangedAudioParameter* originXParam,
               juce::RangedAudioParameter* originYParam);
    ~DustField() override = default;

    // Capacidad del banco de burbujas visuales (voice-stealing de la más vieja). Subido a 64 → el campo
    // se ve POBLADO a la vez (mockup dust-a: muchas burbujas vivas con glow), no media docena.
    static constexpr int kMaxBubbles = 64;

    // ── test-only ────────────────────────────────────────────────────────────
    // Mapeo campo[-1..1]² ⇄ pantalla(px). Públicos para el test unitario de drag
    // 1:1 (screenToField(fieldToScreen(p)) == p ±1px) — son LA fuente de verdad.
    juce::Point<float> dbgFieldToScreen (juce::Point<float> f) const noexcept { return fieldToScreen (f); }
    juce::Point<float> dbgScreenToField (juce::Point<float> s) const noexcept { return screenToField (s); }

    // Corre N frames de animación y devuelve cuántas burbujas NACIERON → prueba
    // que el RATE controla la cadencia del campo (patrón dbgSpawnsOverFrames de HALO).
    int dbgSpawnsOverFrames (int frames) noexcept;

    // Teclado (test-only wrapper): la superficie estrella es operable sin mouse.
    bool dbgKeyPressed (const juce::KeyPress& k) { return keyPressed (k); }

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    // ── teclado (interaction-grammar: foco + flechas — la superficie estrella es accesible) ────
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost   (FocusChangeType) override { repaint(); }

private:
    // ── telemetría (lock-free; el processor escribe, acá sólo se lee) ─────────
    std::atomic<float>& mixSrc;
    std::atomic<float>& rateSrc;
    std::atomic<float>& densitySrc;
    std::atomic<float>& spreadSrc;
    std::atomic<float>& vidaSrc;
    std::atomic<float>& duckGrSrc;
    std::atomic<float>& originXSrc;
    std::atomic<float>& originYSrc;
    std::atomic<float>& wetSrc;
    BubbleEventFifo&    eventsSrc;

    juce::RangedAudioParameter* oxP = nullptr;   // ORIGIN: params del APVTS (drag + dibujo del marcador)
    juce::RangedAudioParameter* oyP = nullptr;

    // ── mapeo campo ⇄ pantalla (UNA sola fuente de verdad para dibujar Y arrastrar) ──
    float fieldRadiusX() const noexcept;
    float fieldRadiusY() const noexcept;
    juce::Point<float> fieldToScreen (juce::Point<float> f) const noexcept;
    juce::Point<float> screenToField (juce::Point<float> s) const noexcept;   // clampea a [-1,1]

    void applyDrag (const juce::MouseEvent& e);
    bool isNearOrigin (juce::Point<float> screenPos) const noexcept;

    // ── estado suavizado (one-pole POST-lectura, k visual) ────────────────────
    float mixSm = 0.35f, rateSm = 0.55f, densitySm = 0.40f, spreadSm = 0.60f;
    float vidaSm = 0.25f, duckGrSm = 0.0f, wetSm = 0.0f;
    float originFx = 0.0f, originFy = 0.35f;   // ORIGIN vigente (de los params; fallback atomics)

    // ── burbujas ─────────────────────────────────────────────────────────────
    struct Bubble
    {
        float ox = 0.0f, oy = 0.0f;     // nacimiento (campo): el ORIGIN al nacer
        float tx = 0.0f, ty = 0.0f;     // destino (campo): donde SUENA el eco (azimut real)
        float life = 1.0f;              // 0 = nace · 1 = estalla y muere
        float lifeStep = 0.02f;         // avance por frame (∝ RATE: ecos lentos viven más)
        float energy = 0.5f;            // 0..1 (ganancia del eco → tamaño/brillo)
        float vida = 0.25f;             // cuánto deriva/flota (VIDA al nacer)
        float seed = 0.0f;              // fase propia (wobble + titileo)
        bool  active = false;
    };
    Bubble bubbles[kMaxBubbles] = {};
    int    nextBubble = 0;

    void spawnBubble (float azimuthRad, float energy01, float vida01) noexcept;
    juce::Point<float> bubblePos (const Bubble& b) const noexcept;   // posición de campo (con deriva)

    // Espaciador idle: si el audio no manda eventos (transporte parado), el campo igual late al
    // RATE (la cadencia es honesta: misma fórmula log 20..2000 ms del processor).
    float spawnAccum       = 0.0f;
    int   framesSinceEvent = 100000;   // arranca "sin audio" → el idle puebla el campo

    float pulsePhase = 0.0f;           // respiración global (wobble de VIDA + titileo)
    juce::Random rng { 0xd057 };       // semilla fija → snapshot/test reproducibles

    // ── interacción ──────────────────────────────────────────────────────────
    bool dragging    = false;
    bool hoverOrigin = false;

    long dbgSpawns = 0;                // contador de nacimientos (test-only)

    // ── bloom ADDITIVO (rediseño 2026-06): todas las burbujas + el ORIGIN se ACUMULAN en una capa
    // premultiplicada (ovni::ui::Bloom) que reproduce el `lighter` del mockup §draw → cada burbuja
    // florece y los solapamientos revientan, en vez de quedar lavados. Cacheada por tamaño. ──────────
    static constexpr float kLumFloor = 0.74f;   // piso de luminosidad de la fuente (mockup §fuente) — alto → cada burbuja brilla
    ovni::ui::Bloom bloom;

    // ── telemetría honesta (strings cacheadas; refrescadas ~cada 0.2 s = 6 frames a 30 fps) ──
    void refreshTelemetry();
    int  teleCountdown = 0;
    juce::String teleCount, teleSpread, teleWet, teleRate, teleLife, teleOrigin;
    float teleWetVal = 0.0f;
    void paintTelemetry (juce::Graphics& g, int w, int h) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustField)
};

} // namespace dust::ui
