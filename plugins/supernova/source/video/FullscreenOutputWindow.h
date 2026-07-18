#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>
#include <memory>

// Ventana nativa dedicada a pantalla completa (RF7): SOLO el visual, en la pantalla elegida. Hospeda una 2ª
// vista Metal del MISMO renderer (2º present target → UNA sola simulación). Esc / cerrar → onClose(). Sin reloj
// propio: la refresca el VBlank del editor principal. Todo por el message thread.
namespace supernova
{
class MetalRenderer;

class FullscreenOutputWindow : public juce::DocumentWindow
{
public:
    FullscreenOutputWindow (MetalRenderer& renderer, const juce::Rectangle<int>& area,
                            std::function<void()> onClose);
    ~FullscreenOutputWindow() override;

    bool keyPressed (const juce::KeyPress& k) override;
    void mouseDown (const juce::MouseEvent& e) override;   // click = salir (nunca quedar atrapado en 1 pantalla)
    void userTriedToCloseWindow() override;

private:
    std::function<void()>                  onCloseCb;
    MetalRenderer&                         renderer;
    std::unique_ptr<juce::NSViewComponent> view;
    void* nsView = nullptr;   // SupernovaMTKView* (+1)
    void* layer  = nullptr;   // CAMetalLayer* (no-owning) — el 2º present target

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FullscreenOutputWindow)
};
}
