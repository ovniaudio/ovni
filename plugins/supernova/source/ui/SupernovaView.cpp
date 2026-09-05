#include "ui/SupernovaView.h"
#include "render/metal/MetalViewComponent.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"

namespace supernova
{
SupernovaView::SupernovaView()
{
    metal = std::make_unique<MetalViewComponent>();
    if (metal->gpuAvailable())
        addAndMakeVisible (*metal);
}

SupernovaView::~SupernovaView() = default;

bool  SupernovaView::gpuAvailable() const noexcept { return metal != nullptr && metal->gpuAvailable(); }
float SupernovaView::lastFps()     const noexcept { return metal != nullptr ? metal->lastFps() : 0.0f; }

void SupernovaView::setAnalysisSource (TripleBuffer<AnalysisFrame>* src) noexcept
{
    if (metal != nullptr) metal->setAnalysisSource (src);
}
void SupernovaView::setMidiTriggerSource (MidiTriggerQueue* q) noexcept
{
    if (metal != nullptr) metal->setMidiTriggerSource (q);
}
void SupernovaView::setParams (const ParticleParams& p) noexcept
{
    if (metal != nullptr) metal->setParams (p);
}
AnalysisFrame SupernovaView::lastFrame() const noexcept
{
    return metal != nullptr ? metal->lastFrame() : AnalysisFrame {};
}
void SupernovaView::loadImage (std::shared_ptr<const LoadedImage> img) noexcept
{
    if (metal != nullptr) metal->loadImage (std::move (img));
}
unsigned SupernovaView::activeParticles() const noexcept { return metal != nullptr ? metal->activeParticles() : 0; }
unsigned SupernovaView::totalParticles()  const noexcept { return metal != nullptr ? metal->totalParticles()  : 0; }
void SupernovaView::setSyphonEnabled (bool on) noexcept { if (metal != nullptr) metal->setSyphonEnabled (on); }
void SupernovaView::snapToHome() noexcept { if (metal != nullptr) metal->snapToHome(); }
void SupernovaView::updateColors (const uint8_t* rgba, int w, int h) noexcept { if (metal != nullptr) metal->updateColors (rgba, w, h); }
bool SupernovaView::isSyphonActive() const noexcept { return metal != nullptr && metal->isSyphonActive(); }
void SupernovaView::setFullscreen (bool on) noexcept { if (metal != nullptr) metal->setFullscreen (on); }
bool SupernovaView::isFullscreen() const noexcept { return metal != nullptr && metal->isFullscreen(); }
void SupernovaView::setOnFullscreenClosed (std::function<void()> cb) noexcept
{
    if (metal != nullptr) metal->onFullscreenClosed = std::move (cb);
}
void SupernovaView::setOnRenderFrame (std::function<void()> cb) noexcept
{
    if (metal != nullptr) metal->onFrameTick = std::move (cb);
}
void SupernovaView::setCanvasAspect (float aspect) noexcept { if (metal != nullptr) metal->setCanvasAspect (aspect); }
void SupernovaView::setFitMode (int mode) noexcept          { if (metal != nullptr) metal->setFitMode (mode); }
void SupernovaView::triggerBurst() noexcept                 { if (metal != nullptr) metal->triggerBurst(); }

void SupernovaView::resized()
{
    if (metal != nullptr && metal->gpuAvailable())
        metal->setBounds (getLocalBounds());
}

void SupernovaView::paint (juce::Graphics& g)
{
    if (gpuAvailable())
        return;   // la vista Metal (nativa) cubre este componente

    // Fallback: sin GPU compatible (spec §8) — UI utilable, host intacto.
    g.fillAll (ovni::ui::theme::bg0);
    g.setColour (ovni::ui::theme::mut);
    g.setFont (ovni::ui::fonts::body (17.0f));
    g.drawText ("SUPERNOVA necesita una GPU compatible con Metal",
                getLocalBounds(), juce::Justification::centred);
}
}
