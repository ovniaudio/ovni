#pragma once
#include "Theme.h"
#include "Fonts.h"
#include "KnobLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace ovni::ui
{
// ================================================================================================
// OvniKnob — el juce::Slider del sello con FEEL nivel FabFilter. Hereda el KnobLookAndFeel dimensional
// (cuerpo metálico cacheado + arco de familia) y le agrega el COMPORTAMIENTO que un Slider pelado no tiene:
//
//   · arrastre directo    — el knob sigue al mouse 1:1 (feel ÓRBITA), sin velocity-drag pegajoso.
//   · Shift = fino        — divide la sensibilidad por theme::state::fineDiv (≈5) mientras Shift está abajo.
//   · cmd/ctrl-click reset— vuelve al default (doubleClickReturnValue + reset por modificador).
//   · doble-click = tipear— el textbox es EDITABLE: doble-click abre el editor, parseo de unidades GRATIS
//                           (el SliderAttachment usa getValueFromText del parámetro: %, Hz/kHz, s/ms, x…).
//   · wheel               — incrementa/decrementa (lo da juce::Slider; acá sólo afinamos el step).
//   · hover/focus/press   — flags propios → se publican en getProperties() y el LookAndFeel dibuja el glow
//                           ENCIMA (sólo alpha de glows = compositor-friendly, no relayout).
//
// El estado por-UI es prolijo (flags privados, repaint puntual). El hue de FAMILIA se pasa como hasta ahora:
//   knob.getProperties().set ("hue", (int) theme::magenta.getARGB());
//
// onHoverName(name,valueText) (opcional): el editor lo engancha para alimentar la BOTTOM-BAR contextual.
// ================================================================================================
class OvniKnob : public juce::Slider
{
public:
    OvniKnob()
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        // --- arrastre DIRECTO (feel ÓRBITA): el knob sigue al mouse, sin velocity-drag -----------
        // El velocity-drag (modo viejo) se sentía "duro/pegajoso" en arrastre lento. Directo =
        // posicional: el valor mapea a la distancia del cursor (suave). Shift = fino (más px/unidad).
        setVelocityBasedMode (false);
        setMouseDragSensitivity (kBaseDrag);

        // cmd/ctrl-click vuelve al default (lo setea el attach; acá habilitamos el gesto).
        setDoubleClickReturnValue (false, 0.0);   // doble-click NO resetea: lo usamos para TIPEAR

        // textbox EDITABLE → doble-click abre el editor de texto (parseo de unidades vía el parámetro).
        setTextBoxStyle (juce::Slider::TextBoxBelow, /*readOnly*/ false, 76, theme::barH - 4);

        setColour (juce::Slider::textBoxTextColourId,       theme::mut);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId,  theme::cyan.withAlpha (0.35f));

        // accesibilidad: el knob recibe foco de teclado (flechas / wheel) → anillo de foco al tabular.
        setWantsKeyboardFocus (true);

        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // El editor pasa el LookAndFeel del sello (compartido) tras construir.
    void setKnobLookAndFeel (KnobLookAndFeel* laf) { setLookAndFeel (laf); }

    // El editor engancha esto para alimentar la bottom-bar (nombre canónico del control + su readout).
    std::function<void (const juce::String& name, const juce::String& valueText)> onHover;
    std::function<void()>                                                          onHoverExit;
    juce::String controlName;   // nombre canónico (del Diccionario) que muestra la bottom-bar

    // ---- estados visuales (publicados al LookAndFeel vía getProperties) -------------------------
    void mouseEnter (const juce::MouseEvent& e) override
    {
        setState ("hovered", true);
        emitHover();
        juce::Slider::mouseEnter (e);
    }
    void mouseExit (const juce::MouseEvent& e) override
    {
        setState ("hovered", false);
        if (onHoverExit) onHoverExit();
        juce::Slider::mouseExit (e);
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        emitHover();   // refresca el valor en la bottom-bar mientras el cursor se mueve sobre el knob
        juce::Slider::mouseMove (e);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // cmd/ctrl-click = reset al default del parámetro (gesto FabFilter), sin entrar a arrastrar.
        if (e.mods.isCommandDown())
        {
            setValue (getDoubleClickReturnValue(), juce::sendNotificationSync);
            return;
        }
        setState ("pressed", true);
        applyFineFromMods (e.mods);
        juce::Slider::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        setState ("pressed", false);
        juce::Slider::mouseUp (e);
        emitHover();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        applyFineFromMods (e.mods);   // Shift puede entrar/salir a mitad del arrastre
        juce::Slider::mouseDrag (e);
        emitHover();
    }

    // Shift fuera del arrastre también: re-evaluar la sensibilidad (y el tick de "fino").
    void modifierKeysChanged (const juce::ModifierKeys& mods) override
    {
        applyFineFromMods (mods);
        juce::Slider::modifierKeysChanged (mods);
    }

    void focusGained (FocusChangeType t) override
    {
        setState ("focused", true);
        juce::Slider::focusGained (t);
    }
    void focusLost (FocusChangeType t) override
    {
        setState ("focused", false);
        juce::Slider::focusLost (t);
    }

private:
    // distancia de arrastre (px) para recorrer todo el rango. 250 = default JUCE = feel de ÓRBITA.
    static constexpr int kBaseDrag = 250;

    void applyFineFromMods (const juce::ModifierKeys& mods)
    {
        const bool fine = mods.isShiftDown();
        if (fine == fineMode) return;
        fineMode = fine;
        // Shift = fino: sube los px necesarios por unidad (arrastre más chico => más preciso).
        setMouseDragSensitivity (fine ? kBaseDrag * theme::state::fineDiv : kBaseDrag);
        setState ("fine", fine);
    }

    void setState (const char* key, bool on)
    {
        getProperties().set (key, on);
        repaint();
    }

    void emitHover()
    {
        if (onHover)
            onHover (controlName.isEmpty() ? getName() : controlName, getTextFromValue (getValue()));
    }

    bool fineMode = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OvniKnob)
};
}
