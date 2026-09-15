#pragma once
#include <algorithm>
#include <array>
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

// ==== TINTAS ============================================================================================
// Rejilla: dos pesos. El mayor es el que estructura (ejes, décadas, 0 dB); el menor subdivide.
inline const juce::Colour gridMajor    = th::line.withMultipliedAlpha (1.85f);
inline const juce::Colour gridMinor    = th::lineSoft;
inline const juce::Colour gridAxis     = th::line.withMultipliedAlpha (2.60f);   // el eje del cero

// Dato: la línea BRILLANTE (el trazo) y el relleno SUAVE (el área bajo el trazo). Que sean dos tokens y
// no un color con dos alphas al azar es lo que hace que doce lentes rellenen con el mismo peso.
inline const juce::Colour dataLine     = th::green;
inline const juce::Colour dataLineDim  = th::greenD;
inline const juce::Colour dataFill     = th::green.withAlpha (0.16f);
inline const juce::Colour dataFillSoft = th::green.withAlpha (0.07f);

inline const juce::Colour accent       = th::cyan;          // selección / lo que el usuario está tocando
inline const juce::Colour caution      = th::amber;         // zona de cuidado (cerca del techo)
inline const juce::Colour alert        = th::red;           // fuera de fase, clip, lo que hay que mirar
inline const juce::Colour reference    = th::magenta;       // la curva de referencia / el objetivo

inline const juce::Colour txtPrimary   = th::txt;
inline const juce::Colour txtSecondary = th::mut;
inline const juce::Colour txtTertiary  = th::fnt;

inline const juce::Colour surface      = th::surf;
inline const juce::Colour surfaceHi    = th::surf2;
inline const juce::Colour well         = th::bg1;           // el "pozo" donde vive el dato

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
// TABULAR = la mono del sello. Todos los dígitos miden lo mismo, así que "-14.6" y "-8.4" ocupan el mismo
// ancho y el número no baila mientras se mezcla. Se usa para TODO lo que sea una cifra.
inline juce::Font tabularFont (float height) { return ovni::ui::fonts::mono (height); }
// Rótulo de bloque / de eje. Medium del sello: pesa lo justo para no competir con el número.
inline juce::Font labelFont (float height)   { return ovni::ui::fonts::label (height); }
inline juce::Font bodyFont (float height)    { return ovni::ui::fonts::body (height); }

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
    g.setColour (th::lineSoft);
    g.drawRoundedRectangle (r.reduced (0.5f), m.radiusLarge, 1.0f);
}

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
    g.setColour (th::bg0.withAlpha (0.88f));
    g.fillRoundedRectangle (r, m.radius);
    g.setColour (tint.withAlpha (0.45f));
    g.drawRoundedRectangle (r.reduced (0.5f), m.radius, 1.0f);
    g.setColour (txtPrimary);
    g.setFont (tabularFont (m.textSmall));
    g.drawText (text, box.reduced (5, 0), juce::Justification::centredLeft, false);
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
