#include "render/metal/MetalViewComponent.h"
#include "render/metal/SupernovaMTKView.h"
#include "render/IRenderer.h"
#include "image/FactoryImage.h"
#include "analysis/AnalysisFrame.h"
#include <cstdio>
#include <vector>

namespace supernova
{
MetalViewComponent::MetalViewComponent()
{
    renderer = std::make_unique<MetalRenderer>();

    if (renderer->isAvailable())
    {
        nsView = createSupernovaMTKView (renderer->deviceHandle());   // +1 retained
        renderer->setLayer (metalLayerOf (nsView));
        setView (nsView);                                             // JUCE retiene su copia

        // M0: grilla 512×512 (~262k partículas, default del spec) + imagen de fábrica (RF1).
        renderer->prepare (512, 512);
        auto factory = makeFactoryImage (512, 512);
        renderer->uploadImage ({ factory.data(), 512, 512 });

        vblank = std::make_unique<juce::VBlankAttachment> (this, [this] (double ts) { tick (ts); });
    }
}

MetalViewComponent::~MetalViewComponent()
{
    if (renderer != nullptr)
        renderer->setSyphonEnabled (false);   // 0. apagar+drenar Syphon: su completion handler no debe firar tras liberar el server
    fsWindow.reset();               // 0.5 cerrar fullscreen (quita el 2º present target) ANTES de soltar el renderer
    vblank.reset();                 // 1. parar el clock — no más render() tras esto
    setView (nullptr);              // 2. JUCE suelta su ref a la NSView
    if (nsView != nullptr)          // 3. soltar el +1 propio
    {
        destroySupernovaMTKView (nsView);
        nsView = nullptr;
    }
    renderer.reset();               // 4. device/queue por último (nadie los usa ya)
}

void MetalViewComponent::setFullscreen (bool on)
{
    if (! on) { fsWindow.reset(); return; }
    if (renderer == nullptr || ! renderer->isAvailable() || fsWindow != nullptr) return;

    // Auto-elegir pantalla: la primera NO principal (el 2º monitor del VJ); si hay una sola, la principal.
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (displays.isEmpty()) return;
    juce::Rectangle<int> area = displays.getReference (0).totalArea;
    for (const auto& d : displays)
        if (! d.isMain) { area = d.totalArea; break; }

    // onClose (Esc / cerrar) no puede destruir la ventana desde su propio callback → diferir al msg thread.
    juce::Component::SafePointer<MetalViewComponent> safe (this);
    fsWindow = std::make_unique<FullscreenOutputWindow> (*renderer, area, [safe]
    {
        juce::MessageManager::callAsync ([safe]
        {
            if (safe == nullptr) return;
            safe->fsWindow.reset();
            if (safe->onFullscreenClosed) safe->onFullscreenClosed();
        });
    });
}

void MetalViewComponent::tick (double timestampSec)
{
    if (renderer == nullptr || ! renderer->isAvailable())
        return;

    const double dt = (lastTs > 0.0) ? juce::jlimit (0.0, 0.1, timestampSec - lastTs) : (1.0 / 60.0);
    lastTs = timestampSec;

    // Imagen pendiente (drag&drop): subir en el borde de frame, serializado con render(). La máscara del
    // sujeto SIEMPRE viaja (si existe); el knob CUTOUT decide en vivo cuánto fondo borrar (uniform).
    if (pendingImage != nullptr)
    {
        if (pendingImage->valid())
            renderer->uploadImage (pendingImage->source (true));
        pendingImage.reset();
    }

    // Triggers MIDI visuales (M3): drenar la cola y armar params efectivos. Colapsar varias notas de un tick en
    // un pulso es correcto (ataque agudo + decay en el renderer). No muta `params` (los knobs del editor).
    ParticleParams eff = params;
    for (MidiTriggerEvent ev; midiSrc != nullptr && midiSrc->pop (ev); )
    {
        if (ev.type == MidiTriggerType::Explosion)           eff.explode = 1.0f;
        else if (ev.type == MidiTriggerType::DirectionalRay) { eff.rayTrigger = true; eff.rayAngle = ev.angle; }
    }

    // Último frame de análisis publicado por el AnalysisThread (o cero si no hay fuente/audio).
    lastAnalysis = (analysisSrc != nullptr) ? analysisSrc->read() : AnalysisFrame {};
    renderer->render (lastAnalysis, eff, dt);

    // fps rolling (30 frames) + log a stderr cada ~1s para medir sin OCR.
    fpsAccum += dt;
    if (++fpsFrames >= 30)
    {
        fps.store ((float) (fpsFrames / juce::jmax (1.0e-6, fpsAccum)), std::memory_order_relaxed);
        fpsAccum = 0.0; fpsFrames = 0;
    }
    logAccum += dt;
    if (logAccum >= 1.0)
    {
        std::fprintf (stderr, "[supernova] %.1f fps\n", fps.load (std::memory_order_relaxed));
        logAccum = 0.0;
    }
}
}
