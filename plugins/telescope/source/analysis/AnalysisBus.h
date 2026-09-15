#pragma once
#include <atomic>
#include "analysis/LockFreeAudioFifo.h"

// ========================================================================================================
// AnalysisBus — el puente entre el audio thread y el AnalysisThread. Dos FIFOs lock-free (L y R): TELESCOPE
// mide el ESPACIO, así que a diferencia de SUPERNOVA no puede mezclar a mono.
//
// El audio NUNCA bloquea: si el consumidor se atrasa, se descarta lo que no entra y se CUENTA (la UI lo
// muestra como "⚠ análisis atrasado" — mentir por omisión sería peor que un número feo).
// Capacidad: 2 s a 192 kHz por canal (384 000 muestras), el peor caso del catálogo.
// ========================================================================================================
namespace telescope
{
class AnalysisBus
{
public:
    // Exactamente 2 s al peor sample rate del catálogo (192 kHz), que es lo que fija el diseño (§4).
    // Más grande sólo esconde un atraso del worker por más tiempo: mejor descartar y AVISAR.
    static constexpr int kCapacity = 2 * 192000;   // 384 000 muestras/canal = 2 s @192k · 8 s @48k

    AnalysisBus() : fifoL (kCapacity), fifoR (kCapacity) {}

    int capacity() const noexcept { return kCapacity; }

    // Audio thread: empuja n muestras de cada canal. noexcept, sin locks ni allocations.
    void push (const float* L, const float* R, int n) noexcept
    {
        if (n <= 0) return;
        // juce::AbstractFifo reserva una posición (getFreeSpace = size - ready - 1): el mínimo de ambos
        // canales manda, así L y R NUNCA se desalinean.
        const int freeL   = kCapacity - 1 - fifoL.numReady();
        const int freeR   = kCapacity - 1 - fifoR.numReady();
        const int canPush = juce::jmin (n, freeL, freeR);

        if (canPush < n)
            dropped.fetch_add ((juce::uint32) (n - canPush), std::memory_order_relaxed);

        if (canPush > 0)
        {
            fifoL.push (L, canPush);
            fifoR.push (R, canPush);
        }
    }

    // Worker thread: saca hasta maxN muestras de cada canal (la misma cantidad de ambos). Devuelve cuántas.
    int pop (float* dstL, float* dstR, int maxN) noexcept
    {
        const int n = juce::jmin (maxN, fifoL.numReady(), fifoR.numReady());
        if (n <= 0) return 0;
        fifoL.pop (dstL, n);
        fifoR.pop (dstR, n);
        return n;
    }

    int numReady() const noexcept { return juce::jmin (fifoL.numReady(), fifoR.numReady()); }

    juce::uint32 droppedSamples() const noexcept { return dropped.load (std::memory_order_relaxed); }

    void reset() noexcept
    {
        fifoL.reset();
        fifoR.reset();
        dropped.store (0, std::memory_order_relaxed);
    }

private:
    LockFreeAudioFifo fifoL, fifoR;
    std::atomic<juce::uint32> dropped { 0 };
};
}
