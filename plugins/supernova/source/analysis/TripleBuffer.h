#pragma once
#include <atomic>
#include <array>

// TripleBuffer atómico (SPSC, latest-wins) — el AnalysisThread PUBLICA un AnalysisFrame completo y el render
// loop CONSUME el último publicado, sin locks ni tearing (a diferencia de un array-of-atomics que se rompe por
// campo). Esquema canónico de 3 slots: el consumidor RETIENE su slot y hace swap en read(), así productor y
// consumidor NUNCA tocan el mismo slot (el productor escribe writeIdx, el consumidor lee readIdx, el 3º es el
// spare/publicado). El productor no bloquea nunca; el consumidor siempre ve un frame coherente.
namespace supernova
{
template <typename T>
class TripleBuffer
{
public:
    TripleBuffer() { for (auto& s : slots) s = T{}; }

    // ---- Productor (AnalysisThread) ----
    T& writeSlot() noexcept { return slots[(size_t) writeIdx]; }

    void publish() noexcept
    {
        const int prev = ready.exchange (writeIdx, std::memory_order_acq_rel);   // publica writeIdx
        writeIdx = (prev >= 0) ? prev                     // reusá el ready no consumido
                               : (3 - writeIdx - readIdx); // o el spare (ni write ni read)
    }

    // ---- Consumidor (render loop) ----
    // Devuelve una referencia estable al último frame publicado (válida hasta el próximo read()).
    const T& read() noexcept
    {
        const int r = ready.exchange (-1, std::memory_order_acq_rel);
        if (r >= 0) readIdx = r;    // adoptá el fresco; el readIdx viejo pasa a spare
        return slots[(size_t) readIdx];
    }

private:
    std::array<T, 3> slots {};
    int writeIdx = 0;                 // solo el productor
    int readIdx  = 1;                 // solo el consumidor (spare inicial = 2)
    std::atomic<int> ready { -1 };    // índice publicado no consumido, o -1
};
}
