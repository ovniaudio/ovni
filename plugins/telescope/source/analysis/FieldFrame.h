#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

// ========================================================================================================
// FieldFrame — la GRILLA de energía por dirección de paneo × frecuencia que dibuja FIELD (lente 11,
// spec §2 fila 11 y §5.5, prompt 53). POD, viaja por su propio TripleBuffer.
//
// QUÉ ES Y QUÉ NO ES. Cada celda acumula ENERGÍA (|L_k|² + |R_k|²) de los bins cuyo paneo por energía cae
// en esa columna y cuya frecuencia cae en esa fila. La columna es PANEO, no azimut: dice cuánta energía
// tiene cada canal, no dónde estaba la fuente. De una mezcla estéreo terminada la posición real no tiene
// solución única (la trampa del ITD de ORBIT, informe 21), así que el eje se rotula L … C … R y jamás
// −90° … +90°. Está en el README y en la lente, fijo, sin poder apagarse.
//
// LOS BORDES DE LA GRILLA, que es donde una visualización miente sin querer:
//
//   FRECUENCIA — 96 filas LOG sobre 20 Hz – 20 kHz (tres décadas exactas), por BORDES y no por centros:
//   la fila r cubre [20·1000^(r/96), 20·1000^((r+1)/96)), o sea 7.48 % de ancho relativo cada una — 32
//   filas por década ≈ 9.63 por octava. Todas las filas miden lo mismo; no hay media celda en los
//   extremos. Su centro geométrico (el que rotula la lente y devuelve la lectura) es
//   20·1000^((r+0.5)/96). Las ocho octavas "redondas" (31.25 Hz … 8 kHz, más 16 kHz) caen adentro.
//
//   LO QUE QUEDA AFUERA SE DESCARTA. Un bin por debajo de 20 Hz o por encima de 20 kHz NO se apila en la
//   fila del borde. Apilarlo encendería la fila 0 con el DC y los subgraves inaudibles — el grave más
//   fuerte del dibujo sería algo que nadie escucha. Con FFT de orden 12 a 48 kHz eso son los bins 0 y 1.
//
//   DIRECCIÓN — 64 columnas sobre pan ∈ [−1, +1], por CENTROS: la columna c está en −1 + 2c/63. La 0 es
//   "sólo L", la 63 "sólo R", y el centro exacto (pan = 0) cae ENTRE la 31 y la 32. Cada bin reparte su
//   energía LINEALMENTE entre sus dos columnas vecinas y nada más: no hay kernel más ancho, porque
//   ensanchar una fuente puntual sería inventar una anchura que la medición no tiene.
//
// LA ESTELA. `trail` guarda las últimas 8 grillas DECIMADAS a 48 filas × 32 direcciones (máximo de cada
// bloque 2×2) — el mismo POD `float trail[8][48][32]` que el prompt escribe "32×48" mirando el eje X
// primero. Los números son los mismos; acá se escribe SIEMPRE filas × direcciones, como el resto del
// archivo (LOW-3 del revisor del 53). Es la
// profundidad del dibujo: 8 grillas enteras serían 8 × 6 144 celdas para pintar algo que se ve chico y
// tenue al fondo. `trail[0]` es SIEMPRE la más vieja y `trail[trailCount-1]` la más nueva; ninguna es la
// grilla actual — la de ahora se dibuja adelante, aparte.
// ========================================================================================================
namespace telescope
{
struct FieldFrame
{
    static constexpr int kDir  = 64;    // columnas de dirección (paneo −1 … +1)
    static constexpr int kRows = 96;    // filas de frecuencia (log, 20 Hz – 20 kHz)

    static constexpr int kTrail     = 8;    // grillas de estela
    static constexpr int kTrailDir  = 32;   // decimación de la estela: kDir / 2
    static constexpr int kTrailRows = 48;   // kRows / 2

    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;
    static constexpr double kDecades = 3.0;   // log10(20000/20)

    float grid  [kRows][kDir] {};
    float trail [kTrail][kTrailRows][kTrailDir] {};

    float        maxCell    = 0.0f;   // la celda más fuerte de `grid` (la normalización del dibujo)
    int          trailCount = 0;      // grillas válidas en `trail`, 0 … kTrail
    juce::uint32 frameIndex = 0;      // frames de campo publicados desde el reset
    float        decaySec   = 0.0f;   // la constante de tiempo EFECTIVA con la que se calculó

    //================================================================================ frecuencia ↔ fila
    // El centro GEOMÉTRICO de la fila (la frecuencia que rotula la lente y devuelve la lectura).
    static double rowFrequency (int row) noexcept
    {
        const double r = (double) std::clamp (row, 0, kRows - 1);
        return kMinHz * std::pow (10.0, kDecades * (r + 0.5) / (double) kRows);
    }

    // La fila de una frecuencia, o −1 si cae FUERA de la grilla (ver "lo que queda afuera se descarta").
    //
    // EL BORDE DE 20 kHz ESTÁ INCLUIDO (LOW-1 del revisor del 53). El intervalo era [20, 20000) por el
    // `t >= 1.0`, mientras el comentario y el README hablan de "20 Hz – 20 kHz" sin aclarar que el
    // extremo de arriba quedaba afuera. En la práctica no cambia un píxel —con `binHz` no entero, ningún
    // bin cae en 20 000.000000 Hz exactos a las tasas y órdenes de FFT que el motor soporta— pero un
    // borde que el texto declara cerrado y el código abre es una trampa para el que lea después. Ahora
    // `hz == kMaxHz` cae en la última fila, `kRows − 1`, que es la que le corresponde.
    static int rowForHz (double hz) noexcept
    {
        if (! (hz > 0.0)) return -1;
        const double t = std::log10 (hz / kMinHz) / kDecades;    // 0 en 20 Hz, 1 en 20 kHz (INCLUIDO)
        if (t < 0.0 || t > 1.0) return -1;
        const int row = (int) (t * (double) kRows);
        return (row >= 0 && row < kRows) ? row : (row == kRows ? kRows - 1 : -1);
    }

    //================================================================================ paneo ↔ columna
    static float columnPan (int c) noexcept
    {
        return -1.0f + 2.0f * (float) std::clamp (c, 0, kDir - 1) / (float) (kDir - 1);
    }

    // La posición FRACCIONARIA de un paneo entre columnas (la que reparte la energía entre las vecinas).
    static double columnPos (double pan) noexcept
    {
        return (std::clamp (pan, -1.0, 1.0) + 1.0) * 0.5 * (double) (kDir - 1);
    }
};
}
