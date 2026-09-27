#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "lenses/BandCorrelationLens.h"
#include "lenses/CqtLens.h"
#include "lenses/DynamicsLens.h"
#include "lenses/LoudnessLens.h"
#include "lenses/PlaceholderLens.h"
#include "lenses/ScopeLens.h"
#include "lenses/SpectrogramLens.h"
#include "lenses/SpectrumLens.h"
#include "lenses/SpiralLens.h"
#include "lenses/StereoSpectrogramLens.h"
#include "lenses/FieldLens.h"        // ===== 53 =====
#include "lenses/WaterfallLens.h"    // ===== 53 =====
// ===== 54: TONAL BALANCE (lente 12) =====
#include "lenses/TonalBalanceLens.h"
// ===== 55: VERDICT (lente 13) =====
#include "lenses/VerdictLens.h"

namespace telescope
{
namespace
{
constexpr int kStripWidth = 150;   // coords de DISEÑO; el zoom S/M/L lo escala solo
}

// Las lentes CONSTRUIDAS. Las que todavía no existen caen en PlaceholderLens: un panel con su nombre y
// nada más (ni "coming soon" ni promesas — ver PlaceholderLens.h).
std::unique_ptr<Lens> TelescopeEditor::makeLens (LensId id)
{
    switch (id)
    {
        case LensId::loudness: return std::make_unique<LoudnessLens> (proc);
        case LensId::dynamics: return std::make_unique<DynamicsLens> (proc);
        case LensId::scope:    return std::make_unique<ScopeLens> (proc);
        case LensId::spectrum:    return std::make_unique<SpectrumLens> (proc);
        case LensId::spectrogram: return std::make_unique<SpectrogramLens> (proc);
        case LensId::cqt:      return std::make_unique<CqtLens> (proc);
        case LensId::spiral:   return std::make_unique<SpiralLens> (proc);
        case LensId::bandCorrelation:   return std::make_unique<BandCorrelationLens> (proc);
        case LensId::stereoSpectrogram: return std::make_unique<StereoSpectrogramLens> (proc);
        // ===== 53: las dos 3D (proyección 2.5D por software, D-46) =====
        case LensId::waterfall: return std::make_unique<WaterfallLens> (proc);
        case LensId::field:     return std::make_unique<FieldLens> (proc);
        // ===== 54: TONAL BALANCE =====
        case LensId::tonalBalance:      return std::make_unique<TonalBalanceLens> (proc);
        // ===== 55: VERDICT — la unica que no muestra: dice =====
        case LensId::verdict:           return std::make_unique<VerdictLens> (proc);
        default:               return std::make_unique<PlaceholderLens> (id);
    }
}

// Verde espectral: TELESCOPE mide el espectro y el espacio, no los mueve (familia Espectral del sello).
TelescopeEditor::TelescopeEditor (TelescopeProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("ANA\xc2\xb7" "01")), proc (p)
{
    // F2 de la 0.2: el tema es la preferencia del usuario (OVNI.settings), leída al abrir la ventana.
    look::setTheme (ThemePreference::load());
    setFamilyHue (look::ink().green);

    proc.editorOpened();   // ver editorOpened/editorClosed: la máscara se apaga cuando se cierra la última

    addToCanvas (strip);
    // Las lentes CONSTRUIDAS. Con VERDICT (55) están LAS TRECE: setBuilt reemplaza la máscara entera.
    strip.setBuilt ((1u << (int) LensId::loudness)
                  | (1u << (int) LensId::dynamics)
                  | (1u << (int) LensId::spectrum)
                  | (1u << (int) LensId::spectrogram)
                  | (1u << (int) LensId::waterfall)
                  | (1u << (int) LensId::cqt)
                  | (1u << (int) LensId::spiral)
                  | (1u << (int) LensId::scope)
                  | (1u << (int) LensId::bandCorrelation)
                  | (1u << (int) LensId::stereoSpectrogram)
                  | (1u << (int) LensId::field)
                  | (1u << (int) LensId::tonalBalance)
                  | (1u << (int) LensId::verdict));

    strip.onSelect = [this] (int i)
    {
        if (auto* param = proc.apvts.getParameter ("lens"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
    };

    strip.onLanguage = [this] (const juce::String& code)
    {
        strings::setLanguage (proc.apvts.state, code);
    };

    strip.onTheme = [this] { toggleTheme(); };
    themeListener.fn = [this] { applyTheme(); };
    ThemePreference::changed().addChangeListener (&themeListener);

    proc.apvts.addParameterListener ("lens", this);
    proc.apvts.state.addListener (this);
    applyLanguage();

    // El VALOR DEL PARÁMETRO, no getParameterAsValue(): el APVTS vuelca los parámetros al ValueTree en
    // diferido (por timer), así que si el host ya dejó `lens` en SCOPE antes de abrir la ventana, el árbol
    // todavía dice LOUDNESS y el editor abriría la lente equivocada. El raw value es sincrónico.
    const auto* lensValue = proc.apvts.getRawParameterValue ("lens");
    showLens (lensValue != nullptr ? juce::roundToInt (lensValue->load()) : 0);

   #if TELESCOPE_HAS_EYEPIECE_INTRO
    // T8 (F4 de la 0.2): la primera vez que se abre el editor con la 0.2, la tarjeta que presenta a EYEPIECE.
    if (EyepieceIntro::shouldShow())
    {
        intro = std::make_unique<EyepieceIntroCard> (strings::languageOf (proc.apvts.state));
        addToCanvas (*intro);
    }
   #endif

    setBaseSize (980, 620);
    applyTheme();
}

// Al cerrar la ventana, lo que la lente encendió se APAGA: el motor vuelve a los módulos siempre-activos
// (kAlwaysOnModules) en cuanto se cierra el ÚLTIMO editor. Sin esto, cerrar el plugin con el espectro a la
// vista dejaba la FFT corriendo para siempre sin nadie mirando (HIGH del revisor del 50). El conteo lo
// lleva el processor: con dos ventanas abiertas, cerrar una no le apaga el módulo a la otra.
TelescopeEditor::~TelescopeEditor()
{
    ThemePreference::changed().removeChangeListener (&themeListener);
    proc.apvts.state.removeListener (this);
    proc.apvts.removeParameterListener ("lens", this);
    proc.editorClosed();
}

void TelescopeEditor::parameterChanged (const juce::String& id, float value)
{
    if (id != "lens") return;
    // El callback del APVTS puede venir de cualquier hilo (automatización del host): al message thread.
    const int index = juce::roundToInt (value);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TelescopeEditor> (this), index]
                                     { if (safe != nullptr) safe->showLens (index); });
}

void TelescopeEditor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& prop)
{
    if (prop.toString() == strings::kLanguageProperty) applyLanguageSafely();
}

// 56c — EL ÁRBOL ENTERO CAMBIÓ DE LUGAR. `apvts.replaceState (tree)` —lo que corre cuando el host
// restaura la sesión— hace `state = newState` (JUCE 8.0.13,
// juce_AudioProcessorValueTreeState.cpp:400), y `ValueTree::operator=` avisa a los listeners por ACÁ, no
// por valueTreePropertyChanged: no hay ninguna propiedad que cambie, cambia el árbol al que se apunta.
// Sin esto, con la ventana abierta el idioma guardado en la sesión no llegaba nunca a la pantalla — el
// árbol decía "de" y la tira seguía en inglés. Es el mismo camino, porque el idioma vigente se vuelve a
// leer del árbol (que ya es el nuevo).
void TelescopeEditor::valueTreeRedirected (juce::ValueTree&)
{
    applyLanguageSafely();
}

// 56c — AL MESSAGE THREAD, COMO parameterChanged. `juce::ValueTree::Listener` notifica de forma SÍNCRONA
// y en el hilo que hizo el `setProperty`, y `applyLanguage()` toca la tira y pide repaint. Un preset
// cargado desde el hilo de automatización del host entraba por acá y tocaba componentes desde ese hilo.
// Cuando ya estamos en el message thread se aplica en el acto: pasar todo por callAsync metería un frame
// de retraso en el caso común (el clic en el chip de la tira) sin ganar nada.
void TelescopeEditor::applyLanguageSafely()
{
    if (juce::MessageManager::existsAndIsCurrentThread()) { applyLanguage(); return; }

    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TelescopeEditor> (this)]
                                     { if (safe != nullptr) safe->applyLanguage(); });
}

// La tira Y la lente visible. La lente no "sabe" el idioma: lo lee del árbol cada vez que pinta, así
// que basta con pedirle que pinte de nuevo — y hay que pedírselo, porque su repaint está suspendido
// mientras el dibujo no cambia (VisualizerBase pausa en reposo).
void TelescopeEditor::applyLanguage()
{
    appliedOnMessageThread.store (juce::MessageManager::existsAndIsCurrentThread());
    applyCount.fetch_add (1);
    strip.setLanguage (strings::languageOf (proc.apvts.state));
    if (lens != nullptr) lens->repaint();
   #if TELESCOPE_HAS_EYEPIECE_INTRO
    if (intro != nullptr) { intro->setLanguage (strings::languageOf (proc.apvts.state)); placeIntro(); }
   #endif
}

void TelescopeEditor::showLens (int index)
{
    index = juce::jlimit (0, kNumLenses - 1, index);
    if (index == currentLens) return;
    currentLens = index;

    lens = makeLens ((LensId) index);

    addToCanvas (*lens);
    lens->setPixelAnchor (this);   // T2: el editor es el que el host pone en un píxel entero (ver Lens.h)
    lens->setBounds (lensArea);

    // LENTE A DEMANDA: corren los módulos que la lente visible necesita MÁS los que no se apagan nunca
    // (ver kAlwaysOnModules en analysis/AnalysisFrame.h: el medidor de loudness acumula, no puede tener
    // agujeros porque el usuario se fue a mirar el espectro un rato).
    proc.setEnabledModules (lens->requiredModules() | kAlwaysOnModules);
    strip.setSelected (index);
   #if TELESCOPE_HAS_EYEPIECE_INTRO
    if (intro != nullptr) intro->toFront (false);   // T8: la tarjeta queda arriba de la lente nueva
   #endif
    applyLanguage();
}

// Reconstruir la lente es lo mismo que elegirla otra vez: sus capas horneadas (la estática, las cachés
// raster) nacen con la tinta nueva. La historia vive en el processor, así que no se pierde nada.
void TelescopeEditor::rebuildLens()
{
    const int index = currentLens;
    currentLens = -1;
    lens.reset();
    showLens (juce::jmax (0, index));
}

// El MARCO. En oscuro, FrameInk por defecto: el marco del sello tal cual, bit por bit. En claro: la base
// lisa (el Panel del sello es una atmósfera oscura y no tiene versión clara), las tintas del papel, sin el
// glow del nombre (sobre papel se ve como una mancha), y el texto del slot A/B encendido en papel sobre el
// verde oscuro.
void TelescopeEditor::applyTheme()
{
    const auto& k = look::ink();
    setFamilyHue (k.green);

    FrameInk f;
    if (look::theme() == look::Theme::light)
    {
        f.base       = k.bg0;
        f.atmosphere = false;
        f.sheen      = juce::Colours::white.withAlpha (0.55f);
        f.topLine    = juce::Colours::white.withAlpha (0.80f);
        f.lineSoft   = k.lineSoft;
        f.txt        = k.txt;
        f.mut        = k.mut;
        f.fnt        = k.mut;        // la insignia ANA·01: en papel, el secundario (el terciario no llega a 4.5:1)
        f.onHue      = k.bg1;
        f.bezel      = k.line;
        f.brackets   = k.txt.withAlpha (0.28f);
        f.nameGlow   = false;
    }
    setFrameInk (f);

    rebuildLens();
    strip.repaint();
   #if TELESCOPE_HAS_EYEPIECE_INTRO
    if (intro != nullptr) intro->repaint();
   #endif
    repaint();
}

void TelescopeEditor::toggleTheme()
{
    const auto next = look::theme() == look::Theme::light ? look::Theme::dark : look::Theme::light;
    look::setTheme (next);
    ThemePreference::save (next);
    // Síncrono: el clic ya está en el message thread, y así la ventana que se clickeó cambia en este cuadro.
    ThemePreference::changed().sendSynchronousChangeMessage();
}

void TelescopeEditor::layoutBody (juce::Rectangle<int> body)
{
    body.reduce (ovni::ui::theme::padIn, ovni::ui::theme::padIn / 2);

    strip.setBounds (body.removeFromLeft (kStripWidth));
    body.removeFromLeft (ovni::ui::theme::padIn);

    lensArea = body;
    if (lens != nullptr) lens->setBounds (lensArea);
   #if TELESCOPE_HAS_EYEPIECE_INTRO
    placeIntro();
   #endif
}

#if TELESCOPE_HAS_EYEPIECE_INTRO
// T8: abajo a la derecha de la lente, con el margen del sello. El ancho es fijo (el texto se lee en un bloque) y
// el alto sale del texto envuelto en ese idioma.
void TelescopeEditor::placeIntro()
{
    if (intro == nullptr || lensArea.isEmpty()) return;
    const int pad = ovni::ui::theme::padIn;
    const int w = juce::jmin (380, lensArea.getWidth() - 2 * pad);
    const int h = intro->heightForWidth (w);
    intro->setBounds (lensArea.getRight() - pad - w, lensArea.getBottom() - pad - h, w, h);
    intro->toFront (false);
}
#endif

void TelescopeEditor::pumpLensFrames (int n)
{
    if (lens != nullptr) lens->pumpFrames (n);
}
}
