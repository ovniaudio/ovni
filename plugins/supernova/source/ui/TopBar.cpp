#include "ui/TopBar.h"
#include "params/ParameterIDs.h"
#include "video/ExportPreset.h"
#include "ui/theme.h"          // look::hue (fuego SUPERNOVA)
#include "ui-kit/Theme.h"      // ovni::ui::theme (paleta del sello)
#include "ui-kit/Fonts.h"      // ovni::ui::fonts (ClashGrotesk/GeneralSans/JetBrainsMono)

namespace supernova {

namespace {
namespace pid   = supernova::params::id;
namespace theme = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;
const juce::Colour kHue = look::hue;   // naranja fuego del plugin
}

// ================================================================================ look del sello
TopBar::BarLnf::BarLnf()
{
    setColour (juce::PopupMenu::backgroundColourId, theme::surf2);
    setColour (juce::PopupMenu::textColourId, theme::txt);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kHue.withAlpha (0.22f));
    setColour (juce::PopupMenu::highlightedTextColourId, theme::txt);
    setColour (juce::TextButton::textColourOffId, theme::txt);
    setColour (juce::TextButton::textColourOnId, kHue);
    setColour (juce::ComboBox::textColourId, theme::txt);
    setColour (juce::Slider::textBoxTextColourId, theme::txt);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, theme::txt);
    setColour (juce::TooltipWindow::backgroundColourId, theme::surf2);
    setColour (juce::TooltipWindow::textColourId, theme::txt);
}

void TopBar::BarLnf::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                           bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    g.setColour (on ? kHue.withAlpha (0.16f) : theme::surf2);
    g.fillRoundedRectangle (r, 5.0f);
    if (down || highlighted)
    {
        g.setColour (kHue.withAlpha (down ? 0.26f : 0.10f));
        g.fillRoundedRectangle (r, 5.0f);
    }
    g.setColour (on ? kHue.withAlpha (0.85f) : theme::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font TopBar::BarLnf::getTextButtonFont (juce::TextButton&, int)
{
    return fonts::body (12.5f);
}

void TopBar::BarLnf::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f);
    g.setColour (theme::surf2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (theme::line);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    // flecha ▾
    juce::Path p;
    const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (theme::mut);
    g.fillPath (p);
}

juce::Font TopBar::BarLnf::getComboBoxFont (juce::ComboBox&) { return fonts::body (13.0f); }

void TopBar::BarLnf::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float, float, juce::Slider::SliderStyle, juce::Slider&)
{
    const float cy = (float) y + (float) h * 0.5f;
    g.setColour (theme::surf2.brighter (0.15f));
    g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
    g.setColour (kHue.withAlpha (0.75f));
    g.fillRoundedRectangle ((float) x, cy - 1.5f, pos - (float) x, 3.0f, 1.5f);
    g.setColour (kHue);
    g.fillEllipse (pos - 5.0f, cy - 5.0f, 10.0f, 10.0f);
}

// ================================================================================ ctor
TopBar::TopBar (SupernovaEditor& ed, SupernovaProcessor& p)
    : editor (ed), proc (p)
{
    setLookAndFeel (&lnf);

    // ---- fila 1 · AUDIO ----
    inputLabel.setText ("INPUT", juce::dontSendNotification);
    inputLabel.setFont (fonts::mono (10.0f));
    inputLabel.setColour (juce::Label::textColourId, theme::mut);
    inputLabel.setTooltip ("SUPERNOVA listens to the track it's inserted on. The audio passes through "
                           "bit-exact - it never alters the sound.");
    addAndMakeVisible (inputLabel);

    gainLabel.setText ("IN", juce::dontSendNotification);
    gainLabel.setFont (fonts::mono (10.0f));
    gainLabel.setColour (juce::Label::textColourId, theme::mut);
    addAndMakeVisible (gainLabel);

    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    gain.setTextValueSuffix (" dB");
    gain.setTooltip ("Visual sensitivity (analysis input gain). Never touches the audio.");
    if (auto* vp = proc.apvts.getParameter (pid::VIS_GAIN))
        gainAttach = std::make_unique<juce::SliderParameterAttachment> (*vp, gain);
    addAndMakeVisible (gain);

    auto styleToggle = [this] (juce::TextButton& b, const juce::String& tip)
    {
        b.setClickingTogglesState (true);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    styleToggle (immersiveBtn,  "Immersive mode (Tab): show/hide the knobs");
    styleToggle (fullscreenBtn, "Fullscreen on a monitor (F)");
    styleToggle (syphonBtn,     "Publish the visual over Syphon (OBS/Resolume)");

    immersiveBtn.onClick  = [this] { editor.setImmersive (immersiveBtn.getToggleState()); };
    fullscreenBtn.onClick = [this] { editor.setFullscreen (fullscreenBtn.getToggleState()); };
    syphonBtn.onClick     = [this] { editor.setSyphonEnabled (syphonBtn.getToggleState()); };

    // TEMPO (Phase B · BeatClock): inside a DAW the BPM follows the host; standalone, TAP sets it.
    tapBtn.setTooltip ("Tap in time to set the tempo (BeatClock). Inside a DAW the BPM follows the host.");
    tapBtn.onClick = [this] { proc.tapTempo(); };
    addAndMakeVisible (tapBtn);
    bpmLabel.setJustificationType (juce::Justification::centredLeft);
    bpmLabel.setColour (juce::Label::textColourId, theme::mut);
    bpmLabel.setFont (fonts::mono (11.0f));
    bpmLabel.setTooltip ("Tempo (BPM) - host inside a DAW, tap in the standalone app.");
    addAndMakeVisible (bpmLabel);

    // ---- fila 2 · MEDIOS ----
    auto styleAction = [this] (juce::TextButton& b, const juce::String& tip)
    {
        b.setClickingTogglesState (false);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };
    styleAction (clearBtn,  "CLEAR - clean, still canvas: design from scratch (INTENSITY brings it back to life)");
    styleAction (imageBtn,  "Load photos or videos (multi-select = sequence). You can also drag them onto the visual.");
    styleAction (rotateBtn, "Rotate the current photo/video 90 degrees");
    clearBtn.onClick  = [this] { editor.clearCanvas(); };
    imageBtn.onClick  = [this] { editor.openMediaPicker(); };
    rotateBtn.onClick = [this] { editor.rotateMedia(); };
    rotateBtn.setEnabled (false);

    // EXPORT (Phase C): a video MP4 — menú de formatos (1080p / 4K / 1:1 / 9:16 vertical). Renderiza el look
    // actual con el audio reciente a un archivo en ~/Movies/SUPERNOVA.
    styleAction (exportBtn, "Export the current look to a video (MP4): 1080p, 4K, square 1:1, or vertical 9:16");
    exportBtn.onClick = [this] { showExportMenu(); };
    editor.onExportDone = [this] (bool ok, juce::String msg)
    {
        exportBtn.setButtonText ("EXPORT");
        exportBtn.setEnabled (true);
        if (ok) juce::File (msg).revealToUser();   // muestra el MP4 en el Finder
    };

    styleAction (presetPrevBtn, "Previous world");
    styleAction (presetNextBtn, "Next world");
    presetPrevBtn.onClick = [this] { stepPreset (-1); };
    presetNextBtn.onClick = [this] { stepPreset (+1); };
    styleAction (worldsBtn, "Browse all the worlds as a live thumbnail grid");
    worldsBtn.onClick = [this] { editor.toggleWorldBrowser(); };
    styleAction (lfoBtn, "Tempo-synced LFOs - modulate any param to the beat");
    lfoBtn.onClick = [this] { editor.toggleLfoPanel(); };
    styleAction (presetsBtn, "Save the current look as your own preset, or recall a saved one");
    presetsBtn.onClick = [this] { showPresetsMenu(); };
    presetLabel.setJustificationType (juce::Justification::centred);
    presetLabel.setColour (juce::Label::textColourId, theme::txt);
    presetLabel.setFont (fonts::display (15.0f));
    presetLabel.setTooltip ("Active world (preset)");
    addAndMakeVisible (presetLabel);

    seqLabel.setJustificationType (juce::Justification::centredRight);
    seqLabel.setColour (juce::Label::textColourId, theme::mut);
    seqLabel.setFont (fonts::mono (11.0f));
    seqLabel.setTooltip ("PHOTO SEQUENCE - seconds per photo (use - / +; load 2+ photos to build it)");
    addAndMakeVisible (seqLabel);
    styleAction (seqMinusBtn, "Fewer seconds per photo");
    styleAction (seqPlusBtn,  "More seconds per photo");
    styleAction (seqPlayBtn,  "Play / pause the sequence (Space)");
    seqMinusBtn.onClick = [this] { stepSeqSeconds (-1.0); };
    seqPlusBtn.onClick  = [this] { stepSeqSeconds (+1.0); };
    seqPlayBtn.onClick  = [this] { editor.toggleSequencePlayback(); };

    refreshSeqAndPreset();
    startTimerHz (30);
}

TopBar::~TopBar()
{
    stopTimer();
    editor.onExportDone = nullptr;   // defensa: la barra que lo seteó muere antes que el editor (review)
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------- fuentes de datos
// Plugin: el pico de salida del chasis (pass-through bit-exacto ⇒ ES la entrada del insert), pre-visGain —
// la MISMA semántica que el medidor de la app (pico crudo pre-gain del AppAudioEngine).
float TopBar::meterLevel() const
{
    return proc.uiOutPeak.load (std::memory_order_relaxed);
}

void TopBar::setDragHover (bool on)
{
    if (dragHover == on) return;
    dragHover = on;
    repaint();
}

void TopBar::visibilityChanged()
{
    // La barra interna del editor queda OCULTA en app-mode (la app pone su AppTopBar): sin poll fantasma.
    if (isVisible()) startTimerHz (30);
    else             stopTimer();
}

// ---------------------------------------------------------------------------- preset + secuencia
void TopBar::stepPreset (int delta)
{
    if (auto* pp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::PRESET)))
    {
        const int n   = pp->choices.size();
        const int nxt = ((pp->getIndex() + delta) % n + n) % n;   // wrap
        pp->beginChangeGesture();
        *pp = nxt;                                                // el editor morphea al nuevo mundo
        pp->endChangeGesture();
    }
}

void TopBar::showExportMenu()
{
    if (editor.isExporting()) return;

    // Duración seleccionable. Si hay una secuencia de fotos activa, ofrecemos "Full photo loop" = todas las
    // fotos × segundos c/u (el export las CICLA). id = formato*10000 + segundos.
    auto& seq = proc.photoSequence();
    const bool haveSeq  = seq.active();
    const int  loopSecs = haveSeq ? juce::jlimit (2, 600, (int) std::ceil (seq.size() * seq.intervalSeconds())) : 0;

    const char* const fmtNames[] = { "1080p (16:9)", "4K (16:9)", "Square (1:1)", "Vertical 9:16 (Reels/TikTok)" };
    const int durs[] = { 8, 15, 30, 60, 120 };

    juce::PopupMenu m;
    m.addSectionHeader ("Export to video");
    // Sonido: muxea los últimos ~12s de audio VIVO (el mismo que ves reaccionar), loopeados en sync.
    m.addItem (90001, "With sound (loops the last 12s you heard)", true, exportWithSound);
    m.addSeparator();
    for (int fi = 0; fi < 4; ++fi)
    {
        juce::PopupMenu sub;
        for (int d : durs) sub.addItem (fi * 10000 + d, juce::String (d) + "s");
        if (haveSeq) sub.addItem (fi * 10000 + loopSecs, "Full photo loop (" + juce::String (loopSecs) + "s)");
        sub.addItem (fi * 10000 + 9999, juce::String::fromUTF8 ("Custom\xE2\x80\xA6"));   // duración libre
        m.addSubMenu (fmtNames[fi], sub);
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportBtn),
                     [this] (int r)
    {
        if (r == 0) return;
        if (r == 90001) { exportWithSound = ! exportWithSound; showExportMenu(); return; }   // toggle + re-abrir
        const int fi   = r / 10000;
        const int code = r % 10000;
        const ExportFormat fmt = (ExportFormat) juce::jlimit (0, 3, fi);
        if (code == 9999)   // Custom…: los segundos que quiera la persona (1-600)
        {
            auto* aw = new juce::AlertWindow ("Export duration", "Length in seconds (1-600):",
                                              juce::MessageBoxIconType::NoIcon);
            aw->addTextEditor ("secs", "45");
            aw->addButton ("Export", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, fmt, aw] (int res)
            {
                const int s = juce::jlimit (1, 600, aw->getTextEditorContents ("secs").getIntValue());
                if (res == 1 && ! editor.isExporting())
                {
                    exportBtn.setButtonText ("...");
                    exportBtn.setEnabled (false);
                    editor.exportVideo (fmt, s, 60, exportWithSound);
                }
            }), true);   // deleteWhenDismissed
            return;
        }
        const int secs = juce::jlimit (1, 600, code);
        exportBtn.setButtonText ("...");
        exportBtn.setEnabled (false);
        editor.exportVideo (fmt, secs, 60, exportWithSound);
    });
}

// ---------------------------------------------------------------------------- presets de usuario
// El chrome de catálogo (header) no existe en la barra: SAVE + la lista de presets del usuario viven acá
// (el chasis ya persiste el APVTS entero en ~/Library/.../OVNI SUPERNOVA/User Presets). Aplicar es
// UNDOABLE: capturamos el estado antes → Cmd/Ctrl+Z lo revierte.
void TopBar::showPresetsMenu()
{
    auto& pm = proc.presets();
    const auto users = pm.userPresets();

    juce::PopupMenu m;
    m.addSectionHeader ("Presets");
    m.addItem (1, "Save current\xE2\x80\xA6");   // "Save current…"

    if (users.size() > 0)
    {
        m.addSeparator();
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension());

        juce::PopupMenu del;
        for (int i = 0; i < users.size(); ++i)
            del.addItem (2000 + i, users[i].getFileNameWithoutExtension());
        m.addSeparator();
        m.addSubMenu ("Delete", del);
    }
    else
    {
        m.addSeparator();
        m.addItem (-1, "No saved presets yet", false, false);   // ítem informativo deshabilitado
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsBtn),
                     [this] (int r)
    {
        if (r <= 0) return;
        auto& pmm = proc.presets();
        if (r == 1) { savePresetDialog(); return; }

        const auto us = pmm.userPresets();
        if (r >= 2000)
        {
            const int idx = r - 2000;
            if (idx >= 0 && idx < us.size()) pmm.deleteUser (us[idx]);
        }
        else if (r >= 1000)
        {
            const int idx = r - 1000;
            if (idx >= 0 && idx < us.size())
            {
                editor.captureUndoState();          // aplicar un preset es reversible (Cmd/Ctrl+Z)
                pmm.applyUserFile (us[idx]);
            }
        }
    });
}

void TopBar::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Save Preset",
                                     "Name this preset — it saves the full look (every parameter) to your "
                                     "user library.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "My Preset");
    w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, w] (int res)
        {
            if (res == 1)
            {
                const auto nm = w->getTextEditorContents ("name").trim();
                if (nm.isNotEmpty()) proc.presets().saveUser (nm);
            }
        }), true);   // deleteWhenDismissed
}

void TopBar::stepSeqSeconds (double delta)
{
    auto& seq = proc.photoSequence();
    seq.setIntervalSeconds (seq.intervalSeconds() + delta);
    proc.syncSequenceToState();
    refreshSeqAndPreset();
}

void TopBar::refreshSeqAndPreset()
{
    if (auto* pp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::PRESET)))
        presetLabel.setText (pp->getCurrentChoiceName(), juce::dontSendNotification);

    auto& seq = proc.photoSequence();
    const bool on = seq.active();
    seqLabel.setAlpha    (on ? 1.0f : 0.55f);
    seqMinusBtn.setAlpha (on ? 1.0f : 0.55f);
    seqPlusBtn.setAlpha  (on ? 1.0f : 0.55f);
    seqPlayBtn.setEnabled (on);
    if (on)
        seqLabel.setText ("SEQ " + juce::String (seq.currentIndex() + 1) + "/" + juce::String (seq.size())
                              + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (seq.intervalSeconds(), 0) + "s",
                          juce::dontSendNotification);
    else
        seqLabel.setText ("SEQ " + juce::String::fromUTF8 ("\xC2\xB7 ") + juce::String (seq.intervalSeconds(), 0)
                              + "s/img", juce::dontSendNotification);
    seqPlayBtn.setButtonText (juce::String::fromUTF8 (seq.playing() ? "\xE2\x8F\xB8" : "\xE2\x96\xB8"));
}

// ---------------------------------------------------------------------------- poll 30Hz
void TopBar::timerCallback()
{
    const float lvl = juce::jlimit (0.0f, 1.0f, meterLevel());
    meterSmoothed = juce::jmax (lvl, meterSmoothed * 0.82f);
    repaint (meterRect);

    bpmLabel.setText (juce::String (proc.tempoBpm(), 1) + " BPM", juce::dontSendNotification);

    immersiveBtn.setToggleState (editor.isImmersive(),   juce::dontSendNotification);
    fullscreenBtn.setToggleState (editor.isFullscreen(), juce::dontSendNotification);
    syphonBtn.setToggleState (editor.isSyphonActive(),   juce::dontSendNotification);
    rotateBtn.setEnabled (editor.mediaRotatable());

    refreshSeqAndPreset();
}

// ---------------------------------------------------------------------------- layout / paint
void TopBar::resized()
{
    auto r = getLocalBounds();
    layoutRow1 (r.removeFromTop (r.getHeight() / 2).reduced (10, 6));
    layoutRow2 (r.reduced (10, 6));
}

// Cluster común derecho de la fila 1 (de afuera hacia adentro): TAP · BPM · SYPHON · ⛶ · ⤢.
// La app inserta los suyos (MIC/MIDI, TOP) entre medio replicando este orden en su override.
void TopBar::layoutRow1Right (juce::Rectangle<int>& row1)
{
    tapBtn.setBounds        (row1.removeFromRight (40));
    row1.removeFromRight (4);
    bpmLabel.setBounds      (row1.removeFromRight (60));
    row1.removeFromRight (8);
    syphonBtn.setBounds     (row1.removeFromRight (64));
    row1.removeFromRight (6);
    fullscreenBtn.setBounds (row1.removeFromRight (30));
    row1.removeFromRight (6);
    immersiveBtn.setBounds  (row1.removeFromRight (30));
    row1.removeFromRight (14);
}

// Fila 1 default (PLUGIN): INPUT · medidor · IN gain a la izquierda; el cluster común a la derecha.
void TopBar::layoutRow1 (juce::Rectangle<int> row1)
{
    layoutRow1Right (row1);

    inputLabel.setBounds (row1.removeFromLeft (50));
    row1.removeFromLeft (6);
    meterRect = row1.removeFromLeft (84).reduced (0, 6);
    row1.removeFromLeft (12);
    gainLabel.setBounds (row1.removeFromLeft (20));
    gain.setBounds (row1);
}

// Fila 2 (idéntica app/plugin): [CLEAR LOAD ⟳ EXPORT LFO PRESETS] … [◂ mundo ▸ ⊞ centrado] … [SEQ − + ▸]
void TopBar::layoutRow2 (juce::Rectangle<int> row2)
{
    clearBtn.setBounds  (row2.removeFromLeft (58));
    row2.removeFromLeft (6);
    imageBtn.setBounds  (row2.removeFromLeft (52));
    row2.removeFromLeft (6);
    rotateBtn.setBounds (row2.removeFromLeft (30));
    row2.removeFromLeft (6);
    exportBtn.setBounds (row2.removeFromLeft (64));
    row2.removeFromLeft (6);
    lfoBtn.setBounds    (row2.removeFromLeft (42));
    row2.removeFromLeft (6);
    presetsBtn.setBounds (row2.removeFromLeft (74));

    seqPlayBtn.setBounds  (row2.removeFromRight (30));
    row2.removeFromRight (4);
    seqPlusBtn.setBounds  (row2.removeFromRight (26));
    row2.removeFromRight (2);
    seqMinusBtn.setBounds (row2.removeFromRight (26));
    row2.removeFromRight (6);
    seqLabel.setBounds    (row2.removeFromRight (100));

    // Cluster de mundo CENTRADO como unidad fija: ◂ [nombre] ▸ ⊞.
    auto centre = row2.withSizeKeepingCentre (juce::jmin (276, row2.getWidth()), row2.getHeight());
    presetPrevBtn.setBounds (centre.removeFromLeft (30));
    worldsBtn.setBounds     (centre.removeFromRight (30));   // ⊞ grilla de mundos
    centre.removeFromRight (4);
    presetNextBtn.setBounds (centre.removeFromRight (30));
    presetLabel.setBounds   (centre);
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg1);
    g.setColour (theme::lineSoft);
    g.drawHorizontalLine (getHeight() / 2, 10.0f, (float) getWidth() - 10.0f);
    g.setColour (theme::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    if (! meterRect.isEmpty())
    {
        g.setColour (theme::surf2);
        g.fillRoundedRectangle (meterRect.toFloat(), 3.0f);
        g.setColour (theme::line);
        g.drawRoundedRectangle (meterRect.toFloat().reduced (0.5f), 3.0f, 1.0f);
        auto fill = meterRect.toFloat().reduced (2.0f);
        fill.setWidth (fill.getWidth() * juce::jlimit (0.0f, 1.0f, meterSmoothed));
        const juce::Colour c = meterSmoothed > 0.9f ? theme::red
                              : meterSmoothed > 0.6f ? theme::amber
                                                     : kHue;
        g.setColour (c.withAlpha (0.9f));
        g.fillRoundedRectangle (fill, 2.0f);
    }

    if (dragHover)   // feedback del drop: la vista Metal ocluye el pintado JUCE → la barra da la señal
    {
        g.setColour (kHue.withAlpha (0.22f));
        g.fillAll();
    }
}

} // namespace supernova
