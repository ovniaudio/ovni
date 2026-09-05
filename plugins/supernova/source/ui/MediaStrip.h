#pragma once
// MediaStrip (MEDIA SESSION PRO, 2026-09-02) — la TIRA de miniaturas de la sesión: el filmstrip que todo
// software de visuales da por sentado (Lightroom/Photos/Resolume/VDMX) y que a SUPERNOVA le faltaba: "no
// puedo ni ver qué imagen cargué". Muestra los items EN ORDEN, numerados, con la ACTUAL resaltada y su
// barra de progreso hacia el próximo cambio; click = CUE (salta ya) · drag = REORDENAR · ✕ = sacar ·
// click derecho = menú (rotar / mover al inicio / mostrar en Finder / sacar / vaciar) · tile + = LOAD.
// Bloque izquierdo: "MEDIA 3/12" + el nombre del archivo actual (o del que está bajo el mouse).
// 100% JUCE (vive FUERA del área de la vista Metal). Las miniaturas se piden por callback (thumbSource):
// nula = "…" hasta que el cache las traiga. Scroll horizontal por rueda; la actual se mantiene a la vista.
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace supernova
{
class MediaStrip : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   public juce::SettableTooltipClient
{
public:
    static constexpr int kHeight = 74;
    static constexpr int kInfoW  = 128;   // bloque izquierdo (contador + nombre)
    static constexpr int kTile   = 58;    // tile cuadrado (contain: la foto vertical se ve vertical)
    static constexpr int kGap    = 6;

    struct Item
    {
        juce::String path;
        int  rot     = 0;
        bool isVideo = false;
        bool missing = false;   // el archivo ya no está donde el proyecto lo dejó (tile "offline")
    };

    MediaStrip();

    void setItems (const std::vector<Item>& items);   // reconstruye (conserva el scroll)
    void setCurrent (int idx);                        // resalta + auto-scroll
    void setProgress (float p01);                     // barra del tile actual (0..1)
    // CUE ARMADO (beat snap): en BEATS el cue espera al próximo compás — ese tile se marca con borde
    // PUNTEADO hasta que dispara, para que el VJ vea que ya está pedido. -1 = ninguno.
    void setPendingCue (int idx);
    int  pendingCue() const noexcept { return pendingIdx; }
    int  count() const noexcept { return (int) items.size(); }
    int  current() const noexcept { return currentIdx; }
    const Item* itemAt (int i) const { return juce::isPositiveAndBelow (i, (int) items.size()) ? &items[(size_t) i] : nullptr; }

    // El editor conecta el cache: devuelve la miniatura (o Image() si todavía no está / falló).
    std::function<juce::Image (const Item&)> thumbSource;

    // click en un tile → cue. `immediate` = venía con Shift: cortar YA aunque el reloj sea BEATS.
    std::function<void (int, bool)> onPick;
    std::function<void (int)>      onRemove;        // ✕ / menú
    std::function<void (int)>      onRotate;        // menú: rotar 90°
    std::function<void (int)>      onMoveToStart;   // menú: mover al inicio (AUTO canvas sigue al primero)
    std::function<void (int)>      onReveal;        // menú: mostrar en Finder
    std::function<void (int)>      onRelink;        // menú de un faltante: buscar EL archivo
    std::function<void (int)>      onRelinkFolder;  // menú de un faltante: buscar su CARPETA (todos de una)
    std::function<void (int, int)> onMove;          // drag: from → to
    std::function<void()>          onAdd;           // tile + → LOAD (agrega)
    // Soltar archivos/carpetas SOBRE la tira: se insertan EN esa posición (antes todo drop iba al final).
    std::function<void (const juce::StringArray&, int)> onInsertFiles;
    std::function<void()>          onClearAll;      // menú: vaciar la sesión

    // Geometría (coords de la tira; también para tests): tile i, o count() = el tile "+". -1 = ninguno.
    juce::Rectangle<int> tileBounds (int idx) const;
    int  tileAt (juce::Point<int> p) const;
    bool isOnRemoveHotspot (int idx, juce::Point<int> p) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove  (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    juce::String getTooltip() override;

    // --- drop de archivos EN una posición (ronda 3) ---
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    int  fileDropIndex() const noexcept { return fileDropAt; }   // dónde caería ahora mismo (-1 = nada)

private:
    int  tilesX0() const noexcept { return kInfoW + kGap; }
    int  tileY() const noexcept   { return (getHeight() - kTile) / 2; }
    int  contentWidth() const noexcept { return (count() + 1) * (kTile + kGap); }   // + el tile "+"
    int  maxScroll() const noexcept;
    void clampScroll();
    void scrollToShow (int idx);
    int  insertionIndexAt (int x) const;   // para el drag: 0..count()
    void showMenu (int idx);
    void paintTile (juce::Graphics& g, int idx, const juce::Rectangle<int>& r);

    std::vector<Item> items;
    int   currentIdx = -1;
    int   pendingIdx = -1;   // tile con cue armado (esperando el compás)
    int   hoverIdx   = -1;
    bool  hoverRemove = false;
    float progress   = 0.0f;
    int   scrollX    = 0;

    // drop de archivos de AFUERA (aparte del drag interno: son dos gestos distintos a la vez)
    int   fileDropAt = -1;

    // drag para reordenar
    int   dragFrom   = -1;
    bool  dragging   = false;
    int   dropAt     = -1;
    juce::Point<int> dragPos;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MediaStrip)
};
}
