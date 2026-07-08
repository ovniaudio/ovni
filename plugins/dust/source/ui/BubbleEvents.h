#pragma once

#include <juce_core/juce_core.h>
#include <array>

// =============================================================================
// BubbleEvents — telemetría de eventos burbuja de DUST (MOV·03).
//
// El hilo de AUDIO publica un evento por cada burbuja que NACE (azimut absoluto,
// energía del slot, VIDA vigente); el visualizador (message thread, etapa UI) los
// drena a su frame-rate y hace nacer la burbuja visual EXACTAMENTE donde suena.
// SPSC lock-free vía juce::AbstractFifo: el audio sólo escribe, la UI sólo lee;
// si el FIFO se llena se DESCARTA el evento (telemetría, nunca bloquear audio).
// =============================================================================
namespace dust::ui
{

struct BubbleEvent
{
    float azimuthRad = 0.0f;   // azimut absoluto al nacer (0 = frente, + = CCW/izquierda)
    float energy     = 0.0f;   // ganancia objetivo del slot (0..~0.85): cuán fuerte es el eco
    float vida       = 0.0f;   // VIDA vigente 0..1 (cuánto deriva/flota la burbuja visual)
};

class BubbleEventFifo
{
public:
    static constexpr int kCapacity = 128;   // > kMaxTaps·varios bloques entre frames de UI a 30 fps

    // Audio thread. Si está lleno, descarta (la UI se perdió un nacimiento visual, el audio no espera).
    void push (const BubbleEvent& e) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            slots[(size_t) scope.startIndex1] = e;
    }

    // Message thread (visualizador). Devuelve cuántos eventos copió a dst (hasta maxN).
    int pop (BubbleEvent* dst, int maxN) noexcept
    {
        const auto scope = fifo.read (juce::jmin (maxN, fifo.getNumReady()));
        int n = 0;
        for (int i = 0; i < scope.blockSize1; ++i) dst[n++] = slots[(size_t) (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i) dst[n++] = slots[(size_t) (scope.startIndex2 + i)];
        return n;
    }

private:
    juce::AbstractFifo fifo { kCapacity };
    std::array<BubbleEvent, (size_t) kCapacity> slots {};
};

} // namespace dust::ui
