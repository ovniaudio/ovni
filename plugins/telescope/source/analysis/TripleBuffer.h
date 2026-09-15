// Copiado de plugins/supernova/source/analysis/TripleBuffer.h el 2026-09-07 (D-45: el motor vive dentro
// del plugin en v1; promoción a shared/ post-merge).
//
// ⚠ DIVERGE DEL ORIGINAL — a propósito, por un BUG REAL que encontró AnalysisChainTest (2026-09-07):
//
// El original hace `writeIdx = (prev >= 0) ? prev : (3 - writeIdx - readIdx)`, o sea que el PRODUCTOR lee
// `readIdx`, que es una variable NO atómica del CONSUMIDOR. Si el productor la ve desactualizada, calcula
// como "libre" justo el slot que el consumidor está leyendo y le escribe encima a mitad de lectura. El
// consumidor entonces ve un frame MEZCLADO (campos nuevos y viejos del mismo struct).
//
// No es teórico: con el consumidor sondeando cada 5 ms y el productor publicando cada ~1 ms, el test
// "un seno de -23 dBFS llega al frame como -23 LUFS" fallaba ~1 de cada 3 corridas con
// momentary = -23.0 y shortTerm = -23.0 (campos ya copiados) pero integrated = -300 (campo todavía sin
// copiar, del frame viejo del slot).
//
// La versión de acá usa el esquema canónico de INTERCAMBIO: cada lado hace un solo exchange atómico que
// pone su slot en `ready` y se lleva el que estaba. {writeIdx, readIdx, ready} es siempre una permutación
// de {0,1,2} → productor y consumidor NUNCA tienen el mismo slot, sin leer variables del otro.
//
// PARA LA AUDITORA: SUPERNOVA usa la versión original y está publicado. Ahí el consumidor es el render
// loop leyendo floats, así que el síntoma es un cuadro visual con datos mezclados (no audio, no crash) —
// severidad baja, pero es el mismo bug. Queda anotado como hallazgo del prompt 48.
#pragma once
#include <atomic>
#include <array>

// TripleBuffer atómico (SPSC, latest-wins) — el AnalysisThread PUBLICA un AnalysisFrame completo y el
// render loop CONSUME el último publicado, sin locks ni tearing (a diferencia de un array-of-atomics que
// se rompe por campo). El productor no bloquea nunca; el consumidor siempre ve un frame coherente.
namespace telescope
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
        // Deja su slot publicado (con el bit de "fresco") y se lleva el que estaba en `ready`.
        const int prev = ready.exchange (writeIdx | kFresh, std::memory_order_acq_rel);
        writeIdx = prev & kIndexMask;
    }

    // ---- Consumidor (render loop) ----
    // Devuelve una referencia estable al último frame publicado (válida hasta el próximo read()).
    const T& read() noexcept
    {
        if ((ready.load (std::memory_order_acquire) & kFresh) != 0)
        {
            const int prev = ready.exchange (readIdx, std::memory_order_acq_rel);   // sin bit fresco
            readIdx = prev & kIndexMask;
        }
        return slots[(size_t) readIdx];
    }

private:
    static constexpr int kIndexMask = 0x3;   // índices 0..2
    static constexpr int kFresh     = 0x4;   // "hay algo sin consumir"

    std::array<T, 3> slots {};
    int              writeIdx = 0;    // sólo el productor
    int              readIdx  = 1;    // sólo el consumidor
    std::atomic<int> ready    { 2 };  // slot publicado (| kFresh si nadie lo consumió todavía)
};
}
