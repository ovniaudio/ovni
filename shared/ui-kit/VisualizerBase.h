#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ovni::ui
{
// =============================================================================
// Base reusable para los VISUALIZADORES del sello (radar, nube de granos, espectro, scope…).
// Generaliza el patrón probado de RadarView (ÓRBITA M5). Da GRATIS, una sola vez, los tres
// trucos de calidad/CPU que hacen que un visualizador se vea "instrumento" y no chupe CPU:
//
//   1) Capa ESTÁTICA cacheada a resolución FÍSICA (getPhysicalPixelScaleFactor) → nítida en Retina.
//      Se rehornea SÓLO al cambiar tamaño/escala, o cuando la subclase llama invalidateStatic().
//   2) Animación a fps fijos (timer) con la capa "viva" dibujada por encima cada frame.
//   3) PAUSA de repaint cuando nada cambia (settleFrames) → en reposo CPU ~0; reanuda al cambiar algo.
//
// Lo ESPECÍFICO de cada plugin (el radar, la nube, el espectro) NO vive acá: la subclase lo aporta
// por los tres hooks de abajo. Ejemplo (un radar) al pie de este archivo.
// =============================================================================
class VisualizerBase : public juce::Component, private juce::Timer
{
public:
    explicit VisualizerBase (int fps = 30);
    ~VisualizerBase() override;

    void paint (juce::Graphics&) override;   // blit de la capa estática + paintLive()
    void resized() override;                  // invalida la capa estática (rehornea con la nueva escala)

    // Test-only: avanza la animación n frames a mano (headless, sin timer/ventana). Lo usan los
    // snapshots de captura real (createComponentSnapshot) para que estela/bloom/partículas se acumulen
    // antes del PNG — el timer no corre cuando el componente no está showing. No afecta el runtime.
    void pumpFrames (int n) noexcept { for (int i = 0; i < n; ++i) advanceFrame(); }

protected:
    //== A IMPLEMENTAR POR LA SUBCLASE ==========================================
    // Dibuja la capa ESTÁTICA en coords LÓGICAS (la base ya aplicó el escalado físico al Graphics).
    virtual void renderStatic (juce::Graphics& g, int width, int height) = 0;
    // Dibuja la capa VIVA (animada) en coords lógicas, encima de la estática, cada frame.
    virtual void paintLive (juce::Graphics& g) = 0;
    // Avanza la animación un frame. Devuelve true si ALGO cambió (hay que repintar). Devolver false
    // de forma sostenida deja que la base pause el repaint (CPU ~0 en reposo).
    virtual bool advanceFrame() { return false; }

    //== UTILIDADES PARA LA SUBCLASE ============================================
    void  invalidateStatic() noexcept { staticDirty = true; }       // forzar rehornear (cambió algo estático)
    void  setSettleHold (int frames) noexcept { settleHold = juce::jmax (0, frames); }  // frames idle antes de pausar
    float currentScale() const noexcept { return staticScale; }     // escala física vigente

    // REDUCED MOTION (accesibilidad): true => el visualizador NO anima (un frame estático coherente, CPU 0).
    // Default = el flag GLOBAL del sello (el host/app lo setea desde la preferencia del SO de "reducir
    // movimiento"). Una subclase puede override-arlo para una fuente propia. Equivalente portable del
    // "Desktop::getAnimationsEnabled()" (que JUCE 8 no expone): wired + testable, sin Obj-C en el header.
    static void setGlobalReducedMotion (bool on) noexcept { globalReducedMotion = on; }
    static bool globalReducedMotionFlag() noexcept        { return globalReducedMotion; }
    virtual bool prefersReducedMotion() const             { return globalReducedMotion; }

private:
    void timerCallback() override;
    void ensureStaticLayer (juce::Graphics& g);

    juce::Image staticLayer;          // capa estática (resolución FÍSICA)
    float       staticScale  = 0.0f;  // escala con la que se horneó staticLayer
    bool        staticDirty  = true;  // pedir rehornear en el próximo paint
    int         settleHold   = 48;    // frames idle antes de pausar el repaint
    int         settleFrames = 0;
    bool        reducedMotion = false; // estado de "menos animación" (accesibilidad) → congela el motion

    static inline bool globalReducedMotion = false;   // preferencia del sello (la setea el host/app)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VisualizerBase)
};
}

// =============================================================================
// EJEMPLO (referencia, NO se compila — es ÓRBITA-específico). Un radar como subclase:
// la base aporta cache px-físico + animación + pausa-CPU; la subclase SÓLO su gancho.
//
//   class RadarView : public ovni::ui::VisualizerBase
//   {
//   public:
//       RadarView (std::atomic<float>& az, std::atomic<float>& dist) : azSrc (az), distSrc (dist) {}
//   protected:
//       void renderStatic (juce::Graphics& g, int w, int h) override
//       {
//           // pozo de profundidad + anillos + ticks de azimut + corner-brackets +
//           // labels mono (FRONT / L·R) + el PATH de la órbita + el orbe-oyente central…
//       }
//       void paintLive (juce::Graphics& g) override
//       {
//           // estela que se afina + punto sonoro (glow sprite cacheado) + halos Room/Width…
//       }
//       bool advanceFrame() override
//       {
//           const float az = azSrc.load (std::memory_order_relaxed);
//           const float d  = distSrc.load (std::memory_order_relaxed);
//           trail[trailPos] = az; trailPos = (trailPos + 1) % kTrail;     // empuja a la estela
//           if (shapeChanged()) invalidateStatic();                       // rehornear el PATH
//           const bool moved = std::abs (az - lastAz) > 1.0e-4f || std::abs (d - lastDist) > 1.0e-4f;
//           lastAz = az; lastDist = d;
//           return moved;     // la base sigue repintando settleHold frames más (para que la estela colapse) y luego pausa
//       }
//   };
// =============================================================================
