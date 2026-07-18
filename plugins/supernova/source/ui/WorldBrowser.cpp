#include "WorldBrowser.h"
#include "ui/theme.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"

namespace supernova
{
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// ============================================================================================ WorldBrowser
WorldBrowser::WorldBrowser()
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    closeBtn.setColour (juce::TextButton::buttonColourId, th::surf2);
    closeBtn.setColour (juce::TextButton::textColourOffId, th::txt);
    addAndMakeVisible (closeBtn);

    viewport.setViewedComponent (&grid, false);          // NO owned (grid es miembro)
    viewport.setScrollBarsShown (true, false);           // solo vertical
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    setWantsKeyboardFocus (false);
}

void WorldBrowser::setNames (const juce::StringArray& n)
{
    grid.names = n;
    if (grid.thumbs.size() != (size_t) grid.names.size())    // fix 3: preservar el cache si el conteo no cambió
        grid.thumbs.assign ((size_t) grid.names.size(), juce::Image());
    grid.hoverIdx = -1;
    resized();
    grid.repaint();
}

void WorldBrowser::setThumbnail (int idx, juce::Image img)
{
    if (idx >= 0 && idx < (int) grid.thumbs.size())
        { grid.thumbs[(size_t) idx] = std::move (img); grid.repaint (grid.tileBounds (idx)); }
}

void WorldBrowser::setActive (int idx) { grid.activeIdx = idx; grid.repaint(); }

void WorldBrowser::clearThumbnails()
{
    for (auto& t : grid.thumbs) t = juce::Image();
    grid.repaint();
}

void WorldBrowser::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeaderH);
    closeBtn.setBounds (header.getRight() - kPad - 88, (kHeaderH - 26) / 2, 88, 26);
    viewport.setBounds (r);
    const int gw = juce::jmax (200, viewport.getMaximumVisibleWidth());   // ancho útil (descuenta scrollbar)
    // OJO: fijar el ANCHO primero — contentHeight()/columns() leen getWidth(); si se computa con el ancho
    // viejo (o 0) las columnas salen mal → filas de más → scroll VACÍO "infinito" (el bug que reportó Joaquín).
    grid.setSize (gw, grid.getHeight());
    grid.setSize (gw, grid.contentHeight());
}

void WorldBrowser::paint (juce::Graphics& g)
{
    g.fillAll (th::bg0);
    g.setColour (th::txt);
    g.setFont (fonts::display (20.0f));
    g.drawText ("WORLDS", kPad, 0, getWidth() - kPad * 2 - 96, kHeaderH, juce::Justification::centredLeft);
    g.setColour (th::mut);
    g.setFont (fonts::mono (11.0f));
    g.drawText (juce::String (grid.names.size()) + " worlds · click to apply",
                kPad + 132, 0, getWidth() - kPad * 2 - 132 - 96, kHeaderH, juce::Justification::centredLeft);
    g.setColour (th::line);
    g.drawHorizontalLine (kHeaderH - 1, 0.0f, (float) getWidth());
}

// ==================================================================================================== Grid
int WorldBrowser::Grid::columns() const
{
    const int usable = juce::jmax (200, getWidth() - kPad * 2);
    return juce::jlimit (2, 6, (usable + kGap) / (kTileTargetW + kGap));
}

juce::Rectangle<int> WorldBrowser::Grid::tileBounds (int idx) const
{
    const int cols = columns();
    if (cols <= 0) return {};
    const int col = idx % cols, row = idx / cols;
    const int usable = getWidth() - kPad * 2;
    const int tw = (usable - kGap * (cols - 1)) / cols;
    const int x = kPad + col * (tw + kGap);
    const int y = kPad + row * (kTileH + kNameH + kGap);
    return { x, y, tw, kTileH + kNameH };
}

int WorldBrowser::Grid::tileAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) names.size(); ++i)
        if (tileBounds (i).contains (p)) return i;
    return -1;
}

int WorldBrowser::Grid::contentHeight() const
{
    const int cols = juce::jmax (1, columns());
    const int rows = ((int) names.size() + cols - 1) / cols;
    if (rows <= 0) return kPad * 2;
    return kPad * 2 + rows * (kTileH + kNameH) + (rows - 1) * kGap;
}

void WorldBrowser::Grid::paint (juce::Graphics& g)
{
    g.fillAll (th::bg0);
    for (int i = 0; i < (int) names.size(); ++i)
    {
        auto tb    = tileBounds (i);
        auto thumb = tb.withHeight (kTileH);
        const bool isActive = (i == activeIdx), isHover = (i == hoverIdx);

        g.setColour (th::surf2);
        g.fillRoundedRectangle (thumb.toFloat(), 6.0f);
        if (i < (int) thumbs.size() && thumbs[(size_t) i].isValid())
        {
            // crop-to-fill (fillDestination): el thumbnail se rindió al aspecto de la fuente (sin barras) →
            // acá se recorta a la celda sin dejar negro.
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip; clip.addRoundedRectangle (thumb.toFloat(), 6.0f);
            g.reduceClipRegion (clip);
            g.drawImage (thumbs[(size_t) i], thumb.toFloat(),
                         juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.setColour (th::mut.withAlpha (0.5f));
            g.setFont (fonts::mono (10.0f));
            g.drawText ("rendering...", thumb, juce::Justification::centred);
        }
        // borde (activo = hue, hover = claro)
        g.setColour (isActive ? look::hue : (isHover ? th::txt.withAlpha (0.6f) : th::line));
        g.drawRoundedRectangle (thumb.toFloat().reduced (0.5f), 6.0f, isActive || isHover ? 2.0f : 1.0f);
        // nombre
        g.setColour (isActive ? look::hue : th::txt);
        g.setFont (fonts::mono (11.0f));
        g.drawText (i < names.size() ? names[i] : juce::String(),
                    tb.getX(), thumb.getBottom() + 3, tb.getWidth(), kNameH - 4, juce::Justification::centred);
    }
}

void WorldBrowser::Grid::mouseMove (const juce::MouseEvent& e)
{
    const int was = hoverIdx;
    hoverIdx = tileAt (e.getPosition());
    if (hoverIdx != was) repaint();
}
void WorldBrowser::Grid::mouseExit (const juce::MouseEvent&) { if (hoverIdx != -1) { hoverIdx = -1; repaint(); } }

void WorldBrowser::Grid::mouseUp (const juce::MouseEvent& e)
{
    const int idx = tileAt (e.getPosition());
    if (idx >= 0 && owner.onPick) owner.onPick (idx);
}
}
