// [supernova][app] — el ciclo de vida del tap con el cartel de TCC ABIERTO.
// Crear el tap bloquea hasta que el usuario contesta: hasta 0.3.0, stop() hacía dispatch_sync sobre la
// misma cola serial, así que cambiar de SOURCE o cerrar la app con el cartel arriba congelaba el hilo de
// mensajes (rueda de playa indefinida). El doble de acá es un setup que se queda esperando, igual que el
// cartel real.
#include <catch2/catch_test_macros.hpp>
#include "app/audio/TapLifecycle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using supernova::TapLifecycle;

namespace {

// El "cartel de TCC": bloquea al setup hasta que alguien conteste.
struct Prompt
{
    void wait()   { std::unique_lock<std::mutex> lk (m); cv.wait (lk, [this] { return answered; }); }
    void answer() { { std::lock_guard<std::mutex> lk (m); answered = true; } cv.notify_all(); }

    // Segunda barrera: el setup real tiene DOS etapas que se pueden interrumpir (crear el tap, y armar
    // aggregate+IOProc antes del AudioDeviceStart). Sirve para parar el setup en la de más adentro.
    void waitSecond()   { std::unique_lock<std::mutex> lk (m); cv.wait (lk, [this] { return second; }); }
    void answerSecond() { { std::lock_guard<std::mutex> lk (m); second = true; } cv.notify_all(); }

    std::mutex m;
    std::condition_variable cv;
    bool answered = false;
    bool second   = false;
};

template <typename Pred>
bool waitUntil (Pred p, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (p()) return true;
        std::this_thread::sleep_for (1ms);
    }
    return p();
}

} // namespace

TEST_CASE ("tap lifecycle: stop() vuelve enseguida aunque el cartel del sistema siga abierto",
           "[supernova][app]")
{
    TapLifecycle life { "com.ovni.supernova.test.tap.setup" };

    Prompt prompt;
    std::atomic<bool> setupEntered { false }, chainBuilt { false }, capturing { false }, tornDown { false };

    REQUIRE (life.start ([&] (int gen)
    {
        setupEntered = true;
        prompt.wait();                       // ← AudioHardwareCreateProcessTap con el cartel arriba
        chainBuilt = true;                   // el tap ya existe
        if (! life.isCurrent (gen)) return;  // caducó: no arranques nada (lo limpia el teardown encolado)
        capturing = true;
    }));

    REQUIRE (waitUntil ([&] { return setupEntered.load(); }));

    // El usuario cambia de SOURCE (o cierra la app) con el cartel todavía abierto.
    const auto t0 = std::chrono::steady_clock::now();
    life.stop ([&] { tornDown = true; });
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds> (
                               std::chrono::steady_clock::now() - t0).count();

    REQUIRE (elapsedMs < 50);          // ← acá 0.3.0 se colgaba hasta que el usuario contestara
    REQUIRE_FALSE (tornDown.load());   // la limpieza está ENCOLADA detrás del setup, no corrió todavía

    prompt.answer();                   // el usuario contesta, al fin

    REQUIRE (waitUntil ([&] { return tornDown.load(); }));
    REQUIRE (chainBuilt.load());       // el setup llegó a crear el tap…
    REQUIRE_FALSE (capturing.load());  // …pero no arrancó: su generación ya había caducado
}

TEST_CASE ("tap lifecycle: la limpieza corre DESPUÉS del setup, nunca en el medio", "[supernova][app]")
{
    TapLifecycle life { "com.ovni.supernova.test.tap.order" };

    Prompt prompt;
    std::atomic<int> step { 0 };
    std::atomic<int> setupEndedAt { -1 }, teardownRanAt { -1 };

    life.start ([&] (int) { prompt.wait(); setupEndedAt = ++step; });
    REQUIRE (waitUntil ([&] { return life.setupInFlight(); }));

    life.stop ([&] { teardownRanAt = ++step; });
    prompt.answer();

    REQUIRE (waitUntil ([&] { return teardownRanAt.load() > 0; }));
    REQUIRE (setupEndedAt.load()  == 1);    // la cola es SERIAL: primero termina el setup…
    REQUIRE (teardownRanAt.load() == 2);    // …y recién ahí se destruye lo que dejó
}

TEST_CASE ("tap lifecycle: un stop() entre 'IOProc creado' y 'start' NO arranca el device",
           "[supernova][app]")
{
    // El setup real tiene DOS etapas largas: crear el tap (bloquea con el cartel de TCC) y, después,
    // armar aggregate + IOProc. Hasta 0.3.1 el guard de generación estaba en la primera y en la que sigue
    // AL AudioDeviceStart — no justo ANTES. Un stop() que caía en esa ventana arrancaba el IO un instante
    // y la línea siguiente lo destruía: ninguna muestra llegaba al engine (el bloque del IOProc chequea
    // la generación), pero el HAL veía un start/stop que nadie pidió.
    TapLifecycle life { "com.ovni.supernova.test.tap.prestart" };

    Prompt tccPrompt, ioProcReady;
    std::atomic<bool> ioProcCreated { false }, deviceStarted { false }, tornDown { false };

    REQUIRE (life.start ([&] (int gen)
    {
        tccPrompt.wait();                        // 1) AudioHardwareCreateProcessTap con el cartel arriba
        if (! life.isCurrent (gen)) return;

        ioProcCreated = true;                    // 2) aggregate + AudioDeviceCreateIOProcIDWithBlock
        ioProcReady.answer();
        tccPrompt.waitSecond();                  // ← la ventana: acá cae el stop()

        if (! life.isCurrent (gen)) return;      // 3) el guard que faltaba, justo antes del start
        deviceStarted = true;                    // AudioDeviceStart
    }));

    tccPrompt.answer();                          // el usuario concede el permiso
    REQUIRE (waitUntil ([&] { return ioProcCreated.load(); }));

    life.stop ([&] { tornDown = true; });        // cambio de SOURCE / cierre con el IOProc ya creado
    tccPrompt.answerSecond();                    // el setup sigue

    REQUIRE (waitUntil ([&] { return tornDown.load(); }));
    REQUIRE (ioProcCreated.load());              // llegó a crear el IOProc…
    REQUIRE_FALSE (deviceStarted.load());        // …y NO arrancó el device: la generación ya había caducado
}

TEST_CASE ("tap lifecycle: no se apilan intentos mientras hay uno en vuelo", "[supernova][app]")
{
    TapLifecycle life { "com.ovni.supernova.test.tap.stack" };

    Prompt prompt;
    std::atomic<int> setups { 0 };

    REQUIRE (life.start ([&] (int) { ++setups; prompt.wait(); }));
    REQUIRE (waitUntil ([&] { return life.setupInFlight(); }));
    REQUIRE_FALSE (life.start ([&] (int) { ++setups; }));   // el poll a 30Hz no puede apilar carteles

    prompt.answer();
    REQUIRE (waitUntil ([&] { return ! life.setupInFlight(); }));
    REQUIRE (setups.load() == 1);
}

TEST_CASE ("tap lifecycle: destruirlo con un setup bloqueado no rompe nada", "[supernova][app]")
{
    // Cerrar la app con el cartel abierto: el dueño se va, el setup sigue colgado. El estado vive en un
    // shared_ptr que capturan las tareas, así que nadie escribe sobre memoria liberada.
    Prompt prompt;
    std::atomic<bool> setupDone { false };

    {
        TapLifecycle life { "com.ovni.supernova.test.tap.death" };
        life.start ([&] (int) { prompt.wait(); setupDone = true; });
        REQUIRE (waitUntil ([&] { return life.setupInFlight(); }));
        life.stop ([] {});
    }   // ← el TapLifecycle muere acá, con el setup todavía bloqueado

    prompt.answer();
    REQUIRE (waitUntil ([&] { return setupDone.load(); }));
}
