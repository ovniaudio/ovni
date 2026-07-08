#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include <cmath>

namespace pid = pulsar::params::id;

// Helper: setea un param por su id (0..1 normalizado vía el AudioParameter).
static void setNorm (juce::AudioProcessorValueTreeState& s, const char* id, float v01)
{
    if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (v01);
}

TEST_CASE ("PULSAR: existe el param division con 4 opciones", "[diccionario][pulsar]")
{
    pulsar::PulsarProcessor proc;
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid::DIVISION));
    REQUIRE (p != nullptr);
    REQUIRE (p->choices.size() == 4);
    REQUIRE (p->choices[0] == "1/4");
    REQUIRE (p->choices[3] == "2 bar");
}

TEST_CASE ("PULSAR: en SYNC el rateHz sale de la division elegida, no del knob Hz", "[diccionario][pulsar]")
{
    pulsar::PulsarProcessor proc;
    proc.prepareToPlay (48000.0, 256);

    setNorm (proc.apvts, pid::SYNC, 1.0f);
    setNorm (proc.apvts, pid::DIVISION, 2.0f / 3.0f);   // index 2 de 4 (0..3) => "1 bar"
    // 1 bar @120 BPM = 2 s => 0.5 ciclos/seg
    REQUIRE (proc.debugSyncRateHz (120.0) == Catch::Approx (0.5f).margin (0.01f));

    setNorm (proc.apvts, pid::DIVISION, 0.0f);          // "1/4" @120 = 0.5 s => 2 ciclos/seg
    REQUIRE (proc.debugSyncRateHz (120.0) == Catch::Approx (2.0f).margin (0.01f));
}

TEST_CASE ("PULSAR: MIX controla dry/wet (mix=0 ~ dry, mix=1 = wet)", "[diccionario][pulsar]")
{
    auto makeDry = [] {
        juce::AudioBuffer<float> b (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i) b.setSample (ch, i, std::sin (i * 0.05f));
        return b;
    };
    const juce::AudioBuffer<float> dry = makeDry();

    // Instancia FRESCA por valor de MIX => sin fuga de estado (smear/trayectoria) entre mediciones.
    auto diffFromDry = [&] (float mix01) {
        pulsar::PulsarProcessor proc;
        proc.prepareToPlay (48000.0, 512);
        REQUIRE (proc.apvts.getParameter (pid::MIX) != nullptr);
        setNorm (proc.apvts, pid::MIX, mix01);
        auto buf = makeDry();
        juce::MidiBuffer m;
        proc.processBlock (buf, m);
        float acc = 0.f;
        for (int i = 0; i < 512; ++i) acc += std::abs (buf.getSample (0, i) - dry.getSample (0, i));
        return acc;
    };

    const float dDry = diffFromDry (0.0f);   // mix=0 => salida ~ dry => diff chico
    const float dWet = diffFromDry (1.0f);   // mix=1 => salida procesada => diff grande
    REQUIRE (dWet > dDry + 1.0f);
}

// ── Regla de layout CRÍTICA (layout ÓRBITA): el POZO (StellarPad) vive a la IZQUIERDA, holgado, y
//    TODA la bahía de controles (TEMPO/SYNC, macros, IN PHASE, I/O, meter) queda a su DERECHA sin
//    tocarlo. Esto codifica el arreglo del "encajonado": antes lo flanqueaban dos rieles. Bounds, no
//    thumbnail.
TEST_CASE ("PULSAR: el pozo manda a la izquierda y la bahía no lo pisa", "[diccionario][pulsar]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    pulsar::PulsarProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 580);   // fuerza el layout (layoutBody) al base
    auto* pe = dynamic_cast<pulsar::PulsarEditor*> (ed.get());
    REQUIRE (pe != nullptr);

    const auto sync = pe->dbgSyncBounds();
    const auto ip   = pe->dbgInPhaseBounds();
    const auto util = pe->dbgUtilUnionBounds();
    const auto pad  = pe->dbgPadBounds();
    INFO ("sync=" << sync.toString().toStdString() << "  inPhase=" << ip.toString().toStdString()
          << "  util=" << util.toString().toStdString() << "  pad=" << pad.toString().toStdString());

    REQUIRE_FALSE (sync.isEmpty());
    REQUIRE_FALSE (util.isEmpty());
    REQUIRE_FALSE (pad.isEmpty());

    // El POZO entero a la IZQUIERDA de toda la bahía (holgado, no encajonado).
    REQUIRE (pad.getRight() <= sync.getX());   // TEMPO/SYNC a la derecha del pozo
    REQUIRE (pad.getRight() <= ip.getX());     // IN PHASE a la derecha del pozo
    REQUIRE (pad.getRight() <= util.getX());   // util (I/O + meter) a la derecha del pozo
    REQUIRE_FALSE (pad.intersects (sync));
    REQUIRE_FALSE (pad.intersects (ip));
    REQUIRE_FALSE (pad.intersects (util));
    REQUIRE_FALSE (sync.intersects (ip));      // TEMPO (arriba) y IN PHASE (abajo) no se pisan

    const auto knobs  = pe->dbgRailKnobBounds();
    const auto labels = pe->dbgRailLabels();
    REQUIRE (knobs.size() == 5);
    const juce::StringArray expected { "MOTION", "SMEAR", "WIDTH", "LOW CUT", "HI CUT" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        INFO ("knob " << labels[i].toStdString() << " bounds=" << knobs[i].toString().toStdString());
        REQUIRE_FALSE (knobs[i].isEmpty());              // todos ubicados (layoutBody los tocó)
        REQUIRE (knobs[i].getX() >= pad.getRight());     // cada knob a la derecha del pozo
        REQUIRE_FALSE (knobs[i].intersects (pad));       // ninguno toca el pozo
        REQUIRE (labels[i] == expected[(int) i]);        // etiqueta canónica presente (MIX incluido)
    }
}

TEST_CASE ("PULSAR: snapshot del rail en FREE y SYNC", "[snapshot][pulsar]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto shoot = [] (float syncV, const char* path) {
        pulsar::PulsarProcessor proc;
        if (auto* p = proc.apvts.getParameter (pid::SYNC)) p->setValueNotifyingHost (syncV);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor()); // el ctor de SyncControl lee el estado sync
        ed->setSize (960, 580);
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        juce::File f (path); f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    };
    shoot (0.0f, "/tmp/pulsar_free.png");
    shoot (1.0f, "/tmp/pulsar_sync.png");
    SUCCEED ("snapshots: /tmp/pulsar_{free,sync}.png");
}
