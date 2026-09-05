#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>
#include "image/CanvasFormat.h"

// Ventana nativa dedicada a pantalla completa (RF7): SOLO el visual, en la pantalla elegida. Hospeda una 2ª
// vista Metal del MISMO renderer (2º present target → UNA sola simulación). Esc / cerrar → onClose(). Sin reloj
// propio: la refresca el VBlank del editor principal. Todo por el message thread.
namespace supernova
{
class MetalRenderer;

class FullscreenOutputWindow : public juce::DocumentWindow
{
public:
    // canvasAspect: 0 = llena la pantalla; >0 = la vista se centra con ese aspecto y el resto queda negro
    // (MEDIA SESSION PRO: la salida muestra EXACTAMENTE el lienzo que ves en el editor).
    FullscreenOutputWindow (MetalRenderer& renderer, const juce::Rectangle<int>& area, float canvasAspect,
                            std::function<void()> onClose);
    ~FullscreenOutputWindow() override;

    void setCanvasAspect (float aspect);   // en vivo (el editor cambió el formato con la salida abierta)

    bool keyPressed (const juce::KeyPress& k) override;
    void mouseDown (const juce::MouseEvent& e) override;   // click = salir (nunca quedar atrapado en 1 pantalla)
    void userTriedToCloseWindow() override;

private:
    // Contenedor negro que posiciona la vista Metal al aspecto del lienzo (ResizableWindow maneja los bounds
    // del content component; el letterbox vive un nivel abajo).
    struct Holder final : juce::Component
    {
        juce::Component* inner = nullptr;
        float aspect = 0.0f;
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
        void resized() override
        {
            if (inner == nullptr) return;
            const auto r = fitRect (0, 0, getWidth(), getHeight(), aspect);
            inner->setBounds (r.x, r.y, r.w, r.h);
        }
    };

    std::function<void()>                  onCloseCb;
    MetalRenderer&                         renderer;
    std::unique_ptr<Holder>                holder;
    std::unique_ptr<juce::NSViewComponent> view;
    void* nsView = nullptr;   // SupernovaMTKView* (+1)
    void* layer  = nullptr;   // CAMetalLayer* (no-owning) — el 2º present target

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullscreenOutputWindow)
};
}
