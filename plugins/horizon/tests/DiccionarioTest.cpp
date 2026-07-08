// [diccionario][horizon] — vocabulario + layout + honestidad del editor de HORIZON.
// Verificación DETERMINÍSTICA por bounds (no por captura): la banda SYNC, el FREEZE y los
// knobs del rail NUNCA pisan la columna de utilidad (IN/OUT/IN PHASE/meter). + SYNC usa la
// división elegida; el RATE FREE no tiene zona muerta; el campo visual responde a las macros
// (regla anti-bug PULSAR); los cortes de curaduría siguen cortados (whisper único nombre).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <atomic>
#include <array>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include "ui/HorizonField.h"

namespace pid = horizon::params::id;

static void setNorm (juce::AudioProcessorValueTreeState& s, const char* id, float v01)
{ if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (v01); }

// ── SYNC = control CORE. En HORIZON controla el re-trigger del freeze. En SYNC,
//    debugSyncRateHz debe usar la DIVISIÓN ELEGIDA (no una fija) — espeja AURORA/HALO.
TEST_CASE ("HORIZON: en SYNC el re-trigger usa la division elegida (no fija)", "[diccionario][horizon]")
{
    horizon::HorizonProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    setNorm (proc.apvts, pid::RATESYNC, 1.0f);

    namespace sd = horizon::params::sync;
    const float hz16 = sd::syncRateHz (120.0, 0);   // idx0 = "1/16" (0.25 beats/ciclo) → el más rápido
    const float hz8  = sd::syncRateHz (120.0, 1);   // idx1 = "1/8"  (0.5 beats/ciclo)
    REQUIRE (hz8 == Catch::Approx (hz16 * 0.5f).margin (0.001f));

    const float denom = (float) (sd::kCount - 1);
    setNorm (proc.apvts, pid::RATEDIV, 0.0f / denom);    // idx0 = "1/16"
    REQUIRE (proc.debugEffectiveRateHz (120.0) == Catch::Approx (hz16).margin (0.001f));
    setNorm (proc.apvts, pid::RATEDIV, 1.0f / denom);    // idx1 = "1/8"
    REQUIRE (proc.debugEffectiveRateHz (120.0) == Catch::Approx (hz8).margin (0.001f));
}

// ── Honestidad del RATE en FREE: 0 = OFF (sostenido), monótono y sin zona muerta arriba;
//    en SYNC manda la división, no el knob.
TEST_CASE ("HORIZON: el RATE en FREE es 0=OFF, monotono sin zona muerta; en SYNC manda la division", "[diccionario][horizon]")
{
    horizon::HorizonProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    REQUIRE (proc.apvts.getParameter (pid::RATE) != nullptr);

    setNorm (proc.apvts, pid::RATESYNC, 0.0f);   // FREE: la velocidad sale de la perilla RATE
    auto rateAt = [&] (float norm) { setNorm (proc.apvts, pid::RATE, norm);
                                     return proc.debugEffectiveRateHz (120.0); };

    REQUIRE (rateAt (0.0f) < 0.01f);                  // 0 = OFF (sostenido = pad)
    REQUIRE (rateAt (1.0f) == Catch::Approx (horizon::params::kRateMaxHz).margin (0.05f));  // tope 8 Hz
    REQUIRE (rateAt (0.8f) > rateAt (0.2f) * 1.5f);   // monótono + audible
    REQUIRE (rateAt (0.25f) > rateAt (0.05f) * 1.4f); // el tramo lento TAMBIÉN responde (sin zona muerta)

    setNorm (proc.apvts, pid::RATESYNC, 1.0f);        // SYNC: manda la división
    namespace sd = horizon::params::sync;
    setNorm (proc.apvts, pid::RATEDIV, 0.0f);         // idx0 = "1/16"
    REQUIRE (proc.debugEffectiveRateHz (120.0) == Catch::Approx (sd::syncRateHz (120.0, 0)).margin (0.001f));
}

// ── Regla de layout CRÍTICA (Diccionario §4): el control SYNC y el FREEZE NO pisan el IN
//    PHASE (columna de utilidad, derecha). Bounds, no thumbnail.
TEST_CASE ("HORIZON: SYNC y FREEZE NO se solapan con IN PHASE", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    horizon::HorizonProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 580);   // fuerza el layout (layoutBody)
    auto* he = dynamic_cast<horizon::HorizonEditor*> (ed.get());
    REQUIRE (he != nullptr);
    const auto s  = he->dbgSyncBounds();
    const auto fz = he->dbgFreezeBounds();
    const auto ip = he->dbgInPhaseBounds();
    INFO ("sync=" << s.toString().toStdString() << "  freeze=" << fz.toString().toStdString()
          << "  inPhase=" << ip.toString().toStdString());
    REQUIRE_FALSE (s.intersects (ip));      // SYNC no pisa IN PHASE
    REQUIRE_FALSE (fz.intersects (ip));     // FREEZE no pisa IN PHASE
    REQUIRE (s.getRight()  <= ip.getX());   // SYNC entero a la IZQUIERDA del IN PHASE
    REQUIRE (fz.getRight() <= ip.getX());   // FREEZE entero a la IZQUIERDA del IN PHASE
}

// ── Ningún knob del rail pisa la columna de utilidad, ni el campo. Etiquetas de curaduría.
TEST_CASE ("HORIZON: rail y campo en regiones disjuntas de la utilidad + etiquetas presentes", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    horizon::HorizonProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 580);
    auto* he = dynamic_cast<horizon::HorizonEditor*> (ed.get());
    REQUIRE (he != nullptr);

    const auto util = he->dbgUtilUnionBounds();
    REQUIRE_FALSE (util.isEmpty());

    const auto knobs  = he->dbgRailKnobBounds();
    const auto labels = he->dbgRailLabels();
    REQUIRE (knobs.size() == 4);
    REQUIRE (labels.size() == 4);

    const juce::StringArray expected { "WHISPER", "SPREAD", "DUCK", "MIX" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        INFO ("knob " << labels[i].toStdString() << " bounds=" << knobs[i].toString().toStdString()
              << "  util=" << util.toString().toStdString());
        REQUIRE_FALSE (knobs[i].isEmpty());
        REQUIRE_FALSE (knobs[i].intersects (util));
        REQUIRE (labels[i] == expected[(int) i]);
    }

    REQUIRE_FALSE (he->dbgFieldBounds().intersects (util));
    REQUIRE_FALSE (he->dbgSyncBounds().intersects (util));
    REQUIRE_FALSE (he->dbgFieldBounds().isEmpty());
}

// ── Regla anti-bug PULSAR: ninguna macro sin efecto VISUAL. El campo (HorizonField) se
//    prueba directo con atomics: el FREEZE cristaliza (las barras aparecen) y el GATE las
//    hace LATIR (gateAmp modula la altura dibujada).
TEST_CASE ("HORIZON campo: el freeze cristaliza y el gate hace latir las barras", "[diccionario][horizon]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr int kB = horizon::ui::HorizonField::kBands;

    std::atomic<float> whisper {0.0f}, spread {0.5f}, duck {0.0f}, mixA {1.0f}, freeze {0.0f};
    std::atomic<float> gateAmp {1.0f}, gatePhase {0.0f}, wetEnergy {0.0f}, duckEnv {0.0f}, rate {0.0f};
    std::array<std::atomic<float>, kB> spectrum {};
    for (int b = 0; b < kB; ++b) spectrum[(size_t) b].store (0.6f);   // espectro congelado lleno

    auto maxBand = [] (horizon::ui::HorizonField& f)
    {
        float m = 0.0f;
        for (int b = 0; b < kB; ++b) m = juce::jmax (m, f.dbgBandHeight (b));
        return m;
    };

    // (a) FREEZE off → campo tenue; FREEZE on → las barras se cristalizan (suben).
    {
        horizon::ui::HorizonField f (whisper, spread, duck, mixA, freeze,
                                     gateAmp, gatePhase, wetEnergy, duckEnv, rate, spectrum);
        f.dbgAdvanceFrames (150);
        const float off = maxBand (f);
        freeze.store (1.0f);
        f.dbgAdvanceFrames (150);
        const float on = maxBand (f);
        INFO ("freezeOff=" << off << "  freezeOn=" << on);
        REQUIRE (on > off * 1.5f);    // el freeze cristaliza el espectro (se VE)
        REQUIRE (on > 0.2f);
    }

    // (b) GATE cerrado (gateAmp→0) agacha las barras; gate abierto las levanta. El latido se VE.
    freeze.store (1.0f);
    {
        horizon::ui::HorizonField f (whisper, spread, duck, mixA, freeze,
                                     gateAmp, gatePhase, wetEnergy, duckEnv, rate, spectrum);
        gateAmp.store (1.0f);
        f.dbgAdvanceFrames (150);
        const float open = maxBand (f);
        gateAmp.store (0.0f);
        f.dbgAdvanceFrames (150);
        const float closed = maxBand (f);
        INFO ("gateOpen=" << open << "  gateClosed=" << closed);
        REQUIRE (open > 0.2f);
        REQUIRE (closed < open * 0.3f);   // el gate cierra el latido (se VE pulsar)
    }
}

// ── Honestidad de curaduría (ES LEY): el CORTE de nombre sigue (whisper como nombre único:
//    NO existe "shimmer") y las 6 controles + SYNC existen con sus ids canónicos.
TEST_CASE ("HORIZON: whisper como nombre unico (corte); los 6 controles + SYNC existen", "[diccionario][horizon]")
{
    horizon::HorizonProcessor proc;
    // El corte de nombre: NO hay "shimmer" (es identidad de HALO) — sólo "whisper".
    REQUIRE (proc.apvts.getParameter ("shimmer") == nullptr);
    REQUIRE (proc.apvts.getParameter (pid::WHISPER) != nullptr);
    // El corte de modo: NO hay un param de "re-captura viva" (v1 = SOLO gate del sostenido).
    REQUIRE (proc.apvts.getParameter ("recapture") == nullptr);
    REQUIRE (proc.apvts.getParameter ("liveCapture") == nullptr);
    for (const char* id : { pid::FREEZE, pid::WHISPER, pid::SPREAD, pid::DUCK, pid::MIX,
                            pid::BYPASS, pid::RATESYNC, pid::RATEDIV, pid::RATE })
        REQUIRE (proc.apvts.getParameter (id) != nullptr);
}
