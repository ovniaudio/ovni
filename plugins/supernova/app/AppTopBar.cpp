#include "AppTopBar.h"
#include "ui/theme.h"          // trae ui-kit/Theme.h (el hue de SUPERNOVA lo usa el aviso)
#include "ui-kit/Theme.h"      // ovni::ui::theme (paleta del sello)
#include "ui-kit/Fonts.h"      // ovni::ui::fonts
#include "AppRelaunch.h"       // REOPEN: relanza ESTA copia (bundleURL), nunca por bundle id

namespace supernova {

namespace {
constexpr int kSystemItemId = 1;   // el resto de los ids del combo son 2 + índice de device
namespace theme = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;
}

// ================================================================================ ctor
AppTopBar::AppTopBar (AppAudioEngine& e, SupernovaEditor& ed, SupernovaProcessor& p)
    : TopBar (ed, p), engine (e)
{
    // La base arma el resto de la barra (medidor, gain→visGain, toggles, TAP/BPM, fila de MEDIOS).
    inputLabel.setVisible (false);   // la app tiene selector de fuente: el label INPUT es del plugin

    sourceLabel.setText ("SOURCE", juce::dontSendNotification);
    sourceLabel.setFont (fonts::mono (10.0f));
    sourceLabel.setColour (juce::Label::textColourId, theme::mut);
    addAndMakeVisible (sourceLabel);

    sourceBox.setTooltip ("Where SUPERNOVA listens. System Audio = whatever is playing on this Mac "
                          "(Spotify/YouTube/Ableton), no drivers.");
    sourceBox.onChange = [this] { sourceChanged(); };
    addAndMakeVisible (sourceBox);

    // Aviso de permiso (oculto hasta que haga falta). ALLOW pide el permiso — es lo ÚNICO que deja salir
    // el cartel de macOS; OPEN lleva al panel exacto de Ajustes; REOPEN relanza ESTA copia.
    notice.onAction = [this]
    {
        if (notice.mode() == NoticeMode::denied) openPrivacySettings();
        else                                     engine.requestSystemAudioPermission();
        refreshPermissionUi();
    };
    notice.onReopen = [this] { relaunchApp(); };
    notice.setVisible (false);
    addChildComponent (notice);

    topBtn.setClickingTogglesState (true);
    topBtn.setTooltip ("Always on top");
    topBtn.onClick = [this] { if (onAlwaysOnTop) onAlwaysOnTop (topBtn.getToggleState()); };
    addAndMakeVisible (topBtn);

    setupBtn.setTooltip ("Choose mic/interface and MIDI. The app does NOT play audio: it only analyzes it "
                         "to drive the visual.");
    setupBtn.onClick = [this] { openAudioSetup(); };
    addAndMakeVisible (setupBtn);

    refreshSources();
}

AppTopBar::~AppTopBar()
{
    stopTimer();
    if (setupDialog != nullptr) setupDialog->exitModalState (0);
}

// ---------------------------------------------------------------------------- fuentes de audio
void AppTopBar::refreshSources()
{
    sourceBox.clear (juce::dontSendNotification);
    sourceBox.addItem ("System Audio (this Mac)", kSystemItemId);

    if (auto* type = engine.deviceManager().getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        const auto names = type->getDeviceNames (true);   // inputs
        for (int i = 0; i < names.size(); ++i)
            sourceBox.addItem (names[i], 2 + i);
    }

    if (engine.model().kind() == AudioSourceModel::Kind::inputDevice
        && engine.model().deviceName().isNotEmpty())
    {
        for (int i = 1; i < sourceBox.getNumItems(); ++i)
            if (sourceBox.getItemText (i) == engine.model().deviceName())
                { sourceBox.setSelectedItemIndex (i, juce::dontSendNotification); break; }
    }
    else
    {
        sourceBox.setSelectedId (kSystemItemId, juce::dontSendNotification);
    }
}

void AppTopBar::sourceChanged()
{
    if (sourceBox.getSelectedId() == kSystemItemId)
        engine.useSystemAudio();
    else
        engine.useInputDevice (sourceBox.getText());
}

void AppTopBar::openAudioSetup()
{
    // Solo ENTRADA + MIDI (la app no reproduce audio: sin selector de salida, sin opciones avanzadas).
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.deviceManager(), 0, 2, 0, 0,
        true,   // MIDI inputs (nota -> explosion/rayo)
        false, true, true);
    selector->setSize (440, 320);

    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector.release());
    o.dialogTitle = "Audio & MIDI Input";
    o.dialogBackgroundColour = theme::surf;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    setupDialog = o.launchAsync();
}

// El panel correcto depende del backend: 14.2+ pide "System Audio Recording", 13 … 14.1 el de pantalla.
void AppTopBar::openPrivacySettings()
{
    juce::URL (settingsPaneUrl (engine.captureBackend())).launchInDefaultBrowser();
}

void AppTopBar::relaunchApp() { relaunchThisBundle(); }   // ver app/AppRelaunch.h

// ---------------------------------------------------------------------------- permiso
void AppTopBar::refreshPermissionUi()
{
    const auto mode = noticeModel.update (engine.permissionState(),
                                          juce::Time::getMillisecondCounterHiRes());
    const bool show = engine.model().kind() == AudioSourceModel::Kind::systemAudio
                    && mode != NoticeMode::hidden;

    notice.setState (mode, engine.captureBackend());   // barato: sólo re-textea si algo cambió
    if (permissionUi == show) return;

    permissionUi = show;
    notice.setVisible (show);
    gainLabel.setVisible (! show);
    gain.setVisible (! show);
    resized();
    repaint();
}

// ---------------------------------------------------------------------------- poll 30Hz
void AppTopBar::timerCallback()
{
    TopBar::timerCallback();   // medidor (engine.meterLevel via virtual) + BPM + toggles + rotate + SEQ

    engine.pollSystemAudio();  // gate ↔ backend (incluye el reintento lento si quedó denegado)
    refreshPermissionUi();
}

// ---------------------------------------------------------------------------- layout (fila 1 de la APP)
// Derecha (de afuera hacia adentro): MIC/MIDI · TAP · BPM · TOP · SYPHON · ⛶ · ⤢; izquierda: SOURCE;
// el medio es medidor+gain O el banner de permiso. (La fila 2 la lay-outea la base, idéntica al plugin.)
void AppTopBar::layoutRow1 (juce::Rectangle<int> row1)
{
    setupBtn.setBounds      (row1.removeFromRight (74));
    row1.removeFromRight (6);
    tapBtn.setBounds        (row1.removeFromRight (40));
    row1.removeFromRight (4);
    bpmLabel.setBounds      (row1.removeFromRight (60));
    row1.removeFromRight (8);
    topBtn.setBounds        (row1.removeFromRight (44));
    row1.removeFromRight (6);
    syphonBtn.setBounds     (row1.removeFromRight (64));
    row1.removeFromRight (6);
    fullscreenBtn.setBounds (row1.removeFromRight (30));
    row1.removeFromRight (6);
    immersiveBtn.setBounds  (row1.removeFromRight (30));
    row1.removeFromRight (14);

    sourceLabel.setBounds (row1.removeFromLeft (50));
    sourceBox.setBounds   (row1.removeFromLeft (215));
    row1.removeFromLeft (12);

    if (permissionUi)
    {
        notice.setBounds (row1);   // la fila ya viene con su margen (26px): no la achiques más
        meterRect = {};
    }
    else
    {
        meterRect = row1.removeFromLeft (84).reduced (0, 6);
        row1.removeFromLeft (12);
        gainLabel.setBounds (row1.removeFromLeft (20));
        gain.setBounds (row1);
    }
}

} // namespace supernova
