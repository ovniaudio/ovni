#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <vector>

// ========================================================================================================
// QuietTail — ¿correr el scroll cambia algún píxel? (prompt 96, la ventana que trababa el DAW)
//
// Los tres sonogramas (SPECTROGRAM, STEREO SPECTROGRAM, WATERFALL) repintan cada vez que el motor escribe
// columnas nuevas: su eje es el tiempo. Pero con el transporte parado el host sigue mandando ceros, el
// motor sigue escribiendo columnas —todas iguales, en el piso— y la lente repintaba 30 veces por segundo
// para siempre mirando silencio.
//
// Si TODAS las columnas que caben en la ventana visible son iguales entre sí (byte a byte: las celdas del
// anillo ya vienen cuantizadas), correr el dibujo una columna no cambia un solo píxel. Esto sigue la
// última columna que difirió de la anterior; mientras la ventana entera quede después de ella, no hay
// nada nuevo que mostrar. Sin umbrales: igualdad exacta de bytes.
//
// Al volver el sonido la primera columna distinta despierta la lente, y el scroll dibuja lo pendiente
// (SpectrogramScroll::refresh ya rehace la imagen entera si lo pendiente no entra en el anillo).
// ========================================================================================================
namespace telescope
{
class QuietTail
{
public:
    // `window` = cuántas columnas del final hay que mirar (la ventana visible, con margen).
    template <typename Ring>
    bool uniform (const Ring& ring, long long window)
    {
        const long long w = ring.writeIndex();
        const long long oldest = juce::jmax (0LL, w - (long long) ring.capacity() + 1);
        if (w < checked || checked < oldest)
        {
            // Se reinició el anillo o nos quedamos atrás más que su capacidad: sin continuidad, lo que no
            // se vio cuenta como cambio.
            havePrev   = false;
            checked    = oldest;
            lastChange = oldest;
        }

        cur.resize ((size_t) Ring::kCellBytes);
        prev.resize ((size_t) Ring::kCellBytes);
        for (long long i = checked; i < w; ++i)
        {
            if (! ring.copyColumn (i, cur.data())) { havePrev = false; lastChange = i; continue; }
            if (! havePrev || ! std::equal (cur.begin(), cur.end(), prev.begin())) lastChange = i;
            prev.swap (cur);
            havePrev = true;
        }
        checked = w;
        return havePrev && w - lastChange > window;
    }

private:
    std::vector<juce::uint8> cur, prev;
    long long checked = 0, lastChange = 0;
    bool havePrev = false;
};
}
