#include "ui/MediaStrip.h"
#include "image/ImageLoader.h"   // looksLikeImage (mismo filtro que el drop del editor)
#include "video/VideoSource.h"   // looksLikeVideo
#include "ui/theme.h"
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"

namespace supernova
{
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

namespace
{
constexpr int   kDragThreshold = 6;    // px antes de que un click se vuelva drag
constexpr int   kHotspot       = 16;   // ✕ arriba-derecha
constexpr float kRadius        = 5.0f;
}

MediaStrip::MediaStrip()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MediaStrip::setItems (const std::vector<Item>& newItems)
{
    items = newItems;
    pendingIdx = -1;                       // la sesión cambió: el cue armado ya no apunta a nada
    if (currentIdx >= count()) currentIdx = count() - 1;
    if (hoverIdx >= count()) hoverIdx = -1;
    dragFrom = -1; dragging = false; dropAt = -1;
    clampScroll();
    repaint();
}

void MediaStrip::setCurrent (int idx)
{
    if (idx == currentIdx) return;
    currentIdx = idx;
    scrollToShow (idx);
    repaint();
}

void MediaStrip::setPendingCue (int idx)
{
    const int v = juce::isPositiveAndBelow (idx, count()) ? idx : -1;
    if (v == pendingIdx) return;
    pendingIdx = v;
    repaint();
}

void MediaStrip::setProgress (float p01)
{
    const float p = juce::jlimit (0.0f, 1.0f, p01);
    if (std::abs (p - progress) < 0.01f) return;
    progress = p;
    if (juce::isPositiveAndBelow (currentIdx, count())) repaint (tileBounds (currentIdx));
}

// ------------------------------------------------------------------------------------------- geometría
juce::Rectangle<int> MediaStrip::tileBounds (int idx) const
{
    if (idx < 0 || idx > count()) return {};
    return { tilesX0() + idx * (kTile + kGap) - scrollX, tileY(), kTile, kTile };
}

int MediaStrip::tileAt (juce::Point<int> p) const
{
    if (p.x < tilesX0()) return -1;
    for (int i = 0; i <= count(); ++i)
        if (tileBounds (i).contains (p)) return i;
    return -1;
}

bool MediaStrip::isOnRemoveHotspot (int idx, juce::Point<int> p) const
{
    if (! juce::isPositiveAndBelow (idx, count())) return false;
    const auto r = tileBounds (idx);
    return juce::Rectangle<int> (r.getRight() - kHotspot, r.getY(), kHotspot, kHotspot).contains (p);
}

int MediaStrip::maxScroll() const noexcept
{
    const int visible = juce::jmax (0, getWidth() - tilesX0() - kGap);
    return juce::jmax (0, contentWidth() - visible);
}

void MediaStrip::clampScroll() { scrollX = juce::jlimit (0, maxScroll(), scrollX); }

void MediaStrip::scrollToShow (int idx)
{
    if (! juce::isPositiveAndBelow (idx, count())) return;
    const int x0 = tilesX0() + idx * (kTile + kGap);   // sin scroll
    const int visL = tilesX0() + scrollX, visR = tilesX0() + scrollX + juce::jmax (0, getWidth() - tilesX0() - kGap);
    if (x0 < visL)                 scrollX = x0 - tilesX0();
    else if (x0 + kTile > visR)    scrollX = x0 + kTile - (visR - scrollX);
    clampScroll();
}

int MediaStrip::insertionIndexAt (int x) const
{
    const float slot = (float) (x - tilesX0() + scrollX) / (float) (kTile + kGap);
    return juce::jlimit (0, count(), (int) std::lround (slot));
}

void MediaStrip::resized() { clampScroll(); }

// ------------------------------------------------------------------------------------------------ paint
void MediaStrip::paint (juce::Graphics& g)
{
    g.fillAll (th::bg1);
    g.setColour (th::line);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());

    // ---- bloque izquierdo: contador + nombre ----
    {
        const int n = count();
        const int shown = juce::isPositiveAndBelow (hoverIdx, n) ? hoverIdx : currentIdx;
        const juce::String counter = n > 0 ? "MEDIA " + juce::String (juce::jmax (0, currentIdx) + 1) + "/" + juce::String (n)
                                           : "MEDIA";
        g.setColour (th::mut);
        g.setFont (fonts::mono (10.0f));
        g.drawText (counter, 12, tileY() + 4, kInfoW - 16, 14, juce::Justification::centredLeft);
        if (auto* it = itemAt (shown))
        {
            // Un archivo que falta se nombra APAGADO: se lee de un vistazo que ese tile no va a sonar.
            g.setColour (it->missing || (juce::isPositiveAndBelow (hoverIdx, n) && hoverIdx != currentIdx)
                             ? th::mut : th::txt);
            g.setFont (fonts::body (11.5f));
            g.drawText (juce::File (it->path).getFileName(), 12, tileY() + 22, kInfoW - 16, 30,
                        juce::Justification::centredLeft, true);
        }
        g.setColour (th::lineSoft);
        g.drawVerticalLine (kInfoW - 2, (float) tileY(), (float) (tileY() + kTile));
    }

    // ---- tiles (clip a la zona scrolleable) ----
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (tilesX0() - 2, 0, getWidth() - tilesX0() + 2, getHeight());
        for (int i = 0; i < count(); ++i)
        {
            const auto r = tileBounds (i);
            if (r.getRight() < tilesX0() - 2 || r.getX() > getWidth()) continue;
            paintTile (g, i, r);
        }
        // tile "+" (LOAD / agregar)
        {
            const auto r = tileBounds (count());
            const bool hov = hoverIdx == count();
            g.setColour (hov ? look::hue.withAlpha (0.14f) : th::surf2.withAlpha (0.6f));
            g.fillRoundedRectangle (r.toFloat(), kRadius);
            g.setColour (hov ? look::hue.withAlpha (0.8f) : th::line);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), kRadius, 1.0f);
            g.setColour (hov ? look::hue : th::mut);
            g.setFont (fonts::display (22.0f));
            g.drawText ("+", r, juce::Justification::centred);
        }
        // indicador de drop: el mismo para el drag interno (reordenar) y para archivos que caen de afuera
        const int dropLine = dragging ? dropAt : fileDropAt;
        if (dropLine >= 0)
        {
            const int x = tilesX0() + dropLine * (kTile + kGap) - scrollX - kGap / 2;
            g.setColour (look::hue);
            g.fillRoundedRectangle ((float) x - 1.0f, (float) tileY() - 3.0f, 2.0f, (float) kTile + 6.0f, 1.0f);
        }
        // fundidos en los bordes cuando hay scroll
        if (scrollX > 0)
        {
            g.setGradientFill (juce::ColourGradient (th::bg1, (float) tilesX0(), 0.0f,
                                                     th::bg1.withAlpha (0.0f), (float) tilesX0() + 18.0f, 0.0f, false));
            g.fillRect (tilesX0(), 0, 18, getHeight());
        }
        if (scrollX < maxScroll())
        {
            g.setGradientFill (juce::ColourGradient (th::bg1.withAlpha (0.0f), (float) getWidth() - 18.0f, 0.0f,
                                                     th::bg1, (float) getWidth(), 0.0f, false));
            g.fillRect (getWidth() - 18, 0, 18, getHeight());
        }
    }

    // el tile arrastrado, flotando
    if (dragging && juce::isPositiveAndBelow (dragFrom, count()))
    {
        auto r = tileBounds (dragFrom).withCentre (dragPos);
        g.setOpacity (0.85f);
        paintTile (g, dragFrom, r);
        g.setOpacity (1.0f);
    }
}

void MediaStrip::paintTile (juce::Graphics& g, int i, const juce::Rectangle<int>& r)
{
    const auto* it = itemAt (i);
    if (it == nullptr) return;
    const bool isCur = (i == currentIdx), isHov = (i == hoverIdx);

    g.setColour (th::surf2);
    g.fillRoundedRectangle (r.toFloat(), kRadius);

    if (it->missing)
    {
        // MEDIA OFFLINE: nada de miniatura (el archivo no está). Un ! grande, del color de aviso, para que
        // se vea desde lejos que hay un hueco en la sesión.
        g.setColour (th::red.withAlpha (0.16f));
        g.fillRoundedRectangle (r.toFloat(), kRadius);
        g.setColour (th::red.withAlpha (0.9f));
        g.setFont (fonts::display (24.0f));
        g.drawText ("!", r, juce::Justification::centred);
    }
    const juce::Image thumb = it->missing ? juce::Image() : (thumbSource ? thumbSource (*it) : juce::Image());
    if (thumb.isValid())
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip; clip.addRoundedRectangle (r.toFloat().reduced (1.0f), kRadius - 1.0f);
        g.reduceClipRegion (clip);
        // CONTAIN: la foto vertical se ve vertical (orientación + orden de un vistazo)
        g.drawImage (thumb, r.toFloat().reduced (2.0f), juce::RectanglePlacement::centred);
    }
    else if (! it->missing)
    {
        g.setColour (th::mut.withAlpha (0.55f));
        g.setFont (fonts::mono (10.0f));
        g.drawText (juce::String::fromUTF8 ("\xE2\x80\xA6"), r, juce::Justification::centred);   // …
    }

    // borde: actual = hue · armado = hue PUNTEADO (el cue espera el compás) · hover = claro · resto = hairline
    if (i == pendingIdx && ! isCur)
    {
        juce::Path box;
        box.addRoundedRectangle (r.toFloat().reduced (1.0f), kRadius);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.8f).createDashedStroke (dashed, box, dashes, 2);
        g.setColour (look::hue);
        g.fillPath (dashed);
    }
    else
    {
        g.setColour (isCur ? look::hue : (isHov ? th::txt.withAlpha (0.55f) : th::line));
        g.drawRoundedRectangle (r.toFloat().reduced (isCur ? 1.0f : 0.5f), kRadius, isCur ? 2.0f : 1.0f);
    }

    // número (badge arriba-izquierda)
    {
        const juce::String num (i + 1);
        g.setFont (fonts::mono (9.0f));
        const int bw = 8 + (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), num));
        juce::Rectangle<int> b (r.getX() + 3, r.getY() + 3, bw, 12);
        g.setColour (isCur ? look::hue.withAlpha (0.9f) : th::bg0.withAlpha (0.78f));
        g.fillRoundedRectangle (b.toFloat(), 3.0f);
        g.setColour (isCur ? th::bg0 : th::txt);
        g.drawText (num, b, juce::Justification::centred);
    }

    // video: glifo ▶ abajo-derecha
    if (it->isVideo)
    {
        juce::Rectangle<int> v (r.getRight() - 16, r.getBottom() - 16, 13, 13);
        g.setColour (th::bg0.withAlpha (0.78f));
        g.fillEllipse (v.toFloat());
        g.setColour (th::txt);
        juce::Path p;
        p.addTriangle ((float) v.getX() + 4.5f, (float) v.getY() + 3.0f, (float) v.getX() + 4.5f, (float) v.getBottom() - 3.0f,
                       (float) v.getRight() - 3.0f, (float) v.getCentreY());
        g.fillPath (p);
    }

    // progreso hacia el próximo cambio (solo la actual)
    if (isCur && progress > 0.0f)
    {
        const auto bar = juce::Rectangle<float> ((float) r.getX() + 3.0f, (float) r.getBottom() - 5.0f,
                                                 ((float) r.getWidth() - 6.0f) * progress, 2.0f);
        g.setColour (look::hue.withAlpha (0.9f));
        g.fillRoundedRectangle (bar, 1.0f);
    }

    // ✕ para sacar (al hover)
    if (isHov && ! dragging)
    {
        juce::Rectangle<float> x ((float) r.getRight() - kHotspot + 2.0f, (float) r.getY() + 2.0f, 12.0f, 12.0f);
        g.setColour (hoverRemove ? th::red.withAlpha (0.95f) : th::bg0.withAlpha (0.82f));
        g.fillEllipse (x);
        g.setColour (hoverRemove ? th::txt : th::mut);
        g.drawLine (x.getX() + 3.5f, x.getY() + 3.5f, x.getRight() - 3.5f, x.getBottom() - 3.5f, 1.2f);
        g.drawLine (x.getRight() - 3.5f, x.getY() + 3.5f, x.getX() + 3.5f, x.getBottom() - 3.5f, 1.2f);
    }
}

// ------------------------------------------------------------------------------------------------ mouse
void MediaStrip::mouseMove (const juce::MouseEvent& e)
{
    const int was = hoverIdx;
    const bool wasRemove = hoverRemove;
    hoverIdx    = tileAt (e.getPosition());
    hoverRemove = isOnRemoveHotspot (hoverIdx, e.getPosition());
    if (hoverIdx != was || hoverRemove != wasRemove) repaint();
}

void MediaStrip::mouseExit (const juce::MouseEvent&)
{
    if (hoverIdx != -1 || hoverRemove) { hoverIdx = -1; hoverRemove = false; repaint(); }
}

void MediaStrip::mouseDown (const juce::MouseEvent& e)
{
    const int idx = tileAt (e.getPosition());
    if (e.mods.isPopupMenu())
    {
        if (juce::isPositiveAndBelow (idx, count())) showMenu (idx);
        return;
    }
    dragFrom = juce::isPositiveAndBelow (idx, count()) ? idx : -1;
    dragging = false;
    dropAt   = -1;
}

void MediaStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStart() >= kDragThreshold) dragging = true;
    if (! dragging) return;
    dragPos = e.getPosition();
    dropAt  = insertionIndexAt (e.x);
    // auto-scroll suave en los bordes
    if (e.x > getWidth() - 24)      scrollX += 6;
    else if (e.x < tilesX0() + 12)  scrollX -= 6;
    clampScroll();
    repaint();
}

void MediaStrip::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    if (dragging)
    {
        const int from = dragFrom;
        int to = dropAt;
        dragging = false; dragFrom = -1; dropAt = -1;
        repaint();
        if (to < 0) return;
        if (to > from) --to;               // el hueco se cierra al sacar `from`
        if (to != from && onMove) onMove (from, to);
        return;
    }
    dragFrom = -1;
    const int idx = tileAt (e.getPosition());
    if (idx < 0) return;
    if (idx == count()) { if (onAdd) onAdd(); return; }
    if (isOnRemoveHotspot (idx, e.getPosition())) { if (onRemove) onRemove (idx); return; }
    // Un tile FALTANTE no se puede cuear (no hay nada que mostrar): el click abre su menú, que empieza con
    // Relink… — que es lo que el usuario vino a hacer. Vale igual con Shift (el "cortar ya").
    if (auto* it = itemAt (idx)) if (it->missing) { showMenu (idx); return; }
    if (onPick) onPick (idx, e.mods.isShiftDown());   // Shift = cortar YA aunque el reloj sea BEATS
}

void MediaStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY);
    scrollX -= (int) std::lround (d * 120.0f);
    clampScroll();
    repaint();
}

// ------------------------------------------------------------------------------- drop de archivos (ronda 3)
// La tira es su PROPIO FileDragAndDropTarget: JUCE entrega el drop al target más profundo que diga que le
// interesa, así que soltar sobre la tira NO cae en el editor (que manda todo al final) sino acá, donde
// sabemos entre qué tiles cayó.
bool MediaStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (ImageLoader::looksLikeImage (file) || VideoSource::looksLikeVideo (file) || file.isDirectory())
            return true;
    }
    return false;
}

void MediaStrip::fileDragMove (const juce::StringArray&, int x, int)
{
    const int at = insertionIndexAt (x);
    if (at == fileDropAt) return;
    fileDropAt = at;
    repaint();
}

void MediaStrip::fileDragExit (const juce::StringArray&)
{
    if (fileDropAt < 0) return;
    fileDropAt = -1;
    repaint();
}

void MediaStrip::filesDropped (const juce::StringArray& files, int x, int)
{
    const int at = insertionIndexAt (x);
    fileDropAt = -1;
    repaint();
    if (onInsertFiles) onInsertFiles (files, at);
}

juce::String MediaStrip::getTooltip()
{
    if (hoverIdx == count()) return "Add photos or videos to the session";
    if (auto* it = itemAt (hoverIdx))
    {
        if (hoverRemove)          return "Remove " + juce::File (it->path).getFileName();
        if (it->missing)          return "Missing - right-click to relink " + juce::File (it->path).getFileName();
        if (hoverIdx == pendingIdx) return "Cue armed - waiting for the next bar (Shift-click = now)";
        return "Cue " + juce::File (it->path).getFileName();
    }
    if (pendingIdx >= 0) return "Cue armed - waiting for the next bar";
    return {};
}

void MediaStrip::showMenu (int idx)
{
    const bool missing = items[(size_t) idx].missing;
    juce::PopupMenu m;
    m.addSectionHeader (juce::File (items[(size_t) idx].path).getFileName());
    if (missing)   // el archivo no está: lo primero que se ofrece es encontrarlo
    {
        m.addItem (7, juce::String::fromUTF8 ("Relink\xE2\x80\xA6"));
        m.addItem (8, juce::String::fromUTF8 ("Relink folder\xE2\x80\xA6 (every missing item of that folder)"));
        m.addSeparator();
    }
    m.addItem (1, "Cue now", ! missing);
    m.addItem (2, juce::String::fromUTF8 ("Rotate 90\xC2\xB0"), ! missing);
    m.addItem (3, "Move to start (AUTO canvas follows the first item)", idx != 0);
    m.addItem (4, "Reveal in Finder");
    m.addSeparator();
    m.addItem (5, "Remove from session");
    m.addItem (6, "Clear session", count() > 0);
    juce::Component::SafePointer<MediaStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (tileBounds (idx))),
                     [safe, idx] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& s = *safe;
        switch (r)
        {
            case 1: if (s.onPick)        s.onPick (idx, true);  break;   // el menú es explícito: corta ya
            case 2: if (s.onRotate)      s.onRotate (idx);      break;
            case 3: if (s.onMoveToStart) s.onMoveToStart (idx); break;
            case 4: if (s.onReveal)      s.onReveal (idx);      break;
            case 5: if (s.onRemove)      s.onRemove (idx);      break;
            case 6: if (s.onClearAll)    s.onClearAll();        break;
            case 7: if (s.onRelink)       s.onRelink (idx);       break;
            case 8: if (s.onRelinkFolder) s.onRelinkFolder (idx); break;
            default: break;
        }
    });
}
}
