#pragma once
#include <cmath>
#include <juce_graphics/juce_graphics.h>
#include "lenses/Look.h"

// ========================================================================================================
// Raster — LA CACHÉ DE PÍXELES DE UNA LENTE, A ESCALA FÍSICA. Una sola vez, para las cinco lentes que
// dibujan sobre una `juce::Image` propia (SPECTROGRAM, STEREO SPECTROGRAM, WATERFALL, FIELD y la estela
// de SCOPE).
//
// ============================ EL DEFECTO QUE ESTE ARCHIVO EXISTE PARA CERRAR ============================
//
// Todas esas cachés se asignaban en píxeles LÓGICOS y se dibujaban con `drawImageAt`, o sea 1:1. En una
// pantalla Retina —donde un píxel lógico son 2×2 físicos— el sistema estira esa imagen al doble y lo que
// se ve es exactamente la MITAD de la resolución de la pantalla: bordes escalonados, texto de las celdas
// empastado, "pixelado". Es lo que Joaquín vio en su DAW el 9-sep contra Insight, y lo que las capturas
// de [uisnap] mostraban sin que lo viéramos porque las mirábamos reducidas.
//
// Lo que NO era: ni el algoritmo, ni la paleta, ni el antialiasing de JUCE. Era el tamaño del buffer.
//
// ============================================ LA REGLA ==================================================
//
//   1 · la imagen se asigna a  tamaño lógico × s , con s = look::physicalScale (g) LEÍDO EN paint;
//   2 · todo lo que se dibuja adentro trabaja en píxeles de DISPOSITIVO (la geometría se multiplica por s);
//   3 · se dibuja con `AffineTransform::scale (1/s)`, de vuelta al tamaño lógico;
//   4 · si s cambia —mover la ventana a otro monitor— la caché se rehace.
//
// EL FILTRO. Con s ENTERO (1, 2, 3) un píxel de la imagen cae exactamente sobre un píxel del dispositivo:
// no hay nada que interpolar y el remuestreo de baja calidad es el CORRECTO además del más barato —
// pedirle bilineal a una correspondencia 1:1 sólo agrega un desenfoque de medio píxel. Con s fraccionaria
// (1.5, 1.25 en un monitor escalado) sí hace falta el filtro bueno, porque ahí un píxel de imagen cae
// entre dos del dispositivo.
//
// s = 1 SE DIBUJA COMO SIEMPRE, con `drawImageAt`. No es una optimización: es la garantía de que en el
// banco de pruebas —que pinta sobre una `juce::Image` sin transformación— el resultado siga siendo BYTE
// IDÉNTICO al de antes de este archivo. De eso vive WATERFALL[horizonte], que compara píxel a píxel
// contra el pintor bruto.
// ========================================================================================================
namespace telescope::raster
{
// ¿La escala cae sobre la grilla de píxeles del dispositivo sin resto?
inline bool isIntegerScale (float s) noexcept
{
    return std::abs (s - std::round (s)) < 1.0e-3f;
}

// De un largo LÓGICO al mismo largo en píxeles de dispositivo. Redondeo hacia arriba: una caché un píxel
// corta deja una franja sin pintar en el borde derecho, y ese borde es justo el "ahora" del espectrograma.
inline int toDevice (int logical, float s) noexcept
{
    return juce::jmax (1, (int) std::ceil ((double) logical * (double) s - 1.0e-6));
}

class Cache
{
public:
    // Deja la imagen lista para pintar `logicalW × logicalH` a la escala física de `g`. Devuelve true si
    // hubo que (re)asignarla: el contenido anterior YA NO VALE y quien llama tiene que redibujar todo.
    bool prepare (const juce::Graphics& g, int logicalW, int logicalH)
    {
        return prepare (look::physicalScale (g), logicalW, logicalH);
    }

    bool prepare (float physScale, int logicalW, int logicalH)
    {
        const float sNew = physScale > 0.0f ? physScale : 1.0f;
        const int   w = toDevice (juce::jmax (1, logicalW), sNew);
        const int   h = toDevice (juce::jmax (1, logicalH), sNew);

        if (img.isValid() && img.getWidth() == w && img.getHeight() == h
            && std::abs (sNew - s) < 1.0e-4f && logicalW == lw && logicalH == lh)
            return false;

        img = juce::Image (juce::Image::ARGB, w, h, false);
        s   = sNew;
        lw  = logicalW;
        lh  = logicalH;
        return true;
    }

    bool  valid()   const noexcept { return img.isValid(); }
    float scale()   const noexcept { return s; }
    int   deviceW() const noexcept { return img.isValid() ? img.getWidth()  : 0; }
    int   deviceH() const noexcept { return img.isValid() ? img.getHeight() : 0; }

    juce::Image&       image()       noexcept { return img; }
    const juce::Image& image() const noexcept { return img; }

    // Fuerza la reasignación en el próximo `prepare` (cambio de settings que invalida el contenido).
    void invalidate() noexcept { img = juce::Image(); s = 0.0f; }

    // Devuelve la caché al plano lógico, en (x, y). Ver la nota del filtro en el encabezado.
    void blit (juce::Graphics& g, int x, int y) const
    {
        if (! img.isValid()) return;
        blitImage (g, img, s, x, y);
    }

    static void blitImage (juce::Graphics& g, const juce::Image& im, float s, int x, int y)
    {
        if (! im.isValid()) return;
        if (std::abs (s - 1.0f) < 1.0e-4f) { g.drawImageAt (im, x, y); return; }

        // saveState/restoreState y no un getter: `LowLevelGraphicsContext` no expone con qué calidad
        // estaba, y dejar la del blit puesta le cambiaría el filtro a lo que dibuje la lente después.
        g.saveState();
        g.setImageResamplingQuality (isIntegerScale (s) ? juce::Graphics::lowResamplingQuality
                                                        : juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (im, juce::AffineTransform::scale (1.0f / s)
                                        .translated ((float) x, (float) y));
        g.restoreState();
    }

private:
    juce::Image img;
    float s = 0.0f;
    int   lw = 0, lh = 0;
};

// ==== ESCRITURA DE PÍXELES ==============================================================================
// Las cinco lentes escriben ARGB a mano sobre la caché (es lo que las hace entrar en el presupuesto). Las
// dos cuentas que todas repetían viven acá.

// Mezcla de 8 bits SIN dividir por 255 (venía de FieldLens.cpp, donde se midió que era la diferencia
// entre entrar y no entrar en el presupuesto): `(v + 128 + ((v + 128) >> 8)) >> 8` da el mismo resultado
// que v/255 para todo v en [0, 65025], que es el rango de a·α + d·(255−α).
inline juce::uint32 blend8 (juce::uint32 srcC, juce::uint32 dstC, juce::uint32 a, juce::uint32 inv) noexcept
{
    const auto v = srcC * a + dstC * inv + 128u;
    return (v + (v >> 8)) >> 8;
}

inline juce::uint32 blendArgb (juce::uint32 src, juce::uint32 dst, juce::uint32 a) noexcept
{
    const auto inv = 255u - a;
    return 0xff000000u
         | (blend8 ((src >> 16) & 0xffu, (dst >> 16) & 0xffu, a, inv) << 16)
         | (blend8 ((src >>  8) & 0xffu, (dst >>  8) & 0xffu, a, inv) <<  8)
         |  blend8 ( src        & 0xffu,  dst        & 0xffu, a, inv);
}

// El mismo color con la luminancia multiplicada por k (0…1), sin pasar por juce::Colour. Lo usan la
// niebla de profundidad de WATERFALL y FIELD y el degradado de los rellenos: son millones de píxeles por
// frame y construir un Colour por píxel costaba más que el dibujo.
inline juce::uint32 scaleRgb (juce::uint32 argb, float k) noexcept
{
    const auto m = (juce::uint32) juce::jlimit (0, 256, (int) (k * 256.0f));
    return (argb & 0xff000000u)
         | ((((argb >> 16) & 0xffu) * m >> 8) << 16)
         | ((((argb >>  8) & 0xffu) * m >> 8) <<  8)
         |  (((argb        & 0xffu) * m >> 8));
}

// Mezcla dos ARGB opacos con peso t (0 = a, 1 = b). Es la niebla: el color de la línea hacia el fondo.
inline juce::uint32 mixArgb (juce::uint32 a, juce::uint32 b, float t) noexcept
{
    const auto w = (juce::uint32) juce::jlimit (0, 256, (int) (t * 256.0f));
    const auto iw = 256u - w;
    return 0xff000000u
         | (((((a >> 16) & 0xffu) * iw + ((b >> 16) & 0xffu) * w) >> 8) << 16)
         | (((((a >>  8) & 0xffu) * iw + ((b >>  8) & 0xffu) * w) >> 8) <<  8)
         |  (((a        & 0xffu) * iw + (b        & 0xffu) * w) >> 8);
}
}
