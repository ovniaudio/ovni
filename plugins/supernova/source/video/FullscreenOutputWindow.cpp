#include "video/FullscreenOutputWindow.h"
#include "render/metal/MetalRenderer.h"
#include "render/metal/SupernovaMTKView.h"

namespace supernova
{
FullscreenOutputWindow::FullscreenOutputWindow (MetalRenderer& r, const juce::Rectangle<int>& area,
                                                std::function<void()> onClose)
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

    setContentNonOwned (view.get(), false);
    setBounds (area);
    setAlwaysOnTop (true);
    addToDesktop (0);                    // borderless
    setVisible (true);
    view->setBounds (getLocalBounds());
    setWantsKeyboardFocus (true);
    toFront (true);          // foco real (en un DAW el editor puede retenerlo) → Esc funciona siempre
    grabKeyboardFocus();
    // el click en la vista burbujea a la ventana → mouseDown cierra (la NSView Metal no intercepta el mouse
    // porque el hit va al Component contenedor JUCE)
    if (view != nullptr) view->setInterceptsMouseClicks (false, false);
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
    view.reset();
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
