#pragma once
// LfoPanel (Phase B UI) — configures the 4 tempo-synced LFOs, one card per slot:
//   ON · TARGET · RATE (beat division) · SHAPE (clickable waveform icons) · DEPTH.
// Writes the processor's LfoBank (persisted). Shown as an OVERLAY with the Metal view hidden (like the
// WorldBrowser) → no occlusion. The modulation is already wired to the visual (PluginEditor::lfoModulation):
// the instant you enable a slot, the chosen param breathes to the beat.
//
// SHAPE is picked from a strip of 6 icons, each DRAWING its real curve (sampled from LfoBank::wave) —
// no text ComboBox. Verifiable without the app by rendering the component to PNG (see UiSnapshotTest).
#include <juce_gui_basics/juce_gui_basics.h>
#include "tempo/LfoBank.h"
#include <functional>
#include <array>
#include <memory>

namespace supernova
{
// A row of 6 clickable waveform icons — each cell draws the real curve from LfoBank::wave, so the
// selector shows the SHAPE instead of naming it. Hover highlights; the tooltip names the shape.
class ShapeStrip final : public juce::Component,
                         public juce::TooltipClient
{
public:
    ShapeStrip();

    void setSelected (int s);
    int  selected() const noexcept { return sel; }
    std::function<void (int)> onSelect;             // fired on click with the new shape index

    // Draws one waveform (one cycle; Sample & Hold shows a staircase) into r, in the given colour.
    static void drawWave (juce::Graphics&, LfoShape, juce::Rectangle<float> r, juce::Colour);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    int cellAt (juce::Point<float>) const noexcept;

    int sel   = 0;
    int hover = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapeStrip)
};

class LfoPanel : public juce::Component,
                 private juce::Timer
{
public:
    explicit LfoPanel (LfoBank& bank);
    ~LfoPanel() override;

    void refreshFromBank();                    // re-reads the bank → repopulates the controls
    std::function<void()> onChange;            // the editor persists (syncLfosToState)
    std::function<void()> onClose;
    std::function<double()> beatPos;           // live beat position (editor: proc.phaseInBeats) → OUTPUT meter

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::TextButton enable { "ON" };       // pill toggle (BarLnf-style, hue when on)
        juce::ComboBox   target, rate;
        ShapeStrip       shape;
        juce::Slider     depth;
    };
    void pushRow (int i);                        // controls of row i → slot i of the bank
    void applyEnabledLook (int i);              // dim the row when the slot is off
    void timerCallback() override;               // 30Hz: repinta SOLO los medidores de salida

    LfoBank& bank;
    std::unique_ptr<juce::LookAndFeel> lnf;      // local pro look (mirrors AppTopBar::BarLnf)
    std::array<Row, LfoBank::kNum> rows;
    juce::TextButton closeBtn { "CLOSE" };

    // Geometry captured in resized() for paint() (card backgrounds + column headers + output meters).
    std::array<juce::Rectangle<int>, LfoBank::kNum> cardRects {};
    std::array<juce::Rectangle<int>, LfoBank::kNum> meterRects {};   // live OUTPUT bar per card
    juce::Rectangle<int> contentArea, targetHead, rateHead, shapeHead, depthHead;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoPanel)
};
}
