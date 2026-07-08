#pragma once
#include <juce_graphics/juce_graphics.h>

// =============================================================================
// Tokens visuales del SELLO OVNI (curados desde ÓRBITA M4 §5; ver sello/DESIGN.md).
// Negro casi puro; hues semánticos que cortan, uno por FAMILIA de plugin:
//   Movimiento = cian · Espacio/Profundidad = magenta · Espectral = verde · Textura = ámbar.
// ámbar/rojo además = caución/clip en los meters.
//
// Reusable por cualquier plugin del catálogo: un plugin elige su hue de familia
// (p.ej. slider.getProperties().set("hue", (int) theme::magenta.getARGB())).
// =============================================================================
namespace ovni::ui::theme
{
    // --- superficies (Joaquín: "más negro") -----------------------------------
    inline const juce::Colour bg0   { 0xff030406 };   // fondo casi puro
    inline const juce::Colour bg1   { 0xff060709 };
    inline const juce::Colour surf  { 0xff0a0c11 };   // superficie elevada (paneles)
    inline const juce::Colour line  { 0x26a0c0e0 };   // hairline ~15% alpha

    // --- acentos semánticos (uno por familia) ---------------------------------
    inline const juce::Colour cyan   { 0xff5ee7f0 };  // Movimiento
    inline const juce::Colour cyanD  { 0xff27c3d2 };
    inline const juce::Colour magenta{ 0xffc98bff };  // Espacio / Profundidad
    inline const juce::Colour magD   { 0xff9d63d6 };
    inline const juce::Colour green  { 0xff5ef0a8 };  // Espectral  (oklch 80% .14 160 → mint)
    inline const juce::Colour greenD { 0xff27c98a };
    inline const juce::Colour amber  { 0xffffb44d };  // Textura / caución (meter)
    inline const juce::Colour red    { 0xffff5733 };  // clip

    // --- superficies / hairlines extra (rediseño 2026-06, mockup pulsar-a; append-only) ---------
    inline const juce::Colour surf2    { 0xff0d1017 };   // superficie elevada clara (cards/header)
    inline const juce::Colour lineSoft { 0x12a0c0e0 };   // hairline suave ~7% (separadores tenues)
    inline const juce::Colour utilHue  { 0xff9fb2c8 };   // hue NEUTRO de la familia "utilidad" (IN/OUT del chasis)

    // --- texto -----------------------------------------------------------------
    inline const juce::Colour txt   { 0xffeaf1f8 };   // primario
    inline const juce::Colour mut   { 0xff8595aa };   // secundario
    inline const juce::Colour fnt   { 0xff566576 };   // terciario / footer

    // --- tamaños (lógicos) -----------------------------------------------------
    inline constexpr int padIn   = 16;
    inline constexpr int headerH = 48;
    inline constexpr int railH   = 56;
    inline constexpr int barH    = 22;   // bottom-bar contextual (nombre + valor del control bajo el cursor)

    // --- feel / estados (curados; sutiles, NO gamificado — ver interaction-grammar §reglas) -----
    // Multiplicadores de alpha por estado (se aplican sobre el color de FAMILIA del control). Un
    // mismo set para todo el ui-kit → los 6 plugins se sienten idénticos. Compositor-friendly: sólo
    // suben/bajan alpha de glows ya dibujados, nunca propiedades de layout.
    namespace state
    {
        inline constexpr float hoverGlow = 0.16f;   // glow extra del borde de familia al pasar el cursor
        inline constexpr float pressGlow = 0.26f;   // realce al presionar (feedback de press)
        inline constexpr float focusRing = 0.55f;   // alpha del anillo de foco de teclado
        inline constexpr float fineHint  = 0.85f;   // alpha del tick de "modo fino" (Shift)
        inline constexpr int   fineDiv   = 5;        // Shift: sensibilidad ÷ fineDiv (drag fino)
    }
}
