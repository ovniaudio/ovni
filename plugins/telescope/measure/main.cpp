// telescope-measure — mide archivos y tramos con el motor de TELESCOPE y devuelve JSON (contrato D-100).
//
//   echo '{"file":"/ruta/stem.wav","from_s":30,"to_s":45}' | telescope-measure
//   telescope-measure --version      → «telescope-measure <versión> <sha>» y exit 0, sin leer la entrada (F4)
//
// Pedidos por la entrada estándar, uno por línea; una línea de JSON por pedido, en el mismo orden, por la salida
// estándar. La especificación es SCHEMA.md, al lado de este archivo. Exit 0 = corrió (cada línea dice si midió
// o se negó); cualquier otro exit es una falla.
#include <cstring>
#include <iostream>
#include "MeasureCore.h"

#if defined(_WIN32)
 #include <fcntl.h>
 #include <io.h>
#endif

int main (int argc, char** argv)
{
    // F4 de la 0.2: --version dice la versión y el sha del motor, los MISMOS del objeto `engine` de cada línea
    // (SCHEMA.md). Cualquier otro argumento se sigue ignorando: los pedidos van por la entrada estándar.
    if (argc > 1 && std::strcmp (argv[1], "--version") == 0)
    {
        const auto e = telescope::measure::builtEngine();
        std::cout << "telescope-measure " << e.version << " " << e.sha << "\n";
        std::cout.flush();
        return std::cout ? 0 : 3;
    }

#if defined(_WIN32)
    // En modo texto Windows convierte "\n" en "\r\n" al escribir y al revés al leer: los bytes de la salida
    // dejarían de ser los mismos que en la Mac. Binario en las dos puntas.
    _setmode (_fileno (stdin), _O_BINARY);
    _setmode (_fileno (stdout), _O_BINARY);
#endif
    std::ios::sync_with_stdio (false);
    return telescope::measure::run (std::cin, std::cout, telescope::measure::builtEngine());
}
