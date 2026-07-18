// [supernova][sequence] — el scheduler de la rotación de fotos: puro, sin decode ni GPU. El editor le
// pregunta shouldAdvance(now) cada tick; si la foto siguiente ya está decodificada, hace el upload y
// confirma con advanced(now). Persistencia por ValueTree (paths muertos se omiten al restaurar).
#include <catch2/catch_test_macros.hpp>
#include "image/PhotoSequence.h"

using supernova::PhotoSequence;

TEST_CASE ("sequence: con menos de 2 fotos queda inactiva", "[supernova][sequence]")
{
    PhotoSequence s;
    REQUIRE (! s.active());
    s.setFiles ({ "/tmp/a.png" });
    REQUIRE (! s.active());
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    REQUIRE (s.active());
    REQUIRE (s.size() == 2);
    REQUIRE (s.currentPath() == "/tmp/a.png");
    REQUIRE (s.nextPath() == "/tmp/b.png");
}

TEST_CASE ("sequence: avanza al cumplirse el intervalo y wrapea", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setIntervalSeconds (8.0);
    s.start (1000.0);                                  // reloj arranca en now=1000 ms

    REQUIRE (! s.shouldAdvance (1000.0 + 7900.0));     // todavía no
    REQUIRE (s.shouldAdvance (1000.0 + 8100.0));       // toca
    s.advanced (1000.0 + 8100.0);
    REQUIRE (s.currentIndex() == 1);
    REQUIRE (s.nextPath() == "/tmp/c.png");

    s.advanced (17000.0);
    REQUIRE (s.currentIndex() == 2);
    REQUIRE (s.nextPath() == "/tmp/a.png");            // wrap
}

TEST_CASE ("sequence: pausa congela el reloj; el intervalo clampea 2..60", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    s.setIntervalSeconds (0.5);
    REQUIRE (s.intervalSeconds() == 2.0);              // clamp abajo
    s.setIntervalSeconds (600.0);
    REQUIRE (s.intervalSeconds() == 60.0);             // clamp arriba

    s.setIntervalSeconds (2.0);
    s.start (0.0);
    s.setPlaying (false);
    REQUIRE (! s.shouldAdvance (10000.0));             // pausada: jamás avanza
    s.setPlaying (true);
    REQUIRE (s.shouldAdvance (10000.0));
}

TEST_CASE ("sequence: removeCurrent poda y sigue; con 1 queda inactiva", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.advanced (0.0);                                   // index 1 (b)
    s.removeCurrent();                                  // b muere
    REQUIRE (s.size() == 2);
    REQUIRE (s.currentPath() == "/tmp/c.png");          // el índice cae al siguiente
    s.removeCurrent();
    REQUIRE (! s.active());                             // 1 foto = ya no es secuencia
}

TEST_CASE ("sequence: removePath poda sin mover la foto actual", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.advanced (0.0);                                   // actual = b
    s.removePath ("/tmp/a.png");                        // podar ANTES de la actual
    REQUIRE (s.currentPath() == "/tmp/b.png");          // la actual no se mueve
    s.removePath ("/tmp/c.png");
    REQUIRE (! s.active());
}

TEST_CASE ("sequence: appendFiles agrega al final sin mover el índice ni las rotaciones", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    s.advanced (0.0);                                   // actual = b (index 1)
    s.rotateCurrent();                                  // b rotada 1
    s.appendFiles ({ "/tmp/c.png" });
    REQUIRE (s.size() == 3);
    REQUIRE (s.currentIndex() == 1);                    // no se movió
    REQUIRE (s.currentRotation() == 1);                 // la rotación de b se conservó
    REQUIRE (s.rotationAt (2) == 0);                    // la nueva entra sin rotar
}

TEST_CASE ("sequence: rotateCurrent cicla 0..3 y es por-foto", "[supernova][sequence]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    REQUIRE (s.currentRotation() == 0);
    s.rotateCurrent(); s.rotateCurrent();
    REQUIRE (s.currentRotation() == 2);                 // a = 2
    s.advanced (0.0);
    REQUIRE (s.currentRotation() == 0);                 // b sigue en 0 (independiente)
    s.rotateCurrent(); s.rotateCurrent(); s.rotateCurrent(); s.rotateCurrent();
    REQUIRE (s.currentRotation() == 0);                 // wrap 4→0
}

TEST_CASE ("sequence: la rotación por-foto sobrevive el round-trip por ValueTree", "[supernova][sequence]")
{
    juce::File a = juce::File::createTempFile ("png");
    juce::File b = juce::File::createTempFile ("png");
    a.replaceWithText ("x"); b.replaceWithText ("x");

    PhotoSequence s;
    s.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    s.rotateCurrent(); s.rotateCurrent(); s.rotateCurrent();   // a = 3
    const auto back = PhotoSequence::fromValueTree (s.toValueTree());
    REQUIRE (back.rotationAt (0) == 3);
    REQUIRE (back.rotationAt (1) == 0);

    a.deleteFile(); b.deleteFile();
}

TEST_CASE ("sequence: round-trip por ValueTree; los paths muertos se omiten", "[supernova][sequence]")
{
    // Dos archivos REALES + uno inexistente: el restore poda el muerto.
    juce::File a = juce::File::createTempFile ("png");
    juce::File b = juce::File::createTempFile ("png");
    a.replaceWithText ("x");
    b.replaceWithText ("x");

    PhotoSequence s;
    s.setFiles ({ a.getFullPathName(), "/tmp/no-existe-jamas.png", b.getFullPathName() });
    s.setIntervalSeconds (12.0);
    s.setPlaying (false);

    const auto vt = s.toValueTree();
    const auto back = PhotoSequence::fromValueTree (vt);
    REQUIRE (back.size() == 2);                        // el muerto se omitió
    REQUIRE (back.intervalSeconds() == 12.0);
    REQUIRE (! back.playing());
    REQUIRE (back.currentPath() == a.getFullPathName());

    a.deleteFile(); b.deleteFile();
}
