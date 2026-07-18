#include "MainComponent.h"

namespace supernova {

namespace {
std::unique_ptr<juce::PropertiesFile> makeSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "SUPERNOVA";
    o.filenameSuffix      = ".settings";
    o.folderName          = "OVNI";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}
}

MainComponent::MainComponent()
{
    settings = makeSettings();
    engine   = std::make_unique<AppAudioEngine> (proc, *settings);

    // El editor real del plugin (visual Metal + knobs). En app-mode esconde su HUD y arranca inmersivo.
    editorHolder.reset (proc.createEditorAndMakeActive());
    editor = dynamic_cast<SupernovaEditor*> (editorHolder.get());
    jassert (editor != nullptr);
    addAndMakeVisible (*editorHolder);

    if (editor != nullptr)
    {
        editor->setHeaderVisible (false);   // sin chrome de PLUGIN (presets/A-B/power/zoom): esto es una app
        editor->setChromeVisible (false);   // la app pone su propia barra (con 📷/CLEAR/⟳/SEQ propios)
        editor->setFlexibleCanvas (true);   // fix 1: el visual llena TODA la ventana (sin base×zoom → sin negro)
        editor->setImmersive (false);       // fix 4: arranca con los KNOBS VISIBLES (modo diseño; inmersivo explícito)
        editor->onAppFullscreenToggle = [this] { toggleAppFullscreen(); };   // fix 1: F = kiosk pantalla actual
    }

    bar = std::make_unique<AppTopBar> (*engine, *editor, proc);
    addAndMakeVisible (*bar);

    engine->begin();   // arranca la captura (System Audio por default, o lo persistido)

    // Tamaño inicial del contenido (la ventana lo agranda a userArea×0.85 o al estado guardado). El editor es
    // flexible → llena lo que le den; acá basta un default razonable.
    setSize (juce::jmax (960, editorHolder->getWidth()), kBarH + juce::jmax (600, editorHolder->getHeight()));
    if (editor != nullptr) editor->grabKeyboardFocus();
}

MainComponent::~MainComponent()
{
    if (engine != nullptr) engine->save();
    bar.reset();            // cierra el diálogo de setup (referencia al deviceManager) antes que el engine
    engine.reset();         // detiene la captura
    if (editor != nullptr) proc.editorBeingDeleted (editorHolder.get());
    editorHolder.reset();
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (bar != nullptr && ! appFullscreen) bar->setBounds (r.removeFromTop (kBarH));
    if (editorHolder != nullptr) editorHolder->setBounds (r);   // el editor flexible LLENA todo el resto
}

void MainComponent::toggleAppFullscreen()
{
    appFullscreen = ! appFullscreen;
    auto& desktop = juce::Desktop::getInstance();
    // kiosk: llena la pantalla ACTUAL, oculta menu-bar/dock; nullptr = salir.
    desktop.setKioskModeComponent (appFullscreen ? getTopLevelComponent() : nullptr, false);
    if (bar != nullptr)    bar->setVisible (! appFullscreen);   // barra oculta en performance
    if (editor != nullptr) editor->setAppFullscreenActive (appFullscreen);   // Esc sale del kiosk
    resized();
    if (editor != nullptr) editor->grabKeyboardFocus();      // F/Esc/Tab siguen andando
}

void MainComponent::setAlwaysOnTopHandler (std::function<void (bool)> fn)
{
    if (bar != nullptr) bar->onAlwaysOnTop = std::move (fn);
}

} // namespace supernova
