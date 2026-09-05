#include "ui/TopBar.h"
#include "params/ParameterIDs.h"
#include "video/ExportPreset.h"
#include "image/CanvasFormat.h"
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

    // MEDIA SESSION PRO: ▦ = la tira de miniaturas (toggle) · FORMAT = formato del lienzo (chip → menú).
    styleToggle (mediaBtn, "Show / hide the MEDIA strip: your photos and videos, in order. Click a tile to cue it, "
                           "drag to reorder, right-click for more.");
    mediaBtn.onClick = [this] { editor.setMediaStripVisible (mediaBtn.getToggleState()); };
    styleAction (formatBtn, "Canvas format. AUTO follows your first photo (vertical = 9:16 for Reels), or pick "
                            "16:9 / 9:16 / 1:1 / 4:5 / 4:3. Also FIT (letterbox) or FILL (crop).");
    formatBtn.onClick = [this] { showFormatMenu(); };

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

    styleAction (seqBtn, "SEQUENCE: click for the clock (seconds / beats / kick), the order (loop / shuffle) "
                         "and the transition (cut / burst). Load 2+ photos to build a sequence.");
    seqBtn.onClick = [this] { showSeqMenu(); };
    styleAction (seqMinusBtn, "Faster: fewer seconds / beats / less gap per photo");
    styleAction (seqPlusBtn,  "Slower: more seconds / beats / more gap per photo");
    styleAction (seqPlayBtn,  "Play / pause the sequence (Space)");
    seqMinusBtn.onClick = [this] { editor.stepSequenceRate (-1); refreshSeqAndPreset(); };
    seqPlusBtn.onClick  = [this] { editor.stepSequenceRate (+1); refreshSeqAndPreset(); };
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
    // fotos × lo que dura cada una CON EL RELOJ ACTIVO (el export las cicla con ese mismo reloj): en BEATS,
    // un compás a BPM del host; en KICK no hay período previsible (depende de la música), así que se ofrece
    // la estimación por segundos, que es la que el export usa cuando no hay onsets grabados.
    auto& seq = proc.photoSequence();
    const bool   haveSeq   = seq.active();
    const double perPhotoS = seq.clock() == SeqClock::Beats ? secondsPerPhotoBeats (seq.intervalBeats(), proc.tempoBpm())
                                                            : seq.intervalSeconds();
    const int    loopSecs  = haveSeq ? juce::jlimit (2, 600, (int) std::ceil (seq.size() * perPhotoS)) : 0;

    const char* const fmtNames[] = { "1080p (16:9)", "4K (16:9)", "Square (1:1)", "Vertical 9:16 (Reels/TikTok)" };
    const int durs[] = { 8, 15, 30, 60, 120 };

    juce::PopupMenu m;
    m.addSectionHeader ("Export to video");
    // Sonido: muxea los últimos ~12s de audio VIVO (el mismo que ves reaccionar), loopeados en sync.
    m.addItem (90001, "With sound (loops the last 12s you heard)", true, exportWithSound);
    m.addSeparator();
    // MEDIA SESSION PRO: el formato que COINCIDE con el lienzo va primero, marcado "canvas" (9:16 para una
    // sesión vertical, 1:1 cuadrada, 1080p el resto). Los 4 siguen disponibles.
    const int pref = (int) defaultExportFormat (editor.canvasAspect());
    int order[4] = { pref, -1, -1, -1 };
    for (int fi = 0, k = 1; fi < 4; ++fi) if (fi != pref) order[k++] = fi;
    for (int k = 0; k < 4; ++k)
    {
        const int fi = order[k];
        juce::PopupMenu sub;
        for (int d : durs) sub.addItem (fi * 10000 + d, juce::String (d) + "s");
        if (haveSeq) sub.addItem (fi * 10000 + loopSecs, "Full photo loop (" + juce::String (loopSecs) + "s)");
        sub.addItem (fi * 10000 + 9999, juce::String::fromUTF8 ("Custom\xE2\x80\xA6"));   // duración libre
        m.addSubMenu (juce::String (fmtNames[fi]) + (fi == pref ? juce::String::fromUTF8 ("  \xC2\xB7 canvas") : juce::String()), sub);
    }

    // SafePointer (review): el menú es asíncrono de verdad — el host puede destruir el editor (y la barra)
    // con el menú abierto; el callback no debe tocar un `this` muerto.
    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportBtn),
                     [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto* self = safe.getComponent();
        if (r == 90001) { self->exportWithSound = ! self->exportWithSound; self->showExportMenu(); return; }   // toggle + re-abrir
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
            aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, fmt, aw] (int res)
            {
                if (safe == nullptr) return;
                auto* me = safe.getComponent();
                const int s = juce::jlimit (1, 600, aw->getTextEditorContents ("secs").getIntValue());
                if (res == 1 && ! me->editor.isExporting())
                {
                    me->exportBtn.setButtonText ("...");
                    me->exportBtn.setEnabled (false);
                    me->editor.exportVideo (fmt, s, 60, me->exportWithSound);
                }
            }), true);   // deleteWhenDismissed
            return;
        }
        const int secs = juce::jlimit (1, 600, code);
        self->exportBtn.setButtonText ("...");
        self->exportBtn.setEnabled (false);
        self->editor.exportVideo (fmt, secs, 60, self->exportWithSound);
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

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetsBtn),
                     [safe] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto* self = safe.getComponent();
        auto& pmm = self->proc.presets();
        if (r == 1) { self->savePresetDialog(); return; }

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
                self->editor.captureUndoState();    // aplicar un preset es reversible (Cmd/Ctrl+Z)
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
    juce::Component::SafePointer<TopBar> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe, w] (int res)
        {
            if (safe == nullptr) return;
            if (res == 1)
            {
                const auto nm = w->getTextEditorContents ("name").trim();
                if (nm.isNotEmpty()) safe->proc.presets().saveUser (nm);
            }
        }), true);   // deleteWhenDismissed
}

// FORMAT (MEDIA SESSION PRO): formato del lienzo + cómo entra la imagen (FIT / FILL).
void TopBar::showFormatMenu()
{
    const auto cur = editor.canvasFormat();
    const char* const labels[] = {
        "AUTO - follows your first photo (vertical = 9:16)",
        "FREE - fill the window",
        "16:9 - landscape / YouTube",
        "9:16 - vertical / Reels, TikTok, Shorts",
        "1:1 - square / feed",
        "4:5 - portrait / Instagram",
        "4:3 - classic",
    };
    juce::PopupMenu m;
    m.addSectionHeader ("Canvas format");
    for (int i = 0; i < (int) CanvasFormat::Count; ++i)
        m.addItem (100 + i, labels[i], true, (int) cur == i);
    if (cur == CanvasFormat::Auto)
        m.addItem (-1, juce::String ("   now: ") + aspectLabel (editor.canvasAspect()), false, false);
    m.addSeparator();
    m.addSectionHeader ("Image in the canvas");
    m.addItem (201, "FIT - whole image, letterboxed", true, editor.fitMode() == FitMode::Fit);
    m.addItem (202, "FILL - crop to fill the canvas", true, editor.fitMode() == FitMode::Fill);

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (formatBtn), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& ed = safe->editor;
        if (r >= 100 && r < 100 + (int) CanvasFormat::Count) ed.setCanvasFormat (canvasFormatFromInt (r - 100));
        else if (r == 201) ed.setFitMode (FitMode::Fit);
        else if (r == 202) ed.setFitMode (FitMode::Fill);
    });
}

// SEQ (MEDIA SESSION PRO): reloj / orden / transición de la secuencia.
void TopBar::showSeqMenu()
{
    auto& seq = proc.photoSequence();
    juce::PopupMenu m;
    m.addSectionHeader ("Sequence clock");
    m.addItem (301, "Seconds - every N seconds (- / +)",                     true, seq.clock() == SeqClock::Seconds);
    m.addItem (302, "Beats - every N beats of the tempo (host BPM / TAP)",   true, seq.clock() == SeqClock::Beats);
    m.addItem (303, "Kick - on every kick, with a minimum gap (- / +)",     true, seq.clock() == SeqClock::Kick);
    m.addSeparator();
    m.addSectionHeader ("Order");
    m.addItem (311, "Loop - in order",                                       true, seq.orderMode() == SeqOrder::Loop);
    m.addItem (312, "Shuffle - random, never the same twice",                true, seq.orderMode() == SeqOrder::Shuffle);
    m.addSeparator();
    m.addSectionHeader ("Transition");
    m.addItem (321, "Cut - the particles travel to the new photo",           true, ! seq.burst());
    m.addItem (322, "Burst - explode, then re-form as the new photo",        true, seq.burst());
    m.addSeparator();
    m.addSectionHeader ("MIDI cue (notes 72-87 = tiles, 88/89/90 = next/prev/random)");
    m.addItem (341, "MIDI cue: on the bar - same as the strip",  true, ! editor.midiCueImmediate());
    m.addItem (342, "MIDI cue: now - cut on the note",           true, editor.midiCueImmediate());
    m.addSeparator();
    m.addItem (331, "Show media strip", true, editor.isMediaStripVisible());

    juce::Component::SafePointer<TopBar> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (seqBtn), [safe] (int r)
    {
        if (safe == nullptr) return;
        auto& ed = safe->editor;
        switch (r)
        {
            case 301: ed.setSequenceClock (SeqClock::Seconds); break;
            case 302: ed.setSequenceClock (SeqClock::Beats);   break;
            case 303: ed.setSequenceClock (SeqClock::Kick);    break;
            case 311: ed.setSequenceOrder (SeqOrder::Loop);    break;
            case 312: ed.setSequenceOrder (SeqOrder::Shuffle); break;
            case 321: ed.setSequenceBurst (false);             break;
            case 322: ed.setSequenceBurst (true);              break;
            case 331: ed.setMediaStripVisible (! ed.isMediaStripVisible()); break;
            case 341: ed.setMidiCueNow (false);                break;
            case 342: ed.setMidiCueNow (true);                 break;
            default: return;
        }
        safe->refreshSeqAndPreset();
    });
}

void TopBar::refreshSeqAndPreset()
{
    if (auto* pp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::PRESET)))
        presetLabel.setText (pp->getCurrentChoiceName(), juce::dontSendNotification);

    auto& seq = proc.photoSequence();
    const bool on = seq.active();
    seqBtn.setAlpha      (on ? 1.0f : 0.55f);
    seqMinusBtn.setAlpha (on ? 1.0f : 0.55f);
    seqPlusBtn.setAlpha  (on ? 1.0f : 0.55f);
    seqPlayBtn.setEnabled (on);

    auto num = [] (double v, int decimals)   // "8" · "0.25" · "1.5" (sin ceros muertos)
    {
        juce::String s (v, decimals);
        if (s.containsChar ('.')) s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s;
    };
    juce::String rate;
    switch (seq.clock())
    {
        case SeqClock::Seconds: rate = num (seq.intervalSeconds(), 0) + "s"; break;
        case SeqClock::Beats:   rate = num (seq.intervalBeats(), 0) + (seq.intervalBeats() == 1.0 ? " beat" : " beats"); break;
        case SeqClock::Kick:    rate = "KICK " + juce::String::fromUTF8 ("\xE2\x89\xA5") + num (seq.kickGapSeconds(), 2) + "s"; break;
    }
    const juce::String dot = juce::String::fromUTF8 (" \xC2\xB7 ");
    if (on)
        seqBtn.setButtonText ("SEQ " + juce::String (seq.currentIndex() + 1) + "/" + juce::String (seq.size()) + dot + rate);
    else
        seqBtn.setButtonText ("SEQ" + dot + rate + (seq.clock() == SeqClock::Seconds ? "/img" : ""));
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
    mediaBtn.setToggleState (editor.isMediaStripVisible(), juce::dontSendNotification);
    const auto chip = editor.canvasChipText();
    if (formatBtn.getButtonText() != chip) formatBtn.setButtonText (chip);

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

// Fila 2 (idéntica app/plugin): [CLEAR LOAD ⟳ ▦ EXPORT FORMAT LFO PRESETS] … [◂ mundo ▸ ⊞ centrado] … [SEQ − + ▸]
void TopBar::layoutRow2 (juce::Rectangle<int> row2)
{
    clearBtn.setBounds  (row2.removeFromLeft (58));
    row2.removeFromLeft (6);
    imageBtn.setBounds  (row2.removeFromLeft (52));
    row2.removeFromLeft (6);
    rotateBtn.setBounds (row2.removeFromLeft (30));
    row2.removeFromLeft (6);
    mediaBtn.setBounds  (row2.removeFromLeft (30));    // ▦ tira de media
    row2.removeFromLeft (6);
    exportBtn.setBounds (row2.removeFromLeft (64));
    row2.removeFromLeft (6);
    formatBtn.setBounds (row2.removeFromLeft (76));    // chip del lienzo: "AUTO 9:16"
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
    seqBtn.setBounds      (row2.removeFromRight (118));

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
