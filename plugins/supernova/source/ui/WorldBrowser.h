#pragma once
// WorldBrowser (Phase C · el #2 mover del brief) — grilla de los 36 MUNDOS con THUMBNAIL vivo, en vez de
// recorrerlos de a uno leyendo nombres ciegos (◂nombre▸). Reusa el render OFFSCREEN: el editor rinde un
// keyframe de cada mundo (sobre TU imagen, o una referencia estructurada) en un hilo de fondo y los va
// cargando. Click en un tile → aplica ese mundo. Se muestra CON LA VISTA METAL OCULTA (el editor la esconde)
// → sin guerra de oclusión del NSView.
//
// fix 3: header FIJO (título + CLOSE) + una grilla SCROLLEABLE (juce::Viewport) → se llega a los 36 mundos.
// La grilla interna pinta los tiles y hit-testea los clicks (más simple/robusto que 36 hijos).
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <functional>

namespace supernova
{
class WorldBrowser : public juce::Component
{
public:
    WorldBrowser();

    void setNames (const juce::StringArray& names);          // los 36 nombres (choices del param 'preset')
    void setThumbnail (int idx, juce::Image img);            // el editor los va cargando desde el bg thread
    void setActive (int idx);                                // resalta el mundo actual
    void clearThumbnails();

    std::function<void (int)> onPick;                        // click en un tile → aplicar ese mundo
    std::function<void()>     onClose;                       // botón cerrar / Esc

    void paint (juce::Graphics&) override;                   // header fijo (título + hairline)
    void resized() override;

private:
    static constexpr int kPad = 16, kGap = 12, kHeaderH = 46, kTileH = 116, kNameH = 20, kTileTargetW = 210;

    // Grilla scrolleable (viewed component del viewport). Pinta los tiles + nombres y hit-testea.
    struct Grid : juce::Component
    {
        WorldBrowser& owner;
        juce::StringArray        names;
        std::vector<juce::Image> thumbs;
        int hoverIdx = -1, activeIdx = -1;

        explicit Grid (WorldBrowser& o) : owner (o) { setWantsKeyboardFocus (false); }
        int columns() const;
        juce::Rectangle<int> tileBounds (int idx) const;     // en coords de la grilla
        int tileAt (juce::Point<int> p) const;
        int contentHeight() const;                           // alto total → el viewport scrollea

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
    };

    juce::Viewport   viewport;
    Grid             grid { *this };
    juce::TextButton closeBtn { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WorldBrowser)
};
}
