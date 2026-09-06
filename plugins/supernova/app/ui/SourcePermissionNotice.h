#pragma once
// SourcePermissionNotice — el aviso de la barra SOURCE cuando System Audio todavía no puede escuchar.
// Un solo componente con los TRES modos (D-33), así el aviso se ve igual en la app y en el [.uisnap]:
//
//   ask       ⚠ System Audio needs permission                                    [ ALLOW ]
//   waiting   ⚠ Waiting for macOS permission…                                    (sin botón)
//   denied    ⚠ Enable in System Settings › Privacy › System Audio Recording     [ OPEN ] [ REOPEN ]
//
// `waiting` es el cartel del sistema abierto: no hay nada que el usuario pueda tocar acá, así que no hay
// botón. Quién decide el modo es SourceNoticeModel (app/SystemAudioPermission.h), que tiene el reloj.
//
// No es texto suelto flotando en la barra: va sobre una placa ámbar tenue con hairline, para que se lea
// como algo accionable (y no como un error). Header-only a propósito — el exe de tests no compila app/*.cpp
// y el snapshot tiene que ser EXACTAMENTE este componente, no una maqueta.
#include <juce_gui_basics/juce_gui_basics.h>

#include "app/SystemAudioPermission.h"
#include "ui/theme.h"        // supernova::look::hue
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"

namespace supernova {

class SourcePermissionNotice final : public juce::Component
{
public:
    std::function<void()> onAction;   // ALLOW → pide el permiso (dispara el cartel) · OPEN → abre Ajustes
    std::function<void()> onReopen;   // sólo en denied: relanza ESTA app para que tome el permiso nuevo

    SourcePermissionNotice()
    {
        text.setJustificationType (juce::Justification::centredLeft);
        text.setColour (juce::Label::textColourId, ovni::ui::theme::amber);
        text.setFont (ovni::ui::fonts::body (12.0f));
        text.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (text);

        actionBtn.setColour (juce::TextButton::buttonColourId, ovni::ui::theme::amber.withAlpha (0.22f));
        actionBtn.onClick = [this] { if (onAction) onAction(); };
        addAndMakeVisible (actionBtn);

        reopenBtn.setColour (juce::TextButton::buttonColourId, look::hue.withAlpha (0.30f));
        reopenBtn.setTooltip ("Relaunches THIS copy of SUPERNOVA so it picks up the permission you just granted.");
        reopenBtn.onClick = [this] { if (onReopen) onReopen(); };
        addChildComponent (reopenBtn);

        setState (NoticeMode::ask, SystemAudioBackend::processTap);
    }

    void setState (NoticeMode mode, SystemAudioBackend backend)
    {
        mode_ = mode;
        text.setText (juce::String::fromUTF8 ("\xE2\x9A\xA0  ") + noticeText (mode, backend),
                      juce::dontSendNotification);

        const auto action = noticeAction (mode);
        actionBtn.setButtonText (action);
        actionBtn.setVisible (action.isNotEmpty());   // esperando al sistema no hay nada que clickear
        actionBtn.setTooltip (mode == NoticeMode::denied
            ? "Opens System Settings on the exact pane. Turn SUPERNOVA on there."
            : "Asks macOS for permission to listen to this Mac's audio. Nothing is recorded or stored.");
        reopenBtn.setVisible (mode == NoticeMode::denied);
        resized();
        repaint();
    }

    NoticeMode mode() const noexcept { return mode_; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ovni::ui::theme::amber.withAlpha (0.07f));
        g.fillRoundedRectangle (r, kRadius);
        g.setColour (ovni::ui::theme::amber.withAlpha (0.28f));
        g.drawRoundedRectangle (r, kRadius, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (kPadX, kPadY);
        if (reopenBtn.isVisible())
        {
            reopenBtn.setBounds (r.removeFromRight (kReopenW));
            r.removeFromRight (kGap);
        }
        if (actionBtn.isVisible())
        {
            actionBtn.setBounds (r.removeFromRight (kActionW));
            r.removeFromRight (kGap);
        }
        text.setBounds (r);
    }

private:
    static constexpr int   kPadX = 8, kPadY = 3, kGap = 6, kActionW = 66, kReopenW = 74;
    static constexpr float kRadius = 5.0f;

    juce::Label      text;
    juce::TextButton actionBtn, reopenBtn { "REOPEN" };
    NoticeMode       mode_ = NoticeMode::ask;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SourcePermissionNotice)
};

} // namespace supernova
