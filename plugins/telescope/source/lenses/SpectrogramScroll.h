#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <vector>

// ========================================================================================================
// SpectrogramScroll — el MAPEO y el DESPLAZAMIENTO de los sonogramas de TELESCOPE, en un solo lugar.
//
// Lo comparten SPECTROGRAM (lente 4, nivel) y STEREO SPECTROGRAM (lente 10, fase). Las dos dibujan lo
// mismo —tiempo en X, frecuencia log en Y, la historia entera entrando justa en el ancho— y lo único que
// cambia entre ellas es DE QUÉ COLOR pintan la celda. Todo lo demás (que es la parte con aristas) es
// idéntico, así que vive acá:
//
// EL MAPEO TIEMPO → PÍXEL, EN DOS PASOS Y SIEMPRE ENTERO. Primero se AGRUPAN `srcPerPixel` columnas del
// anillo en una (la lente decide cómo combinarlas); después esos grupos se reparten sobre el ancho con un
// acumulador de Bresenham, así cada grupo ocupa un número ENTERO de píxeles (1 o 2, alternando) y la
// historia elegida entra JUSTA en el plot — ni sobra panel negro ni se cae tiempo por el borde. Con un
// reparto entero fijo, una historia corta dejaba un tercio del panel en negro para siempre; con un mapeo
// fraccionario, el desplazamiento vibraba medio píxel por frame.
//
// EL MAPEO FILA → PÍXEL. La imagen tiene la fila 0 ARRIBA (20 kHz) y el anillo la 0 abajo (20 Hz). Donde
// varias filas del anillo caen en el mismo píxel, la lente decide con cuál se queda; acá sólo se guarda
// el rango [rowFrom, rowTo) que le toca a cada fila de píxel.
//
// EL REGISTRO DE FUENTES (`pxSrc`) es lo que le permite a la LECTURA volver al dato: para cada columna de
// píxel, el índice absoluto en el anillo del grupo que se dibujó ahí. Se corre junto con la imagen. Son
// 8 bytes por píxel de ancho (~8 KB), muchísimo más barato que guardar las 512 filas de cada columna.
// ========================================================================================================
namespace telescope
{
class SpectrogramScroll
{
public:
    // Fuerza el redibujo COMPLETO en el próximo refresh (resize, cambio de settings, test de presupuesto).
    void invalidate() noexcept { dirty = true; }

    // ¿Hay algo nuevo que dibujar? Lo usa `advanceFrame` de la lente.
    bool needsRepaint (long long ringWriteIndex) const noexcept
    {
        return dirty || (ringWriteIndex - nextSrc) >= (long long) srcPerPixel;
    }

    int  columnsPerGroup() const noexcept { return srcPerPixel; }
    int  mappedRows()      const noexcept { return (int) rowStart.size(); }
    int  rowFrom (int y)   const noexcept { return rowStart[(size_t) y]; }
    int  rowTo   (int y)   const noexcept { return rowEnd[(size_t) y]; }

    // El índice ABSOLUTO en el anillo del grupo dibujado en esa columna de píxel. -1 = todavía sin pintar.
    long long sourceAt (int px) const noexcept
    {
        return (px >= 0 && px < (int) pxSrc.size()) ? pxSrc[(size_t) px] : -1;
    }

    // Los segundos que entran de verdad en el ancho: los grupos que se reparten sobre él, por las columnas
    // que agrupa cada uno, divididos por la tasa de columnas. Da (casi exactamente) la historia elegida.
    double visibleSeconds (double columnsPerSecond) const noexcept
    {
        if (columnsPerSecond <= 0.0) return 0.0;
        return (double) groupsTotal * (double) srcPerPixel / columnsPerSecond;
    }

    // (Re)arma el mapeo para el plot y el anillo dados. `rows` son las filas del anillo.
    template <typename Ring>
    void configure (const Ring& ring, int w, int h, int rows)
    {
        w = juce::jmax (1, w);
        h = juce::jmax (1, h);
        const int capacity = juce::jmax (1, ring.capacity());

        // Al menos una columna por grupo, y las que hagan falta para que los grupos no superen el ancho
        // (redondeando hacia arriba: si no, el reparto de abajo no tendría píxeles para todos).
        srcPerPixel = juce::jmax (1, (capacity + w - 1) / w);
        groupsTotal = juce::jmax (1, capacity / srcPerPixel);
        pxAcc = 0;

        pxSrc.assign ((size_t) w, (long long) -1);
        rowStart.assign ((size_t) h, 0);
        rowEnd.assign ((size_t) h, 1);
        for (int y = 0; y < h; ++y)
        {
            const double t0 = 1.0 - (double) (y + 1) / (double) h;
            const double t1 = 1.0 - (double) y / (double) h;
            const int a = juce::jlimit (0, rows - 1, (int) std::floor (t0 * (double) (rows - 1)));
            const int b = juce::jlimit (a + 1, rows, (int) std::ceil (t1 * (double) (rows - 1)) + 1);
            rowStart[(size_t) y] = a;
            rowEnd[(size_t) y]   = b;
        }

        lastCapacity   = ring.capacity();
        lastColsPerSec = ring.columnsPerSecond();
        dirty = true;
    }

    // ¿Cambió el anillo por debajo (historia, tamaño de FFT, solape)? Entonces el mapeo ya no vale.
    template <typename Ring>
    bool ringMoved (const Ring& ring) const noexcept
    {
        return ring.capacity() != lastCapacity || ring.columnsPerSecond() != lastColsPerSec;
    }

    // EL CICLO COMPLETO. Valida el mapeo y la imagen, y pinta lo que falte llamando a
    // `drawGroup (firstPx, pixels, firstSrc)` UNA VEZ POR GRUPO — no por columna de píxel: un grupo puede
    // ocupar dos píxeles, y armar dos veces la misma columna sería hacer el doble de trabajo para dibujar
    // exactamente lo mismo. La lente pone el color.
    template <typename Ring, typename DrawGroup>
    void refresh (const Ring& ring, juce::Image& image, int w, int h, int rows,
                  juce::Colour clearColour, DrawGroup&& drawGroup)
    {
        if (w <= 0 || h <= 0) return;

        if (ringMoved (ring) || (int) pxSrc.size() != w || (int) rowStart.size() != h)
            configure (ring, w, h, rows);

        if (! image.isValid() || image.getWidth() != w || image.getHeight() != h)
        {
            image = juce::Image (juce::Image::ARGB, w, h, false);
            dirty = true;
        }

        const long long writeIndex = ring.writeIndex();
        if (dirty || nextSrc > writeIndex || writeIndex - nextSrc > (long long) ring.capacity())
        {
            fullRebuild (ring, image, w, clearColour, drawGroup);
            return;
        }

        const int groups = (int) ((writeIndex - nextSrc) / srcPerPixel);
        if (groups <= 0) return;

        std::vector<int> steps;
        int acc = pxAcc;
        const int newPixels = distribute (groups, w, acc, steps);
        if (newPixels <= 0) return;
        if (newPixels >= w) { fullRebuild (ring, image, w, clearColour, drawGroup); return; }

        // Se corre lo ya pintado a la izquierda y entran las columnas nuevas por la derecha. El registro
        // de fuentes se corre EXACTAMENTE igual: es el índice con el que la lectura vuelve al dato.
        image.moveImageSection (0, 0, newPixels, 0, w - newPixels, h);
        std::move (pxSrc.begin() + newPixels, pxSrc.end(), pxSrc.begin());

        int x = w - newPixels;
        for (int g = 0; g < groups; ++g)
        {
            const long long src = nextSrc + (long long) g * (long long) srcPerPixel;
            const int px = juce::jmin (steps[(size_t) g], w - x);
            if (px > 0) drawGroup (x, px, src);
            for (int r = 0; r < px; ++r, ++x) pxSrc[(size_t) x] = src;
        }
        nextSrc += (long long) groups * (long long) srcPerPixel;
        pxAcc = acc;
    }

private:
    // Bresenham: cuántos píxeles ocupa cada uno de los `groups` grupos sobre un ancho de `w`. Devuelve el
    // total y deja el acumulador donde quedó (es lo que hace que el reparto siga siendo continuo entre
    // llamadas: sin eso, el desplazamiento acumularía un píxel de error cada tantos grupos).
    int distribute (int groups, int w, int& acc, std::vector<int>& steps) const
    {
        steps.assign ((size_t) juce::jmax (0, groups), 0);
        int total = 0;
        for (int g = 0; g < groups; ++g)
        {
            acc += w;
            steps[(size_t) g] = acc / groupsTotal;
            acc %= groupsTotal;
            total += steps[(size_t) g];
        }
        return total;
    }

    template <typename Ring, typename DrawGroup>
    void fullRebuild (const Ring& ring, juce::Image& image, int w, juce::Colour clearColour,
                      DrawGroup&& drawGroup)
    {
        image.clear (image.getBounds(), clearColour);
        std::fill (pxSrc.begin(), pxSrc.end(), (long long) -1);

        const int groups = juce::jmin (ring.count() / srcPerPixel, groupsTotal);
        std::vector<int> steps;
        int acc = 0;
        const int totalPx = juce::jmin (distribute (groups, w, acc, steps), w);

        const long long firstSrc = ring.writeIndex() - (long long) groups * (long long) srcPerPixel;
        int x = w - totalPx;
        for (int g = 0; g < groups; ++g)
        {
            const long long src = firstSrc + (long long) g * (long long) srcPerPixel;
            const int px = juce::jmin (steps[(size_t) g], w - x);
            if (px > 0) drawGroup (x, px, src);
            for (int r = 0; r < px; ++r, ++x) pxSrc[(size_t) x] = src;
        }

        nextSrc = firstSrc + (long long) groups * (long long) srcPerPixel;
        pxAcc = acc;
        dirty = false;
    }

    bool      dirty = true;
    long long nextSrc = 0;        // índice absoluto de la próxima columna del anillo a consumir
    int       srcPerPixel = 1;
    int       groupsTotal = 1;    // grupos que entran en la historia = denominador de Bresenham
    int       pxAcc = 0;          // acumulador de Bresenham (0 … groupsTotal-1)
    int       lastCapacity = 0;
    double    lastColsPerSec = 0.0;

    std::vector<int>       rowStart, rowEnd;   // filas del anillo que le tocan a cada fila de píxel
    std::vector<long long> pxSrc;              // el grupo dibujado en cada columna de píxel (-1 = ninguno)
};
}
