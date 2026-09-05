// [supernova][sequence] — el scheduler de la rotación de fotos: puro, sin decode ni GPU. El editor le
// pregunta shouldAdvance(now) cada tick; si la foto siguiente ya está decodificada, hace el upload y
// confirma con advanced(now). Persistencia por ValueTree (desde la ronda 3 los paths muertos se CONSERVAN
// marcados como faltantes, no se podan).
#include <catch2/catch_test_macros.hpp>
#include "image/PhotoSequence.h"
#include <catch2/catch_approx.hpp>

using Catch::Approx;
constexpr auto PhotoSequence_Clock_Seconds = supernova::SeqClock::Seconds;
constexpr auto PhotoSequence_Clock_Beats   = supernova::SeqClock::Beats;
constexpr auto PhotoSequence_Clock_Kick    = supernova::SeqClock::Kick;
constexpr auto PhotoSequence_Order_Loop    = supernova::SeqOrder::Loop;
constexpr auto PhotoSequence_Order_Shuffle = supernova::SeqOrder::Shuffle;

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

// (Los helpers removeCurrent / removePath se fueron en la ronda 3: PODAR era justo lo que hacía
// desaparecer las fotos en silencio. Un archivo que no está se MARCA faltante — ver los tests de más
// abajo — y sacarlo de verdad es una decisión del usuario, que va por removeAt.)

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

// CONTRATO CAMBIADO EN LA RONDA 3: antes el restore PODABA los paths muertos ("patrón sampler") y las
// fotos desaparecían sin explicación al mover una carpeta. Ahora la secuencia los CONSERVA tal cual — quién
// existe y quién no lo mira el EDITOR (es el filesystem, no el modelo) y marca los que faltan con
// setMissing, como Premiere/Resolume: el tile se queda, marcado, y se relinkea.
TEST_CASE ("sequence: round-trip por ValueTree; los paths muertos se CONSERVAN (no se podan)", "[supernova][sequence]")
{
    // Dos archivos REALES + uno inexistente: el restore devuelve los TRES.
    juce::File a = juce::File::createTempFile ("png");
    juce::File b = juce::File::createTempFile ("png");
    a.replaceWithText ("x");
    b.replaceWithText ("x");

    PhotoSequence s;
    s.setFiles ({ a.getFullPathName(), "/tmp/no-existe-jamas.png", b.getFullPathName() });
    s.setIntervalSeconds (12.0);
    s.setPlaying (false);

    const auto vt = s.toValueTree();
    auto back = PhotoSequence::fromValueTree (vt);
    REQUIRE (back.size() == 3);                        // el muerto sigue ahí, en su posición
    REQUIRE (back.pathAt (1) == "/tmp/no-existe-jamas.png");
    REQUIRE (! back.isMissing (1));                    // el modelo no toca el disco: lo marca el editor
    REQUIRE (back.intervalSeconds() == 12.0);
    REQUIRE (! back.playing());
    REQUIRE (back.currentPath() == a.getFullPathName());

    a.deleteFile(); b.deleteFile();
}

// ===================== RONDA 3 · B — MEDIA FALTANTE (el tile se queda, marcado) =====================
TEST_CASE ("sequence: el reloj SALTEA los items faltantes; si faltan todos se queda donde está",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    REQUIRE (! s.isMissing (0));
    REQUIRE (s.nextIndex() == 1);

    s.setMissing (1, true);
    REQUIRE (s.isMissing (1));
    REQUIRE (s.nextIndex() == 2);            // la 1 falta: la saltea

    s.setMissing (2, true);
    REQUIRE (s.nextIndex() == 3);            // saltea las dos

    // Avanzar de verdad respeta el salteo.
    s.advanced (supernova::SeqTick { 0.0, 0.0, 0u });
    REQUIRE (s.currentIndex() == 3);

    // Con TODAS las otras faltando, se queda donde está (mejor congelar que cortar a la nada).
    s.setMissing (0, true);
    REQUIRE (s.nextIndex() == 3);
    s.advanced (supernova::SeqTick { 0.0, 0.0, 0u });
    REQUIRE (s.currentIndex() == 3);
}

TEST_CASE ("sequence: el orden de reproducción del export tampoco pasa por los faltantes",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setMissing (1, true);
    const auto order = supernova::playOrderFrom (s, 5);
    REQUIRE (order.size() == 5);
    for (int i : order) REQUIRE (i != 1);    // el MP4 no puede mostrar un archivo que no está
}

TEST_CASE ("sequence: setPathAt relinkea el item y le saca la marca de faltante", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setRotationAt (1, 2);
    s.setMissing (1, true);

    REQUIRE (s.setPathAt (1, "/tmp/nueva/b.png"));
    REQUIRE (s.pathAt (1) == "/tmp/nueva/b.png");
    REQUIRE (! s.isMissing (1));             // relinkeado = encontrado
    REQUIRE (s.rotationAt (1) == 2);         // la rotación que le habías dado se conserva
    REQUIRE (s.nextIndex() == 1);            // vuelve a entrar en el reloj

    REQUIRE (! s.setPathAt (9, "/tmp/x.png"));   // índice inválido: no hace nada
}

TEST_CASE ("sequence: sacar y reordenar mantienen alineadas las marcas de faltante", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setMissing (2, true);

    s.moveItem (2, 0);                       // el faltante va al principio
    REQUIRE (s.pathAt (0) == "/tmp/c.png");
    REQUIRE (s.isMissing (0));
    REQUIRE (! s.isMissing (1));

    s.removeAt (1);                          // sacar otro no corre la marca
    REQUIRE (s.size() == 2);
    REQUIRE (s.isMissing (0));
    REQUIRE (! s.isMissing (1));
}

// ============================== MEDIA SESSION PRO (2026-09-02): cue · reorden · sacar · relojes · shuffle ==

TEST_CASE ("sequence: jumpTo salta al item y re-arma el reloj", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    s.setIntervalSeconds (4.0);
    s.start (1000.0);
    REQUIRE (s.jumpTo (2));
    REQUIRE (s.currentIndex() == 2);
    REQUIRE (s.currentPath() == "/tmp/c.png");
    REQUIRE (s.nextPath() == "/tmp/d.png");
    REQUIRE (! s.shouldAdvance (1000.0 + 3900.0));        // primer tick tras el cue = armar (no avanza aunque pasaron 3.9s)
    REQUIRE (! s.shouldAdvance (1000.0 + 3900.0 + 3900.0));
    REQUIRE (s.shouldAdvance (1000.0 + 3900.0 + 4100.0));  // 4s después del re-arme
    REQUIRE (! s.jumpTo (4));                              // fuera de rango
    REQUIRE (! s.jumpTo (-1));
    REQUIRE (s.currentIndex() == 2);
}

TEST_CASE ("sequence: moveItem reordena y el item actual sigue siendo el actual", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    s.setRotationAt (3, 2);                                // d rotada 2 (viaja con ella)
    s.advanced (0.0);                                      // actual = b (1)

    REQUIRE (s.moveItem (3, 0));                           // d al principio: [d a b c]
    REQUIRE (s.pathAt (0) == "/tmp/d.png");
    REQUIRE (s.rotationAt (0) == 2);
    REQUIRE (s.currentPath() == "/tmp/b.png");             // b sigue actual...
    REQUIRE (s.currentIndex() == 2);                       // ...aunque ahora es la #3

    REQUIRE (s.moveItem (2, 3));                           // mover el ACTUAL al final: [d a c b]
    REQUIRE (s.currentIndex() == 3);
    REQUIRE (s.currentPath() == "/tmp/b.png");
    REQUIRE (s.nextPath() == "/tmp/d.png");                // wrap

    REQUIRE (! s.moveItem (1, 1));                         // no-op
    REQUIRE (! s.moveItem (0, 9));                         // fuera de rango
}

TEST_CASE ("sequence: removeAt saca por índice; el actual cae al siguiente; antes del actual ajusta el índice",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    s.advanced (0.0); s.advanced (0.0);                    // actual = c (2)
    s.removeAt (0);                                        // sacar a (antes del actual)
    REQUIRE (s.currentPath() == "/tmp/c.png");
    REQUIRE (s.currentIndex() == 1);
    s.removeAt (1);                                        // sacar el ACTUAL (c) → cae a d
    REQUIRE (s.currentPath() == "/tmp/d.png");
    REQUIRE (s.currentIndex() == 1);
    s.removeAt (1);                                        // sacar d (actual, último) → wrap a b
    REQUIRE (s.currentPath() == "/tmp/b.png");
    REQUIRE (s.currentIndex() == 0);
    REQUIRE (! s.active());
    s.removeAt (7);                                        // fuera de rango = no-op
    REQUIRE (s.size() == 1);
}

TEST_CASE ("sequence: progress del reloj SECONDS va de 0 a 1", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    s.setIntervalSeconds (10.0);
    REQUIRE (s.progress ({ 5000.0, 0.0, 0u }) == 0.0f);    // sin armar
    s.start (0.0);
    REQUIRE (s.progress ({ 5000.0, 0.0, 0u }) == Approx (0.5f));
    REQUIRE (s.progress ({ 20000.0, 0.0, 0u }) == 1.0f);   // clamp
}

TEST_CASE ("sequence: reloj BEATS cambia al cruzar cada ventana de N beats (y re-arma si el transport rebobina)",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setClock (PhotoSequence_Clock_Beats);
    s.setIntervalBeats (4.0);                              // un compás

    REQUIRE (! s.shouldAdvance ({ 0.0,   1.5, 0u }));      // primer tick = armar en beat 1.5
    REQUIRE (! s.shouldAdvance ({ 100.0, 3.9, 0u }));      // misma ventana [0,4)
    REQUIRE (  s.shouldAdvance ({ 200.0, 4.1, 0u }));      // cruzó el compás
    REQUIRE (  s.shouldAdvance ({ 210.0, 4.2, 0u }));      // pegajoso hasta advanced()
    s.advanced ({ 210.0, 4.2, 0u });
    REQUIRE (s.currentIndex() == 1);
    REQUIRE (! s.shouldAdvance ({ 300.0, 7.9, 0u }));
    REQUIRE (  s.shouldAdvance ({ 400.0, 8.0, 0u }));
    s.advanced ({ 400.0, 8.0, 0u });
    // rebobinó (el DAW volvió al principio): se re-arma sin disparar
    REQUIRE (! s.shouldAdvance ({ 500.0, 0.5, 0u }));
    REQUIRE (! s.shouldAdvance ({ 600.0, 3.5, 0u }));
    REQUIRE (  s.shouldAdvance ({ 700.0, 4.0, 0u }));
    // progreso = posición dentro de la ventana
    REQUIRE (s.progress ({ 0.0, 6.0, 0u }) == Approx (0.5f));
}

TEST_CASE ("sequence: reloj KICK cambia con cada onset nuevo respetando el gap mínimo", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setClock (PhotoSequence_Clock_Kick);
    s.setKickGapSeconds (1.0);

    REQUIRE (! s.shouldAdvance ({ 0.0,    0.0, 5u }));     // armar: el contador actual es la base
    REQUIRE (! s.shouldAdvance ({ 100.0,  0.0, 5u }));     // sin onset nuevo
    REQUIRE (! s.shouldAdvance ({ 500.0,  0.0, 6u }));     // onset, pero dentro del gap (0.5s < 1s)
    REQUIRE (! s.shouldAdvance ({ 900.0,  0.0, 6u }));
    REQUIRE (  s.shouldAdvance ({ 1200.0, 0.0, 7u }));     // onset fuera del gap → dispara
    REQUIRE (  s.shouldAdvance ({ 1250.0, 0.0, 7u }));     // pegajoso (esperando el decode)
    s.advanced ({ 1250.0, 0.0, 7u });
    REQUIRE (s.currentIndex() == 1);
    REQUIRE (! s.shouldAdvance ({ 1300.0, 0.0, 8u }));     // dentro del gap desde el cambio
    REQUIRE (  s.shouldAdvance ({ 2300.0, 0.0, 9u }));
    REQUIRE (s.progress ({ 1750.0, 0.0, 9u }) == Approx (0.5f));   // mitad del gap
}

TEST_CASE ("sequence: stepIntervalBeats recorre 1·2·4·8·16·32·64 y clampea", "[supernova][sequence][media]")
{
    PhotoSequence s;
    REQUIRE (s.intervalBeats() == 4.0);
    s.stepIntervalBeats (+1); REQUIRE (s.intervalBeats() == 8.0);
    s.stepIntervalBeats (+1); s.stepIntervalBeats (+1); s.stepIntervalBeats (+1); s.stepIntervalBeats (+1);
    REQUIRE (s.intervalBeats() == 64.0);
    s.stepIntervalBeats (+1); REQUIRE (s.intervalBeats() == 64.0);   // techo
    for (int i = 0; i < 10; ++i) s.stepIntervalBeats (-1);
    REQUIRE (s.intervalBeats() == 1.0);                                // piso
    s.setKickGapSeconds (0.01); REQUIRE (s.kickGapSeconds() == 0.25);
    s.setKickGapSeconds (99.0); REQUIRE (s.kickGapSeconds() == 8.0);
}

TEST_CASE ("sequence: SHUFFLE elige el próximo por adelantado (prefetch) y nunca repite el actual", "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png", "/tmp/e.png" });
    s.setOrder (PhotoSequence_Order_Shuffle);
    s.setSeed (1234u);
    for (int step = 0; step < 200; ++step)
    {
        const int cur = s.currentIndex();
        const int nxt = s.nextIndex();
        REQUIRE (nxt != cur);
        REQUIRE (s.nextPath() == s.pathAt (nxt));          // el prefetch decodifica exactamente ese
        s.advanced (0.0);
        REQUIRE (s.currentIndex() == nxt);                 // el cambio va a donde el prefetch fue
    }
    // Determinista por semilla.
    PhotoSequence t1, t2;
    t1.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" }); t1.setOrder (PhotoSequence_Order_Shuffle); t1.setSeed (7u);
    t2.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" }); t2.setOrder (PhotoSequence_Order_Shuffle); t2.setSeed (7u);
    for (int i = 0; i < 20; ++i) { REQUIRE (t1.nextIndex() == t2.nextIndex()); t1.advanced (0.0); t2.advanced (0.0); }
    // Volver a LOOP → el siguiente vuelve a ser el +1.
    s.setOrder (PhotoSequence_Order_Loop);
    REQUIRE (s.nextIndex() == (s.currentIndex() + 1) % 5);
}

TEST_CASE ("sequence: reloj/orden/burst/beats/gap sobreviven el round-trip por ValueTree (con defaults viejos)",
           "[supernova][sequence][media]")
{
    juce::File a = juce::File::createTempFile ("png");
    juce::File b = juce::File::createTempFile ("png");
    a.replaceWithText ("x"); b.replaceWithText ("x");

    PhotoSequence s;
    s.setFiles ({ a.getFullPathName(), b.getFullPathName() });
    s.setClock (PhotoSequence_Clock_Beats);
    s.setIntervalBeats (16.0);
    s.setKickGapSeconds (2.5);
    s.setOrder (PhotoSequence_Order_Shuffle);
    s.setBurst (true);
    const auto back = PhotoSequence::fromValueTree (s.toValueTree());
    REQUIRE (back.clock() == PhotoSequence_Clock_Beats);
    REQUIRE (back.intervalBeats() == 16.0);
    REQUIRE (back.kickGapSeconds() == 2.5);
    REQUIRE (back.orderMode() == PhotoSequence_Order_Shuffle);
    REQUIRE (back.burst());

    // Un state VIEJO (sin las props nuevas) restaura los defaults = comportamiento de siempre.
    juce::ValueTree old ("sequence");
    old.setProperty ("interval", 8.0, nullptr);
    for (const auto& f : { a, b }) { juce::ValueTree c ("photo"); c.setProperty ("path", f.getFullPathName(), nullptr); old.appendChild (c, nullptr); }
    const auto legacy = PhotoSequence::fromValueTree (old);
    REQUIRE (legacy.clock() == PhotoSequence_Clock_Seconds);
    REQUIRE (legacy.orderMode() == PhotoSequence_Order_Loop);
    REQUIRE (! legacy.burst());
    REQUIRE (legacy.intervalBeats() == 4.0);

    a.deleteFile(); b.deleteFile();
}

// MEDIA SESSION PRO ronda 2 · T3 — CUE ARMADO (beat snap). En BEATS, cuear un tile no corta al instante:
// espera el próximo límite de ventana (lo que hace Resolume y todo software de VJ). El scheduler sólo dice
// CUÁNDO; el editor hace el salto.
TEST_CASE ("sequence: armCue espera el próximo límite de ventana de beats y jumpTo lo consume",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setClock (PhotoSequence_Clock_Beats);
    s.setIntervalBeats (4.0);                                  // ventanas: [0,4) [4,8) [8,12)…

    REQUIRE (s.pendingCue() == -1);
    REQUIRE (! s.shouldFireCue ({ 0.0, 1.0, 0u }));            // sin cue armado no dispara nada

    s.armCue (2);
    REQUIRE (s.pendingCue() == 2);
    REQUIRE (! s.shouldFireCue ({ 0.0, 1.0, 0u }));            // primer tick: se toma el beat de armado
    REQUIRE (! s.shouldFireCue ({ 0.0, 2.5, 0u }));            // misma ventana [0,4): todavía no
    REQUIRE (! s.shouldFireCue ({ 0.0, 3.99, 0u }));
    REQUIRE (s.shouldFireCue ({ 0.0, 4.0, 0u }));              // cruzó al compás siguiente: AHORA
    REQUIRE (s.currentIndex() == 0);                           // el scheduler no salta solo

    s.jumpTo (s.pendingCue());
    REQUIRE (s.currentIndex() == 2);
    REQUIRE (s.pendingCue() == -1);                            // jumpTo consume el cue
    REQUIRE (! s.shouldFireCue ({ 0.0, 9.0, 0u }));

    // cancelCue lo descarta; un índice inválido no arma nada
    s.armCue (1);
    REQUIRE (s.pendingCue() == 1);
    s.cancelCue();
    REQUIRE (s.pendingCue() == -1);
    s.armCue (7);
    REQUIRE (s.pendingCue() == -1);
}

TEST_CASE ("sequence: el cue armado sobrevive un rebobinado del transport (se re-arma, no dispara)",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    s.setClock (PhotoSequence_Clock_Beats);
    s.setIntervalBeats (4.0);
    s.armCue (1);
    REQUIRE (! s.shouldFireCue ({ 0.0, 6.0, 0u }));            // armado dentro de la ventana [4,8)
    REQUIRE (! s.shouldFireCue ({ 0.0, 1.0, 0u }));            // el transport rebobinó: re-arma, no dispara
    REQUIRE (s.pendingCue() == 1);
    REQUIRE (! s.shouldFireCue ({ 0.0, 3.0, 0u }));
    REQUIRE (s.shouldFireCue ({ 0.0, 4.5, 0u }));              // recién al cruzar la ventana
}

// ===================== RONDA 3 · randomOtherIndex — la foto ALEATORIA del cue por MIDI =====================
// La nota 90 pide "otra cualquiera": nunca la que ya está en pantalla (si no, el pad no haría nada visible).
// Usa el MISMO RNG propio que el SHUFFLE → determinista con la misma semilla, testeable sin <random>.
TEST_CASE ("sequence: randomOtherIndex nunca devuelve el actual y es determinista con la semilla",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png", "/tmp/e.png" });
    s.setSeed (12345u);

    std::vector<int> first;
    for (int i = 0; i < 40; ++i)
    {
        const int r = s.randomOtherIndex();
        REQUIRE (r >= 0);
        REQUIRE (r < s.size());
        REQUIRE (r != s.currentIndex());     // JAMÁS la actual
        first.push_back (r);
        s.jumpTo (r);                        // el VJ salta ahí y vuelve a pedir otra
    }

    // Misma semilla + mismo recorrido = misma secuencia de índices (determinismo).
    PhotoSequence t;
    t.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png", "/tmp/e.png" });
    t.setSeed (12345u);
    for (int i = 0; i < 40; ++i) { const int r = t.randomOtherIndex(); REQUIRE (r == first[(size_t) i]); t.jumpTo (r); }
}

TEST_CASE ("sequence: randomOtherIndex con 1 solo item (o ninguno) devuelve el actual", "[supernova][sequence][media]")
{
    PhotoSequence s;
    REQUIRE (s.randomOtherIndex() == 0);            // sin fotos: no hay a dónde ir
    s.setFiles ({ "/tmp/a.png" });
    REQUIRE (s.randomOtherIndex() == 0);            // con una sola, la única
}

// ============== RONDA 3 · C — INSERTAR en una posición (soltar archivos ENTRE dos tiles) ==============
// Hasta acá todo drop iba al FINAL. Soltar entre dos tiles inserta ahí: el orden es parte del pase, y
// reordenar cinco fotos de a una es trabajo que la tira ya no debería pedir.
TEST_CASE ("sequence: insertFiles mete en la posición pedida y la foto EN PANTALLA no cambia",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.jumpTo (2);                                  // estamos mirando la c
    REQUIRE (s.currentPath() == "/tmp/c.png");

    s.insertFiles (1, { "/tmp/x.png" });
    REQUIRE (s.size() == 4);
    REQUIRE (s.pathAt (0) == "/tmp/a.png");
    REQUIRE (s.pathAt (1) == "/tmp/x.png");        // entró entre a y b
    REQUIRE (s.pathAt (2) == "/tmp/b.png");
    REQUIRE (s.pathAt (3) == "/tmp/c.png");
    REQUIRE (s.currentPath() == "/tmp/c.png");     // la misma foto, otro número
    REQUIRE (s.currentIndex() == 3);

    // varios de una vez, en orden, al final y al principio
    s.insertFiles (4, { "/tmp/y.png", "/tmp/z.png" });
    REQUIRE (s.pathAt (4) == "/tmp/y.png");
    REQUIRE (s.pathAt (5) == "/tmp/z.png");
    REQUIRE (s.currentIndex() == 3);               // insertar DESPUÉS no corre a la actual
    s.insertFiles (0, { "/tmp/w.png" });
    REQUIRE (s.pathAt (0) == "/tmp/w.png");
    REQUIRE (s.currentIndex() == 4);
    REQUIRE (s.currentPath() == "/tmp/c.png");

    // rotación y marca de faltante arrancan limpias en los nuevos, y no se desalinean las de los viejos
    REQUIRE (s.rotationAt (0) == 0);
    REQUIRE (! s.isMissing (0));
}

TEST_CASE ("sequence: insertFiles clampea la posición, ignora una lista vacía y suelta el cue armado",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    s.armCue (1);
    REQUIRE (s.pendingCue() == 1);

    s.insertFiles (99, { "/tmp/c.png" });          // más allá del final → al final
    REQUIRE (s.size() == 3);
    REQUIRE (s.pathAt (2) == "/tmp/c.png");
    REQUIRE (s.pendingCue() == -1);                // los índices se corrieron: el cue ya no apunta a nada

    s.insertFiles (-5, {});                        // lista vacía → no-op
    REQUIRE (s.size() == 3);
}

// ================== RONDA 3b · F1 — NINGÚN camino de cue cae en un faltante ==================
// La ronda 3 hizo que el RELOJ saltee los faltantes, pero el sorteo de la aleatoria (la nota 90 y el
// SHUFFLE, que lo reusa) todavía podía elegir un archivo que no está: el corte no mostraba nada y la tira
// marcaba como actual un tile con el glifo `!`. El sorteo pasa a ser entre los VIVOS que no son el actual.
TEST_CASE ("sequence: randomOtherIndex nunca cae en un faltante", "[supernova][sequence][media][missing]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    s.setSeed (7u);
    s.setMissing (1, true);
    s.setMissing (3, true);
    // desde la 0, el único vivo que no es el actual es el 2 → la aleatoria cae SIEMPRE ahí
    for (int i = 0; i < 60; ++i) REQUIRE (s.randomOtherIndex() == 2);

    s.setMissing (2, true);                       // ya no queda ningún otro vivo
    REQUIRE (s.randomOtherIndex() == s.currentIndex());

    // Que la ACTUAL falte no cambia la regla: sortea entre los vivos que no son ella.
    PhotoSequence t;
    t.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    t.setSeed (7u);
    t.setMissing (0, true);
    for (int i = 0; i < 40; ++i) { const int r = t.randomOtherIndex(); REQUIRE ((r == 1 || r == 2)); }
}

// ← / → (y las notas 88/89) tienen que caminar al vecino VIVO más cercano en esa dirección: hasta acá el
// paso era el índice crudo ±1 y podía aterrizar en un faltante. `prevIndex()` es el espejo de `nextIndex()`.
TEST_CASE ("sequence: prevIndex/nextIndex caminan al vecino vivo; con uno solo vivo se quedan",
           "[supernova][sequence][media][missing]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    REQUIRE (s.nextIndex() == 1);
    REQUIRE (s.prevIndex() == 3);                 // hacia atrás wrapea al último

    s.setMissing (1, true);
    REQUIRE (s.nextIndex() == 2);                 // saltea el faltante
    s.setMissing (3, true);
    REQUIRE (s.prevIndex() == 2);                 // hacia atrás, lo mismo

    s.jumpTo (2);
    REQUIRE (s.nextIndex() == 0);                 // wrap hacia adelante salteando el 3
    REQUIRE (s.prevIndex() == 0);                 // y hacia atrás salteando el 1

    s.setMissing (0, true);                       // el 2 es el ÚNICO vivo: no hay a dónde ir
    REQUIRE (s.nextIndex() == 2);
    REQUIRE (s.prevIndex() == 2);
}

// ================== RONDA 3b · F2 — el cue armado se ancla AL ARMAR ==================
// `armCue` dejaba la ventana sin fijar y `shouldFireCue` la tomaba del PRIMER tick (hasta 33 ms después).
// Si el compás cruzaba en esa ventana, el corte caía un compás entero tarde: justo el gesto del VJ que
// anticipa el downbeat. Con el beatPos del momento del armado, ese tick ya dispara.
TEST_CASE ("sequence: armCue con beatPos ancla ya; cruzar el compás en el primer tick dispara",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png" });
    s.setClock (PhotoSequence_Clock_Beats);
    s.setIntervalBeats (4.0);                                  // ventanas: [0,4) [4,8) [8,12)…

    s.armCue (2, 3.99);                                        // armado anticipando el downbeat
    REQUIRE (s.pendingCue() == 2);
    REQUIRE (s.shouldFireCue ({ 0.0, 4.01, 0u }));             // el PRIMER tick ya cruzó: corta ahí

    // La firma vieja (sin beatPos) conserva el comportamiento: el primer tick sólo fija la ventana.
    PhotoSequence t;
    t.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    t.setClock (PhotoSequence_Clock_Beats);
    t.setIntervalBeats (4.0);
    t.armCue (1);
    REQUIRE (! t.shouldFireCue ({ 0.0, 4.01, 0u }));
    REQUIRE (! t.shouldFireCue ({ 0.0, 6.0, 0u }));            // misma ventana [4,8)
    REQUIRE (t.shouldFireCue ({ 0.0, 8.0, 0u }));

    // Y el re-anclaje al rebobinar sigue valiendo con la ventana ya fijada.
    PhotoSequence u;
    u.setFiles ({ "/tmp/a.png", "/tmp/b.png" });
    u.setClock (PhotoSequence_Clock_Beats);
    u.setIntervalBeats (4.0);
    u.armCue (1, 3.99);
    REQUIRE (! u.shouldFireCue ({ 0.0, 1.0, 0u }));            // el transport rebobinó: re-arma, no dispara
    REQUIRE (u.pendingCue() == 1);
    REQUIRE (u.shouldFireCue ({ 0.0, 4.5, 0u }));
}

// ================== RONDA 3b · F3 — insertFiles sobre una secuencia VACÍA ==================
// `if (pos <= index) index += paths.size()` corría el índice también cuando no había nada que correr: la
// lista quedaba con el índice UNO PASADO EL FINAL (currentPath() lo clampeaba y devolvía la última en vez
// de la primera). Hoy es inalcanzable desde la UI (sin secuencia el drop cae en ingestMedia), pero es una
// bomba armada para el próximo camino que llame a insertFiles.
TEST_CASE ("sequence: insertFiles en una secuencia vacía deja el índice en 0 y arranca en la primera",
           "[supernova][sequence][media]")
{
    PhotoSequence s;
    REQUIRE (s.size() == 0);
    s.insertFiles (0, { "/tmp/a.png", "/tmp/b.png" });
    REQUIRE (s.size() == 2);
    REQUIRE (s.currentIndex() == 0);
    REQUIRE (s.currentPath() == "/tmp/a.png");
    REQUIRE (s.nextIndex() == 1);

    PhotoSequence t;
    t.insertFiles (5, { "/tmp/x.png" });          // posición más allá del final de una lista vacía
    REQUIRE (t.size() == 1);
    REQUIRE (t.currentIndex() == 0);
    REQUIRE (t.currentPath() == "/tmp/x.png");
}

// ================== RONDA 3c · F8 — el paso a mano es LINEAL, también bajo SHUFFLE ==================
// `stepMedia` caminaba con `nextIndex()`, que bajo SHUFFLE devuelve el SORTEADO: → y ← dejaban de ser
// espejo (→ saltaba al sorteo, ← al vecino de la tira) y → seguido de ← no volvía a donde estabas. En vivo
// eso es inaceptable. El sorteo queda para el reloj automático (`nextIndex()`, y con él el prefetch y el
// export, no cambian); el pulgar del VJ camina por la tira con `nextAliveIndex()` / `prevIndex()`.
TEST_CASE ("sequence: nextAliveIndex es lineal aunque el orden sea SHUFFLE",
           "[supernova][sequence][media][missing]")
{
    PhotoSequence s;
    s.setFiles ({ "/tmp/a.png", "/tmp/b.png", "/tmp/c.png", "/tmp/d.png" });
    s.setOrder (PhotoSequence_Order_Shuffle);
    // Una semilla cuyo sorteo NO sea el vecino lineal: es el caso que distingue los dos caminos.
    for (uint32_t seed = 1; seed < 512 && s.nextIndex() == 1; ++seed) s.setSeed (seed);
    REQUIRE (s.nextIndex() != 1);                 // el reloj automático va al sorteado…
    REQUIRE (s.nextAliveIndex() == 1);            // …y el paso a mano, al vecino de la tira
    REQUIRE (s.prevIndex() == 3);                 // espejo exacto: hacia atrás wrapea al último

    s.setMissing (1, true);                       // saltea los faltantes igual que prevIndex()
    REQUIRE (s.nextAliveIndex() == 2);
    s.jumpTo (2);
    s.setMissing (3, true);
    REQUIRE (s.nextAliveIndex() == 0);            // wrap hacia adelante salteando el 3
    s.setMissing (0, true);                       // el 2 es el ÚNICO vivo: no hay a dónde ir
    REQUIRE (s.nextAliveIndex() == 2);
    REQUIRE (s.prevIndex() == 2);

    PhotoSequence one;                            // sin secuencia (<2) tampoco hay paso
    one.setFiles ({ "/tmp/a.png" });
    REQUIRE (one.nextAliveIndex() == 0);
}
