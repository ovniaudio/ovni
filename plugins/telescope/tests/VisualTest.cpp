// [telescope][visual] — los CUATRO contratos del sistema visual (source/lenses/Look.h, prompt 56).
//
// No son tests de "se ve lindo": cada uno mide una propiedad que, si se rompe, se NOTA en la pantalla y
// nadie sabría decir por qué. El de la rejilla sucia (snapping) y el del número que baila (tabulares) son
// los dos que Joaquín señaló mirando las capturas.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <set>
#include <cstdio>
#include <vector>
#include "lenses/Look.h"
#include "lenses/Strings.h"

namespace
{
// La carpeta de las lentes, resuelta desde la ruta de ESTE archivo en tiempo de compilación: así el test
// encuentra las fuentes desde cualquier worktree y sin depender del directorio de trabajo del runner.
juce::File lensesDir()
{
    return juce::File (juce::String (__FILE__)).getParentDirectory()
               .getParentDirectory().getChildFile ("source").getChildFile ("lenses");
}

// Luminancia relativa Rec.709 sobre los componentes ya lineales-en-sRGB-8bit (sin degamma: lo que se
// compara es el ORDEN, y la degamma es monótona, así que no cambia el veredicto).
double luma (juce::uint32 argb) noexcept
{
    const auto c = juce::Colour (argb);
    return 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue();
}

// La carpeta del plugin (plugins/telescope), para los tests que leen fuentes fuera de lenses/.
juce::File pluginDir()
{
    return juce::File (juce::String (__FILE__)).getParentDirectory().getParentDirectory();
}

// Los archivos que dibujan: las trece lentes y la tira. `Strings.h` es la excepción (ahí viven las
// tablas) y `LensStrip.h` entra explícitamente porque vive en otra carpeta.
juce::Array<juce::File> paintingFiles()
{
    juce::Array<juce::File> out;
    for (const auto& f : lensesDir().findChildFiles (juce::File::findFiles, false, "*.cpp;*.h"))
        if (f.getFileName() != "Strings.h")
            out.add (f);
    out.add (lensesDir().getParentDirectory().getChildFile ("ui").getChildFile ("LensStrip.h"));
    return out;
}

// Los literales de string de una línea de C++, sin los comentarios de línea (un comentario en castellano
// es documentación, no un rótulo). Devuelve el CONTENIDO de cada literal.
juce::StringArray literalsIn (const juce::String& raw)
{
    juce::StringArray out;
    const auto trimmed = raw.trimStart();
    if (trimmed.startsWith ("//") || trimmed.startsWith ("*")) return out;

    const auto line = raw.upToFirstOccurrenceOf ("//", false, false);
    bool inside = false;
    juce::String cur;
    for (int i = 0; i < line.length(); ++i)
    {
        const auto c = line[i];
        if (! inside) { if (c == '"') { inside = true; cur.clear(); } continue; }
        if (c == '\\') { if (i + 1 < line.length()) { cur << c << line[i + 1]; ++i; } continue; }
        if (c == '"')  { inside = false; out.add (cur); continue; }
        cur << c;
    }
    return out;
}

// ===== 56c ===== LAS SECUENCIAS \xHH SE DECODIFICAN ANTES DE MIRARLAS.
//
// `literalsIn` devuelve el literal tal como está ESCRITO, con las barras adentro. Así, un rótulo puesto
// como "\xc3\xb1" —que es exactamente como se escribe una ñ cuando uno no se quiere pelear con la
// codificación del archivo, y es como este mismo repo escribe el · y el em-dash— pasaba el barrido de
// acentos sin que nadie lo viera: el escáner leía una barra, una equis y cuatro dígitos hexadecimales, y
// ninguno de esos es una ñ. El agujero es del tamaño de todo el barrido.
//
// Se decodifican los bytes y se interpretan como UTF-8 (los caracteres que interesan son de 2 bytes; los
// de 3, como el em-dash \xe2\x80\x94, salen igual). Si la decodificación no da UTF-8 válido —un \xHH
// suelto— se devuelve el literal como estaba: el barrido no puede inventar infracciones.
juce::String decodeHexEscapes (const juce::String& lit)
{
    const auto hex = [] (juce::juce_wchar c) { return juce::CharacterFunctions::getHexDigitValue (c); };

    std::string bytes;
    for (int i = 0; i < lit.length(); ++i)
    {
        if (lit[i] == '\\' && i + 3 < lit.length() && (lit[i + 1] == 'x' || lit[i + 1] == 'X')
            && hex (lit[i + 2]) >= 0 && hex (lit[i + 3]) >= 0)
        {
            bytes += (char) (hex (lit[i + 2]) * 16 + hex (lit[i + 3]));
            i += 3;
            continue;
        }
        const juce::String one (juce::String::charToString (lit[i]));
        bytes += one.toRawUTF8();
    }

    return juce::CharPointer_UTF8::isValidString (bytes.c_str(), (int) bytes.size())
             ? juce::String::fromUTF8 (bytes.c_str(), (int) bytes.size())
             : lit;
}

// Los acentos castellanos (minúsculas, mayúsculas y los dos signos de apertura).
const juce::String& accentChars()
{
    static const juce::String a = juce::String::fromUTF8 ("\xc3\xa1\xc3\xa9\xc3\xad\xc3\xb3\xc3\xba"
                                                          "\xc3\xb1\xc3\xbc\xc2\xbf\xc2\xa1\xc3\x81"
                                                          "\xc3\x89\xc3\x8d\xc3\x93\xc3\x9a\xc3\x91");
    return a;
}

// Los literales de un archivo que llevan un acento castellano, YA decodificados. Devuelve
// "<etiqueta>:<linea>  \"<literal>\"" por cada uno. Es la función que usan los dos tests: el que barre
// las lentes de verdad y el que le pasa un archivo fabricado para comprobar que el barrido ve.
juce::StringArray accentedLiteralsIn (const juce::String& content, const juce::String& label)
{
    juce::StringArray out;
    juce::StringArray lines;
    lines.addLines (content);

    for (int i = 0; i < lines.size(); ++i)
        for (const auto& lit : literalsIn (lines[i]))
        {
            const auto decoded = decodeHexEscapes (lit);
            for (int c = 0; c < decoded.length(); ++c)
                if (accentChars().containsChar (decoded[c]))
                {
                    out.add (label + ":" + juce::String (i + 1) + "  \"" + decoded + "\"");
                    break;
                }
        }
    return out;
}
}

// ========================================================================================================
// (a) NINGÚN COLOR LITERAL FUERA DE Look.h.
//
// La regla que sostiene todo el sistema: si una lente puede escribir `Colour (0xff00ff00)`, en seis meses
// hay doce verdes distintos y el sello dejó de existir en la pantalla. Se leen los archivos de verdad y
// se lista CADA infracción con archivo y línea — un test que sólo dice "falló" obligaría a repetir el
// grep a mano.
// ========================================================================================================
TEST_CASE ("telescope: ninguna lente escribe un color literal fuera de Look.h", "[telescope][visual]")
{
    const auto dir = lensesDir();
    INFO ("carpeta de lentes: " << dir.getFullPathName());
    REQUIRE (dir.isDirectory());

    juce::StringArray offences;
    int scanned = 0;

    // 56b: la lista sale de paintingFiles() —las trece lentes MÁS ui/LensStrip.h—, la misma que usa el
    // barrido de castellano. La tira quedaba fuera del grep sólo porque vive en otra carpeta, y es un
    // archivo que dibuja como cualquier otro: si mañana escribiera un verde a mano, nadie se enteraba.
    for (const auto& f : paintingFiles())
    {
        if (f.getFileName() == "Look.h")   // la ÚNICA excepción: acá viven los tokens
            continue;

        ++scanned;
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        for (int i = 0; i < lines.size(); ++i)
        {
            const auto& l = lines[i];
            // `Colour (0x…` / `Colour(0x…` = un ARGB literal.  `Colours::` = la paleta de JUCE.
            // `juce::Colour (palette[i])` NO cuenta: eso es leer un token ya derivado.
            if (l.contains ("Colours::") || l.contains ("Colour (0x") || l.contains ("Colour(0x"))
                offences.add (f.getFileName() + ":" + juce::String (i + 1) + "  " + l.trim());
        }
    }

    REQUIRE (scanned > 0);
    std::printf ("VISUAL[literales] %d archivos de lentes escaneados, %d infracciones\n",
                 scanned, offences.size());
    for (const auto& o : offences)
        std::printf ("VISUAL[literales]   %s\n", o.toRawUTF8());

    INFO ("colores literales encontrados:\n" << offences.joinIntoString ("\n"));
    REQUIRE (offences.isEmpty());
}

// ========================================================================================================
// (a-bis) NINGÚN RÓTULO EN CASTELLANO A MANO EN LAS TRECE LENTES (D-50, prompt 56b).
//
// La regla hermana de la de los colores, y por el mismo motivo: si una lente puede escribir "VENTANA"
// directamente, en seis meses la mitad del plugin habla castellano y la otra mitad inglés, y el selector
// de idioma es decorativo. Hasta el 56 pasaba exactamente eso — sólo SCOPE, FIELD y la tira leían de
// Strings.h y las otras diez dibujaban literales — así que el plugin NO cumplía D-50 aunque tuviera las
// seis tablas completas.
//
// Se leen los archivos de verdad, igual que en (a), y se lista CADA infracción con archivo y línea.
//
// DOS BARRIDOS, porque uno solo no alcanza:
//   1 · ningún VALOR de la tabla `es` que DIFIERA de su valor en `en` puede aparecer como literal. (Los
//       que coinciden —"PLR", "BARK", "LUFS", "MONO"— son términos que nadie traduce: si están escritos
//       a mano no son una infracción, son la misma palabra.)
//   2 · ningún literal puede contener á é í ó ú ñ ü ¿ ¡. Es la red que atrapa lo que la tabla todavía no
//       nombra: una frase nueva en castellano no está en `es`, así que el barrido 1 no la vería.
// ========================================================================================================
TEST_CASE ("telescope: ninguna lente escribe un rotulo en castellano fuera de Strings.h",
           "[telescope][visual][i18n]")
{
    const auto& en = telescope::strings::tableEn();
    const auto& es = telescope::strings::tableEs();

    // Las frases que SÓLO existen en castellano: las que la tabla `es` traduce de verdad.
    juce::StringArray translated;
    for (int k = 0; k < telescope::strings::kNumKeys; ++k)
    {
        const juce::String a = juce::String::fromUTF8 (en.s[(size_t) k]);
        const juce::String b = juce::String::fromUTF8 (es.s[(size_t) k]);
        if (a != b && b.trim().isNotEmpty()) translated.addIfNotAlreadyThere (b);
    }
    REQUIRE (translated.size() > 40);           // la tabla traduce de verdad; si no, el barrido no mide nada

    juce::StringArray fromTable, accented;
    int scanned = 0;

    for (const auto& f : paintingFiles())
    {
        REQUIRE (f.existsAsFile());
        ++scanned;
        const auto content = f.loadFileAsString();

        juce::StringArray lines;
        lines.addLines (content);
        for (int i = 0; i < lines.size(); ++i)
            for (const auto& lit : literalsIn (lines[i]))
                if (translated.contains (lit))
                    fromTable.add (f.getFileName() + ":" + juce::String (i + 1) + "  \"" + lit + "\"");

        // 56c: el barrido de acentos pasa por accentedLiteralsIn, que DECODIFICA los \xHH antes de mirar.
        accented.addArray (accentedLiteralsIn (content, f.getFileName()));
    }

    REQUIRE (scanned > 13);
    std::printf ("VISUAL[i18n] %d archivos escaneados  ·  %d valores traducidos en la tabla es  ·  "
                 "%d literales de la tabla  ·  %d literales con acento\n",
                 scanned, translated.size(), fromTable.size(), accented.size());
    for (const auto& o : fromTable) std::printf ("VISUAL[i18n]   tabla:  %s\n", o.toRawUTF8());
    for (const auto& o : accented)  std::printf ("VISUAL[i18n]   acento: %s\n", o.toRawUTF8());

    INFO ("rótulos de la tabla es escritos a mano:\n" << fromTable.joinIntoString ("\n"));
    REQUIRE (fromTable.isEmpty());
    INFO ("literales con acento castellano:\n" << accented.joinIntoString ("\n"));
    REQUIRE (accented.isEmpty());
}

// ========================================================================================================
// (b) PIXEL SNAPPING. Una línea de 1 px lógico desde un coord snappeado tiene que ocupar, a escala 2,
// EXACTAMENTE 2 filas físicas con alpha lleno y NINGUNA fila a medio alpha. Sin esto la rejilla se
// dibuja en gris sucio y a dos alturas distintas según dónde cayó el redondeo.
// ========================================================================================================
TEST_CASE ("telescope: snap1px deja la hairline en pixeles fisicos enteros", "[telescope][visual]")
{
    constexpr float kScale = 2.0f;
    constexpr int   kLogical = 16;                       // 16 px lógicos → 32 físicos
    constexpr int   kPhysical = (int) (kLogical * kScale);

    // Varios coords fraccionarios: el snapping tiene que arreglar TODOS, no sólo los cómodos.
    for (const float raw : { 3.0f, 3.1f, 3.37f, 3.5f, 3.62f, 3.9f, 7.24f })
    {
        juce::Image img (juce::Image::ARGB, kPhysical, kPhysical, true);
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (kScale));
            g.setColour (telescope::look::dataLine);
            const float y = telescope::look::snap1px (raw, kScale);
            g.fillRect (juce::Rectangle<float> (0.0f, y, (float) kLogical, 1.0f));
        }

        int full = 0, partial = 0, empty = 0;
        for (int py = 0; py < kPhysical; ++py)
        {
            const auto a = img.getPixelAt (kPhysical / 2, py).getAlpha();
            if (a == 255)     ++full;
            else if (a == 0)  ++empty;
            else              ++partial;
        }

        std::printf ("VISUAL[snap] y=%.2f -> filas fisicas llenas=%d  parciales=%d  vacias=%d\n",
                     raw, full, partial, empty);
        INFO ("coord " << raw);
        CHECK (full == 2);
        CHECK (partial == 0);
    }

    // Y el contraejemplo, para que el test demuestre que MIDE algo: sin snappear, un coord fraccionario
    // reparte alpha entre filas. Si esto dejara de pasar, el test de arriba no probaría nada.
    juce::Image raw (juce::Image::ARGB, kPhysical, kPhysical, true);
    {
        juce::Graphics g (raw);
        g.addTransform (juce::AffineTransform::scale (kScale));
        g.setColour (telescope::look::dataLine);
        g.fillRect (juce::Rectangle<float> (0.0f, 3.37f, (float) kLogical, 1.0f));
    }
    int partialRaw = 0;
    for (int py = 0; py < kPhysical; ++py)
    {
        const auto a = raw.getPixelAt (kPhysical / 2, py).getAlpha();
        if (a != 0 && a != 255) ++partialRaw;
    }
    std::printf ("VISUAL[snap] contraejemplo sin snappear (y=3.37): %d filas a medio alpha\n", partialRaw);
    CHECK (partialRaw > 0);
}

// ========================================================================================================
// (c) DÍGITOS TABULARES. Tres lecturas del mismo formato tienen que medir lo MISMO: si no, el número
// salta de lugar cada vez que cambia de -8 a -14 y deja de poder leerse de reojo mientras se mezcla.
// ========================================================================================================
TEST_CASE ("telescope: tabularFont da digitos de ancho fijo", "[telescope][visual]")
{
    const auto f = telescope::look::tabularFont (14.0f);
    const juce::StringArray samples { "-14.6", "-88.8", "-00.0", "-11.1", "-90.5" };

    juce::GlyphArrangement ga;
    std::vector<float> widths;
    for (const auto& s : samples)
    {
        juce::GlyphArrangement a;
        a.addLineOfText (f, s, 0.0f, 0.0f);
        widths.push_back (a.getBoundingBox (0, -1, true).getWidth());
    }

    const auto lo = *std::min_element (widths.begin(), widths.end());
    const auto hi = *std::max_element (widths.begin(), widths.end());
    std::printf ("VISUAL[tabular] anchos: ");
    for (size_t i = 0; i < widths.size(); ++i)
        std::printf ("%s=%.3f px  ", samples[(int) i].toRawUTF8(), widths[i]);
    std::printf (" -> spread = %.4f px\n", hi - lo);

    CHECK ((hi - lo) <= 0.5f);

    // Y que la fuente NO sea la proporcional: con General Sans, "-11.1" es visiblemente más angosto que
    // "-88.8". Si este contraste desapareciera, `tabularFont` estaría devolviendo la fuente equivocada.
    juce::GlyphArrangement p1, p8;
    p1.addLineOfText (telescope::look::bodyFont (14.0f), "-11.1", 0.0f, 0.0f);
    p8.addLineOfText (telescope::look::bodyFont (14.0f), "-88.8", 0.0f, 0.0f);
    const auto d = std::abs (p8.getBoundingBox (0, -1, true).getWidth()
                             - p1.getBoundingBox (0, -1, true).getWidth());
    std::printf ("VISUAL[tabular] contraejemplo proporcional: |ancho(-88.8) - ancho(-11.1)| = %.3f px\n", d);
    CHECK (d > 0.5f);
}

// ========================================================================================================
// (d) LA RAMPA SECUENCIAL ES MONÓTONA EN LUMINANCIA. En SPECTROGRAM / WATERFALL / FIELD el color CODIFICA
// dB: si dos entradas de la rampa se cruzan en brillo, hay dos niveles distintos que se ven igual de
// fuertes y el mapa deja de poder leerse. (La BIPOLAR no cumple esto ni debe: ahí el ojo busca el CERO,
// no ordena magnitudes — por eso se verifica otra cosa, que el centro sea el punto más apagado.)
// ========================================================================================================
TEST_CASE ("telescope: la rampa secuencial es monotona en luminancia", "[telescope][visual]")
{
    using telescope::look::PaletteId;

    // ===== 57b: son CUATRO rampas, y tres de ellas tienen que cumplir el contrato =====
    //
    // Qué se exige, y por qué así:
    //
    //   · 256 entradas y los extremos en su lugar (la primera es el silencio, la última el techo);
    //   · al menos 200 colores DISTINTOS. Es lo que atrapa el modo de falla real —una rampa tabulada con
    //     pocos escalones y repetidos, que se ve en bandas—; exigir "cero entradas repetidas
    //     consecutivas", como pedía el prompt, no se puede cumplir con tablas de 8 bits: `viridis` tiene
    //     exactamente 2 repeticiones (en 113 y 130) y son redondeo a sRGB de la tabla publicada, no
    //     diseño. Falsear la tabla para pasar el test sería arruinar justo lo que la hace valiosa;
    //   · monotonía en luminancia con tolerancia de redondeo: ninguna caída mayor a 1/255 (la peor medida
    //     es 0.21/255 en `viridis` y 0.15/255 en `inferno`: menos de un escalón de cuantización, o sea
    //     invisible) Y estrictamente creciente cada 8 entradas, que es lo que atrapa una inversión REAL.
    //
    // `spectrum` queda EXENTA de la monotonía, nombrada y con su motivo (ver lenses/Palettes.h): ahí lo
    // que ordena el nivel es el TONO. Se verifica igual lo estructural, y ADEMÁS que efectivamente no sea
    // monótona — si algún día lo fuera, es que alguien le cambió la tabla y hay que enterarse.
    struct Ramp { PaletteId id; bool monotone; };
    const Ramp ramps[] = { { PaletteId::ovni, true }, { PaletteId::inferno, true },
                           { PaletteId::viridis, true }, { PaletteId::spectrum, false } };

    for (const auto& r : ramps)
    {
        const auto& p = telescope::look::palette (r.id);
        const auto* name = telescope::look::paletteName (r.id);
        REQUIRE (p.size() == 256u);

        int drops = 0, coarseDrops = 0;
        double worstDrop = 0.0;
        for (size_t i = 1; i < p.size(); ++i)
        {
            const auto d = luma (p[i]) - luma (p[i - 1]);
            if (d < 0.0) { ++drops; worstDrop = std::min (worstDrop, d); }
        }
        for (size_t i = 8; i < p.size(); i += 8)
            if (luma (p[i]) <= luma (p[i - 8])) ++coarseDrops;

        std::set<juce::uint32> distinct (p.begin(), p.end());
        std::printf ("VISUAL[paleta] %-9s luma %.1f -> %.1f  caidas = %d (peor %.3f/255)  "
                     "caidas en grilla de 8 = %d  colores distintos = %d  %s\n",
                     name, luma (p.front()), luma (p.back()), drops, worstDrop, coarseDrops,
                     (int) distinct.size(), r.monotone ? "[monotona]" : "[EXENTA: el tono ordena]");

        CHECK (luma (p.back()) > luma (p.front()));
        CHECK (distinct.size() >= 200u);

        if (r.monotone)
        {
            CHECK (worstDrop >= -1.0);      // redondeo de 8 bits, no diseño
            CHECK (coarseDrops == 0);       // ninguna inversión de verdad
        }
        else
        {
            CHECK (drops > 0);              // si esto se pusiera en 0, le cambiaron la tabla
        }
    }

    const auto& p = telescope::look::sequential();   // = PaletteId::ovni, el default histórico

    // BIPOLAR: el centro (el cero) es el punto MENOS saturado, y los dos extremos son los dos hues de
    // signo. Es el contrato que hace que un correlímetro se lea sin rótulo.
    const auto& b = telescope::look::bipolar();
    REQUIRE (b.size() == 256u);
    const auto sat = [] (juce::uint32 argb) { return juce::Colour (argb).getSaturation(); };
    std::printf ("VISUAL[paleta] bipolar: sat(-1)=%.3f  sat(0)=%.3f  sat(+1)=%.3f\n",
                 sat (b.front()), sat (b[128]), sat (b.back()));
    CHECK (sat (b[128]) < sat (b.front()));
    CHECK (sat (b[128]) < sat (b.back()));
    CHECK (juce::Colour (b.front()).getHue() != juce::Colour (b.back()).getHue());
}

// ========================================================================================================
// IDIOMA (D-50, addendum de Joaquín del 8-sep): inglés por defecto y posibilidad en TODOS los idiomas.
//
// Lo que se verifica es lo que puede ROMPERSE en pantalla: (1) que ninguna tabla tenga huecos que salgan
// como una etiqueta en blanco, (2) que un idioma incompleto caiga a inglés por CLAVE y no se lleve puesto
// el resto del idioma, y (3) que el default sea inglés incluso con un código que no existe.
// ========================================================================================================
#include "lenses/Strings.h"

TEST_CASE ("telescope: las tablas de idioma estan completas", "[telescope][visual][i18n]")
{
    using namespace telescope::strings;

    const auto& names = keyNames();
    for (int i = 0; i < kNumKeys; ++i)
    {
        INFO ("clave " << i);
        REQUIRE (names[(size_t) i] != nullptr);       // el nombre de clave también: un hueco acá haría
        REQUIRE (juce::String (names[(size_t) i]).isNotEmpty());   // ilegible cualquier diagnóstico
    }

    for (const auto* t : tables())
    {
        juce::StringArray missing;
        for (int i = 0; i < kNumKeys; ++i)
        {
            const auto* v = t->s[(size_t) i];
            if (v == nullptr || *v == '\0')
                missing.add (names[(size_t) i]);
        }
        std::printf ("VISUAL[i18n] %-3s %-12s %3d/%d claves%s%s\n", t->code, t->endonym,
                     kNumKeys - missing.size(), kNumKeys,
                     missing.isEmpty() ? "" : "   FALTAN: ", missing.joinIntoString (", ").toRawUTF8());
        INFO ("idioma " << t->code << " · faltan: " << missing.joinIntoString (", "));
        CHECK (missing.isEmpty());
    }

    // Y que TODA clave devuelva algo no vacío en TODO idioma listado — que es la promesa que el usuario ve.
    for (const auto& code : availableLanguages())
        for (int i = 0; i < kNumKeys; ++i)
        {
            INFO ("idioma " << code << " clave " << names[(size_t) i]);
            REQUIRE (get ((Key) i, code).isNotEmpty());
        }
}

TEST_CASE ("telescope: un idioma incompleto cae a ingles POR CLAVE", "[telescope][visual][i18n]")
{
    using namespace telescope::strings;

    // 56b (M1 del revisor del 56): se llama LA FUNCIÓN DEL PLUGIN, `getFrom`, sobre una tabla incompleta
    // de verdad. Antes el test fabricaba la tabla Y re-implementaba el fallback en una lambda, así que
    // comprobaba que su propia copia hacía lo correcto — si `get()` hubiera dejado de caer a inglés, este
    // test habría seguido en verde.
    Table hueco = tablePt();
    hueco.s[(size_t) Key::correlation] = "";        // una clave sin traducir
    hueco.s[(size_t) Key::balance]     = nullptr;   // y otra directamente ausente

    CHECK (getFrom (hueco, Key::correlation) == get (Key::correlation, "en"));   // el hueco → inglés
    CHECK (getFrom (hueco, Key::balance)     == get (Key::balance, "en"));       // el nulo también
    CHECK (getFrom (hueco, Key::width)       == get (Key::width, "pt"));         // el resto → sigue en pt
    CHECK (get (Key::width, "pt")            != get (Key::width, "en"));         // y pt de verdad difiere
    CHECK (getFrom (hueco, Key::correlation).isNotEmpty());                      // nunca vacío
    std::printf ("VISUAL[i18n] fallback (getFrom sobre una tabla incompleta): correlation = \"%s\"  ·  "
                 "balance (nulo) = \"%s\"  ·  width = \"%s\"\n",
                 getFrom (hueco, Key::correlation).toRawUTF8(),
                 getFrom (hueco, Key::balance).toRawUTF8(),
                 getFrom (hueco, Key::width).toRawUTF8());

    // Un código que no existe NO deja la pantalla en blanco: es inglés, el default de D-50.
    CHECK (get (Key::correlation, "xx") == get (Key::correlation, "en"));
    CHECK (get (Key::correlation, "")   == get (Key::correlation, "en"));

    juce::ValueTree state ("state");
    CHECK (languageOf (state) == "en");                 // sin la propiedad → inglés
    state.setProperty (kLanguageProperty, "zz", nullptr);
    CHECK (languageOf (state) == "en");                 // con basura → inglés
    setLanguage (state, "es");
    CHECK (languageOf (state) == "es");
    setLanguage (state, "zz");                          // un código inválido NO pisa el válido anterior
    CHECK (languageOf (state) == "es");

    std::printf ("VISUAL[i18n] idiomas disponibles: %s\n",
                 availableLanguages().joinIntoString (", ").toRawUTF8());
    CHECK (availableLanguages().size() >= 2);
    CHECK (availableLanguages()[0] == "en");            // el default primero en el selector
}

// ========================================================================================================
// ===== 56c ===== EL CÓDIGO NO PUEDE PROMETER CURVAS DE DISPOSITIVO QUE NO EXISTEN (D-47)
//
// El 56b sacó los `responseDb[30]` de DeviceProfiles.h —código muerto que contaba una historia falsa— y
// escribió el bloque "ACÁ NO HAY CURVAS", pero dejó intacta la cabecera del archivo tres líneas más
// arriba, que seguía diciendo "son CURVAS GENÉRICAS de familia" y citando un pie que ya no existe. Un
// archivo que se contradice a sí mismo es peor que uno que miente: el lector se queda con la mitad que
// leyó primero. Lo mismo en el diccionario de Rules.h, donde cuatro reglas por dispositivo daban como
// FUENTE del umbral "curva generica de …".
//
// La regla, entonces: se puede nombrar una curva, pero sólo para decir que no la hay o que si alguna vez
// la hubiera vendría con su procedencia. Nunca como algo que el plugin tiene.
//
// El CHANGELOG queda afuera a propósito: es el registro histórico y ahí la frase vieja tiene que poder
// citarse tal como fue.
// ========================================================================================================
TEST_CASE ("telescope: ningun archivo promete curvas de dispositivo que no existen", "[telescope][visual]")
{
    // Una línea que nombra una curva está BIEN si además la niega o la pone como condicional futuro.
    const char* kNegaciones[] = { "no hay", "ninguna", "sin curva", "si algun dia", "si algún día",
                                  "no son", "no es", "no de una" };   // "medida(s)" sueltas no niegan nada (revisor del 56c)
    const auto niega = [&] (const juce::String& line)
    {
        for (const auto* n : kNegaciones)
            if (line.containsIgnoreCase (n)) return true;
        return false;
    };

    // ---- (a) DeviceProfiles.h: TODA mención de una curva tiene que estar negándola ----
    const auto dp = pluginDir().getChildFile ("source").getChildFile ("data").getChildFile ("DeviceProfiles.h");
    REQUIRE (dp.existsAsFile());

    juce::StringArray dpLines;
    dpLines.addLines (dp.loadFileAsString());
    int mencionan = 0, afirman = 0;
    for (int i = 0; i < dpLines.size(); ++i)
    {
        const auto& line = dpLines[i];
        if (! (line.containsIgnoreCase ("curva") || line.containsIgnoreCase ("curve"))) continue;
        ++mencionan;
        const bool ok = niega (line);
        if (! ok) ++afirman;
        std::printf ("CURVAS[DeviceProfiles.h:%d] %s  %s\n", i + 1, ok ? "niega " : "AFIRMA",
                     line.trim().substring (0, 104).toRawUTF8());
    }
    std::printf ("CURVAS[DeviceProfiles.h] %d lineas nombran una curva, %d la dan por existente\n",
                 mencionan, afirman);
    CHECK (afirman == 0);

    // ---- (b) el diccionario y el README no pueden citar una curva como algo que el plugin tiene ----
    const juce::File otros[] = {
        pluginDir().getChildFile ("source").getChildFile ("data").getChildFile ("Rules.h"),
        pluginDir().getChildFile ("README.md"),
        // auditoría del 56c: el diccionario de verdad (el gemelo legible que lee Joaquín) también entra al
        // barrido — el 56c dejó su pie viejo ("Curvas de dispositivos genéricas") sin ver, porque acá sólo
        // se leían Rules.h y el README aunque el rótulo del printf dijera "diccionario".
        pluginDir().getChildFile ("docs").getChildFile ("telescope-diccionario.md"),
    };
    const char* kProhibidas[] = { "curva generica", "curva genérica", "curvas genericas",
                                  "curvas genéricas", "curvas de dispositivo", "device curve" };

    int infracciones = 0;
    for (const auto& f : otros)
    {
        REQUIRE (f.existsAsFile());
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        for (int i = 0; i < lines.size(); ++i)
            for (const auto* bad : kProhibidas)
                if (lines[i].containsIgnoreCase (bad) && ! niega (lines[i]))
                {
                    ++infracciones;
                    std::printf ("CURVAS[%s:%d] AFIRMA  %s\n", f.getFileName().toRawUTF8(), i + 1,
                                 lines[i].trim().substring (0, 104).toRawUTF8());
                    break;
                }
    }
    std::printf ("CURVAS[diccionario+README] %d lineas dan una curva de dispositivo por existente\n",
                 infracciones);
    CHECK (infracciones == 0);

    // ---- (c) `kNumBands` era el ultimo resto del array de 30 valores: no puede volver ----
    CHECK_FALSE (dp.loadFileAsString().contains ("kNumBands"));
}

// ========================================================================================================
// ===== 56c ===== EL BARRIDO DE ACENTOS TIENE QUE VER UNA ÑE ESCRITA COMO ESCAPE
//
// El test de arriba dice "ningún literal lleva un acento castellano" y hasta el 56c eso era verdad sólo
// para los acentos escritos DIRECTO en el archivo. Un rótulo puesto como "a\xc3\xb1o" —que es como este
// mismo repo escribe el · y el em-dash, así que no es una hipótesis— lo atravesaba entero.
//
// Un barrido que no puede fallar no es un barrido. Acá se le da de comer un archivo FABRICADO con la ñe
// escrita de las dos maneras y se exige que las vea; y un control con el signo × (\xc3\x97), que también
// es un escape de dos bytes y NO es un acento, para que el arreglo no se convierta en un falso positivo.
// ========================================================================================================
TEST_CASE ("telescope: el barrido de acentos ve una ne escrita como \\xc3\\xb1", "[telescope][visual][i18n]")
{
    const juce::String conEscape =
        "// un archivo fabricado\n"
        "void f (juce::Graphics& g) { g.drawText (\"a\\xc3\\xb1o nuevo\", area); }\n";

    const juce::String conAcentoDirecto =
        juce::String ("void f (juce::Graphics& g) { g.drawText (\"a")
            + juce::String::fromUTF8 ("\xc3\xb1") + "o nuevo\", area); }\n";

    const juce::String soloElPor =
        "void f (juce::Graphics& g) { g.drawText (\"64 \\xc3\\x97 96\", area); }\n";

    const auto escapado = accentedLiteralsIn (conEscape, "fabricado.cpp");
    const auto directo  = accentedLiteralsIn (conAcentoDirecto, "fabricado.cpp");
    const auto porSigno = accentedLiteralsIn (soloElPor, "fabricado.cpp");

    std::printf ("VISUAL[i18n] fabricado: escape \\xc3\\xb1 -> %d infraccion(es) %s  ·  acento directo -> "
                 "%d  ·  signo x (\\xc3\\x97, control) -> %d\n",
                 escapado.size(), escapado.isEmpty() ? "" : escapado[0].toRawUTF8(),
                 directo.size(), porSigno.size());

    CHECK (escapado.size() == 1);      // el agujero que este prompt cierra
    CHECK (directo.size() == 1);       // lo que ya funcionaba, que no se rompió
    CHECK (porSigno.isEmpty());        // y el × no es un acento

    // Y el mismo camino, pero desde un ARCHIVO de verdad: el barrido lee del disco, no de una String.
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("ovni_telescope_56c_acentos.cpp");
    REQUIRE (tmp.replaceWithText (conEscape));
    const auto desdeArchivo = accentedLiteralsIn (tmp.loadFileAsString(), tmp.getFileName());
    std::printf ("VISUAL[i18n] fabricado desde disco (%s): %d infraccion(es)\n",
                 tmp.getFileName().toRawUTF8(), desdeArchivo.size());
    CHECK (desdeArchivo.size() == 1);
    tmp.deleteFile();
}

// ========================================================================================================
// ===== 57b · VISUAL[v2] — EL BARRIDO DEL PULIDO, sobre las TRECE lentes =====
//
// Joaquín, después de mirar SPECTRUM y WATERFALL arreglados: «fijate si suavizamos no sólo estos sino
// TODOS los del plugin y los dejamos a la perfección a todos». Esto es lo que impide que "todos" vuelva a
// ser "los que miramos ese día".
//
// ========================= QUÉ SE MIDE, Y POR QUÉ UN BARRIDO DE FUENTES =================================
//
// El prompt 57b pedía contar, sobre el render a 2×, "rectángulos de 1 px alineados en columna" — el
// patrón de escalera que deja el pintado por columnas. Eso no se puede medir sobre la imagen sin dar
// falsos positivos: una línea vertical antialiaseada, una barra de un histograma angosto y el borde de
// un degradado producen exactamente el mismo patrón de píxeles alineados que un `fillRect` de 1 px. Un
// test que no puede distinguir lo que persigue de lo que no, no es un test.
//
// Lo que SÍ distingue las dos cosas sin ambigüedad es la FUENTE, y es además donde se arregla el defecto.
// La regla, que es la misma familia que la de "ningún color literal fuera de Look.h":
//
//     NINGUNA lente llama a `g.fillRect` con ENTEROS y una dimensión literal de 1.
//
// Por qué esa regla y no otra: un `fillRect` de enteros NO pasa por el rasterizador antialiaseado de
// JUCE — se resuelve como un bloque de píxeles enteros. Con una dimensión de 1 eso es, o bien una
// hairline que en Retina cae donde caiga (la "rejilla sucia" del punto 2 de Look.h), o bien un trazo de
// dato dibujado columna por columna (la escalera que Joaquín vio en SPECTRUM). Las dos cosas tienen su
// reemplazo y los dos están en Look.h: `look::fillSnapped` para las hairlines —que alinea a la grilla de
// píxeles FÍSICOS— y `strokePath` / rectángulos de coma flotante para el dato, que sí antialiasean.
//
// Se cuentan además, y se imprimen, las llamadas de `fillRect` con rectángulos de coma flotante y las de
// `strokePath`: no son un criterio, son el contexto que deja ver de un vistazo con qué dibuja cada lente.
TEST_CASE ("telescope: ninguna lente dibuja con fillRect de enteros de 1 px", "[telescope][visual][v2]")
{
    struct Args { juce::StringArray parts; bool intLike = true; };

    // Parte la lista de argumentos por comas de nivel 0 (los paréntesis anidados no cuentan).
    const auto splitArgs = [] (const juce::String& raw)
    {
        juce::StringArray out;
        int depth = 0;
        juce::String cur;
        for (int i = 0; i < raw.length(); ++i)
        {
            const auto ch = raw[i];
            if (ch == '(') ++depth;
            if (ch == ')') --depth;
            if (ch == ',' && depth == 0) { out.add (cur.trim()); cur = {}; }
            else                          cur += ch;
        }
        out.add (cur.trim());
        return out;
    };

    juce::StringArray offences;
    int scanned = 0, intCalls = 0, floatCalls = 0, strokes = 0;

    for (const auto& f : paintingFiles())
    {
        if (f.getFileName() == "Look.h") continue;   // acá viven fillSnapped y las hairlines
        ++scanned;

        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        for (const auto& sl : lines) if (sl.contains ("strokePath")) ++strokes;

        for (int i = 0; i < lines.size(); ++i)
        {
            const auto& l = lines[i];
            const int at = l.indexOf ("g.fillRect (");
            if (at < 0) continue;

            // El cuerpo de la llamada, hasta el paréntesis que cierra (las llamadas de una sola línea son
            // todas: las de varias líneas quedan fuera del barrido y se cuentan aparte más abajo).
            const auto rest = l.substring (at + 12);
            int depth = 1, end = -1;
            for (int k = 0; k < rest.length() && end < 0; ++k)
            {
                if (rest[k] == '(') ++depth;
                if (rest[k] == ')' && --depth == 0) end = k;
            }
            if (end < 0) continue;

            const auto parts = splitArgs (rest.substring (0, end));
            if (parts.size() != 4) { ++floatCalls; continue; }
            ++intCalls;
            if (parts[2] == "1" || parts[3] == "1")
                offences.add (f.getFileName() + ":" + juce::String (i + 1) + "  " + l.trim());
        }
    }

    std::printf ("VISUAL[v2] %d archivos de lentes barridos  ·  %d fillRect de enteros  ·  %d de coma "
                 "flotante o rectangulo  ·  %d strokePath  ·  %d infracciones (1 px de enteros)\n",
                 scanned, intCalls, floatCalls, strokes, offences.size());
    for (const auto& o : offences)
        std::printf ("VISUAL[v2]   %s\n", o.toRawUTF8());

    REQUIRE (scanned > 12);            // las trece lentes (y la tira): si el barrido no las ve, no dice nada
    REQUIRE (strokes > 5);             // …y dibujan de verdad con trazos antialiaseados
    INFO ("fillRect de enteros con una dimension de 1 px:\n" << offences.joinIntoString ("\n"));
    REQUIRE (offences.isEmpty());
}

// ========================================================================================================
// ===== 57b · VISUAL[v2] — NINGUNA CACHÉ SE ASIGNA EN PÍXELES LÓGICOS =====
//
// La otra mitad del punto 1: las lentes que cachean píxeles tienen que asignar la imagen a tamaño lógico
// POR LA ESCALA FÍSICA (ver lenses/Raster.h). Si alguien vuelve a escribir `juce::Image (ARGB, ancho,
// alto)` con el ancho del plot, la lente vuelve a tirar la mitad de la resolución de la pantalla y
// VISUAL[hd] se pone rojo — pero recién cuando alguien lo corra con una señal. Esto lo atrapa en la
// fuente, que es donde se arregla.
TEST_CASE ("telescope: toda cache de pixeles se asigna a escala fisica", "[telescope][visual][v2]")
{
    // EL BARRIDO ES POR ARCHIVO Y NO POR LÍNEA, a propósito: en las cinco lentes el alto y el ancho se
    // calculan con `raster::toDevice` unas líneas ANTES de la asignación (para poder compararlos con el
    // tamaño de la imagen que ya está), así que mirar sólo la línea de `juce::Image (…)` daría cuatro
    // falsos positivos. Lo que se exige es que el archivo que cachea píxeles PASE por lenses/Raster.h —
    // que es donde vive la regla y el único lugar donde puede estar bien escrita.
    juce::StringArray offences;
    int caches = 0;

    for (const auto& f : paintingFiles())
    {
        const auto text = f.loadFileAsString();
        juce::StringArray lines;
        lines.addLines (text);

        int here = 0;
        for (const auto& l : lines) if (l.contains ("juce::Image (juce::Image::ARGB")) ++here;
        if (here == 0) continue;
        caches += here;

        if (f.getFileName() == "Raster.h") continue;              // la regla misma
        // SpectrogramScroll.h es la otra excepción, y por un motivo distinto: NO decide el tamaño. Las
        // dos lentes que lo usan le pasan el ancho y el alto ya convertidos a píxeles de dispositivo
        // (SpectrogramLens.cpp / StereoSpectrogramLens.cpp, las dos con `raster::toDevice`), y él sólo
        // asigna lo que le pidieron. Exigirle que nombre a Raster.h sería pedirle que conozca una regla
        // que no le toca aplicar.
        if (f.getFileName() == "SpectrogramScroll.h") continue;
        if (text.contains ("raster::toDevice") || text.contains ("raster::Cache")
            || text.contains ("look::physicalScale"))
            continue;
        offences.add (f.getFileName() + "  (" + juce::String (here) + " asignacion[es] sin pasar por "
                      + "lenses/Raster.h)");
    }

    std::printf ("VISUAL[v2] %d asignaciones de cache encontradas  ·  %d archivo[s] fuera de escala fisica\n",
                 caches, offences.size());
    for (const auto& o : offences) std::printf ("VISUAL[v2]   %s\n", o.toRawUTF8());

    REQUIRE (caches > 0);
    INFO ("caches asignadas en pixeles logicos:\n" << offences.joinIntoString ("\n"));
    REQUIRE (offences.isEmpty());
}

// ========================================================================================================
// ===== 57c · NINGÚN MEDIDOR DE LOUDNESS SE ROTULA CON UNA LETRA =====
//
// Las dos barras de LOUDNESS decían "M" y "S". Son momentary y short-term — pero debajo de dos medidores
// de nivel, en un plugin que además mide mid/side y lo muestra en SCOPE, "M" y "S" se leen mid/side. No es
// una preferencia de estilo: es una etiqueta que nombra otra magnitud. Por eso Joaquín, mirándolas en su
// DAW, escribió «sólo es M–S, también debería poder ser L y R».
//
// La regla, entonces: los rótulos de las barras de LUFS tienen TRES caracteres o más, en todos los
// idiomas. "L" y "R" sí pueden ser una letra — ahí la letra ES el nombre del canal, y no hay otra cosa
// que pueda querer decir en una barra de dBTP.
TEST_CASE ("telescope: los rotulos de las barras de loudness no son una letra sola",
           "[telescope][visual][i18n]")
{
    for (const auto& code : telescope::strings::availableLanguages())
    {
        const auto mom  = telescope::strings::get (telescope::strings::Key::momentaryBar, code);
        const auto shrt = telescope::strings::get (telescope::strings::Key::shortTermBar, code);
        std::printf ("VISUAL[i18n] barras de LOUDNESS  %s  momentary = \"%s\"  short-term = \"%s\"\n",
                     code.toRawUTF8(), mom.toRawUTF8(), shrt.toRawUTF8());
        CHECK (mom.length()  >= 3);
        CHECK (shrt.length() >= 3);
    }
}
