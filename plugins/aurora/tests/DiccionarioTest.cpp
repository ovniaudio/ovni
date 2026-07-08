// [diccionario][aurora] — vocabulario + layout + honestidad del editor de AURORA.
// Verificación DETERMINÍSTICA por bounds (no por captura — lección de NÉBULA): la banda
// SYNC y los knobs del rail NUNCA pisan la columna de utilidad (IN/OUT/IN PHASE/meter).
// + SYNC usa la división elegida; el RATE FREE no tiene zona muerta; el campo visual
// responde a las macros (regla anti-bug PULSAR); los cortes de curaduría siguen cortados.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <atomic>
#include <array>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include "ui/AuroraField.h"

namespace pid = aurora::params::id;

static void setNorm (juce::AudioProcessorValueTreeState& s, const char* id, float v01)
{ if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (v01); }

// ── SYNC = control CORE del sello. En AURORA controla su elemento rítmico natural: el MOTION del
//    abanico. En SYNC, debugMotionRateHz debe usar la DIVISIÓN ELEGIDA (no una fija) — espeja HALO.
TEST_CASE ("AURORA: en SYNC el MOTION usa la division elegida (no fija)", "[diccionario][aurora]")
{
    aurora::AuroraProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    setNorm (proc.apvts, pid::MOTIONSYNC, 1.0f);

    namespace sd = aurora::params::sync;
    const float hzQuarter = sd::motionRateHz (120.0, 0);   // idx0 = "1/4" (1 beat/ciclo)
    const float hzHalf    = sd::motionRateHz (120.0, 1);   // idx1 = "1/2" (2 beats/ciclo)
    REQUIRE (hzHalf == Catch::Approx (hzQuarter * 0.5f).margin (0.001f));

    // motionDivision es un choice de kCount entradas; el valor normalizado del índice i es i/(kCount-1).
    const float denom = (float) (sd::kCount - 1);
    setNorm (proc.apvts, pid::MOTIONDIV, 0.0f / denom);    // idx0 = "1/4"
    REQUIRE (proc.debugEffectiveMotionRateHz (120.0) == Catch::Approx (hzQuarter).margin (0.001f));
    setNorm (proc.apvts, pid::MOTIONDIV, 1.0f / denom);    // idx1 = "1/2"
    REQUIRE (proc.debugEffectiveMotionRateHz (120.0) == Catch::Approx (hzHalf).margin (0.001f));
}

// ── Honestidad del RATE en FREE (lección del RATE de HALO): mapeo log → el knob cambia la velocidad
//    en TODO su recorrido (sin zona muerta), y en SYNC manda la división, no el knob.
TEST_CASE ("AURORA: el RATE en FREE es monotono y sin zona muerta; en SYNC manda la division", "[diccionario][aurora]")
{
    aurora::AuroraProcessor proc;
    proc.prepareToPlay (48000.0, 256);
    REQUIRE (proc.apvts.getParameter (pid::MOTIONRATE) != nullptr);

    setNorm (proc.apvts, pid::MOTIONSYNC, 0.0f);   // FREE: la velocidad sale de la perilla RATE
    auto rateAt = [&] (float norm) { setNorm (proc.apvts, pid::MOTIONRATE, norm);
                                     return proc.debugEffectiveMotionRateHz (120.0); };

    REQUIRE (rateAt (0.0f) <= 0.03f);                       // piso 0.02 Hz (~50 s/ciclo: respiración lenta real)
    REQUIRE (rateAt (0.8f) > rateAt (0.2f) * 1.5f);         // monótono + audible
    REQUIRE (rateAt (0.25f) > rateAt (0.05f) * 1.4f);       // el tramo lento TAMBIÉN responde (sin zona muerta)

    setNorm (proc.apvts, pid::MOTIONSYNC, 1.0f);            // SYNC: manda la división
    namespace sd = aurora::params::sync;
    setNorm (proc.apvts, pid::MOTIONDIV, 0.0f);             // idx0 = "1/4"
    REQUIRE (proc.debugEffectiveMotionRateHz (120.0) == Catch::Approx (sd::motionRateHz (120.0, 0)).margin (0.001f));
}

// ── Regla de layout CRÍTICA (Diccionario §4): el control SYNC NO pisa el IN PHASE (columna de
//    utilidad, derecha). Bounds, no thumbnail.
TEST_CASE ("AURORA: el control SYNC NO se solapa con IN PHASE", "[diccionario][aurora]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    aurora::AuroraProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 580);   // fuerza el layout (layoutBody)
    auto* ae = dynamic_cast<aurora::AuroraEditor*> (ed.get());
    REQUIRE (ae != nullptr);
    const auto s  = ae->dbgSyncBounds();
    const auto ip = ae->dbgInPhaseBounds();
    INFO ("sync=" << s.toString().toStdString() << "  inPhase=" << ip.toString().toStdString());
    REQUIRE_FALSE (s.intersects (ip));     // no se pisan
    REQUIRE (s.getRight() <= ip.getX());   // SYNC entero a la IZQUIERDA del IN PHASE
}

// ── Ningún knob del rail pisa la columna de utilidad (meter ∪ IN ∪ OUT ∪ IN PHASE), y la aurora
//    (el campo) tampoco. Las etiquetas del rail están presentes y son las de la curaduría.
TEST_CASE ("AURORA: rail y campo en regiones disjuntas de la utilidad + etiquetas presentes", "[diccionario][aurora]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    aurora::AuroraProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    ed->setSize (960, 580);
    auto* ae = dynamic_cast<aurora::AuroraEditor*> (ed.get());
    REQUIRE (ae != nullptr);

    const auto util = ae->dbgUtilUnionBounds();
    REQUIRE_FALSE (util.isEmpty());

    const auto knobs  = ae->dbgRailKnobBounds();
    const auto labels = ae->dbgRailLabels();
    REQUIRE (knobs.size() == 6);
    REQUIRE (labels.size() == 6);

    const juce::StringArray expected { "SPREAD", "TILT", "MOTION", "MONO SAFE", "DUCK", "MIX" };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        INFO ("knob " << labels[i].toStdString() << " bounds=" << knobs[i].toString().toStdString()
              << "  util=" << util.toString().toStdString());
        REQUIRE_FALSE (knobs[i].isEmpty());                       // todos ubicados (layoutBody los tocó)
        REQUIRE_FALSE (knobs[i].intersects (util));               // nadie pisa la utilidad
        REQUIRE (labels[i] == expected[(int) i]);                 // etiqueta canónica presente
    }

    // El campo (la aurora) y la banda SYNC tampoco pisan la utilidad.
    REQUIRE_FALSE (ae->dbgFieldBounds().intersects (util));
    REQUIRE_FALSE (ae->dbgSyncBounds().intersects (util));
    REQUIRE_FALSE (ae->dbgFieldBounds().isEmpty());
}

// ── Regla anti-bug PULSAR: ninguna macro sin efecto VISUAL. El campo (AuroraField) se prueba
//    directo con atomics (como los circulitos de HALO): SPREAD abre el abanico (γ), TILT lo
//    reordena (graves⇄agudos) y MONO SAFE amarra los graves al centro.
TEST_CASE ("AURORA campo: gamma abre, TILT reordena y MONO SAFE amarra los graves", "[diccionario][aurora]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr int kB = aurora::ui::AuroraField::kBands;

    // Telemetría sintética: sin energía (idle) → el campo dibuja la LEY del motor replicada.
    std::atomic<float> spread {0.85f}, tilt {0.0f}, motion {0.0f}, ms {0.0f}, duck {0.0f}, mixA {1.0f};
    std::atomic<float> gamma {0.85f}, duckEnv {0.0f}, rate {0.45f};
    std::array<std::atomic<float>, kB> energy {};
    std::array<std::atomic<float>, kB> pos {};

    auto maxAbsIn = [] (aurora::ui::AuroraField& f, int b0, int b1)
    {
        float m = 0.0f;
        for (int b = b0; b < b1; ++b) m = juce::jmax (m, std::abs (f.dbgDrawPos (b)));
        return m;
    };

    // (a) γ alto (spread abierto) → el abanico se DESPLIEGA; γ→0 → colapsa al centro.
    {
        aurora::ui::AuroraField f (spread, tilt, motion, ms, duck, mixA, gamma, duckEnv, rate, energy, pos);
        f.dbgAdvanceFrames (150);
        const float wide = maxAbsIn (f, 0, kB);
        gamma.store (0.02f);
        f.dbgAdvanceFrames (150);
        const float narrow = maxAbsIn (f, 0, kB);
        INFO ("wide=" << wide << "  narrow=" << narrow);
        REQUIRE (wide > 0.3f);       // desplegado de verdad
        REQUIRE (narrow < 0.08f);    // cerrado de verdad (γ≈0 = mono)
    }

    // (b) TILT 0: los agudos van a los bordes y los graves quedan cerca del centro;
    //     TILT −100: se INVIERTE (los graves a los bordes, el aire amarrado al centro).
    gamma.store (0.85f);
    {
        tilt.store (0.0f);
        aurora::ui::AuroraField f (spread, tilt, motion, ms, duck, mixA, gamma, duckEnv, rate, energy, pos);
        f.dbgAdvanceFrames (150);
        const float lows = maxAbsIn (f, 0, 6), highs = maxAbsIn (f, 18, kB);
        INFO ("tilt0: lows=" << lows << "  highs=" << highs);
        REQUIRE (highs > lows * 2.0f);
    }
    {
        tilt.store (-1.0f);
        aurora::ui::AuroraField f (spread, tilt, motion, ms, duck, mixA, gamma, duckEnv, rate, energy, pos);
        f.dbgAdvanceFrames (150);
        const float lows = maxAbsIn (f, 0, 6), highs = maxAbsIn (f, 18, kB);
        INFO ("tilt-100: lows=" << lows << "  highs=" << highs);
        REQUIRE (lows > highs * 1.5f);
    }

    // (c) MONO SAFE 100 (corte 700 Hz): los graves quedan AMARRADOS al centro; el aire sigue libre.
    {
        tilt.store (0.0f);
        ms.store (1.0f);
        aurora::ui::AuroraField f (spread, tilt, motion, ms, duck, mixA, gamma, duckEnv, rate, energy, pos);
        f.dbgAdvanceFrames (150);
        const float lows = maxAbsIn (f, 0, 6), highs = maxAbsIn (f, 18, kB);
        INFO ("monoSafe100: lows=" << lows << "  highs=" << highs);
        REQUIRE (lows < 0.05f);      // graves al centro (la red se VE)
        REQUIRE (highs > 0.3f);      // el aire sigue desplegado
    }

    // (d) Con SEÑAL la cortina va donde el MOTOR la puso (honestidad: posición real > ley idle).
    {
        ms.store (0.0f);
        energy[20].store (0.05f);    // banda de aire con energía
        pos[20].store (-0.9f);       // el motor la mandó bien a la IZQUIERDA
        aurora::ui::AuroraField f (spread, tilt, motion, ms, duck, mixA, gamma, duckEnv, rate, energy, pos);
        f.dbgAdvanceFrames (150);
        INFO ("band20 drawPos=" << f.dbgDrawPos (20));
        REQUIRE (f.dbgDrawPos (20) == Catch::Approx (-0.9f).margin (0.05f));
        energy[20].store (0.0f);
        pos[20].store (0.0f);
    }
}

// ── Honestidad de curaduría (ES LEY): los CORTES siguen cortados (sin curvas custom dibujables)
//    y las 6 macros + SYNC existen con sus ids canónicos.
TEST_CASE ("AURORA: sin curvas custom (corte de curaduria); las 6 macros + SYNC existen", "[diccionario][aurora]")
{
    aurora::AuroraProcessor proc;
    REQUIRE (proc.apvts.getParameter ("curve")       == nullptr);
    REQUIRE (proc.apvts.getParameter ("customCurve") == nullptr);
    REQUIRE (proc.apvts.getParameter ("map")         == nullptr);
    for (const char* id : { pid::SPREAD, pid::TILT, pid::MOTION, pid::MONOSAFEAMT, pid::DUCK, pid::MIX,
                            pid::BYPASS, pid::MOTIONSYNC, pid::MOTIONDIV, pid::MOTIONRATE })
        REQUIRE (proc.apvts.getParameter (id) != nullptr);
}
