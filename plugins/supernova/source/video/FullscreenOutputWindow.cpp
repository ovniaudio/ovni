#include "video/FullscreenOutputWindow.h"
#include "render/metal/MetalRenderer.h"
#include "render/metal/SupernovaMTKView.h"

namespace supernova
{
FullscreenOutputWindow::FullscreenOutputWindow (MetalRenderer& r, const juce::Rectangle<int>& area,
                                                float canvasAspect, std::function<void()> onClose)
    : juce::DocumentWindow (juce::String::fromUTF8 ("SUPERNOVA \xE2\x80\x94 salida"), juce::Colours::black, 0),
      onCloseCb (std::move (onClose)), renderer (r)
{
    setOpaque (true);
    setUsingNativeTitleBar (false);
    setTitleBarHeight (0);

    // 2ª vista Metal: MISMO device del renderer, registrada como present target PRIMARY (la salida grande/limpia
    // → Syphon publica esta). El preview del editor pasa a secundario.
    view   = std::make_unique<juce::NSViewComponent>();
    nsView = createSupernovaMTKView (renderer.deviceHandle());   // +1
    layer  = metalLayerOf (nsView);
    renderer.addPresentTarget (layer, /*primary*/ true);
    view->setView (nsView);

    // Holder negro → letterbox al aspecto del lienzo (0 = pantalla entera). Ni el holder ni la vista
    // interceptan el mouse: el click burbujea a la ventana → mouseDown cierra.
    holder = std::make_unique<Holder>();
    holder->inner  = view.get();
    holder->aspect = canvasAspect > 0.0f ? canvasAspect : 0.0f;
    holder->addAndMakeVisible (*view);
    holder->setInterceptsMouseClicks (false, false);
    view->setInterceptsMouseClicks (false, false);

    setContentNonOwned (holder.get(), false);
    setBounds (area);
    setAlwaysOnTop (true);
    addToDesktop (0);                    // borderless
    setVisible (true);
    holder->setBounds (getLocalBounds());
    holder->resized();
    setWantsKeyboardFocus (true);
    toFront (true);          // foco real (en un DAW el editor puede retenerlo) → Esc funciona siempre
    grabKeyboardFocus();
}

void FullscreenOutputWindow::setCanvasAspect (float aspect)
{
    if (holder == nullptr) return;
    holder->aspect = aspect > 0.0f ? aspect : 0.0f;
    holder->resized();
}

void FullscreenOutputWindow::mouseDown (const juce::MouseEvent&)
{
    if (onCloseCb) onCloseCb();   // click en cualquier lado = salir (nunca atrapado con 1 sola pantalla)
}

FullscreenOutputWindow::~FullscreenOutputWindow()
{
    // Teardown ordenado (R1): dejar de dibujar ese target ANTES de soltar la NSView; el renderer re-promueve el
    // preview a primary. Un cb en vuelo retiene sus propias texturas hasta completar (no hay UAF).
    renderer.removePresentTarget (layer);
    if (view != nullptr) view->setView (nullptr);
    if (holder != nullptr) { holder->removeAllChildren(); holder->inner = nullptr; }   // nada apunta a la vista muerta
    view.reset();
    holder.reset();
    if (nsView != nullptr) { destroySupernovaMTKView (nsView); nsView = nullptr; }
}

bool FullscreenOutputWindow::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { if (onCloseCb) onCloseCb(); return true; }
    return false;
}

void FullscreenOutputWindow::userTriedToCloseWindow()
{
    if (onCloseCb) onCloseCb();
}
}
