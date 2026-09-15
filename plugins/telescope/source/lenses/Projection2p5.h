#pragma once
#include <juce_graphics/juce_graphics.h>
#include <algorithm>

// ========================================================================================================
// Projection2p5 — la proyección 2.5D por SOFTWARE de TELESCOPE. La comparten WATERFALL (lente 5) y FIELD
// (lente 11), que son las dos lentes "3D" del plugin.
//
// POR QUÉ NO HAY GPU (D-46 del spec). Metal sólo existe en macOS y OpenGL en JUCE es un contexto aparte que
// varios hosts pelean con su propio dibujo. Un analizador que en Windows —o en el host equivocado— muestra
// dos lentes menos no es el mismo producto. Así que las dos 3D se dibujan con `juce::Graphics` y píxeles,
// y a cambio los conteos están ACOTADOS: WATERFALL ≤ 120 líneas × 256 puntos, y FIELD siempre las mismas
// cuatro imágenes chicas (un plano de 64×96 y tres láminas de 32×48), que es más fuerte que un tope.
//
// POR QUÉ ES OBLICUA Y NO EN PERSPECTIVA. Una perspectiva de verdad divide por z, y esa división rompe dos
// cosas que en un instrumento de medición valen más que el realismo: que el eje sea LEGIBLE (la misma
// distancia en frecuencia mide lo mismo en cualquier profundidad, salvo una escala global) y que la
// proyección sea MONÓTONA y sin punto de fuga adentro del dibujo. Acá `z` sólo hace dos cosas, las dos
// lineales: sube la escena (`tilt`) y la encoge hacia el centro (`depth`). Sin división, sin cámara, sin
// matriz de 4×4 — tres multiplicaciones por punto.
//
//     s  = 1 − depth·z                                    (la escala horizontal a esa profundidad)
//     px = x0 + w·(0.5 + (x − 0.5)·s)
//     py = y0 + h·(1 − tilt·z − (1 − tilt)·y)
//
// LO QUE GARANTIZA, y lo que el test verifica:
//   · `px` es estrictamente creciente en x a z fijo, y `py` estrictamente decreciente en y a z fijo y en z
//     a y fijo. O sea: más frecuencia = más a la derecha, más nivel = más arriba, más viejo = más al fondo.
//     Nunca se cruzan dos profundidades, así que el algoritmo de oclusión puede confiar en el orden.
//   · el punto siempre cae DENTRO del área, para cualquier (x, y, z) ∈ [0,1]³ — sin clamps de emergencia
//     en el que dibuja.
//   · (0,0,0) es la esquina de abajo a la izquierda del plano de ADELANTE y (1,1,1) el borde de arriba del
//     plano del FONDO, corrido `depth/2·w` hacia adentro (la fuga es central: los dos costados se meten
//     lo mismo).
//
// `px` NO es monótona en z, y eso es a propósito: es exactamente la fuga. A la izquierda del centro la
// escena se corre a la derecha al alejarse y a la derecha se corre a la izquierda; en x = 0.5 no se mueve.
// ========================================================================================================
namespace telescope
{
struct Projection2p5
{
    float x0 = 0.0f, y0 = 0.0f, w = 1.0f, h = 1.0f;   // el área de dibujo, en píxeles

    float tilt  = 0.35f;   // fracción del ALTO que se lleva el eje de profundidad (0 = plano, sin 3D)
    float depth = 0.18f;   // fracción del ANCHO que se encoge la escena en el fondo (0 = sin fuga)

    juce::Point<float> project (float x01, float y01, float z01) const noexcept
    {
        const float x = std::clamp (x01, 0.0f, 1.0f);
        const float y = std::clamp (y01, 0.0f, 1.0f);
        const float z = std::clamp (z01, 0.0f, 1.0f);

        const float s = 1.0f - depth * z;
        return { x0 + w * (0.5f + (x - 0.5f) * s),
                 y0 + h * (1.0f - tilt * z - (1.0f - tilt) * y) };
    }

    // El SUELO de un plano de profundidad: la y de todo ese plano cuando el valor vale 0. Es la que usa
    // la oclusión —nada de un plano puede caer por debajo de su propio suelo— y la que ubica el rótulo
    // de tiempo de cada profundidad.
    float floorY (float z01) const noexcept { return project (0.0f, 0.0f, z01).y; }

    // Los dos bordes horizontales de un plano de profundidad (lo que ocupa de ancho al alejarse).
    float leftX  (float z01) const noexcept { return project (0.0f, 0.0f, z01).x; }
    float rightX (float z01) const noexcept { return project (1.0f, 0.0f, z01).x; }

    // La INVERSA horizontal: de un píxel a la coordenada 0–1 del plano que está a esa profundidad. La
    // necesita el que dibuja línea por columna de píxel (y la lectura bajo el cursor).
    float unprojectX (float px, float z01) const noexcept
    {
        const float s = 1.0f - depth * std::clamp (z01, 0.0f, 1.0f);
        if (s <= 0.0f || w <= 0.0f) return 0.0f;
        return std::clamp (0.5f + ((px - x0) / w - 0.5f) / s, 0.0f, 1.0f);
    }
};
}
