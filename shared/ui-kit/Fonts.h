#pragma once
#include <juce_graphics/juce_graphics.h>

// =============================================================================
// Familias del SELLO OVNI embebidas (ver sello/DESIGN.md §Tipografía).
//   display = Clash Grotesk Semibold  — wordmark / display
//   body    = General Sans Regular    — texto
//   label   = General Sans Medium     — labels de control
//   mono    = JetBrains Mono          — readouts / coordenadas (el "instrumento alien")
// Tres familias = excepción deliberada (la mono es funcional). Typefaces cacheadas
// por proceso; fallback a system si los assets faltaran.
// =============================================================================
namespace ovni::ui::fonts
{
    juce::Font display (float height);
    juce::Font body    (float height);
    juce::Font label   (float height);
    juce::Font mono    (float height);
}
