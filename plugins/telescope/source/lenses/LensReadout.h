#pragma once
#include <juce_graphics/juce_graphics.h>

// ========================================================================================================
// LensReadout — la cajita de lectura que sigue al cursor, compartida por las lentes que la tienen
// (SPECTRUM, SPECTROGRAM, BAND CORRELATION, STEREO SPECTROGRAM).
//
// Existe por una razón chica y concreta (nit del revisor del 50): la caja se dibuja a la DERECHA del
// cursor salvo que no entre, y "no entra" hay que resolverlo en los dos bordes. La cuenta ingenua
//
//     tx = jlimit (plot.getX(), plot.getRight() - textWidth, cursorX + 8)
//
// se rompe cuando el texto es MÁS ANCHO que el plot (lente angosta, o una lectura larga): ahí el límite
// inferior queda por encima del superior y jlimit devuelve el superior — o sea una x a la IZQUIERDA del
// área de dibujo, con la caja escapándose por el borde. Acá el ancho se ACOTA primero al área y recién
// después se ubica: la caja siempre queda adentro, aunque el texto tenga que recortarse.
// ========================================================================================================
namespace telescope
{
inline juce::Rectangle<int> readoutBoxFor (juce::Rectangle<int> area, int cursorX, int textWidth,
                                           int boxHeight = 20, int topMargin = 6, int gap = 8) noexcept
{
    const int w  = juce::jlimit (0, juce::jmax (0, area.getWidth()), textWidth);
    const int hi = juce::jmax (area.getX(), area.getRight() - w);
    const int x  = juce::jlimit (area.getX(), hi, cursorX + gap);
    return { x, area.getY() + topMargin, w, boxHeight };
}
}
