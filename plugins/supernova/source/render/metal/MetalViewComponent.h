#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <atomic>
#include <memory>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/TripleBuffer.h"
#include "analysis/AnalysisFrame.h"
#include "analysis/MidiTriggerQueue.h"
#include "image/ImageLoader.h"
#include "video/FullscreenOutputWindow.h"
#include <functional>

// MetalViewComponent — juce::NSViewComponent que hospeda la CAMetalLayer y la maneja con VBlankAttachment
// (tick en el message thread → esquiva las trampas de CVDisplayLink). Teardown ordenado (R1):
// parar el clock → soltar la view → liberar el device. Si no hay GPU, no crea view (gpuAvailable()==false).
namespace supernova
{
class MetalViewComponent : public juce::NSViewComponent
{
public:
    MetalViewComponent();
    ~MetalViewComponent() override;

    bool  gpuAvailable() const noexcept { return renderer != nullptr && renderer->isAvailable(); }
    void  setParams (const ParticleParams& p) noexcept { params = p; }
    float lastFps() const noexcept { return fps.load (std::memory_order_relaxed); }

    // Fuente de análisis (el TripleBuffer del processor). Consumidor ÚNICO: este componente.
    void setAnalysisSource (TripleBuffer<AnalysisFrame>* src) noexcept { analysisSrc = src; }
    AnalysisFrame lastFrame() const noexcept { return lastAnalysis; }

    // Cola de triggers MIDI visuales (explosión / rayo). Consumidor ÚNICO: este componente (drena en tick()).
    void setMidiTriggerSource (MidiTriggerQueue* q) noexcept { midiSrc = q; }

    // Carga una imagen del usuario (RF1). La subida al GPU se aplica en tick() (borde de frame, mismo hilo del
    // render → sin carrera con la GPU); el DECODE ya ocurrió fuera del render loop (RNF4). Llamar en msg thread.
    void loadImage (std::shared_ptr<const LoadedImage> img) noexcept { pendingImage = std::move (img); }

    // Densidad efectiva actual (RNF2) para el HUD.
    unsigned activeParticles() const noexcept { return renderer != nullptr ? renderer->activeParticles() : 0; }
    unsigned totalParticles()  const noexcept { return renderer != nullptr ? renderer->totalParticles()  : 0; }

    // Servidor Syphon (RF7) on/off.
    void setSyphonEnabled (bool on) noexcept { if (renderer != nullptr) renderer->setSyphonEnabled (on); }
    bool isSyphonActive() const noexcept { return renderer != nullptr && renderer->isSyphonActive(); }

    // CLEAR (estado cero): el lienzo aparece en el primer frame — sin viaje de vuelta al hogar.
    void snapToHome() noexcept { if (renderer != nullptr) renderer->snapToHome(); }
    // VIDEO: recolorea el lattice desde un frame (camino rápido, sin re-análisis).
    void updateColors (const uint8_t* rgba, int w, int h) noexcept
    { if (renderer != nullptr) renderer->updateColors (rgba, w, h); }

    // Fullscreen a monitor (RF7): SOLO el visual en una pantalla dedicada (Esc vuelve). Auto-elige la secundaria.
    void setFullscreen (bool on);
    bool isFullscreen() const noexcept { return fsWindow != nullptr; }
    std::function<void()> onFullscreenClosed;   // el editor lo engancha para sincronizar el botón

    MetalRenderer* getRenderer() noexcept { return renderer.get(); }   // para prepare()/uploadImage()

private:
    void tick (double timestampSec);

    std::unique_ptr<MetalRenderer>          renderer;
    void*                                   nsView = nullptr;   // SupernovaMTKView* (+1)
    std::unique_ptr<juce::VBlankAttachment> vblank;
    ParticleParams params;
    TripleBuffer<AnalysisFrame>* analysisSrc = nullptr;
    MidiTriggerQueue*            midiSrc     = nullptr;
    AnalysisFrame lastAnalysis {};
    std::shared_ptr<const LoadedImage> pendingImage;   // set en msg thread, aplicado en tick() (msg thread)
    std::unique_ptr<FullscreenOutputWindow> fsWindow;   // ventana de salida fullscreen (2º present target)

    double lastTs = 0.0;
    std::atomic<float> fps { 0.0f };
    double fpsAccum = 0.0;
    int    fpsFrames = 0;
    double logAccum = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetalViewComponent)
};
}
