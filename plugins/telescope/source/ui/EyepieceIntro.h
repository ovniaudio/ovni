#pragma once
#include <juce_core/juce_core.h>

// ========================================================================================================
// EyepieceIntro — T8 de la 0.2 (D-79, D-113): TELESCOPE le cuenta al usuario que existe EYEPIECE, UNA vez.
//
// SÓLO EN LA MAC. EYEPIECE 0.1 sale sólo para Mac (D-113): en Windows compila y anda, pero no se distribuye.
// Por eso acá, fuera de la Mac, no hay nada: ni la clase, ni la tarjeta, ni la detección. El editor pregunta
// TELESCOPE_HAS_EYEPIECE_INTRO y el test [eyepiece] de Windows verifica que valga 0.
//
// QUÉ ES. Una tarjeta ADENTRO del editor, abajo a la derecha de la lente: no es una ventana modal y no bloquea
// nada. Aparece la primera vez que se abre el editor con la 0.2. «Cerrar» la cierra para siempre; el botón
// principal también cuenta como vista.
//   · EYEPIECE instalada → «Open EYEPIECE», que la abre.
//   · si no → «Get EYEPIECE», que abre https://ovniaudio.com/eyepiece en el navegador (la página es del sitio,
//     para el día D).
//
// DÓNDE SE GUARDA QUE YA SE VIO. En OVNI.settings, la clave `telescopeEyepieceIntro` = "seen", con el MISMO
// mecanismo que el tema (ThemePreference::setValue): sólo esa clave, relectura justo antes de escribir y el lock
// entre procesos (la regla de D-100 §2quater, punto 4). EYEPIECE puede estar escribiendo el mismo archivo.
//
// CÓMO SABE SI EYEPIECE ESTÁ. Por bundle id, com.ovniaudio.eyepiece, con LaunchServices
// (LSCopyApplicationURLsForBundleIdentifier: la encuentra esté donde esté); si no, mira EYEPIECE.app en
// /Applications y ~/Applications. En los tests la detección se reemplaza (detectorForTest).
//
// EN EL RUNNER ARRANCA APAGADA. Las fotos y los tiempos de siempre no pueden verla: sólo aparece si un test la
// prende (enabledForTest). Tiene sus fotos propias.
// ========================================================================================================
#if JUCE_MAC
 #define TELESCOPE_HAS_EYEPIECE_INTRO 1
#else
 #define TELESCOPE_HAS_EYEPIECE_INTRO 0
#endif

#if TELESCOPE_HAS_EYEPIECE_INTRO
#include <functional>
#include <string>
#include <juce_gui_basics/juce_gui_basics.h>

namespace telescope
{
// La búsqueda por LaunchServices (ui/EyepieceDetect.cpp, sin JUCE: CoreServices no convive con sus nombres).
// Devuelve la ruta de la app, o vacío.
std::string findApplicationByBundleId (const char* bundleId);

struct EyepieceIntro
{
    static constexpr const char* kBundleId = "com.ovniaudio.eyepiece";
    static constexpr const char* kSeenKey  = "telescopeEyepieceIntro";
    static constexpr const char* kSeenValue = "seen";
    static constexpr const char* kGetUrl   = "https://ovniaudio.com/eyepiece";

    // ¿Ya se vio? (en OVNI.settings, compartido por todas las instancias y los productos del sello)
    static bool seen();
    static bool markSeen();
    // ¿Hay que mostrarla al abrir el editor?
    static bool shouldShow();

    // La app instalada, o un File vacío.
    static juce::File findEyepiece();

    // Los textos, en los seis idiomas del plugin (en y es: los aprobó Joaquín, D-113; pt, fr, de e it: la F4).
    struct Texts { const char* title; const char* body; const char* get; const char* open; const char* close; };
    static const Texts& textsFor (const juce::String& language);

   #if TELESCOPE_TEST_BUILD
    static bool& enabledForTest() { static bool on = false; return on; }
    // Lo que devuelve findEyepiece() en los tests (sin esto: la detección de verdad).
    static std::function<juce::File()>& detectorForTest() { static std::function<juce::File()> f; return f; }
    // Lo que pasa al apretar el botón principal (sin esto: abrir la app o el navegador de verdad).
    static std::function<void (const juce::String&)>& launcherForTest() { static std::function<void (const juce::String&)> f; return f; }
   #endif
};

// La tarjeta. La crea el editor si EyepieceIntro::shouldShow(); se esconde sola al apretar cualquiera de los dos.
class EyepieceIntroCard : public juce::Component
{
public:
    explicit EyepieceIntroCard (juce::String language);

    void setLanguage (const juce::String& language);
    // Lo que la tarjeta ocupa a este ancho (el alto depende del texto envuelto y del idioma).
    int  heightForWidth (int width) const;

    enum Button { primary = 0, close };
    void press (Button b);   // público: el test aprieta sin fabricar clics (el mouse llama a esto mismo)

    bool         eyepieceInstalled() const noexcept { return ! app.getFullPathName().isEmpty(); }
    juce::String primaryLabel() const;
    juce::String titleText() const;
    juce::String bodyText() const;
    juce::String closeLabel() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String language;
    juce::File   app;
    juce::Rectangle<int> primaryBox, closeBox;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EyepieceIntroCard)
};
}
#endif
