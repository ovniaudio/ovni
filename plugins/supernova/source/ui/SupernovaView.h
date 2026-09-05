#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include "render/ParticleParams.h"
#include "analysis/TripleBuffer.h"
#include "analysis/AnalysisFrame.h"
#include "analysis/MidiTriggerQueue.h"
#include "image/ImageLoader.h"

namespace supernova
{
class MetalViewComponent;

// SupernovaView — el "pozo" grande del editor. Hospeda la vista Metal si hay GPU; si no, pinta el panel
// fallback "GPU no compatible" (spec §8) y el host queda intacto.
class SupernovaView : public juce::Component
{
public:
    SupernovaView();
    ~SupernovaView() override;

    bool  gpuAvailable() const noexcept;
    float lastFps() const noexcept;

    void setAnalysisSource (TripleBuffer<AnalysisFrame>* src) noexcept;
    void setMidiTriggerSource (MidiTriggerQueue* q) noexcept;
    void setParams (const ParticleParams& p) noexcept;
    AnalysisFrame lastFrame() const noexcept;

    // Carga una imagen del usuario (RF1). Decode ya hecho fuera del render loop; se sube en el próximo frame.
    void loadImage (std::shared_ptr<const LoadedImage> img) noexcept;
    unsigned activeParticles() const noexcept;
    unsigned totalParticles()  const noexcept;

    void setSyphonEnabled (bool on) noexcept;
    bool isSyphonActive() const noexcept;

    void snapToHome() noexcept;   // CLEAR: lienzo instantáneo (partículas al hogar + acumuladores a cero)
    void updateColors (const uint8_t* rgba, int w, int h) noexcept;   // VIDEO: recolorea el lattice

    void setFullscreen (bool on) noexcept;
    bool isFullscreen() const noexcept;

    // MEDIA SESSION PRO: aspecto del lienzo (→ salida fullscreen), FIT/FILL y BURST (ver MetalViewComponent).
    void  setCanvasAspect (float aspect) noexcept;
    void  setFitMode (int mode) noexcept;
    void  triggerBurst() noexcept;
    void setOnFullscreenClosed (std::function<void()> cb) noexcept;   // sincroniza el botón cuando Esc cierra
    // Hook de CUADRO del render (VBlank): el editor recalcula la modulación de los LFO por cuadro.
    void setOnRenderFrame (std::function<void()> cb) noexcept;


    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    std::unique_ptr<MetalViewComponent> metal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SupernovaView)
};
}
