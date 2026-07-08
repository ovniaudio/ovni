#pragma once
#include "template/PluginEditorBase.h"   // ovni::PluginEditorBase (header browser del sello, S3 + ui-kit S2)
#include "ui-kit/KnobLookAndFeel.h"      // ovni::ui::KnobLookAndFeel (knob dimensional, S2)
#include "ui-kit/OutputMeter.h"          // ovni::ui::OutputMeter (meter desacoplado, S2)
#include "PluginProcessor.h"

// ========================================================================================================
// PluginEditor (_probe) — deriva el editor base del sello (que ya pinta el header browser + fondo atmósfera)
// y SÓLO agrega el cuerpo: 1 knob "mix" (hue cian = familia MOVIMIENTO) + el OutputMeter del sello.
// ========================================================================================================
class PluginEditor : public ovni::PluginEditorBase
{
public:
    explicit PluginEditor (PluginProcessor& p);
    ~PluginEditor() override;

protected:
    void layoutBody (juce::Rectangle<int> body) override;   // ubicar knob + meter bajo el header
    void paintBody  (juce::Graphics& g) override;           // caption del knob

private:
    ovni::ui::KnobLookAndFeel knobLAF;
    juce::Slider              mixKnob;
    ovni::ui::OutputMeter     meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttach;
    juce::Rectangle<int>      captionArea, knobArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
