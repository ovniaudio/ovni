// SUPERNOVA — app standalone PRO. Shell propio (reemplaza el StandaloneFilterWindow genérico de JUCE).
// Al ser un juce_add_gui_app NO define JucePlugin_* → START_JUCE_APPLICATION entra por el camino limpio.
#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"

namespace supernova {

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow ("SUPERNOVA", juce::Colours::black, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto* mc = new MainComponent();
        mc->setAlwaysOnTopHandler ([this] (bool on) { setAlwaysOnTop (on); if (on) toFront (true); });
        props = &mc->props();
        setContentOwned (mc, true);        // dimensiona la ventana al componente

        // fix 1: RESIZABLE, abre GRANDE. El editor es flexible → el visual llena todo (sin negro al costado).
        setResizable (true, true);
        setResizeLimits (720, 480, 100000, 100000);   // mínimos sanos; sin tope real

        const auto saved = props != nullptr ? props->getValue ("windowState") : juce::String();
        if (saved.isNotEmpty())
        {
            restoreWindowStateFromString (saved);
        }
        else if (auto* disp = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto ua = disp->userArea;
            setBounds (ua.withSizeKeepingCentre ((int) (ua.getWidth() * 0.85), (int) (ua.getHeight() * 0.85)));
        }
        setVisible (true);
    }

    ~MainWindow() override { saveState(); }

    void closeButtonPressed() override
    {
        saveState();
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    void saveState()
    {
        if (props != nullptr && juce::Desktop::getInstance().getKioskModeComponent() == nullptr)
        {
            props->setValue ("windowState", getWindowStateAsString());   // no persistir el bounds de kiosk
            props->saveIfNeeded();
        }
    }

    juce::PropertiesFile* props = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class SupernovaApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "SUPERNOVA"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override                       { window = nullptr; }
    void systemRequestedQuit() override            { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace supernova

START_JUCE_APPLICATION (supernova::SupernovaApp)
