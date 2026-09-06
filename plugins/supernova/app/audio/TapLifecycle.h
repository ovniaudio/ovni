#pragma once
// TapLifecycle — la COREOGRAFÍA del ciclo de vida del tap: quién corre en qué cola, quién limpia, y
// cuándo. Sin CoreAudio adentro (libdispatch es C puro), así que se puede testear con un setup que
// BLOQUEE, que es exactamente el caso difícil.
//
// El caso difícil: crear el tap (AudioHardwareCreateProcessTap) bloquea hasta que el usuario contesta el
// cartel de TCC — pueden ser minutos. Hasta 0.3.0 stop() hacía dispatch_sync sobre esa misma cola, así
// que cambiar de SOURCE o cerrar la app con el cartel abierto congelaba el HILO DE MENSAJES: rueda de
// playa hasta que el usuario contestara.
//
// Ahora stop() NO espera a nadie: bumpea la generación y ENCOLA la limpieza. Como la cola es SERIAL, la
// limpieza corre necesariamente DESPUÉS del setup en vuelo — que, al ver que su generación caducó, no
// arranca nada y deja lo que haya creado para que la limpieza lo destruya en el orden correcto.
//
// Vidas: el estado va en un shared_ptr que capturan las tareas, así que destruir el TapLifecycle (o el
// dueño entero) mientras un setup está bloqueado es seguro — la última tarea en soltar la referencia lo
// libera. Y la COLA se libera con el State, ahora que se puede: hasta 0.3.1 el miembro era un
// `dispatch_queue_t` y este header se compila en TUs con ARC (los .mm de la app) y sin ARC (los .cpp de
// los tests) — bajo ARC ese miembro trae destructor implícito y sin ARC no, o sea la MISMA struct con dos
// destructores según quién la incluya: ODR. Así que acá adentro no se nombra NINGÚN tipo de Objective-C;
// el puente a libdispatch vive en TapLifecycle.cpp, una sola TU, y el header compila idéntico en todas.
#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace supernova {

// El puente. Implementado en TapLifecycle.cpp (sin ARC: ahí `dispatch_release` existe y es lo correcto).
namespace tapqueue {
    void* createSerial (const char* label) noexcept;                       // cola serial nueva, ya nuestra
    void  destroy (void* queue) noexcept;                                  // idempotente con nullptr
    void  async (void* queue, void* context, void (*fn) (void*)) noexcept; // dispatch_async_f
}

class TapLifecycle
{
public:
    using Setup    = std::function<void (int gen)>;   // arma la cadena; PUEDE BLOQUEAR (cartel de TCC)
    using Teardown = std::function<void()>;           // destruye lo que exista (idempotente)

    explicit TapLifecycle (const char* queueLabel)
        : st (std::make_shared<State>())
    {
        st->queue = tapqueue::createSerial (queueLabel);
    }

    TapLifecycle (const TapLifecycle&) = delete;
    TapLifecycle& operator= (const TapLifecycle&) = delete;

    // Abre una generación nueva y corre `setup` fuera del hilo llamador.
    // false = ya había un setup en vuelo (no se apilan intentos).
    bool start (Setup setup)
    {
        bool expected = false;
        if (! st->inFlight.compare_exchange_strong (expected, true)) return false;

        auto* task = new Task { st, std::move (setup), {}, ++st->generation };
        tapqueue::async (st->queue, task, &runSetup);
        return true;
    }

    // Invalida todo lo en vuelo y ENCOLA la limpieza detrás del setup. Vuelve enseguida: NO espera.
    void stop (Teardown teardown)
    {
        ++st->generation;
        auto* task = new Task { st, {}, std::move (teardown), 0 };
        tapqueue::async (st->queue, task, &runTeardown);
    }

    // ¿La generación `gen` sigue siendo la vigente? false ⇒ hubo stop() (o un start() nuevo): no arranques.
    bool isCurrent (int gen) const noexcept { return st->generation.load() == gen; }
    int  generation()         const noexcept { return st->generation.load(); }
    bool setupInFlight()      const noexcept { return st->inFlight.load(); }

private:
    struct State
    {
        void*             queue = nullptr;   // opaca a propósito: ver la cabecera del archivo
        std::atomic<int>  generation { 0 };
        std::atomic<bool> inFlight   { false };

        State() = default;
        ~State() { tapqueue::destroy (queue); queue = nullptr; }

        State (const State&) = delete;
        State& operator= (const State&) = delete;
    };

    struct Task
    {
        std::shared_ptr<State> state;   // mantiene vivo el estado aunque el dueño ya no exista
        Setup    setup;
        Teardown teardown;
        int      gen;
    };

    static void runSetup (void* ctx)
    {
        const std::unique_ptr<Task> t { static_cast<Task*> (ctx) };
        if (t->setup) t->setup (t->gen);
        t->state->inFlight.store (false);
    }

    static void runTeardown (void* ctx)
    {
        const std::unique_ptr<Task> t { static_cast<Task*> (ctx) };
        if (t->teardown) t->teardown();
    }

    std::shared_ptr<State> st;
};

} // namespace supernova
