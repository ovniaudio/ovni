#pragma once
#include <functional>
#include "lenses/LensIds.h"
#include "lenses/Look.h"
#include "lenses/Strings.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

// ========================================================================================================
// LensStrip — la tira de las 13 lentes, a la izquierda del cuerpo. Las construidas se pueden elegir; las
// que todavía no existen se dibujan APAGADAS con su nombre y su punto, y nada más: sin "coming soon", sin
// tooltips prometiendo cosas. El catálogo se lee del spec §2 y el orden es el del índice del parámetro.
// ========================================================================================================
namespace telescope
{
class LensStrip : public juce::Component
{
public:
    LensStrip()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int)> onSelect;                  // sólo se llama para lentes construidas
    std::function<void (const juce::String&)> onLanguage;   // 56b: el código elegido en el chip del pie
    std::function<void()> onTheme;                          // F2 de la 0.2: el clic en la fila del tema

    // 56: el idioma de los nombres. Lo fija quien tenga el ValueTree a mano; por defecto es inglés (D-50).
    void setLanguage (const juce::String& code) { if (code != lang) { lang = code; repaint(); } }
    const juce::String& language() const noexcept { return lang; }

    // ================== EL SELECTOR DE IDIOMA (56b, D-50) ==================
    // UNO SOLO, y acá. Hasta el 56 el único control estaba adentro de VERDICT: para leer SPECTRUM en
    // castellano había que ir a otra lente, cambiarlo, y volver. El idioma no es un ajuste de VERDICT,
    // es del plugin — y la tira es lo único que está siempre a la vista.
    //
    // Muestra el ENDÓNIMO (Español, Deutsch, …) y no el código: alguien que no lee inglés tampoco sabe
    // que su idioma se llama "de". Un clic avanza al siguiente de strings::availableLanguages(), así
    // que agregar una tabla lo suma acá sin tocar una línea de esta clase.
    static constexpr int kLanguageRowH = 26;

    juce::Rectangle<int> languageRow() const
    {
        return getLocalBounds().removeFromBottom (kLanguageRowH).reduced (6, 3);
    }

    // ================== EL TEMA (F2 de la 0.2) ==================
    // Arriba del idioma, con el mismo peso: los dos son ajustes del plugin, no del análisis. Un clic
    // alterna oscuro / claro; quién guarda la preferencia lo decide el editor (ThemePreference.h).
    juce::Rectangle<int> themeRow() const
    {
        auto b = getLocalBounds();
        b.removeFromBottom (kLanguageRowH);
        return b.removeFromBottom (kLanguageRowH).reduced (6, 3);
    }

    void cycleLanguage()
    {
        const auto codes = strings::availableLanguages();
        if (codes.isEmpty()) return;
        const int next = (juce::jmax (0, codes.indexOf (lang)) + 1) % codes.size();
        if (onLanguage) onLanguage (codes[next]);
    }

    // Los nombres LOCALIZADOS. `kLensNames` sigue siendo el contrato (el índice del parámetro y lo que ve
    // el host); esto es sólo lo que se muestra. Un nombre que se traduce no puede ser el mismo que
    // identifica un preset.
    static strings::Key keyFor (int i) noexcept
    {
        return (strings::Key) ((int) strings::Key::lensLoudness + juce::jlimit (0, kNumLenses - 1, i));
    }

    // ================== LA GEOMETRÍA DE LAS FILAS (56c) ==================
    // UNA sola base para las dos cuentas. Hasta el 56b `paint()` repartía las filas con
    // `jmax (kNumLenses, alto - kLanguageRowH)` y `rowAt()` —el que decide qué lente eligió el clic— con
    // `jmax (0, alto - kLanguageRowH)`. Dos pisos distintos para la misma división: donde no coinciden, el
    // usuario aprieta una lente y se abre otra. En los tamaños reales las dos daban el mismo número y por
    // eso nadie lo vio; el piso está para que la fila nunca mida menos de un píxel, y tiene que valer para
    // las dos o no vale para ninguna.
    //
    // Públicas porque el test tiene que apretar donde la fila ESTÁ dibujada: si copiara la cuenta,
    // mediría su propia copia.
    int listHeight() const noexcept { return juce::jmax (kNumLenses, getHeight() - 2 * kLanguageRowH); }   // tema + idioma

    juce::Rectangle<float> rowBounds (int i) const noexcept
    {
        const float rowH = (float) listHeight() / (float) kNumLenses;
        return { 0.0f, (float) i * rowH, (float) getWidth(), rowH };
    }

    int rowAt (int y) const noexcept
    {
        const int i = y * kNumLenses / listHeight();
        return (i >= 0 && i < kNumLenses) ? i : -1;
    }

    // ================== UN NOMBRE LARGO, EN DOS RENGLONES (F2b de la 0.2) ==================
    // Con el piso de 11 px (F2) los nombres largos dejaron de entrar en una línea de la tira: en castellano,
    // a M, «CORRELACIÓN POR BAN…» y «ESPECTROGRAMA ESTÉ…»; en alemán «STEREO-SPEKTROGRAMM», en italiano
    // «BILANCIAMENTO TONALE». La tira no se ensancha (movería las lentes) ni se achica la letra (es el piso):
    // un nombre que no entra se parte en DOS renglones, por el espacio —o después del guion— que deje el
    // renglón más largo lo más corto posible. Si ningún corte entra, sale en uno con elipsis, y [tira] lo ve.
    static juce::StringArray linesFor (const juce::String& name, const juce::Font& f, float width)
    {
        const auto w = [&f] (const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); };
        if (w (name) <= width) return { name };

        juce::String bestA, bestB;
        float best = width + 1.0f;
        for (int i = 1; i < name.length() - 1; ++i)
        {
            const auto c = name[i];
            if (c != ' ' && c != '-') continue;
            const auto a = c == ' ' ? name.substring (0, i) : name.substring (0, i + 1);
            const auto b = name.substring (i + 1);
            const float longest = juce::jmax (w (a), w (b));
            if (longest <= width && longest < best) { best = longest; bestA = a; bestB = b; }
        }
        if (bestA.isEmpty()) return { name };
        return { bestA, bestB };
    }

    void setBuilt (juce::uint32 mask)  { builtMask = mask; repaint(); }
    void setSelected (int index)       { if (index != selected) { selected = index; repaint(); } }

    bool isBuilt (int i) const noexcept { return (builtMask & (1u << i)) != 0u; }

    void paint (juce::Graphics& g) override
    {
        namespace th = telescope::look::tint;   // F2: el tema vigente (Look.h)
        const auto hue = th::green;
        const auto  langRow = languageRow();
        const auto  m = look::metricsFor (juce::jmax (760, getHeight()));   // 56: la tira escala con el alto

        g.setColour (th::surf.withAlpha (0.5f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (th::lineSoft);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 3.0f, 1.0f);

        for (int i = 0; i < kNumLenses; ++i)
        {
            const auto row   = rowBounds (i).reduced (6.0f, 1.0f);
            const bool built = isBuilt (i);
            const bool on    = built && i == selected;

            // ===== 56 ===== los tres estados de una fila tenían casi el mismo peso y la tira se leía como
            // una lista plana. Ahora: la ACTIVA lleva la barra de acento a la izquierda (el gesto de
            // "estás acá" que se lee de un metro), la construida-no-activa es texto legible, y la que
            // todavía no existe queda claramente apagada — sin prometer nada.
            if (on)
            {
                g.setColour (hue.withAlpha (0.16f));
                g.fillRoundedRectangle (row, m.radius);
                g.setColour (hue.withAlpha (0.55f));
                g.drawRoundedRectangle (row.reduced (0.5f), m.radius, 1.0f);
                // La barra de acento: 3 px pegados al borde izquierdo de la fila.
                g.setColour (hue);
                g.fillRoundedRectangle (row.getX() + 1.0f, row.getY() + 2.0f, 3.0f,
                                        juce::jmax (0.0f, row.getHeight() - 4.0f), 1.5f);
            }
            else if (built && i == hovered)
            {
                g.setColour (hue.withAlpha (th::state::hoverGlow));
                g.fillRoundedRectangle (row, m.radius);
            }

            // El punto: lleno si la lente existe, ANILLO VACÍO si todavía no. Lleno-contra-vacío se
            // distingue de reojo; dos rellenos con alphas distintos, no.
            const auto dot = juce::Rectangle<float> (row.getX() + 9.0f, row.getCentreY() - 2.5f, 5.0f, 5.0f);
            if (built)
            {
                g.setColour (on ? hue : hue.withAlpha (0.60f));
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (look::txtTertiary.withAlpha (0.55f));
                g.drawEllipse (dot, 1.0f);
            }

            g.setColour (on ? look::txtPrimary : (built ? look::txtSecondary
                                                        : look::txtTertiary.withAlpha (0.62f)));
            const auto font = look::label (on ? m.textSmall + 0.5f : m.textSmall);
            g.setFont (font);
            const juce::Rectangle<int> textBox (juce::roundToInt (row.getX() + 21.0f), juce::roundToInt (row.getY()),
                                                juce::roundToInt (row.getWidth() - 23.0f),
                                                juce::roundToInt (row.getHeight()));
            const auto lines = linesFor (strings::get (keyFor (i), lang), font, (float) textBox.getWidth());
            if (lines.size() == 1)
            {
                g.drawText (lines[0], textBox, juce::Justification::centredLeft, true);
            }
            else
            {
                const int lineH = (int) std::ceil (font.getHeight());
                auto block = textBox.withSizeKeepingCentre (textBox.getWidth(), 2 * lineH);
                g.drawText (lines[0], block.removeFromTop (lineH), juce::Justification::centredLeft, true);
                g.drawText (lines[1], block, juce::Justification::centredLeft, true);
            }
        }

        // ---- el chip de idioma, al pie de la tira ----
        // Pesa MENOS que una lente a propósito: es un ajuste, no una decimocuarta vista. Por eso va con el
        // color terciario, sin punto y sin barra de acento, separado por una hairline.
        const auto themeR = themeRow();
        g.setColour (th::lineSoft);
        look::fillSnapped (g, { (float) (themeR.getX()), (float) (themeR.getY() - 3), (float) (themeR.getWidth()), 1.0f });

        if (themeHovered)
        {
            g.setColour (hue.withAlpha (th::state::hoverGlow));
            g.fillRoundedRectangle (themeR.toFloat(), m.radius);
        }
        g.setColour (themeHovered ? look::txtSecondary : look::txtTertiary);
        g.setFont (look::label (m.textSmall - 0.5f));
        g.drawText (strings::get (strings::Key::theme, lang),
                    themeR.getX() + 15, themeR.getY(), themeR.getWidth() - 17, themeR.getHeight(),
                    juce::Justification::centredLeft, true);
        g.setColour (themeHovered ? look::txtPrimary : look::txtSecondary);
        g.setFont (look::label (m.textSmall));
        g.drawText (strings::get (look::theme() == look::Theme::light ? strings::Key::themeLight
                                                                       : strings::Key::themeDark, lang),
                    themeR.getX(), themeR.getY(), themeR.getWidth() - 4, themeR.getHeight(),
                    juce::Justification::centredRight, true);

        if (langHovered)
        {
            g.setColour (hue.withAlpha (th::state::hoverGlow));
            g.fillRoundedRectangle (langRow.toFloat(), m.radius);
        }
        g.setColour (langHovered ? look::txtSecondary : look::txtTertiary);
        g.setFont (look::label (m.textSmall - 0.5f));
        g.drawText (strings::get (strings::Key::language, lang),
                    langRow.getX() + 15, langRow.getY(), langRow.getWidth() - 17, langRow.getHeight(),
                    juce::Justification::centredLeft, true);
        g.setColour (langHovered ? look::txtPrimary : look::txtSecondary);
        g.setFont (look::label (m.textSmall));
        g.drawText (strings::endonymOf (lang), langRow.getX(), langRow.getY(),
                    langRow.getWidth() - 4, langRow.getHeight(), juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (languageRow().contains (e.getPosition())) { cycleLanguage(); return; }
        if (themeRow().contains (e.getPosition()))    { if (onTheme) onTheme(); return; }
        const int i = rowAt (e.y);
        if (i >= 0 && isBuilt (i) && onSelect) onSelect (i);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool overLang  = languageRow().contains (e.getPosition());
        const bool overTheme = themeRow().contains (e.getPosition());
        const int  i = (overLang || overTheme) ? -1 : rowAt (e.y);
        if (i != hovered || overLang != langHovered || overTheme != themeHovered)
        { hovered = i; langHovered = overLang; themeHovered = overTheme; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered != -1 || langHovered || themeHovered)
        { hovered = -1; langHovered = false; themeHovered = false; repaint(); }
    }

private:
    juce::String lang { strings::kDefaultLanguage };   // 56
    juce::uint32 builtMask = 1u;   // por ahora sólo LOUDNESS (bit 0)
    int  selected = 0;
    int  hovered  = -1;
    bool langHovered = false;
    bool themeHovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LensStrip)
};
}
