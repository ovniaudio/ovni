#include "AppTopBar.h"
#include "ui/theme.h"          // look::hue (fuego SUPERNOVA)
#include "ui-kit/Theme.h"      // ovni::ui::theme (paleta del sello)
#include "ui-kit/Fonts.h"      // ovni::ui::fonts
#include <unistd.h>            // getpid() — REOPEN relanza esperando la muerte de este proceso

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

    // Banner de permiso (oculto hasta que haga falta).
    permLabel.setText (juce::String::fromUTF8 ("\xE2\x9A\xA0 1) Enable SUPERNOVA in Screen Recording"
                       "   2) REOPEN"), juce::dontSendNotification);
    permLabel.setFont (fonts::body (12.0f));
    permLabel.setColour (juce::Label::textColourId, theme::amber);
    permLabel.setVisible (false);
    addAndMakeVisible (permLabel);
    permBtn.setTooltip ("Opens Settings > Privacy > Screen & System Audio Recording. Enable SUPERNOVA there.");
    permBtn.onClick = [this] { openScreenRecordingSettings(); };
    permBtn.setVisible (false);
    addAndMakeVisible (permBtn);
    reopenBtn.setTooltip ("Relaunches SUPERNOVA so it picks up the permission (macOS only applies it on reopen).");
    reopenBtn.setColour (juce::TextButton::buttonColourId, look::hue.withAlpha (0.30f));
    reopenBtn.onClick = [this] { relaunchApp(); };
    reopenBtn.setVisible (false);
    addAndMakeVisible (reopenBtn);

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

void AppTopBar::openScreenRecordingSettings()
{
    juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture")
        .launchInDefaultBrowser();
}

void AppTopBar::relaunchApp()
{
    // macOS aplica el permiso de Screen Recording recién en un proceso NUEVO → relanzamos el bundle.
    // BUG viejo: "open -n" + quit inmediato → la instancia nueva arrancaba mientras la vieja seguía viva y,
    // con moreThanOneInstanceAllowed=false, se cerraba sola → "aprieto REOPEN y no se vuelve a abrir".
    // FIX: un shell DESADJUNTADO espera a que ESTE proceso muera (kill -0 sobre nuestro PID) y RECIÉN
    // AHÍ abre una instancia fresca. Instancia única preservada (no más "2 SUPERNOVA") + reapertura
    // confiable. El hijo sobrevive a nuestra salida (JUCE no mata al ChildProcess en su dtor; queda
    // huérfano reasignado a launchd, sin terminal de control → sin SIGHUP).
    const auto path = juce::File::getSpecialLocation (juce::File::currentApplicationFile).getFullPathName();
    const auto pid  = juce::String ((int) getpid());

    juce::StringArray argv;
    argv.add ("/bin/sh");
    argv.add ("-c");
    argv.add ("while /bin/kill -0 " + pid + " 2>/dev/null; do sleep 0.15; done; "
              "/usr/bin/open \"" + path + "\"");

    juce::ChildProcess relauncher;
    relauncher.start (argv);   // desadjuntado: sobrevive a systemRequestedQuit()
    juce::Timer::callAfterDelay (200, [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
}

// ---------------------------------------------------------------------------- permiso
void AppTopBar::setPermissionUi (bool denied)
{
    if (permissionUi == denied) return;
    permissionUi = denied;
    permLabel.setVisible (denied);
    permBtn.setVisible (denied);
    reopenBtn.setVisible (denied);
    gainLabel.setVisible (! denied);
    gain.setVisible (! denied);
    resized();
    repaint();
}

// ---------------------------------------------------------------------------- poll 30Hz
void AppTopBar::timerCallback()
{
    TopBar::timerCallback();   // medidor (engine.meterLevel via virtual) + BPM + toggles + rotate + SEQ

    const bool denied = engine.model().kind() == AudioSourceModel::Kind::systemAudio
                      && engine.systemStatus() == SystemAudioSource::Status::permissionDenied;
    setPermissionUi (denied);

    // Auto-reconexión: si quedó sin permiso, reintentá cada ~2.5s. Al activarlo en Ajustes el permiso
    // se toma EN VIVO (CGPreflight) → el banner se va solo, sin relanzar la app.
    if (denied && (++retryTicks % 75) == 0)
        engine.retrySystemAudio();
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
        reopenBtn.setBounds (row1.removeFromRight (78));
        row1.removeFromRight (6);
        permBtn.setBounds (row1.removeFromRight (72));
        row1.removeFromRight (8);
        permLabel.setBounds (row1);
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
