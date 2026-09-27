#include "ui/EyepieceIntro.h"

#if TELESCOPE_HAS_EYEPIECE_INTRO
#include "lenses/Look.h"
#include "ui/ThemePreference.h"

namespace telescope
{
namespace
{
namespace th = telescope::look::tint;

// ========================================================================================================
// LOS TEXTOS. En y es son los que aprobó Joaquín (D-113, prompt 105); pt (de Brasil, como el resto del plugin),
// fr, de e it los tradujo la F4 y están dichos en su reporte. «EYEPIECE» y «TELESCOPE» no se traducen.
// ========================================================================================================
const EyepieceIntro::Texts kEn { "Meet EYEPIECE",
    "TELESCOPE measures your mix. EYEPIECE, OVNI's free Mac app, reads your Ableton Live or FL Studio project "
    "and turns it into a snapshot you can paste into any AI. It uploads nothing.",
    "Get EYEPIECE", "Open EYEPIECE", "Close" };

const EyepieceIntro::Texts kEs { "Conocé EYEPIECE",
    "TELESCOPE mide tu mezcla. EYEPIECE, la app gratis de OVNI para Mac, lee tu proyecto de Ableton Live o "
    "FL Studio y lo convierte en una foto que podés pegar en cualquier IA. No sube nada.",
    "Bajar EYEPIECE", "Abrir EYEPIECE", "Cerrar" };

const EyepieceIntro::Texts kPt { "Conheça o EYEPIECE",
    "O TELESCOPE mede a sua mixagem. O EYEPIECE, o app gratuito da OVNI para Mac, lê o seu projeto do Ableton "
    "Live ou do FL Studio e o transforma em um retrato que você pode colar em qualquer IA. Ele não envia nada.",
    "Baixar o EYEPIECE", "Abrir o EYEPIECE", "Fechar" };

const EyepieceIntro::Texts kFr { "Découvrez EYEPIECE",
    "TELESCOPE mesure votre mix. EYEPIECE, l'app Mac gratuite d'OVNI, lit votre projet Ableton Live ou "
    "FL Studio et en fait un instantané que vous pouvez coller dans n'importe quelle IA. Il n'envoie rien.",
    "Obtenir EYEPIECE", "Ouvrir EYEPIECE", "Fermer" };

const EyepieceIntro::Texts kDe { "EYEPIECE kennenlernen",
    "TELESCOPE misst deinen Mix. EYEPIECE, die kostenlose Mac-App von OVNI, liest dein Ableton-Live- oder "
    "FL-Studio-Projekt und macht daraus einen Schnappschuss, den du in jede KI einfügen kannst. Es lädt nichts hoch.",
    "EYEPIECE holen", "EYEPIECE öffnen", "Schließen" };

const EyepieceIntro::Texts kIt { "Scopri EYEPIECE",
    "TELESCOPE misura il tuo mix. EYEPIECE, l'app gratuita di OVNI per Mac, legge il tuo progetto di Ableton "
    "Live o FL Studio e lo trasforma in un'istantanea da incollare in qualsiasi IA. Non carica nulla.",
    "Scarica EYEPIECE", "Apri EYEPIECE", "Chiudi" };

juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

constexpr int kPad = 14, kTitleH = 20, kGap = 8, kButtonH = 26, kButtonGap = 8;
float bodyHeightPx() { return 12.0f; }
}

// ======================================================================================== EyepieceIntro
bool EyepieceIntro::seen()     { return ThemePreference::getValue (kSeenKey, {}) == kSeenValue; }
bool EyepieceIntro::markSeen() { return ThemePreference::setValue (kSeenKey, kSeenValue); }

bool EyepieceIntro::shouldShow()
{
   #if TELESCOPE_TEST_BUILD
    if (! enabledForTest()) return false;   // en el runner arranca apagada (ver el encabezado)
   #endif
    return ! seen();
}

juce::File EyepieceIntro::findEyepiece()
{
   #if TELESCOPE_TEST_BUILD
    if (auto& f = detectorForTest()) return f();
   #endif
    const auto viaLs = findApplicationByBundleId (kBundleId);
    if (! viaLs.empty()) return juce::File (juce::String::fromUTF8 (viaLs.c_str()));

    for (const auto& dir : { juce::File ("/Applications"),
                             juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Applications") })
    {
        const auto app = dir.getChildFile ("EYEPIECE.app");
        if (app.isDirectory()) return app;
    }
    return {};
}

const EyepieceIntro::Texts& EyepieceIntro::textsFor (const juce::String& language)
{
    if (language == "es") return kEs;
    if (language == "pt") return kPt;
    if (language == "fr") return kFr;
    if (language == "de") return kDe;
    if (language == "it") return kIt;
    return kEn;
}

// ======================================================================================== la tarjeta
EyepieceIntroCard::EyepieceIntroCard (juce::String lang) : language (std::move (lang))
{
    setName ("eyepieceIntro");
    app = EyepieceIntro::findEyepiece();   // una vez, al abrir: unos pocos exists() y una consulta a LaunchServices
    setInterceptsMouseClicks (true, false);
}

void EyepieceIntroCard::setLanguage (const juce::String& lang)
{
    if (lang == language) return;
    language = lang;
    resized();
    repaint();
}

juce::String EyepieceIntroCard::titleText() const   { return u8 (EyepieceIntro::textsFor (language).title); }
juce::String EyepieceIntroCard::bodyText() const    { return u8 (EyepieceIntro::textsFor (language).body); }
juce::String EyepieceIntroCard::closeLabel() const  { return u8 (EyepieceIntro::textsFor (language).close); }
juce::String EyepieceIntroCard::primaryLabel() const
{
    const auto& t = EyepieceIntro::textsFor (language);
    return u8 (eyepieceInstalled() ? t.open : t.get);
}

static juce::TextLayout bodyLayout (const juce::String& text, int width)
{
    juce::AttributedString a;
    a.setWordWrap (juce::AttributedString::byWord);
    a.setJustification (juce::Justification::topLeft);
    a.append (text, look::body (bodyHeightPx()), th::txt);
    juce::TextLayout l;
    l.createLayout (a, (float) juce::jmax (60, width));
    return l;
}

int EyepieceIntroCard::heightForWidth (int width) const
{
    const auto l = bodyLayout (bodyText(), width - 2 * kPad);
    return kPad + kTitleH + kGap + (int) std::ceil (l.getHeight()) + kGap + 4 + kButtonH + kPad;
}

void EyepieceIntroCard::resized()
{
    auto r = getLocalBounds().reduced (kPad);
    auto buttons = r.removeFromBottom (kButtonH);
    const auto font = look::label (11.0f);
    const int  closeW   = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, closeLabel())) + 24;
    const int  primaryW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, primaryLabel())) + 28;
    closeBox   = buttons.removeFromRight (closeW);
    buttons.removeFromRight (kButtonGap);
    primaryBox = buttons.removeFromRight (primaryW);
}

void EyepieceIntroCard::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();

    // La placa: una superficie del tema con un filo del verde de TELESCOPE. Es un aviso, no un dato: sigue el tema.
    g.setColour (th::surf2);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (th::green.withAlpha (0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto area = getLocalBounds().reduced (kPad);
    g.setColour (th::txt);
    g.setFont (look::label (14.0f));
    g.drawText (titleText(), area.removeFromTop (kTitleH), juce::Justification::centredLeft, false);
    area.removeFromTop (kGap);

    const auto body = bodyLayout (bodyText(), area.getWidth());
    body.draw (g, area.removeFromTop ((int) std::ceil (body.getHeight())).toFloat());

    const auto button = [&] (juce::Rectangle<int> box, const juce::String& text, bool filled, bool hot)
    {
        const auto b = box.toFloat();
        if (filled)
        {
            g.setColour (th::green.withAlpha (hot ? 1.0f : 0.88f));
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (th::bg0);
        }
        else
        {
            g.setColour (hot ? th::surf : th::surf2);
            g.fillRoundedRectangle (b, 3.0f);
            g.setColour (th::line);
            g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (th::txt);
        }
        g.setFont (look::label (11.0f));
        g.drawText (text, box, juce::Justification::centred, false);
    };
    button (primaryBox, primaryLabel(), true, hovered == primary);
    button (closeBox, closeLabel(), false, hovered == close);
}

void EyepieceIntroCard::press (Button b)
{
    if (b == primary)
    {
        const auto what = eyepieceInstalled() ? app.getFullPathName() : juce::String (EyepieceIntro::kGetUrl);
       #if TELESCOPE_TEST_BUILD
        if (auto& f = EyepieceIntro::launcherForTest()) f (what);
        else
       #endif
        if (eyepieceInstalled()) app.startAsProcess();
        else                     juce::URL (EyepieceIntro::kGetUrl).launchInDefaultBrowser();
    }
    // Los dos cuentan como vista: la tarjeta no vuelve nunca más, en ninguna instancia ni en ningún DAW.
    EyepieceIntro::markSeen();
    setVisible (false);
}

void EyepieceIntroCard::mouseDown (const juce::MouseEvent& e)
{
    if (primaryBox.contains (e.getPosition())) press (primary);
    else if (closeBox.contains (e.getPosition())) press (close);
}

void EyepieceIntroCard::mouseMove (const juce::MouseEvent& e)
{
    const int was = hovered;
    hovered = primaryBox.contains (e.getPosition()) ? (int) primary : (closeBox.contains (e.getPosition()) ? (int) close : -1);
    if (hovered != was) repaint();
}

void EyepieceIntroCard::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}
}
#endif
