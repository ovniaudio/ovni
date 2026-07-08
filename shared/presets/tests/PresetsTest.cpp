// Unit-tests [s3] de la infra de presets del sello OVNI (PresetManager, ABState, FactoryPresets-interfaz).
// Auto-contenido: define un AudioProcessor headless con un APVTS chico y SU PROPIA tabla factoryPresets()
// (igual que hará cada plugin). Verifica: applyFactory reset+set, modificado, next/prev wrap,
// save/load/delete round-trip en temp, userDir parametrizado, y el comparador A/B.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "presets/PresetTypes.h"
#include "presets/PresetManager.h"
#include "presets/ABState.h"

//==================================================================================================
// Tabla de presets de prueba — la "provee el plugin". La base sólo declara factoryPresets().
//==================================================================================================
namespace ovni::presets
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using C = Category;
    static const std::vector<FactoryPreset> presets = {
        { "Wide Open",  C::Production,  {{"width", 80.0f}, {"mode", 0}} },
        { "Deep Space", C::SoundDesign, {{"width", 30.0f}, {"depth", 90.0f}, {"mode", 1}} },
        { "Centered",   C::Production,  {{"width", 50.0f}} },
    };
    return presets;
}
}

//==================================================================================================
// AudioProcessor headless con un APVTS chico (width, depth, mode) — sustituto de un plugin real.
//==================================================================================================
namespace
{
class FakeProcessor : public juce::AudioProcessor
{
public:
    FakeProcessor() : juce::AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
                      apvts (*this, nullptr, "PARAMETERS", layout()) {}

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        constexpr int v = 1;
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID{"width", v}, "Width", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID{"depth", v}, "Depth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID{"mode",  v}, "Mode",  StringArray{"Phones", "Speakers"}, 0));
        return l;
    }

    // boilerplate mínimo de AudioProcessor (no procesa audio: sólo aloja el APVTS para los tests)
    const juce::String getName() const override { return "Fake"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
};

float rawVal (FakeProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }
}

using ovni::presets::PresetManager;
using ovni::presets::ABState;

//==================================================================================================
// FactoryPresets — la interfaz devuelve la tabla del plugin; todos los IDs existen en el APVTS
//==================================================================================================
TEST_CASE ("FactoryPresets: la interfaz devuelve la tabla del plugin", "[s3]")
{
    const auto& all = ovni::presets::factoryPresets();
    REQUIRE (all.size() == 3);
    REQUIRE (juce::String (all[0].name) == "Wide Open");
    REQUIRE (all[0].category == ovni::presets::Category::Production);
    REQUIRE (all[1].category == ovni::presets::Category::SoundDesign);
}

TEST_CASE ("FactoryPresets: todos los IDs del preset existen en el APVTS", "[s3]")
{
    FakeProcessor proc;
    for (const auto& preset : ovni::presets::factoryPresets())
        for (const auto& pp : preset.params)
        {
            INFO ("preset=" << preset.name << " id=" << pp.id);
            REQUIRE (proc.apvts.getParameter (pp.id) != nullptr);
        }
}

//==================================================================================================
// PresetManager — aplicar fábrica: resetea TODO a default y setea sólo los del preset
//==================================================================================================
TEST_CASE ("PresetManager: applyFactory resetea a default y setea los del preset", "[s3]")
{
    FakeProcessor proc;
    PresetManager pm (proc.apvts, "TEST");

    // ensuciar el estado
    proc.apvts.getParameter ("depth")->setValueNotifyingHost (1.0f);   // depth -> 100
    proc.apvts.getParameter ("width")->setValueNotifyingHost (0.0f);   // width -> 0

    // "Wide Open" (idx 0): width=80, mode=0; depth NO está en el preset -> vuelve a su default (50)
    pm.applyFactory (0);
    REQUIRE (std::abs (rawVal (proc, "width") - 80.0f) < 0.5f);
    REQUIRE (std::abs (rawVal (proc, "depth") - 50.0f) < 0.5f);        // reset a default
    REQUIRE (pm.current().name == juce::String ("Wide Open"));
    REQUIRE (pm.current().category == ovni::presets::Category::Production);
    REQUIRE (! pm.current().modified);
}

TEST_CASE ("PresetManager: marca 'modificado' al tocar un parámetro tras cargar", "[s3]")
{
    FakeProcessor proc;
    PresetManager pm (proc.apvts, "TEST");
    pm.applyFactory (0);
    REQUIRE (! pm.current().modified);

    proc.apvts.getParameter ("width")->setValueNotifyingHost (0.123f);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);   // los listeners del APVTS se entregan en el message thread
    REQUIRE (pm.current().modified);
}

//==================================================================================================
// PresetManager — navegación next/prev con wrap
//==================================================================================================
TEST_CASE ("PresetManager: next/prev recorre y envuelve", "[s3]")
{
    FakeProcessor proc;
    PresetManager pm (proc.apvts, "TEST");
    const auto& all = ovni::presets::factoryPresets();

    pm.applyFactory (0);
    pm.next();
    REQUIRE (pm.current().name == juce::String (all[1].name));

    pm.applyFactory (pm.numFactory() - 1);
    pm.next();   // wrap al primero
    REQUIRE (pm.current().name == juce::String (all[0].name));

    pm.prev();   // wrap al último
    REQUIRE (pm.current().name == juce::String (all[(size_t) pm.numFactory() - 1].name));
}

//==================================================================================================
// PresetManager — User presets: save/load/delete round-trip en temp dir
//==================================================================================================
TEST_CASE ("PresetManager: guardar/cargar/borrar User preset (round-trip en temp)", "[s3]")
{
    FakeProcessor proc;
    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ovni_s3_presets_ZZ");
    tmp.deleteRecursively();
    PresetManager pm (proc.apvts, "TEST", tmp);

    // estado distintivo: width = 88
    auto* wp = proc.apvts.getParameter ("width");
    wp->setValueNotifyingHost (wp->convertTo0to1 (88.0f));
    const juce::String nm = "MyUserPreset_ZZ";
    pm.saveUser (nm);

    // cambiar el estado y recargar el user
    wp->setValueNotifyingHost (0.0f);
    pm.rescanUser();
    juce::File f = pm.userDir().getChildFile (nm + ".preset");
    REQUIRE (f.existsAsFile());
    pm.applyUserFile (f);
    REQUIRE (std::abs (rawVal (proc, "width") - 88.0f) < 0.5f);
    REQUIRE (pm.current().isUser);

    // borrar
    pm.deleteUser (f);
    REQUIRE (! f.existsAsFile());
    tmp.deleteRecursively();
}

TEST_CASE ("PresetManager: userDir vive en Application Support/OVNI <plugin> (nunca root-owned)", "[s3]")
{
    FakeProcessor proc;
    PresetManager pm (proc.apvts, "PULSAR");
    const auto path = pm.userDir().getFullPathName();
    REQUIRE (path.contains ("Application Support"));
    REQUIRE (path.contains ("OVNI PULSAR"));
    REQUIRE (! path.contains ("Audio/Presets"));   // gotcha heredado de ÓRBITA: nunca la carpeta root-owned
}

//==================================================================================================
// ABState — comparador A/B en memoria
//==================================================================================================
TEST_CASE ("ABState: toggle alterna estados sin perder datos", "[s3]")
{
    FakeProcessor proc;
    ABState ab (proc.apvts);
    auto* wp = proc.apvts.getParameter ("width");

    wp->setValueNotifyingHost (wp->convertTo0to1 (80.0f));
    ab.toggle();   // guarda A(width=80), pasa a B (estado inicial 50)
    wp->setValueNotifyingHost (wp->convertTo0to1 (20.0f));
    ab.toggle();   // guarda B(width=20), vuelve a A(width=80)

    REQUIRE (std::abs (rawVal (proc, "width") - 80.0f) < 0.5f);
    REQUIRE (ab.activeSlot() == 'A');

    ab.toggle();   // vuelve a B(width=20)
    REQUIRE (std::abs (rawVal (proc, "width") - 20.0f) < 0.5f);
    REQUIRE (ab.activeSlot() == 'B');
}

TEST_CASE ("ABState: copyActiveToOther arranca el otro slot desde el activo", "[s3]")
{
    FakeProcessor proc;
    ABState ab (proc.apvts);
    auto* wp = proc.apvts.getParameter ("width");

    wp->setValueNotifyingHost (wp->convertTo0to1 (70.0f));   // A = 70
    ab.copyActiveToOther();                                   // B := A (70)
    ab.toggle();                                              // ahora en B
    REQUIRE (ab.activeSlot() == 'B');
    REQUIRE (std::abs (rawVal (proc, "width") - 70.0f) < 0.5f);
}
