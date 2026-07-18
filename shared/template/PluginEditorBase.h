#pragma once

#include "template/PluginProcessorBase.h"
#include "ui-kit/Theme.h"      // ui::theme::{bg0,surf,cyan,magenta,amber,red,txt,mut,fnt}
#include "ui-kit/Fonts.h"      // ui::fonts::{display,body,mono}
#include "ui-kit/Panel.h"      // ui::Panel (fondo atmósfera)
#include "ui-kit/Controls.h"   // ui::paintBrandMark, ui::paintPowerIcon
#include "ui-kit/BottomBar.h"  // ui::BottomBar (franja contextual: nombre+valor del control bajo el cursor)

// ========================================================================================================
// PluginEditorBase — editor base del sello OVNI. HEADER BROWSER de M5 (marca + designación · power/SAVE/
// ‹nombre›/A-B) + selector de tamaño S·M·L. El cuerpo (knobs/visualizer/meter) es punto de extensión.
//
// RESIZE (3 tamaños fijos S/M/L): el AudioProcessorEditor NO lleva transform propio — JUCE reserva el
// transform del editor para el escalado DPI del host (jassert en editorResized). Por eso TODO el contenido
// vive en un hijo `Canvas` que se escala con AffineTransform; el editor sólo se dimensiona a base·factor.
// El layout/paint del plugin sigue en coords BASE (p.ej. 860×600); el zoom es puramente visual.
// Usa ovni::ui (Theme/Fonts/Panel/Controls) + ovni::presets. Ver `shared/template/_README.md`.
// ========================================================================================================
namespace ovni
{
class PluginEditorBase : public juce::AudioProcessorEditor,
                         private juce::ChangeListener
{
public:
    enum class Zoom { small, medium, large };
    static constexpr float kZoomS = 0.8f, kZoomM = 1.0f, kZoomL = 1.25f;
    static float zoomFactor (Zoom z) noexcept
    {
        return z == Zoom::small ? kZoomS : (z == Zoom::large ? kZoomL : kZoomM);
    }

    // `designation` = el código chico tras el wordmark (ej. "MOV·01").
    explicit PluginEditorBase (PluginProcessorBase& p, juce::String designation = "OVNI");
    ~PluginEditorBase() override;

    void paint (juce::Graphics&) override;     // sólo fondo de respaldo
    void resized() override;                    // reposiciona el canvas

    // ======== API que usa el plugin concreto (en su ctor) ========
    void setBaseSize (int w, int h);            // reemplaza setSize(): tamaño de DISEÑO (coords base)
    void addToCanvas (juce::Component& c);      // reemplaza addAndMakeVisible(): hijo del canvas (escala)

    // ======== zoom ========
    void applyZoom (Zoom z);                     // aplica (transform + setSize). NO persiste. (público: tests)
    void setZoom   (Zoom z);                     // aplica + persiste (lo usa el selector)

    // ======== header browser on/off (H14 · app-mode) ========
    // La app standalone esconde el chrome de PLUGIN (presets/A-B/power/zoom) y pone su propia barra.
    // ADITIVO: default visible → ningún plugin del catálogo cambia. paintHeader se guarda con área vacía.
    void setHeaderVisible (bool on)
    {
        if (on == (headerHeight > 0)) return;
        if (! on) { savedHeaderHeight = headerHeight; headerHeight = 0; }
        else        headerHeight = savedHeaderHeight > 0 ? savedHeaderHeight : 58;
        layoutCanvas();
    }

    // ======== flexible canvas (fix 1 · app full-bleed) ========
    // Por default el editor es base×zoom (aspecto FIJO): el canvas mide baseW×baseH y un transform lo escala.
    // Dentro de un DAW eso es correcto. Pero la APP standalone quiere LLENAR una ventana/pantalla de cualquier
    // tamaño (el visual full-bleed, sin negro muerto al costado). En modo flexible el canvas toma el tamaño
    // REAL del editor (transform identidad) y layoutCanvas/layoutBody reflowean a ese tamaño → la vista Metal
    // llena todo el ancho (la figura es invariante al tamaño por resScale). ADITIVO: default OFF → ningún
    // plugin del catálogo cambia. El render NO se toca → goldens byte-exactos.
    void setFlexibleCanvas (bool on);
    bool isFlexibleCanvas() const noexcept { return flexible; }

protected:
    // ======== PUNTOS DE EXTENSIÓN (el plugin concreto los define; coords BASE) ========

    // PLUGIN: ubicá tus controles (knobs, visualizer, meter) dentro de `body` (el área bajo el header).
    virtual void layoutBody (juce::Rectangle<int> body) = 0;

    // PLUGIN (opcional): pintá tus superficies/labels. El fondo (Panel) y el header browser ya están pintados.
    virtual void paintBody (juce::Graphics&) {}

    // PLUGIN (opcional): manejá clicks que caen FUERA de las zonas del header.
    virtual void mouseDownBody (const juce::MouseEvent&) {}

    PluginProcessorBase& processor;   // acceso para subclases: apvts, presets(), ab(), uiOutPeak/uiClip
    int headerHeight = 58;            // alto del header browser (px); ajustable por el plugin antes de resized()

    // HUE de FAMILIA del plugin (Movimiento=cian · Espacio=magenta · Espectral=verde · Textura=ámbar). El
    // plugin lo setea en su ctor → el fondo (Panel) respeta TODO el color de familia (mockup #plugin::before)
    // y el nombre/power del header se tiñen con él. Default cian = backward-compatible.
    void setFamilyHue (juce::Colour h) { familyHue = h; }
    juce::Colour family() const { return familyHue; }

    // ======== BOTTOM-BAR contextual (opt-in; doctrina interaction-grammar) ========
    // El plugin la habilita en su ctor (hue de familia + hint en reposo). Habilitada, la base le RESERVA
    // una tira (theme::barH) al pie del cuerpo ANTES de llamar a layoutBody → el plugin recibe un `body`
    // que ya excluye la barra (no rompe los layouts existentes que no la habilitan). El plugin alimenta la
    // barra enganchando onHover/onHoverExit de sus OvniKnob a bottomBar().show()/clear().
    void enableBottomBar (juce::Colour familyHue, juce::String idleHint = {})
    {
        bottomBarOn = true;
        addToCanvas (bar);
        bar.setHue (familyHue);
        bar.setIdleHint (std::move (idleHint));
    }
    ui::BottomBar& bottomBar() { return bar; }

private:
    // Canvas: hijo que contiene fondo+header+body y se ESCALA con el zoom. Delega todo al editor.
    struct Canvas : juce::Component
    {
        PluginEditorBase& owner;
        explicit Canvas (PluginEditorBase& o) : owner (o) { setInterceptsMouseClicks (true, true); }
        void paint (juce::Graphics& g) override            { owner.paintCanvas (g); }
        void paintOverChildren (juce::Graphics& g) override { owner.paintBezel (g); }   // bisel ENCIMA de todo
        void resized() override                            { owner.layoutCanvas(); }
        void mouseDown (const juce::MouseEvent& e) override { owner.canvasMouseDown (e); }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void paintCanvas (juce::Graphics&);                  // ex-paint()  (coords base)

protected:
    // Re-corre el layout del canvas (header + zonas + layoutBody). PROTECTED a propósito: resized() del
    // editor NO re-lay-outea el cuerpo (solo fija el canvas base, y con el mismo tamaño es no-op) — un
    // plugin que cambia su layout interno (p.ej. modo inmersivo de SUPERNOVA) llama esto directamente.
    void layoutCanvas();                                 // ex-resized() (coords base)

private:
    void canvasMouseDown (const juce::MouseEvent&);      // ex-mouseDown()
    void paintHeader (juce::Graphics&);
    void paintBezel (juce::Graphics&);                   // doble hairline inset + corner-brackets (mockup)
    void showPresetMenu();
    void showSaveDialog();

    static juce::PropertiesFile& uiSettings();           // settings global del sello (compartido por los 6)

    // Tamaño LÓGICO del canvas: base×zoom por default; en modo flexible = tamaño real del editor.
    int  canvasW() const noexcept { return flexible ? juce::jmax (1, getWidth())  : baseW; }
    int  canvasH() const noexcept { return flexible ? juce::jmax (1, getHeight()) : baseH; }

    juce::String designation;
    juce::Colour familyHue = ui::theme::cyan;   // hue de familia (lo setea el plugin; tiñe Panel + header)
    Canvas content { *this };
    int  baseW = 0, baseH = 0;
    int  savedHeaderHeight = 0;   // headerHeight previo a setHeaderVisible(false)
    bool flexible = false;        // fix 1: canvas full-bleed (app) vs base×zoom (DAW)
    Zoom zoom = Zoom::medium;

    juce::Rectangle<int> headerArea;
    // zonas clickeables del browser (replican el chrome dibujado en paintHeader)
    juce::Rectangle<int> presetPrevZone, presetNameZone, presetNextZone, presetSaveZone, presetAbZone, bypassZone;
    juce::Rectangle<int> zoomSZone, zoomMZone, zoomLZone;   // selector S·M·L

    ui::Panel     panel;   // fondo atmósfera del sello (compartido por todos los plugins)
    ui::BottomBar bar;     // franja contextual (opt-in via enableBottomBar)
    bool          bottomBarOn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditorBase)
};
}
