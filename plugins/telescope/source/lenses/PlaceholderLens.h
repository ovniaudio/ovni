#pragma once
#include "lenses/Lens.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"

// ========================================================================================================
// PlaceholderLens — lo que se ve cuando el host selecciona una de las 12 lentes que todavía no existen.
// Un panel con el nombre y nada más. Ni "coming soon", ni una barra de progreso, ni una promesa: si no
// está construida, lo honesto es un panel vacío con su nombre.
// ========================================================================================================
namespace telescope
{
class PlaceholderLens : public Lens
{
public:
    explicit PlaceholderLens (LensId which) : Lens (1), lens (which) {}

    juce::String name() const override            { return lensName (lens); }
    LensId       id() const override              { return lens; }
    juce::uint32 requiredModules() const override { return 0; }   // no pide análisis: no calcula nada

protected:
    void renderStatic (juce::Graphics& g, int width, int height) override
    {
        namespace th = ovni::ui::theme;
        const auto r = juce::Rectangle<int> (0, 0, width, height).reduced (ovni::ui::theme::padIn);

        g.setColour (th::surf.withAlpha (0.45f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (th::lineSoft);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);

        g.setColour (th::fnt);
        g.setFont (ovni::ui::fonts::label (12.0f));
        g.drawText (name(), r, juce::Justification::centred, false);
    }

    void paintLive (juce::Graphics&) override {}

private:
    LensId lens;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaceholderLens)
};
}
