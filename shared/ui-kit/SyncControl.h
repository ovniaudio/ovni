#pragma once
#include "Theme.h"
#include "Fonts.h"
#include "Controls.h"
#include "KnobLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

namespace ovni::ui
{
// Control de tempo UNIFICADO del catálogo OVNI. Selector FREE⇄SYNC + (FREE: knob RATE si hay /
// SYNC: chips de división, SIEMPRE visibles). Autónomo: posee el knob, su L&F y su attachment.
// rateParamID VACÍO => sin knob (FREE no tiene rate de usuario, p.ej. el breath orgánico de NÉBULA);
// en ese caso FREE muestra `freeCaption` tenue. Backward-compatible: rate no-vacío = comportamiento previo.
//
// Rediseño 2026-06 (mockup pulsar-a): el toggle pasa a SEGMENTADO [FREE|SYNC] (mismo bool del APVTS)
// y el control gana un modo VERTICAL automático (bounds más altos que anchos = rail angosto): selector
// arriba, knob/chips abajo, chips en grilla 2×2 + caption "DIVISION". Bandas horizontales (los otros
// plugins) conservan el layout histórico (selector a la izquierda, contenido a la derecha).
class SyncControl : public juce::Component, private juce::Timer
{
public:
    SyncControl (juce::AudioProcessorValueTreeState& state,
                 const juce::String& rateParamID, juce::String syncParamID,
                 const juce::String& divisionParamID, juce::StringArray divisionLabels,
                 juce::Colour familyHue = theme::cyan, juce::String freeCaption = {})
        : apvts (state), syncId (std::move (syncParamID)), caption (std::move (freeCaption)),
          hasRate (rateParamID.isNotEmpty()),
          toggle (state, syncId, { "FREE", "SYNC" }),
          divisions (state, divisionParamID, std::move (divisionLabels))
    {
        toggle.setHue (familyHue);
        divisions.setHue (familyHue);
        if (hasRate)
        {
            rate.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            rate.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);
            rate.setColour (juce::Slider::textBoxTextColourId,       theme::fnt);   // valor discreto (mockup: sin caja)
            rate.setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
            rate.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            rate.setLookAndFeel (&knobLaf);
            rate.getProperties().set ("hue", (int) familyHue.getARGB());
            rateAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, rateParamID, rate);
            addAndMakeVisible (rate);
        }
        addAndMakeVisible (toggle);
        addAndMakeVisible (divisions);
        syncOn = apvts.getRawParameterValue (syncId)->load() >= 0.5f;
        applyVisibility();
        startTimerHz (12);
    }

    ~SyncControl() override { if (hasRate) rate.setLookAndFeel (nullptr); }

    void resized() override
    {
        auto b = getLocalBounds();
        vertical = b.getHeight() > b.getWidth();

        if (vertical)
        {
            // rail angosto (mockup pulsar-a): [FREE|SYNC] arriba, knob RATE / chips 2×2 abajo + caption.
            toggle.setBounds (b.removeFromTop (26));
            b.removeFromTop (10);
            slot = b;
            if (hasRate)
            {
                auto rz = b.withTrimmedBottom (16);   // deja lugar al caption "RATE"
                rate.setBounds (rz.withSizeKeepingCentre (juce::jmin (rz.getWidth(), 96),
                                                          juce::jmin (rz.getHeight(), 82)));
            }
            divisions.setRows (2);
            divisions.setBounds (b.removeFromTop (juce::jmin (62, b.getHeight() - 14)));
            return;
        }

        // Banda fina (p.ej. NÉBULA, 40px) → el selector LLENA la banda para igualar el alto de los chips.
        // Banda alta → se insetea 8px como antes (queda alto al lado del knob RATE).
        const int togInset = (b.getHeight() <= 44) ? 0 : 8;
        toggle.setBounds (b.removeFromLeft (juce::jmin (140, b.getWidth() / 2)).reduced (0, togInset));
        b.removeFromLeft (12);
        slot = b;
        if (hasRate) rate.setBounds (b.withSizeKeepingCentre (juce::jmin (b.getWidth(), 104), b.getHeight()));
        divisions.setRows (1);
        divisions.setBounds (b.withSizeKeepingCentre (b.getWidth(), juce::jmin (40, b.getHeight())));
    }

    void paint (juce::Graphics& g) override
    {
        if (vertical)
        {
            // caption bajo el contenido: "DIVISION" en SYNC (mockup §divCap) · "RATE" en FREE (kname)
            g.setColour (syncOn ? theme::fnt : theme::txt.withAlpha (0.92f));
            g.setFont (fonts::mono (syncOn ? 8.0f : 11.0f));
            if (syncOn)
                g.drawText ("DIVISION", slot.getX(), divisions.getBottom() + 4, slot.getWidth(), 12,
                            juce::Justification::centred);
            else if (hasRate)
                g.drawText ("RATE", slot.getX(), rate.getBottom() + 1, slot.getWidth(), 13,
                            juce::Justification::centred);
        }
        if (! hasRate && ! syncOn && caption.isNotEmpty())
        {
            g.setColour (theme::mut);
            g.setFont (fonts::mono (11.0f));
            g.drawText (caption, slot, juce::Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
        const bool s = apvts.getRawParameterValue (syncId)->load() >= 0.5f;
        if (s != syncOn) { syncOn = s; applyVisibility(); repaint(); }
    }
    void applyVisibility()
    {
        if (hasRate) rate.setVisible (! syncOn);
        divisions.setVisible (syncOn);
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String syncId, caption;
    bool hasRate = false, vertical = false;
    juce::Rectangle<int> slot;
    ovni::ui::KnobLookAndFeel knobLaf;
    ovni::ui::SegControl      toggle;      // [FREE|SYNC] ligado al bool sync (0=FREE · 1=SYNC)
    ovni::ui::SegControl      divisions;
    juce::Slider              rate;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttach;
    bool syncOn = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SyncControl)
};
}
