// [supernova][live][.] — smoke EN DISPLAY REAL del fullscreen (abre ventanas de verdad; oculto por default,
// correr a mano: OvniSupernovaTests "[live]"). Evidencia para el reporte "se abre y se ve negro": muestra por
// stderr los targets/tamaños/nil-drawables del camino vivo con el 2º present target activo.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_extra/juce_gui_extra.h>
#include "ui/SupernovaView.h"

TEST_CASE ("live: fullscreen a monitor renderea con 2 targets (smoke display real)", "[supernova][live][.]")
{
    supernova::SupernovaView view;
    if (! view.gpuAvailable()) { WARN ("sin GPU — smoke salteado"); SUCCEED(); return; }

    view.setOpaque (true);
    view.setBounds (80, 80, 640, 400);
    view.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    view.setVisible (true);

    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    pump (1500);                    // preview solo: deben aparecer diag lines con targets=1
    REQUIRE_FALSE (view.isFullscreen());

    view.setFullscreen (true);      // abre la ventana fullscreen (2º target, primary)
    pump (2500);                    // deben aparecer diag lines con targets=2 y el tamaño de la pantalla
    REQUIRE (view.isFullscreen());

    view.setFullscreen (false);     // cierra → vuelve a 1 target
    pump (600);
    REQUIRE_FALSE (view.isFullscreen());

    view.removeFromDesktop();
    SUCCEED();
}

// [supernova][live][.] — el crash de campo: apagar Syphon / cerrar la ventana con Syphon ACTIVO hacía
// firar el completion handler de Syphon sobre un server ya liberado (SIGSEGV en publishNewFrame). El fix
// drena el cb en vuelo antes de soltar el server. Si este teardown no crashea, el fix aguanta.
TEST_CASE ("live: Syphon activo → teardown limpio (drena el cb antes de soltar el server)", "[supernova][live][.]")
{
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    {
        supernova::SupernovaView view;
        if (! view.gpuAvailable()) { WARN ("sin GPU — smoke salteado"); SUCCEED(); return; }
        view.setOpaque (true);
        view.setBounds (80, 80, 640, 400);
        view.addToDesktop (juce::ComponentPeer::windowIsTemporary);
        view.setVisible (true);

        pump (600);
        view.setSyphonEnabled (true);
        pump (800);                     // varios frames publican a Syphon (registran completion handlers en sus cb)
        view.setSyphonEnabled (false);  // apagar CON frames en vuelo → debe drenar, no crashear
        REQUIRE_FALSE (view.isSyphonActive());

        view.setSyphonEnabled (true);   // re-activar y…
        pump (400);
        view.removeFromDesktop();
        // …destruir la view con Syphon activo (el dtor apaga+drena Syphon primero).
    }
    pump (200);
    SUCCEED();                          // llegamos sin SIGSEGV → teardown de Syphon limpio
}
