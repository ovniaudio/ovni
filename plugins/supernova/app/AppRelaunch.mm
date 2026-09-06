// Ver AppRelaunch.h para el porqué de cada pieza (bundleURL en vez de identificador, y el waiter).
#include "AppRelaunch.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>

#include <unistd.h>   // getpid()
#include <cstdio>
#include <juce_gui_basics/juce_gui_basics.h>

namespace supernova {

juce::String runningBundlePath()
{
    @autoreleasepool
    {
        NSURL* url = NSBundle.mainBundle.bundleURL;   // la copia EXACTA que se está ejecutando
        if (url == nil || url.path == nil) return {};
        return juce::String::fromUTF8 (url.path.UTF8String);
    }
}

void relaunchThisBundle()
{
    const auto path = runningBundlePath();
    if (path.isEmpty())
    {
        std::fprintf (stderr, "[supernova] REOPEN: no pude resolver el bundle en ejecución — no relanzo\n");
        return;
    }
    std::fprintf (stderr, "[supernova] REOPEN: relanzo esta copia → %s\n", path.toRawUTF8());

    // argv completo (ver AppRelaunch.h): la ruta va como argumento posicional, nunca dentro del script.
    // path está garantizado no-vacío por el early-return de arriba — importa: JUCE descarta los argumentos
    // VACÍOS antes del execvp (juce_SharedCode_posix.h:1149), y un hueco correría las posiciones $1/$2/$3.
    const auto argv = relaunchWaiterCommand (path, (int) getpid());

    // Desadjuntado: sobrevive a nuestra salida (queda huérfano bajo launchd).
    juce::ChildProcess relauncher;
    if (! relauncher.start (argv))
    {
        // Si el relanzador no arrancó, cerrar sería dejar al usuario sin app y sin explicación.
        std::fprintf (stderr, "[supernova] REOPEN: no pude lanzar el relanzador — no cierro\n");
        return;
    }
    juce::Timer::callAfterDelay (200, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
}

} // namespace supernova

#endif // JUCE_MAC
