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
    int listHeight() const noexcept { return juce::jmax (kNumLenses, getHeight() - kLanguageRowH); }

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

    void setBuilt (juce::uint32 mask)  { builtMask = mask; repaint(); }
    void setSelected (int index)       { if (index != selected) { selected = index; repaint(); } }

    bool isBuilt (int i) const noexcept { return (builtMask & (1u << i)) != 0u; }

    void paint (juce::Graphics& g) override
    {
        namespace th = ovni::ui::theme;
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
            g.setFont (ovni::ui::fonts::label (on ? m.textSmall + 0.5f : m.textSmall));
            g.drawText (strings::get (keyFor (i), lang),
                        juce::roundToInt (row.getX() + 21.0f), juce::roundToInt (row.getY()),
                        juce::roundToInt (row.getWidth() - 23.0f), juce::roundToInt (row.getHeight()),
                        juce::Justification::centredLeft, false);
        }

        // ---- el chip de idioma, al pie de la tira ----
        // Pesa MENOS que una lente a propósito: es un ajuste, no una decimocuarta vista. Por eso va con el
        // color terciario, sin punto y sin barra de acento, separado por una hairline.
        g.setColour (th::lineSoft);
        look::fillSnapped (g, { (float) (langRow.getX()), (float) (langRow.getY() - 3), (float) (langRow.getWidth()), 1.0f });

        if (langHovered)
        {
            g.setColour (hue.withAlpha (th::state::hoverGlow));
            g.fillRoundedRectangle (langRow.toFloat(), m.radius);
        }
        g.setColour (langHovered ? look::txtSecondary : look::txtTertiary);
        g.setFont (ovni::ui::fonts::label (m.textSmall - 0.5f));
        g.drawText (strings::get (strings::Key::language, lang),
                    langRow.getX() + 15, langRow.getY(), langRow.getWidth() - 17, langRow.getHeight(),
                    juce::Justification::centredLeft, false);
        g.setColour (langHovered ? look::txtPrimary : look::txtSecondary);
        g.setFont (ovni::ui::fonts::label (m.textSmall));
        g.drawText (strings::endonymOf (lang), langRow.getX(), langRow.getY(),
                    langRow.getWidth() - 4, langRow.getHeight(), juce::Justification::centredRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (languageRow().contains (e.getPosition())) { cycleLanguage(); return; }
        const int i = rowAt (e.y);
        if (i >= 0 && isBuilt (i) && onSelect) onSelect (i);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool overLang = languageRow().contains (e.getPosition());
        const int  i = overLang ? -1 : rowAt (e.y);
        if (i != hovered || overLang != langHovered) { hovered = i; langHovered = overLang; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered != -1 || langHovered) { hovered = -1; langHovered = false; repaint(); }
    }

private:
    juce::String lang { strings::kDefaultLanguage };   // 56
    juce::uint32 builtMask = 1u;   // por ahora sólo LOUDNESS (bit 0)
    int  selected = 0;
    int  hovered  = -1;
    bool langHovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LensStrip)
};
}
