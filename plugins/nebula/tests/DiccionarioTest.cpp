#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace pid = nebula::params::id;

static void setNorm (juce::AudioProcessorValueTreeState& s, const char* id, float v01)
{ if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (v01); }

TEST_CASE ("NEBULA: en SYNC el breath usa la division elegida (no fija a 1 bar)", "[diccionario][nebula]")
{
    nebula::NebulaProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    setNorm (proc.apvts, pid::BREATHSYNC, 1.0f);

    namespace sd = nebula::params::sync;
    const float hz1bar  = sd::breathRateHz (120.0, 1);
    const float hz2bars = sd::breathRateHz (120.0, 2);
    REQUIRE (hz2bars == Catch::Approx (hz1bar * 0.5f).margin (0.001f));

    setNorm (proc.apvts, pid::BREATHDIV, 1.0f / 3.0f);   // idx1 = "1 bar"
    REQUIRE (proc.debugBreathRateHz (120.0) == Catch::Approx (hz1bar).margin (0.001f));
    setNorm (proc.apvts, pid::BREATHDIV, 2.0f / 3.0f);   // idx2 = "2 bars"
    REQUIRE (proc.debugBreathRateHz (120.0) == Catch::Approx (hz2bars).margin (0.001f));
}

// Regresión del bug que Joaquín cazó: el control SYNC se montaba sobre el botón IN PHASE.
// Verificación DETERMINÍSTICA (sin mirar capturas): regiones DISJUNTAS — el SYNC (rail izquierdo, vertical)
// no pisa la columna de utilidad (meter/filtros/IN/OUT/IN PHASE, derecha), el campo nebular vive entre
// medio sin tocar a nadie, y el rail de macros (abajo) no pisa la utilidad ni el campo. Bounds, no thumbnail.
TEST_CASE ("NEBULA: SYNC, campo y rail de macros NO pisan la utilidad", "[diccionario][nebula]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    nebula::NebulaProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (900, 600);   // fuerza el layout (layoutBody)
    auto* ne = dynamic_cast<nebula::NebulaEditor*> (ed.get());
    REQUIRE (ne != nullptr);

    const auto sync  = ne->dbgSyncBounds();
    const auto ip    = ne->dbgInPhaseBounds();
    const auto util  = ne->dbgUtilUnionBounds();
    const auto cloud = ne->dbgCloudBounds();
    INFO ("sync=" << sync.toString().toStdString() << "  inPhase=" << ip.toString().toStdString()
          << "  util=" << util.toString().toStdString() << "  cloud=" << cloud.toString().toStdString());

    REQUIRE_FALSE (sync.isEmpty());
    REQUIRE_FALSE (util.isEmpty());
    REQUIRE_FALSE (cloud.isEmpty());
    REQUIRE_FALSE (sync.intersects (ip));      // no se pisan
    REQUIRE (sync.getRight() <= ip.getX());    // SYNC entero a la IZQUIERDA del IN PHASE
    REQUIRE_FALSE (sync.intersects (util));    // el SYNC no toca la utilidad
    REQUIRE_FALSE (cloud.intersects (util));   // el campo nebular no toca la utilidad
    REQUIRE_FALSE (cloud.intersects (sync));   // ni al rail de tempo

    const auto knobs  = ne->dbgRailKnobBounds();
    const auto labels = ne->dbgRailLabels();
    REQUIRE (knobs.size() == 5);
    const juce::StringArray expected { "SIZE", "DECAY", "TONE", "BREATH", "MIX" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        INFO ("knob " << labels[i].toStdString() << " bounds=" << knobs[i].toString().toStdString());
        REQUIRE_FALSE (knobs[i].isEmpty());            // todos ubicados (layoutBody los tocó)
        REQUIRE_FALSE (knobs[i].intersects (util));    // nadie pisa la utilidad
        REQUIRE_FALSE (knobs[i].intersects (cloud));   // ni el campo nebular
        REQUIRE (labels[i] == expected[(int) i]);      // etiqueta canónica presente (MIX incluido)
    }
}

TEST_CASE ("NEBULA: snapshot del sync en FREE y SYNC", "[snapshot][nebula]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto shoot = [] (float syncV, const char* path) {
        nebula::NebulaProcessor proc;
        if (auto* p = proc.apvts.getParameter (pid::BREATHSYNC)) p->setValueNotifyingHost (syncV);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (900, 600); ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File f (path); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
    };
    shoot (0.0f, "/tmp/nebula_free.png");
    shoot (1.0f, "/tmp/nebula_sync.png");
    SUCCEED ("snapshots written");
}

// =============================================================================
// [honestidad] La telemetría RT60 de la nube dice LA VERDAD del motor: el readout
// tiene que salir de FdnReverb::t60ForDecay (única fuente de verdad), no de una
// fórmula propia del mockup. Regresión del bug 2026-07-16: la nube mostraba
// "RT60 6.3 s" (0.2 + decay·map(size, 4→11.8)) cuando el motor apuntaba a ~2.9 s,
// y el SIZE ni siquiera cambia el T60 real (el motor recalcula g_i para conservarlo).
// =============================================================================
#include "ui/NebulaCloud.h"
#include "engines/fdn/FdnReverb.h"

TEST_CASE ("NEBULA: el readout RT60 == t60ForDecay del motor (no formula de mockup)", "[diccionario][honestidad][nebula]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::atomic<float> size { 0.6f }, decay { 0.5f }, tone { 0.4f }, breath { 0.25f }, breathLfo { 0.0f };
    nebula::ui::NebulaCloud cloud (size, decay, tone, breath, breathLfo, nullptr, nullptr);

    for (float d : { 0.25f, 0.5f, 0.7f, 0.75f, 1.0f })
        for (float s : { 0.0f, 0.6f, 1.0f })         // el SIZE NO debe mover el readout
        {
            decay.store (d); size.store (s);
            cloud.dbgSettleTelemetry();
            const float shown  = cloud.dbgRt60Text().upToFirstOccurrenceOf (" ", false, false).getFloatValue();
            const float target = ovni::engines::FdnReverb::t60ForDecay (d);
            INFO ("decay=" << d << " size=" << s << "  readout=" << shown << "  motor=" << target);
            REQUIRE (shown == Catch::Approx (target).margin (0.06));   // margen del redondeo "%.1f s"
        }
}
