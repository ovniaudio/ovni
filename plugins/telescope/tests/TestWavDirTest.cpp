// [telescope][tmp] — F4 de la 0.2 (T9, lo que dejó la F3): los WAV de los tests caen en la carpeta de build del
// worktree, no en el temporal que comparten las sesiones. TestWav.h buscaba una carpeta llamada `build`, y con
// `cmake-build-02/` o `cmake-build-f3/` caía al temporal del usuario. Ahora reconoce cualquier carpeta de build por
// su `CMakeCache.txt`. Este test lo verifica desde el runner que corre: si el runner vive en una carpeta de build,
// tempDir() está adentro de ella.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include "TestWav.h"

TEST_CASE ("telescope: los WAV de los tests caen en la carpeta de build del worktree", "[telescope][tmp]")
{
    // La carpeta de build del runner, buscada igual que un humano: la primera con CMakeCache.txt hacia arriba.
    juce::File build;
    for (auto d = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
         d.getFullPathName().isNotEmpty() && d != d.getParentDirectory(); d = d.getParentDirectory())
        if (d.getChildFile ("CMakeCache.txt").existsAsFile()) { build = d; break; }

    const auto dir = telescope::test::tempDir();
    const auto sys = juce::File::getSpecialLocation (juce::File::tempDirectory);
    std::printf ("TMP[wav] build=«%s» · tempDir=«%s»\n", build.getFullPathName().toRawUTF8(), dir.getFullPathName().toRawUTF8());
    // Una COPIA del runner fuera del build (las series pesadas corren copias guardadas) no tiene carpeta de build:
    // ahí TestWav cae al temporal con nombre propio, a propósito, y este test no tiene qué verificar.
    if (build == juce::File()) { WARN ("el runner no está adentro de un build de CMake: nada que verificar"); return; }
    REQUIRE (dir.isAChildOf (build));
    REQUIRE_FALSE (dir.isAChildOf (sys));
    REQUIRE (dir.getFileName() == "tests-tmp");
}
