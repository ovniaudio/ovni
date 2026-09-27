#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <juce_graphics/juce_graphics.h>
#include "lenses/Palettes.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"

// ========================================================================================================
// Look — EL SISTEMA VISUAL de las lentes de TELESCOPE, en un solo archivo (prompt 56).
//
// POR QUÉ EXISTE. Con doce lentes escritas por tandas distintas, cada una había resuelto por su cuenta lo
// mismo: cuánto alpha lleva una línea de rejilla, qué verde es "el dato", de qué tamaño es el número
// grande, qué rampa de color usa el mapa de calor. El resultado se ve —lo dijo Joaquín mirando las doce
// capturas— como doce plugins parecidos y no como un instrumento. Acá viven esas decisiones UNA vez.
//
// LA REGLA QUE LO SOSTIENE, y que un test verifica: fuera de este archivo NINGUNA lente escribe un color
// literal (`Colour (0x…)` ni `Colours::…`). Todo color sale de un token de acá, y todo token de acá sale
// de `ui-kit/Theme.h` — que es del SELLO y no se toca. Si mañana el sello cambia el verde, cambian las
// doce lentes; si una lente necesita un color que no existe, se agrega acá y queda disponible para todas.
//
// LOS CUATRO PROBLEMAS MEDIBLES QUE RESUELVE (los cuatro tienen test en tests/VisualTest.cpp):
//
//   1 · JERARQUÍA DE REJILLA. Antes toda línea era la misma línea: `theme::line` a 15 %. Con todo al mismo
//       peso el ojo no encuentra el eje y la lente "lee ruido". Ahora hay dos pesos declarados —mayor
//       (la década, el eje, el 0 dB) y menor (las subdivisiones)— y una lente que dibuja las dos.
//
//   2 · PIXEL SNAPPING. Una línea de 1 px lógico en un coord fraccionario cae entre dos píxeles FÍSICOS y
//       el antialiasing la reparte: dos filas a medio alpha en vez de una entera. En Retina eso se ve
//       como una rejilla sucia y descolorida. `snap1px` alinea el coord a la grilla física real.
//
//   3 · DÍGITOS TABULARES. Un readout que cambia de -8.4 a -14.6 y "salta" de ancho es un readout que el
//       ojo no puede leer de reojo mientras mezcla. `tabularFont` es la mono del sello (JetBrains Mono),
//       donde todos los dígitos miden lo mismo.
//
//   4 · UNA SOLA RAMPA DE COLOR. La rampa secuencial estaba copiada en SpectrogramLens.cpp y en
//       FieldLens.cpp (mismas cuatro líneas, dos veces). Acá se arma una vez, y es MONÓTONA EN LUMINANCIA
//       a propósito: en estas lentes el color CODIFICA dB, así que "más claro = más fuerte" tiene que
//       valer para cualquier par de valores, o el mapa deja de ser legible.
//
// GLOW: como máximo UNO por lente y de 2–4 px. No es un adorno, es jerarquía — marca la capa que el ojo
// tiene que encontrar primero (el dato de adelante). Un glow en todo es un glow en nada.
// ========================================================================================================
namespace telescope::look
{
namespace th = ovni::ui::theme;

// ==== EL TEMA (F2 de la 0.2) ============================================================================
// Un usuario pidió una interfaz clara: la oscura le cansa la vista. El OSCURO sigue siendo el default y
// es, color por color, el del sello (darkInk = ovni::ui::theme, sin tocar un bit). El CLARO es una opción
// que elige el usuario (ThemePreference.h guarda la elección en OVNI.settings).
//
// Cómo llega a las lentes sin tocar su código de dibujo: las lentes dicen `th::green`, `th::txt`… y `th`
// apunta a `look::tint` (abajo), que tiene los mismos nombres que el tema del sello pero los RESUELVE AL
// USARLOS con la tinta vigente. Lo que no es color (`th::padIn`, `th::state`) sigue siendo el del sello.
enum class Theme { dark = 0, light = 1 };

struct Ink
{
    juce::Colour bg0, bg1, surf, surf2, line, lineSoft, txt, mut, fnt;
    juce::Colour green, greenD, cyan, magenta, amber, red;
};

inline const Ink& darkInk()
{
    static const Ink k { th::bg0, th::bg1, th::surf, th::surf2, th::line, th::lineSoft, th::txt, th::mut, th::fnt,
                         th::green, th::greenD, th::cyan, th::magenta, th::amber, th::red };
    return k;
}

// EL CLARO, derivado de los tokens del sello (no hay una paleta clara en la marca v2: se derivó y se dice):
//   · el papel es el `txt` del sello (#eaf1f8, el blanco azulado de la marca); el pozo, ese mismo papel
//     llevado a mitad de camino del blanco; las superficies, el papel oscurecido hacia el `mut`;
//   · el texto primario es el GRAFITO de la marca v2 (#0a0c14, «la marca sobre claro»); el secundario, el
//     `fnt` del sello llevado un tercio hacia el grafito (#3c4755); el gris de las marcas, el `mut`;
//   · las líneas, el grafito con el mismo alpha que el sello le da a su hairline;
//   · los hues de familia, llevados hacia el grafito hasta dar 7:1 sobre `surf2`, la superficie más oscura
//     del claro (8.2–8.3:1 sobre el papel). 7 y no 4.5: el texto oscuro fino sobre claro pierde contraste
//     con el suavizado, y con 4.5 nominal los rótulos de S medían 3.5–4.4 (primera pasada de [contraste]).
//     Son los mismos cinco colores con la luz de un día claro; el `greenD` (el dato atenuado) queda más
//     claro a propósito.
inline const Ink& lightInk()
{
    static const Ink k = []
    {
        const juce::Colour graphite (0xff0a0c14);                     // marca v2: la marca sobre claro
        const auto paper = th::txt;                                   // #eaf1f8
        const auto toGraphite = [graphite] (juce::Colour c, float t) { return c.interpolatedWith (graphite, t); };
        Ink i;
        i.bg0      = paper;
        i.bg1      = paper.interpolatedWith (juce::Colours::white, 0.5f);
        i.surf     = paper.interpolatedWith (th::mut, 0.10f);
        i.surf2    = paper.interpolatedWith (th::mut, 0.18f);
        i.line     = graphite.withAlpha (th::line.getFloatAlpha());
        i.lineSoft = graphite.withAlpha (th::lineSoft.getFloatAlpha());
        i.txt      = graphite;
        i.mut      = toGraphite (th::fnt,     0.34f);   // #3c4755
        i.fnt      = th::mut;
        i.green    = toGraphite (th::green,   0.71f);   // #224e3f
        i.greenD   = toGraphite (th::greenD,  0.50f);   // el dato atenuado: más claro a propósito
        i.cyan     = toGraphite (th::cyan,    0.71f);   // #224c54
        i.magenta  = toGraphite (th::magenta, 0.62f);   // #533c6d
        i.amber    = toGraphite (th::amber,   0.68f);   // #584226
        i.red      = toGraphite (th::red,     0.55f);   // #782e22
        return i;
    }();
    return k;
}

// La elección es del USUARIO, no de cada instancia (prompt 99): una sola, para todas las ventanas del
// proceso. Se escribe y se lee en el message thread (el clic en la tira, la apertura del editor).
inline std::atomic<int>& themeState() noexcept { static std::atomic<int> t { (int) Theme::dark }; return t; }
inline Theme theme() noexcept             { return (Theme) themeState().load (std::memory_order_relaxed); }
inline void  setTheme (Theme t) noexcept  { themeState().store ((int) t, std::memory_order_relaxed); }

// ==== LA PANTALLA DE DATOS (D-109, F2b de la 0.2) =======================================================
// En el tema claro, las cuatro lentes de mapa de calor —SPECTROGRAM, WATERFALL, STEREO SPECTROGRAM y FIELD—
// conservan su «pantalla» oscura y la paleta del oscuro, como la pantalla de un instrumento en un panel
// claro: el marco, los rótulos y los controles de alrededor siguen el claro. En la F2 la tinta clara se
// colaba adentro del dato: la rampa de fase de STEREO SPECTROGRAM terminaba en el grafito («mono», que en
// una mezcla es casi todo, salía negro sobre negro: luminancia máxima 11 de 255 contra 217 en oscuro), el
// piso de WATERFALL y el fondo de FIELD salían del papel, y la rejilla de encima del dato, del grafito.
//
// Mientras vive un `ScreenInk`, `ink()` devuelve la tinta OSCURA en cualquier tema. Cada lente abre uno
// alrededor de lo que dibuja ADENTRO de su pantalla (la imagen del dato, la rejilla y los rótulos que van
// encima, la lectura del cursor) y de las tablas de color que se hornean para ella. Es por hilo y se anida:
// se pinta en el message thread, y un alcance olvidado abierto en otro hilo no puede teñir éste.
inline int& screenInkDepth() noexcept { thread_local int depth = 0; return depth; }

struct ScreenInk
{
    ScreenInk() noexcept  { ++screenInkDepth(); }
    ~ScreenInk() noexcept { --screenInkDepth(); }
    ScreenInk (const ScreenInk&) = delete;
    ScreenInk& operator= (const ScreenInk&) = delete;
};

inline const Ink& ink() noexcept
{
    return theme() == Theme::light && screenInkDepth() == 0 ? lightInk() : darkInk();
}

// Un color que se resuelve AL USARSE con la tinta vigente. Tiene los tres métodos que las lentes le piden
// a un color del tema (medido: withAlpha, withMultipliedAlpha, interpolatedWith) y se convierte solo a
// juce::Colour en todo lo demás.
struct Tint
{
    juce::Colour (*get)() noexcept;
    operator juce::Colour() const noexcept                              { return get(); }
    juce::Colour withAlpha (float a) const noexcept                     { return get().withAlpha (a); }
    juce::Colour withMultipliedAlpha (float a) const noexcept           { return get().withMultipliedAlpha (a); }
    juce::Colour interpolatedWith (juce::Colour o, float p) const noexcept { return get().interpolatedWith (o, p); }
    juce::uint32 getARGB() const noexcept                               { return get().getARGB(); }
};

// Lo que las lentes llaman `th::`. Mismos nombres que ovni::ui::theme.
namespace tint
{
using ovni::ui::theme::padIn;
namespace state = ovni::ui::theme::state;
inline const Tint bg0      { [] () noexcept { return ink().bg0; } };
inline const Tint bg1      { [] () noexcept { return ink().bg1; } };
inline const Tint surf     { [] () noexcept { return ink().surf; } };
inline const Tint surf2    { [] () noexcept { return ink().surf2; } };
inline const Tint line     { [] () noexcept { return ink().line; } };
inline const Tint lineSoft { [] () noexcept { return ink().lineSoft; } };
inline const Tint txt      { [] () noexcept { return ink().txt; } };
inline const Tint mut      { [] () noexcept { return ink().mut; } };
inline const Tint fnt      { [] () noexcept { return ink().fnt; } };
inline const Tint green    { [] () noexcept { return ink().green; } };
inline const Tint greenD   { [] () noexcept { return ink().greenD; } };
inline const Tint cyan     { [] () noexcept { return ink().cyan; } };
inline const Tint magenta  { [] () noexcept { return ink().magenta; } };
inline const Tint amber    { [] () noexcept { return ink().amber; } };
inline const Tint red      { [] () noexcept { return ink().red; } };
}

// ==== TINTAS ============================================================================================
// Rejilla: dos pesos. El mayor es el que estructura (ejes, décadas, 0 dB); el menor subdivide.
inline const Tint gridMajor    { [] () noexcept { return ink().line.withMultipliedAlpha (1.85f); } };
inline const Tint gridMinor    { [] () noexcept { return ink().lineSoft; } };
inline const Tint gridAxis     { [] () noexcept { return ink().line.withMultipliedAlpha (2.60f); } };   // el eje del cero

// Dato: la línea BRILLANTE (el trazo) y el relleno SUAVE (el área bajo el trazo). Que sean dos tokens y
// no un color con dos alphas al azar es lo que hace que doce lentes rellenen con el mismo peso.
inline const Tint dataLine     { [] () noexcept { return ink().green; } };
inline const Tint dataLineDim  { [] () noexcept { return ink().greenD; } };
inline const Tint dataFill     { [] () noexcept { return ink().green.withAlpha (0.16f); } };
inline const Tint dataFillSoft { [] () noexcept { return ink().green.withAlpha (0.07f); } };

inline const Tint accent       { [] () noexcept { return ink().cyan; } };      // selección / lo que se toca
inline const Tint caution      { [] () noexcept { return ink().amber; } };     // zona de cuidado (cerca del techo)
inline const Tint alert        { [] () noexcept { return ink().red; } };       // fuera de fase, clip
inline const Tint reference    { [] () noexcept { return ink().magenta; } };   // la curva de referencia

inline const Tint txtPrimary   { [] () noexcept { return ink().txt; } };
inline const Tint txtSecondary { [] () noexcept { return ink().mut; } };
// F2 de la 0.2 · QUE SE PUEDA LEER. El terciario del sello (`fnt`, #566576) daba 2.3–3.4:1 contra los pozos
// y los rellenos de las lentes (medido en [contraste]: 797 de 1275 rótulos por debajo de 4.5:1, casi todos
// con este color) — es el gris oscuro que un usuario no llegaba a leer en los ejes. Como TEXTO pasa a ser el
// secundario del sello (`mut`, 5.2–6.3:1): lo mismo que ya había hecho el sitio (`--faint` = `--muted`). La
// jerarquía entre rótulos la llevan el tamaño, la caja y la posición, no un gris que no se lee.
inline const Tint txtTertiary  { [] () noexcept { return ink().mut; } };
// El gris terciario sigue existiendo para lo que NO es texto: marcas y pastillas de los ejes.
inline const Tint tick         { [] () noexcept { return ink().fnt; } };

inline const Tint surface      { [] () noexcept { return ink().surf; } };
inline const Tint surfaceHi    { [] () noexcept { return ink().surf2; } };
inline const Tint well         { [] () noexcept { return ink().bg1; } };      // el "pozo" donde vive el dato

// ==== MÉTRICAS POR TAMAÑO (S / M / L) ===================================================================
// El sello escala la ventana entera, pero un texto de 10 px escalado a 12.5 no es lo mismo que un texto
// pensado para 12.5: acá cada tamaño declara su propio ritmo. `pad` y `gap` son distintos a propósito —
// padding uniforme en todo es exactamente el "look de plantilla" que no queremos.
enum class Size { s, m, l };

struct Metrics
{
    Size  size        = Size::m;
    float pad         = 10.0f;   // margen del pozo contra el borde de la lente
    float gap         = 8.0f;    // separación entre bloques hermanos
    float gapTight    = 4.0f;    // separación DENTRO de un bloque (rótulo ↔ su número)
    float radius      = 3.0f;    // radio de las superficies chicas
    float radiusLarge = 5.0f;    // radio del pozo principal
    float gridMajorW  = 1.0f;    // grosor lógico de la rejilla mayor
    float gridMinorW  = 1.0f;
    float dataW       = 1.6f;    // grosor del trazo del dato
    float textMicro   = 8.5f;    // rótulos de eje
    float textSmall   = 10.0f;   // rótulos de bloque
    float textBody    = 11.5f;
    float textNumber  = 15.0f;   // los números de lectura
    float textHero    = 30.0f;   // EL número (LUFS integrado, la tonalidad…)
    float glowRadius  = 3.0f;
};

// El tamaño sale del ANCHO del área de dibujo, no de un flag: una lente no sabe (ni tiene por qué saber)
// en qué zoom está el editor. Los cortes son los tres tamaños del sello (S ≈ 656, M ≈ 820, L ≈ 1025).
inline Metrics metricsFor (int width) noexcept
{
    Metrics m;
    if (width < 740)
    {
        m.size = Size::s;
        m.pad = 7.0f;  m.gap = 6.0f;  m.gapTight = 3.0f;
        m.radius = 2.5f; m.radiusLarge = 4.0f;
        m.dataW = 1.4f;
        m.textMicro = 7.5f; m.textSmall = 9.0f; m.textBody = 10.0f; m.textNumber = 13.0f; m.textHero = 24.0f;
        m.glowRadius = 2.0f;
    }
    else if (width >= 940)
    {
        m.size = Size::l;
        m.pad = 13.0f; m.gap = 10.0f; m.gapTight = 5.0f;
        m.radius = 3.5f; m.radiusLarge = 6.0f;
        m.dataW = 1.8f;
        m.textMicro = 9.5f; m.textSmall = 11.0f; m.textBody = 13.0f; m.textNumber = 17.0f; m.textHero = 36.0f;
        m.glowRadius = 4.0f;
    }
    return m;
}

// ==== TIPOGRAFÍA ========================================================================================
// EL PISO (F2 de la 0.2). Ningún texto de las lentes baja de 11 px de diseño. A tamaño M un px de diseño es
// un punto en la Mac y un DIP en Windows: 11 queda arriba del texto más chico de macOS (10 pt) y a un paso del
// de Windows 11 (12 px), y en el caso mínimo —un usuario, Windows al 125 % en 1344 × 840— son 13.75 px
// físicos. Antes los ejes iban a 8.5–9 px (10.6–11.25 físicos ahí): es la «letra chica» que reportaron.
// Las lentes piden su fuente por acá (`look::mono`, `look::label`…) y no a `ovni::ui::fonts` directo: así el
// piso vale para todas y [contraste] lo verifica en cada rótulo.
inline constexpr float kMinTextPx = 11.0f;
inline float legible (float height) noexcept { return std::max (height, kMinTextPx); }

inline juce::Font mono (float height)    { return ovni::ui::fonts::mono (legible (height)); }
inline juce::Font label (float height)   { return ovni::ui::fonts::label (legible (height)); }
inline juce::Font body (float height)    { return ovni::ui::fonts::body (legible (height)); }
inline juce::Font display (float height) { return ovni::ui::fonts::display (legible (height)); }

// TABULAR = la mono del sello. Todos los dígitos miden lo mismo, así que "-14.6" y "-8.4" ocupan el mismo
// ancho y el número no baila mientras se mezcla. Se usa para TODO lo que sea una cifra.
inline juce::Font tabularFont (float height) { return mono (height); }
// Rótulo de bloque / de eje. Medium del sello: pesa lo justo para no competir con el número.
inline juce::Font labelFont (float height)   { return label (height); }
inline juce::Font bodyFont (float height)    { return body (height); }

// ==== PIXEL SNAPPING ====================================================================================
// Alinea un coord LÓGICO a la grilla de píxeles FÍSICOS: con escala 2, a múltiplos de 0.5. Una línea de
// 1 px lógico dibujada desde un coord así cubre 2 filas físicas ENTERAS, sin repartir alpha en los bordes.
inline float snap1px (float coord, float scale) noexcept
{
    const float s = scale > 0.0f ? scale : 1.0f;
    return std::round (coord * s) / s;
}

// La escala FÍSICA real del contexto (Retina = 2). Es la que hay que pasarle a snap1px: usar 1.0 fijo
// dejaría las hairlines desalineadas en exactamente las pantallas donde más se nota.
inline float physicalScale (const juce::Graphics& g) noexcept
{
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    return s > 0.0f ? s : 1.0f;
}

// UN RECTÁNGULO CON EL COLOR YA PUESTO, SNAPPEADO a la grilla de píxeles FÍSICOS (57b).
//
// Es el reemplazo de `g.fillRect (x, y, w, 1)` con enteros, que era como las trece lentes dibujaban sus
// hairlines —rejillas, ticks, cruces, la línea del cero—. Con enteros lógicos, en una pantalla de escala 2
// esa línea cae donde caiga: el sistema la estira y la reparte entre dos filas físicas a medio alpha, que
// es la "rejilla sucia y descolorida" que describe el punto 2 del encabezado. Snappeada, cubre filas
// físicas ENTERAS y se ve como una línea.
//
// No lleva color a propósito: se llama con el `setColour` que la lente ya hizo, así la conversión de las
// cuarenta y un llamadas fue mecánica y no hubo que adivinar ningún color.
inline void fillSnapped (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto s = physicalScale (g);
    g.fillRect (juce::Rectangle<float> (snap1px (r.getX(), s), snap1px (r.getY(), s),
                                        r.getWidth(), r.getHeight()));
}

// Las dos hairlines que dibujan todas las lentes. Toman la escala del propio Graphics, así que quien las
// llama no puede olvidarse de snappear.
inline void hLine (juce::Graphics& g, float x0, float x1, float y, juce::Colour c, float thickness = 1.0f)
{
    const auto s = physicalScale (g);
    g.setColour (c);
    g.fillRect (juce::Rectangle<float> (x0, snap1px (y, s), x1 - x0, thickness));
}

inline void vLine (juce::Graphics& g, float x, float y0, float y1, juce::Colour c, float thickness = 1.0f)
{
    const auto s = physicalScale (g);
    g.setColour (c);
    g.fillRect (juce::Rectangle<float> (snap1px (x, s), y0, thickness, y1 - y0));
}

// ==== GLOW ==============================================================================================
// El glow del sello, acotado: se dibuja el MISMO trazo 2 veces con alpha bajo y grosor creciente. Barato
// (no hay blur ni imagen intermedia) y compositor-friendly. UNO por lente.
inline void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float baseWidth, float radius)
{
    g.setColour (c.withMultipliedAlpha (0.16f));
    g.strokePath (p, juce::PathStrokeType (baseWidth + radius * 1.6f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    g.setColour (c.withMultipliedAlpha (0.26f));
    g.strokePath (p, juce::PathStrokeType (baseWidth + radius * 0.7f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
}

// ==== RELLENOS CON DEGRADADO ============================================================================
// La regla del 57b: NINGÚN relleno de dato es plano. Un área plana bajo una curva se lee como un bloque;
// con un degradado que se apaga al alejarse del dato, el ojo encuentra el borde solo. Son dos llamadas y
// un ColourGradient — el costo está en los PÍXELES, que son los mismos.
//
// `verticalFill` va de la línea hacia abajo (medidores, áreas bajo curva) y `radialFill` desde un centro
// (el hemisferio, la rueda de croma). Los dos toman UN color y le ponen los dos alphas: que la relación
// entre el trazo y su relleno sea la misma en las trece lentes es justamente lo que se está arreglando.
inline void verticalFill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c,
                          float topAlpha = 0.42f, float bottomAlpha = 0.05f)
{
    if (r.getHeight() <= 0.0f || r.getWidth() <= 0.0f) return;
    g.setGradientFill (juce::ColourGradient (c.withAlpha (topAlpha), r.getX(), r.getY(),
                                             c.withAlpha (bottomAlpha), r.getX(), r.getBottom(), false));
    g.fillRect (r);
}

inline void radialFill (juce::Graphics& g, const juce::Path& p, juce::Colour c, juce::Point<float> centre,
                        float radius, float innerAlpha = 0.42f, float outerAlpha = 0.06f)
{
    g.setGradientFill (juce::ColourGradient (c.withAlpha (innerAlpha), centre.x, centre.y,
                                             c.withAlpha (outerAlpha), centre.x, centre.y - radius, true));
    g.fillPath (p);
}

// UNA BARRA DE DATO: relleno con degradado y TAPA brillante arriba. La tapa es lo que la convierte en una
// lectura (el ojo va al filo y lee el valor) en vez de una mancha de color.
inline void dataBar (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float capThickness = 1.5f)
{
    if (r.getHeight() <= 0.0f || r.getWidth() <= 0.0f) return;
    verticalFill (g, r, c, 0.46f, 0.10f);
    g.setColour (c.withAlpha (0.95f));
    g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(),
                                        juce::jmin (capThickness, r.getHeight())));
}

// ==== SUAVIZADO DE PANTALLA =============================================================================
// Media móvil de radio `r` sobre un arreglo, en el lugar. Es "lo que se DIBUJA", nunca lo que se mide: las
// lentes que la usan tienen su readout leyendo el dato crudo. Sobre un eje logarítmico un radio en píxeles
// ES una fracción de octava fija, que es de donde sale el "1/12 oct" de SPECTRUM y de WATERFALL.
inline void smoothInPlace (float* v, int n, int radius)
{
    if (v == nullptr || n <= 2 || radius <= 0) return;
    std::vector<float> out ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        int   cnt = 0;
        for (int d = -radius; d <= radius; ++d)
        {
            const int j = i + d;
            if (j < 0 || j >= n) continue;
            sum += v[j];
            ++cnt;
        }
        out[(size_t) i] = cnt > 0 ? sum / (float) cnt : v[i];
    }
    std::copy (out.begin(), out.end(), v);
}

// ==== EL POZO ===========================================================================================
// La superficie donde vive el dato. Un escalón de superficie + hairline: es lo que da PROFUNDIDAD sin
// sombras caras, y lo que hace que las doce lentes se sientan del mismo instrumento.
inline void drawWell (juce::Graphics& g, juce::Rectangle<float> r, const Metrics& m)
{
    g.setColour (well);
    g.fillRoundedRectangle (r, m.radiusLarge);
    g.setColour (ink().lineSoft);
    g.drawRoundedRectangle (r.reduced (0.5f), m.radiusLarge, 1.0f);
}

// ==== EL FILO DE LA PANTALLA (D-109) ====================================================================
// En oscuro, la pantalla de datos lleva la hairline de siempre alrededor (o nada, en las lentes que no la
// tenían): la dibuja cada lente y acá no se toca. En CLARO la pantalla es un rectángulo oscuro sobre el
// papel, y un filo oscuro de 1 px lógico por fuera la cierra como la pantalla de un instrumento: sin él, la
// hairline gris del claro quedaba pegada al borde de la imagen y se leía como una caja mal recortada.
// Devuelve si lo dibujó (en oscuro no hace nada).
inline bool drawScreenEdge (juce::Graphics& g, juce::Rectangle<int> screen)
{
    if (theme() != Theme::light) return false;
    g.setColour (darkInk().bg0);
    g.fillRect (screen.expanded (1));
    return true;
}

// ==== EL PASO DE COLUMNAS DE FIELD Y WATERFALL (M-3 del revisor del 57c; F4 de la 0.2) ====================
// A escala física ≥ 1.5 las dos lentes resuelven la oclusión cada DOS columnas (ver FieldLens.cpp y
// WaterfallLens.cpp). En el runner un test puede forzar el camino fino para compararlo píxel a píxel ([columnas]).
// El paso sigue siendo 1 o 2 y nada más: la primera versión del gancho dejaba forzar cualquier entero, y con eso
// el compilador del runner perdía lo que sabía del paso y FIELD@2 pintaba ~0,35 ms más lento (medido contra el
// binario de antes, tres rondas) — un gancho no puede cambiar lo que mide [budget].
#if TELESCOPE_TEST_BUILD
inline bool& fineColumnsForTest() noexcept { static bool on = false; return on; }
inline bool  coarseColumns (bool atScale) noexcept { return atScale && ! fineColumnsForTest(); }
#else
constexpr bool coarseColumns (bool atScale) noexcept { return atScale; }
#endif

// ==== CROSSHAIR =========================================================================================
// La cruz que sigue al cursor. Discreta a propósito (el dato manda), pero con un punto en la intersección
// para que se entienda que es UNA lectura y no dos líneas sueltas. Pasar y < 0 dibuja sólo la vertical.
inline void drawCrosshair (juce::Graphics& g, juce::Rectangle<int> area, int x, int y,
                           juce::Colour c = accent)
{
    const auto a = area.toFloat();
    if (x >= area.getX() && x <= area.getRight())
        vLine (g, (float) x, a.getY(), a.getBottom(), c.withAlpha (0.38f));

    if (y >= area.getY() && y <= area.getBottom())
    {
        hLine (g, a.getX(), a.getRight(), (float) y, c.withAlpha (0.22f));
        g.setColour (c.withAlpha (0.9f));
        g.fillEllipse ((float) x - 2.0f, (float) y - 2.0f, 4.0f, 4.0f);
    }
}

// ==== READOUT ===========================================================================================
// La cajita de lectura que sigue al cursor. La geometría (que la caja NUNCA se escape del área, ni
// siquiera cuando el texto es más ancho que la lente) es la de LensReadout.h, que sigue siendo la única
// fuente de esa cuenta; acá se le agrega el DIBUJO, que antes hacía cada lente por su lado.
inline void drawReadoutBox (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& text,
                            const Metrics& m, juce::Colour tint = accent)
{
    const auto r = box.toFloat();
    g.setColour (ink().bg0.withAlpha (0.88f));
    g.fillRoundedRectangle (r, m.radius);
    g.setColour (tint.withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.5f), m.radius, 1.0f);
    g.setColour (txtPrimary);
    g.setFont (tabularFont (m.textSmall));
    g.drawText (text, box.reduced (5, 0), juce::Justification::centredLeft, true);
}

// ==== RÓTULO SOBRE EL DATO ==============================================================================
// Un rótulo que vive ADENTRO del gráfico (las octavas de CQT, arriba del plot) tiene detrás lo que dibuje el
// dato en ese momento: una barra alta lo dejaba en 4.4:1 (medido en [contraste], F2 de la 0.2). Con una
// pastilla del color del pozo detrás, el contraste es el del pozo, esté donde esté el dato.
inline void drawTagOverData (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r,
                             juce::Justification just, juce::Colour ink)
{
    const auto f  = g.getCurrentFont();
    const int  tw = juce::jmin (r.getWidth(), (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, text)));
    auto chip = r.withWidth (tw);
    if (just.testFlags (juce::Justification::right))                 chip = chip.withX (r.getRight() - tw);
    else if (just.testFlags (juce::Justification::horizontallyCentred)) chip = chip.withX (r.getCentreX() - tw / 2);
    g.setColour (well.withAlpha (0.82f));
    g.fillRoundedRectangle (chip.toFloat().expanded (2.0f, 0.0f), 2.0f);
    g.setColour (ink);
    g.drawText (text, r, just, true);
}

// ==== LA CAJA QUE NO DEPENDE DEL LOCALE (F5b de la 0.2, D-126) ============================================
// `juce::String::toUpperCase` y `toLowerCase` llaman a towupper / towlower, que DEPENDEN DEL LOCALE DEL PROCESO:
// con el «C», que es el de un programa que no lo cambia, «Cómo se va a sentir» salía «CóMO SE VA A SENTIR»; con
// es_AR.UTF-8, «CÓMO SE VA A SENTIR». TELESCOPE vive adentro del proceso del DAW y el locale es del host (cambiarlo
// cambiaría el del host), así que la caja se decide acá, con una tabla fija: sale igual en cualquier host y en
// cualquier sistema.
//
// Cubre las letras de los seis idiomas (en, es, pt, fr, de, it): medidas sobre Rules.h y Strings.h, todas son de
// Latin-1. ASCII; de U+00E0 a U+00FE ↔ de U+00C0 a U+00DE, a 0x20 (salvo ÷ y ×, que no son letras); ÿ ↔ Ÿ (U+0178);
// œ ↔ Œ; y ß → «SS» (que al bajar vuelve «ss», como en cualquier otro lado). Todo lo demás queda igual: el japonés
// no tiene caja. [mayus] lo prueba con el locale «C» puesto a propósito.
inline juce::juce_wchar upperChar (juce::juce_wchar c) noexcept
{
    if (c >= 'a' && c <= 'z')                   return c - 0x20;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7)    return c - 0x20;   // à..þ → À..Þ
    if (c == 0xFF)                              return 0x178;      // ÿ → Ÿ
    if (c == 0x153)                             return 0x152;      // œ → Œ
    return c;
}

inline juce::juce_wchar lowerChar (juce::juce_wchar c) noexcept
{
    if (c >= 'A' && c <= 'Z')                   return c + 0x20;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)    return c + 0x20;   // À..Þ → à..þ
    if (c == 0x178)                             return 0xFF;       // Ÿ → ÿ
    if (c == 0x152)                             return 0x153;      // Œ → œ
    return c;
}

inline juce::String upper (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes (s.getNumBytesAsUTF8() + 8);
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();
        if (c == 0xDF) out << "SS";                                 // ß no tiene mayúscula de una letra en uso
        else           out += upperChar (c);
    }
    return out;
}

inline juce::String lower (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes (s.getNumBytesAsUTF8() + 8);
    for (auto p = s.getCharPointer(); ! p.isEmpty();)
        out += lowerChar (p.getAndAdvance());
    return out;
}

// ==== UN RENGLÓN QUE SE PUEDE VERIFICAR (F2b de la 0.2) =================================================
// `g.drawText (…, false)` pierde glifos en silencio (en JUCE 8, palabras enteras: corta como un envoltorio de
// un solo renglón), y con `true` sale «…». Ninguna de las dos cosas le dice a un test QUÉ texto se pidió ni en
// qué caja, así que un test que mira sólo lo dibujado ve el «…» pero no el recorte silencioso (veredicto 99,
// reparo 4: con `false` y un renglón angostado, [reacomoda] seguía verde). VERDICT, la lente de las frases
// largas, dibuja cada renglón por acá: en el runner de tests el pedido queda anotado —texto, caja y fuente— y
// [reacomoda] mide con la misma cuenta de JUCE si entraba, se haya pedido elipsis o no. En el plugin es un
// drawText y nada más.
#if TELESCOPE_TEST_BUILD
struct TextRequest { juce::String text; juce::Rectangle<float> box; juce::Font font; };
inline std::vector<TextRequest>*& textRequestSink() noexcept { static std::vector<TextRequest>* s = nullptr; return s; }
#endif

inline void drawTextLine (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                          juce::Justification just, bool useEllipses = true)
{
   #if TELESCOPE_TEST_BUILD
    if (auto* sink = textRequestSink()) sink->push_back ({ text, area.toFloat(), g.getCurrentFont() });
   #endif
    g.drawText (text, area, just, useEllipses);
}

// ==== CELDA RÓTULO + VALOR (los botones de las lentes) ==================================================
// Ocho lentes partían la celda en proporciones fijas (1/2, 2/5, 3/5 para el rótulo). Con el piso de 11 px
// (F2 de la 0.2) un rótulo largo ya no entraba en su parte —«SMOOTHING» pedía 60 px y tenía 52— aunque la
// celda entera tenía lugar de sobra. Ahora el VALOR se queda con lo que mide y el rótulo con el resto; si
// igual no entran los dos, se corta el rótulo con elipsis, nunca el valor: el valor es lo que se lee.
inline void drawLabelValue (juce::Graphics& g, juce::Rectangle<int> r,
                            const juce::String& label, const juce::Font& labelF, juce::Colour labelC,
                            const juce::String& value, const juce::Font& valueF, juce::Colour valueC)
{
    const int vw = juce::jmin (r.getWidth(),
                               (int) std::ceil (juce::GlyphArrangement::getStringWidth (valueF, value)) + 1);
    const auto valueBox = r.removeFromRight (vw);
    r.removeFromRight (6);
    g.setColour (labelC);
    g.setFont (labelF);
    g.drawText (label, r, juce::Justification::centredLeft, true);
    g.setColour (valueC);
    g.setFont (valueF);
    g.drawText (value, valueBox, juce::Justification::centredRight, true);
}

// ==== PALETAS ===========================================================================================
// SECUENCIAL — el mapa de calor de SPECTROGRAM, STEREO SPECTROGRAM, WATERFALL y FIELD: t = 0 es silencio,
// t = 1 es el techo. Desde el 57b hay CUATRO para elegir y no una: los datos, la atribución de las
// publicadas y por qué son cuatro están en lenses/Palettes.h, que es de donde sale `palette (id)`.
//
// `sequential()` es la rampa del SELLO (`PaletteId::ovni`) y sigue existiendo con ese nombre porque es el
// default histórico y lo que verifica el contrato de monotonía de [visual]. Una lente que deja elegir la
// rampa NO llama a ésta: llama a `palette (id)` con el id del setting.
inline const std::array<juce::uint32, 256>& sequential() { return palette (PaletteId::ovni); }

// BIPOLAR — para lo que tiene SIGNO y un cero que importa: correlación, balance, fase por banda. Rojo en
// -1, neutro apagado en 0, verde en +1. NO es monótona en luminancia y no debe serlo: acá el ojo tiene
// que encontrar el CERO, no ordenar magnitudes.
inline const std::array<juce::uint32, 256>& bipolar()
{
    static const std::array<juce::uint32, 256> p = []
    {
        std::array<juce::uint32, 256> a {};
        const auto mid = th::surf2.interpolatedWith (th::mut, 0.35f);
        for (int i = 0; i < 256; ++i)
        {
            const float t = (float) i / 255.0f;
            const auto c = t < 0.5f ? th::red.interpolatedWith (mid, t / 0.5f)
                                    : mid.interpolatedWith (th::green, (t - 0.5f) / 0.5f);
            a[(size_t) i] = c.withAlpha (1.0f).getARGB();
        }
        return a;
    }();
    return p;
}

inline juce::Colour sequentialAt (float t) noexcept { return paletteAt (PaletteId::ovni, t); }

inline juce::Colour bipolarAt (float signed01) noexcept   // -1 … +1
{
    const auto t = (juce::jlimit (-1.0f, 1.0f, signed01) + 1.0f) * 0.5f;
    return juce::Colour (bipolar()[(size_t) juce::jlimit (0, 255, (int) std::lround (t * 255.0f))]);
}

// ==== ZONAS DE NIVEL ====================================================================================
// El color de un medidor según DÓNDE está respecto del techo. Es lo que convierte un medidor plano en uno
// que se lee de reojo: verde cuando hay aire, ámbar cuando queda poco, alerta cuando ya no queda.
inline juce::Colour levelZone (float db, float cautionDb = -3.0f, float alertDb = -0.5f) noexcept
{
    if (db >= alertDb)   return alert;
    if (db >= cautionDb) return caution;
    return dataLine;
}
}
