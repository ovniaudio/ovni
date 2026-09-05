// [supernova][lfo] — la modulación tal como la aplica el render: escala por rango del parámetro, clamp al
// dominio legal, suma de varios slots al mismo destino, y RESOLUCIÓN TEMPORAL (el escalón entre cuadros).
// Estos tests son los que faltaban: los 5 de LfoBankTest sólo miran la matemática pura de valueFor().
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "tempo/LfoModulation.h"
#include "params/ParamMapping.h"
#include "params/ParameterIDs.h"
#include <cmath>
#include <cstring>
#include <vector>

using Catch::Approx;
using supernova::LfoBank;
using supernova::LfoShape;
using supernova::lfoModulated;
namespace pid = supernova::params::id;

namespace
{
// Los 12 destinos curados del LfoPanel, con el campo de ParticleParams que alimentan y su DOMINIO físico.
struct Dest { const char* id; const char* field; float lo, hi; };
const Dest kDests[] = {
    { pid::HUE,           "hueShift",     -3.14159274f, 3.14159274f },   // ±180° en rad
    { pid::SAT,           "satAmt",        0.0f, 2.0f },
    { pid::INTENSITY,     "intensity",     0.0f, 1.0f },
    { pid::PARTICLE_SIZE, "particleSize",  0.5f, 4.0f },
    { pid::GLOW,          "glow",          0.0f, 1.0f },
    { pid::CHAOS,         "chaos",         0.0f, 1.0f },
    { pid::ROTATE,        "rotateRate",   -0.5235988f, 0.5235988f },     // ±30°/s en rad
    { pid::ORBIT,         "orbitRate",    -0.7853982f, 0.7853982f },     // ±45°/s en rad
    { pid::SCATTER,       "scatterAmt",    0.0f, 1.0f },
    { pid::TRAILS,        "trailAmt",      0.0f, 1.0f },
    { pid::LINKS,         "linksAmt",      0.0f, 1.0f },
    { pid::DEPTH,         "depthAmt",      0.0f, 1.0f },
};
constexpr int kNumDests = (int) (sizeof (kDests) / sizeof (kDests[0]));

float fieldOf (const supernova::ParticleParams& pp, const char* f)
{
    if (! std::strcmp (f, "hueShift"))     return pp.hueShift;
    if (! std::strcmp (f, "satAmt"))       return pp.satAmt;
    if (! std::strcmp (f, "intensity"))    return pp.intensity;
    if (! std::strcmp (f, "particleSize")) return pp.particleSize;
    if (! std::strcmp (f, "glow"))         return pp.glow;
    if (! std::strcmp (f, "chaos"))        return pp.chaos;
    if (! std::strcmp (f, "rotateRate"))   return pp.rotateRate;
    if (! std::strcmp (f, "orbitRate"))    return pp.orbitRate;
    if (! std::strcmp (f, "scatterAmt"))   return pp.scatterAmt;
    if (! std::strcmp (f, "trailAmt"))     return pp.trailAmt;
    if (! std::strcmp (f, "linksAmt"))     return pp.linksAmt;
    if (! std::strcmp (f, "depthAmt"))     return pp.depthAmt;
    return 0.0f;
}
}

TEST_CASE ("lfo: un LFO a fondo nunca saca un ParticleParams fuera de su dominio", "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    supernova::SupernovaProcessor proc;   // los rangos REALES del APVTS, no una tabla que puede derivar

    for (const auto& d : kDests)
    {
        const auto range = proc.apvts.getParameterRange (juce::String (d.id));

        for (const bool bipolar : { true, false })
        for (const float basePct : { 0.0f, 0.5f, 1.0f })
        for (const int nSlots : { 1, 4 })                      // 4 LFO al MISMO destino: se suman
        {
            LfoBank bank;
            for (int i = 0; i < nSlots; ++i)
            {
                auto& s = bank.slot (i);
                s.enabled = true; s.target = d.id; s.depth = 1.0f; s.bipolar = bipolar;
                s.shape = (LfoShape) (i % 6);
                s.beatsPerCycle = 0.25f * (float) (i + 1);
            }
            const float base = range.start + basePct * (range.end - range.start);

            for (int k = 0; k < 400; ++k)
            {
                const double beat = 16.0 * k / 400.0;
                const float v = lfoModulated (bank, d.id, base, range.start, range.end, beat);

                INFO ("destino " << d.id << " base=" << base << " bipolar=" << bipolar
                      << " slots=" << nSlots << " beat=" << beat << " -> APVTS=" << v);
                REQUIRE (v >= std::min (range.start, range.end) - 1e-4f);
                REQUIRE (v <= std::max (range.start, range.end) + 1e-4f);

                supernova::ParticleParams pp = supernova::mapParticleParams (
                    [&] (const char* id) -> float
                    {
                        if (! std::strcmp (id, d.id)) return v;
                        if (! std::strcmp (id, pid::SAT))     return 50.0f;
                        if (! std::strcmp (id, pid::DENSITY)) return 100.0f;
                        if (! std::strcmp (id, pid::SPEED))   return 50.0f;
                        if (! std::strcmp (id, pid::FORM))    return 100.0f;
                        return 0.0f;
                    });

                // Ningún campo alimentado por un destino de LFO puede salirse de su dominio físico.
                for (const auto& c : kDests)
                {
                    const float got = fieldOf (pp, c.field);
                    INFO ("campo " << c.field << " = " << got << " (dominio " << c.lo << ".." << c.hi << ")");
                    REQUIRE (got >= c.lo - 1e-4f);
                    REQUIRE (got <= c.hi + 1e-4f);
                }
            }
        }
    }
    (void) kNumDests;
}

TEST_CASE ("lfo: depth 100 % bipolar cubre exactamente el rango del parámetro", "[supernova][lfo]")
{
    LfoBank bank;
    auto& s = bank.slot (0);
    s.enabled = true; s.target = pid::PARTICLE_SIZE; s.shape = LfoShape::Sine;
    s.depth = 1.0f; s.bipolar = true; s.beatsPerCycle = 4.0f;

    float lo = 1.0e9f, hi = -1.0e9f;
    for (int k = 0; k < 4000; ++k)
    {
        const float v = lfoModulated (bank, pid::PARTICLE_SIZE, 50.0f, 0.0f, 100.0f, 4.0 * k / 4000.0);
        lo = std::min (lo, v); hi = std::max (hi, v);
    }
    REQUIRE (lo == Approx (0.0f).margin (0.05f));     // base 50 → toca el piso
    REQUIRE (hi == Approx (100.0f).margin (0.05f));   // …y el techo, sin pasarse
    REQUIRE ((hi - lo) == Approx (100.0f).margin (0.1f));   // excursión = 100 % del rango, no 200 %

    // Unipolar a fondo: misma excursión (100 % del rango), desde la base hacia arriba.
    s.bipolar = false;
    lo = 1.0e9f; hi = -1.0e9f;
    for (int k = 0; k < 4000; ++k)
    {
        const float v = lfoModulated (bank, pid::PARTICLE_SIZE, 0.0f, 0.0f, 100.0f, 4.0 * k / 4000.0);
        lo = std::min (lo, v); hi = std::max (hi, v);
    }
    REQUIRE (lo == Approx (0.0f).margin (0.05f));
    REQUIRE (hi == Approx (100.0f).margin (0.05f));
}

// ============================================================ L2 · resolución temporal (informe 24, M3/M4)
namespace
{
struct MockPlayHead : juce::AudioPlayHead
{
    double bpm = 120.0, ppq = 0.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

// Escalón máximo entre cuadros consecutivos, muestreando la MISMA función que usa el render.
double maxStepAt (double frameHz, double bpm, float beatsPerCycle)
{
    LfoBank bank;
    auto& s = bank.slot (0);
    s.enabled = true; s.target = pid::PARTICLE_SIZE; s.shape = LfoShape::Sine;
    s.depth = 1.0f; s.bipolar = true; s.beatsPerCycle = beatsPerCycle;

    double maxStep = 0.0;
    float prev = lfoModulated (bank, pid::PARTICLE_SIZE, 50.0f, 0.0f, 100.0f, 0.0);
    for (int k = 1; k <= (int) (frameHz * 4.0); ++k)
    {
        const double beat = (k / frameHz) * (bpm / 60.0);
        const float v = lfoModulated (bank, pid::PARTICLE_SIZE, 50.0f, 0.0f, 100.0f, beat);
        maxStep = std::max (maxStep, (double) std::fabs (v - prev));
        prev = v;
    }
    return maxStep;   // en unidades del param (rango 100) = % del rango
}
}

TEST_CASE ("lfo: el escalón entre cuadros a 60 Hz con un seno a 2 Hz es ≤ 21 % del rango (≤ 11 % a 120 Hz)",
           "[supernova][lfo]")
{
    // 1/4 a 120 BPM = 2 Hz — la división MÁS LENTA que un VJ usa habitualmente. El informe 24 (M4) midió
    // 41,6 % de escalón a 30 Hz; ESE número era con la escala ×2 que L1 corrigió, así que hoy el mismo
    // muestreo a 30 Hz da la mitad. Los techos del pedido (21 % / 11 %) siguen valiendo y quedan holgados.
    const double at30  = maxStepAt (30.0,  120.0, 1.0f);
    const double at60  = maxStepAt (60.0,  120.0, 1.0f);
    const double at120 = maxStepAt (120.0, 120.0, 1.0f);
    INFO ("escalón máx: 30 Hz = " << at30 << " % · 60 Hz = " << at60 << " % · 120 Hz = " << at120 << " %");
    REQUIRE (at60  <= 21.0);
    REQUIRE (at120 <= 11.0);
    // Y la verdad ajustada de hoy: cada duplicación de la tasa parte el escalón al medio.
    REQUIRE (at30  > 20.0);
    REQUIRE (at60  <= at30  * 0.55);
    REQUIRE (at120 <= at60  * 0.55);
}

TEST_CASE ("lfo: el visual recalcula la modulación en CADA cuadro del render, no en el timer de 30 Hz",
           "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    supernova::SupernovaProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);
    ed->setBounds (0, 0, 1100, 760);

    // Después de construir el editor: su ctor hidrata el banco desde el state (vacío = todo apagado).
    auto& s = proc.lfoBank().slot (0);
    s.enabled = true; s.target = pid::PARTICLE_SIZE; s.shape = LfoShape::Sine;
    s.depth = 1.0f; s.bipolar = true; s.beatsPerCycle = 1.0f;   // 1 ciclo por negra

    MockPlayHead ph; proc.setPlayHead (&ph);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;

    // renderFrameTick() es EXACTAMENTE lo que MetalViewComponent::tick llama por VBlank. El timer de 30 Hz
    // no corre acá (nadie bombea el message loop): si el valor cambia, es porque el cuadro lo recalculó.
    auto sizeAtBeat = [&] (double beat)
    {
        ph.ppq = beat; buf.clear(); midi.clear();
        proc.processBlock (buf, midi);
        ed->renderFrameTick();
        return ed->visualParams().particleSize;
    };

    const float atZero = sizeAtBeat (0.0);    // seno en fase 0 → sin desvío
    const float atPeak = sizeAtBeat (0.25);   // 1/4 de ciclo → pico
    INFO ("particleSize beat 0 = " << atZero << " · beat 0.25 = " << atPeak);
    REQUIRE (atPeak > atZero + 1.0f);         // el cuadro siguiente YA ve el LFO movido

    proc.setPlayHead (nullptr);
}

// ======================================================= L4 · bipolar / fase / retrigger (informe 24 · M6)
#include "ui/LfoPanel.h"

namespace
{
supernova::LfoPanel* dummy = nullptr;   // (silencia -Wunused si el include no se usa en otra config)

template <typename T>
T* control (supernova::LfoPanel& p, const char* id) { return dynamic_cast<T*> (p.findChildWithID (id)); }
}

TEST_CASE ("lfo: un slot unipolar restaurado NO se pisa al tocar otro control de la fila", "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LfoBank bank;
    bank.slot (0) = { true, 2.0f, LfoShape::Saw, 0.5f, 0.25f, /*bipolar*/ false, "intensity" };

    supernova::LfoPanel panel (bank);
    panel.setSize (1120, 640);

    auto* depth = control<juce::Slider> (panel, "lfo0.depth");
    REQUIRE (depth != nullptr);
    depth->setValue (80.0, juce::sendNotificationSync);   // el usuario mueve DEPTH, nada más

    REQUIRE (bank.slot (0).depth == Approx (0.8f));
    REQUIRE_FALSE (bank.slot (0).bipolar);                    // no se pisa a true
    REQUIRE (bank.slot (0).phaseOffset == Approx (0.25f));    // la fase tampoco se pierde
    (void) dummy;
}

TEST_CASE ("lfo: el toggle Bi/Uni y la perilla PHASE escriben el slot y vuelven del banco", "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LfoBank bank;
    bank.slot (1) = { true, 1.0f, LfoShape::Sine, 0.5f, 0.0f, true, "glow" };

    supernova::LfoPanel panel (bank);
    panel.setSize (1120, 640);

    auto* polarity = control<juce::TextButton> (panel, "lfo1.polarity");
    auto* phase    = control<juce::Slider>     (panel, "lfo1.phase");
    REQUIRE (polarity != nullptr);
    REQUIRE (phase != nullptr);
    REQUIRE (polarity->getToggleState());                 // arrancó reflejando el banco (bipolar)
    REQUIRE (polarity->getButtonText() == "BI");

    polarity->setToggleState (false, juce::sendNotificationSync);
    phase->setValue (90.0, juce::sendNotificationSync);    // un cuarto de ciclo

    REQUIRE_FALSE (bank.slot (1).bipolar);
    REQUIRE (bank.slot (1).phaseOffset == Approx (0.25f));
    REQUIRE (polarity->getButtonText() == "UNI");

    // …y al re-leer el banco la fila vuelve a mostrar lo mismo (nada se pierde en el viaje de ida y vuelta).
    panel.refreshFromBank();
    REQUIRE_FALSE (polarity->getToggleState());
    REQUIRE (phase->getValue() == Approx (90.0));
}

TEST_CASE ("lfo: RETRIG pone el ciclo en fase 0 en el beat actual sin mover la perilla PHASE", "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LfoBank bank;
    bank.slot (2) = { true, 4.0f, LfoShape::Saw, 1.0f, 0.0f, true, "chaos" };

    supernova::LfoPanel panel (bank);
    panel.setSize (1120, 640);
    double now = 7.3;                       // una posición de beat cualquiera, ni redonda ni al inicio
    panel.beatPos = [&now] { return now; };

    auto* phase = control<juce::Slider> (panel, "lfo2.phase");
    REQUIRE (phase != nullptr);
    phase->setValue (72.0, juce::sendNotificationSync);        // 0.2 de ciclo, puesto por el usuario
    REQUIRE (bank.slot (2).phaseOffset == Approx (0.2f));

    auto* retrig = control<juce::TextButton> (panel, "lfo2.retrig");
    REQUIRE (retrig != nullptr);
    REQUIRE (retrig->onClick != nullptr);
    retrig->onClick();

    // Saw bipolar en fase 0 = −depth: el ciclo arranca EXACTAMENTE acá.
    REQUIRE (bank.valueFor (2, now) == Approx (-1.0f).margin (1e-4));
    // …y medio ciclo (2 beats) después ya recorrió la mitad de la rampa.
    REQUIRE (bank.valueFor (2, now + 2.0) == Approx (0.0f).margin (1e-4));
    // La perilla del usuario quedó donde estaba.
    REQUIRE (bank.slot (2).phaseOffset == Approx (0.2f));
    REQUIRE (phase->getValue() == Approx (72.0));

    // Y persiste: el retrigger sobrevive a guardar/abrir el proyecto.
    LfoBank back; back.deserialize (bank.serialize());
    REQUIRE (back.slot (2).retrigOffset == Approx (bank.slot (2).retrigOffset).margin (1e-5));
    REQUIRE (back.valueFor (2, now) == Approx (-1.0f).margin (1e-3));
}

TEST_CASE ("lfo: undo devuelve el banco de LFO al estado anterior", "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    supernova::SupernovaProcessor proc;
    auto holder = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());
    auto* ed = dynamic_cast<supernova::SupernovaEditor*> (holder.get());
    REQUIRE (ed != nullptr);

    auto& s = proc.lfoBank().slot (0);
    s.enabled = true; s.target = pid::GLOW; s.depth = 0.4f; s.bipolar = true; s.beatsPerCycle = 1.0f;
    proc.syncLfosToState();

    ed->captureUndoState();                 // …el usuario va a hacer algo destructivo

    s.target = pid::CHAOS; s.depth = 0.9f; s.bipolar = false;
    proc.syncLfosToState();

    REQUIRE (ed->doUndo());
    REQUIRE (proc.lfoBank().slot (0).target == pid::GLOW);
    REQUIRE (proc.lfoBank().slot (0).depth == Approx (0.4f));
    REQUIRE (proc.lfoBank().slot (0).bipolar);
}

// ==================================================== L5 · rate libre en Hz + tresillo/puntillo (informe A)
TEST_CASE ("lfo: en modo Hz el ciclo lo manda el tiempo, no el tempo", "[supernova][lfo]")
{
    LfoBank bank;
    auto& s = bank.slot (0);
    s.enabled = true; s.target = "glow"; s.shape = LfoShape::Saw; s.depth = 1.0f; s.bipolar = true;
    s.freeHz = true; s.hz = 2.0f;              // 2 Hz = un ciclo cada 0,5 s
    s.beatsPerCycle = 4.0f;                     // …y la división de tempo queda ignorada

    // La posición de beat NO cambia nada: el mismo segundo da el mismo valor con cualquier beat.
    REQUIRE (bank.valueFor (0, 0.0, 0.0) == Approx (bank.valueFor (0, 99.0, 0.0)).margin (1e-5));
    // Un ciclo entero cada 0,5 s.
    REQUIRE (bank.valueFor (0, 0.0, 0.0)  == Approx (-1.0f).margin (1e-4));   // saw bipolar en fase 0
    REQUIRE (bank.valueFor (0, 0.0, 0.25) == Approx ( 0.0f).margin (1e-4));   // medio ciclo
    REQUIRE (bank.valueFor (0, 0.0, 0.5)  == Approx (-1.0f).margin (1e-4));   // vuelta a empezar

    // Y en sync (el modo de siempre) el tiempo no influye.
    s.freeHz = false;
    REQUIRE (bank.valueFor (0, 1.0, 0.0) == Approx (bank.valueFor (0, 1.0, 12.34)).margin (1e-5));
}

TEST_CASE ("lfo: la lista de RATE trae tresillo (×2/3) y puntillo (×1.5) de cada división, y Hz",
           "[supernova][lfo]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LfoBank bank;
    supernova::LfoPanel panel (bank);
    panel.setSize (1120, 640);

    auto* rate = control<juce::ComboBox> (panel, "lfo0.rate");
    REQUIRE (rate != nullptr);

    auto selectByText = [&] (const juce::String& want) -> bool
    {
        for (int k = 0; k < rate->getNumItems(); ++k)
            if (rate->getItemText (k) == want) { rate->setSelectedItemIndex (k, juce::sendNotificationSync); return true; }
        return false;
    };

    REQUIRE (selectByText (juce::String::fromUTF8 ("1/4" "\xc2\xb7" "T")));
    REQUIRE (bank.slot (0).beatsPerCycle == Approx (2.0f / 3.0f));   // tresillo de negra
    REQUIRE_FALSE (bank.slot (0).freeHz);

    REQUIRE (selectByText (juce::String::fromUTF8 ("1/4" "\xc2\xb7" "D")));
    REQUIRE (bank.slot (0).beatsPerCycle == Approx (1.5f));          // negra con puntillo

    REQUIRE (selectByText (juce::String::fromUTF8 ("1 bar" "\xc2\xb7" "T")));
    REQUIRE (bank.slot (0).beatsPerCycle == Approx (8.0f / 3.0f));

    // Hz: modo libre. El slider de frecuencia sólo aparece en este modo.
    auto* hz = control<juce::Slider> (panel, "lfo0.hz");
    REQUIRE (hz != nullptr);
    REQUIRE_FALSE (hz->isVisible());

    REQUIRE (selectByText ("Hz"));
    REQUIRE (bank.slot (0).freeHz);
    REQUIRE (hz->isVisible());

    hz->setValue (5.0, juce::sendNotificationSync);
    REQUIRE (bank.slot (0).hz == Approx (5.0f));

    // Round-trip: el modo libre y su frecuencia sobreviven a guardar/abrir.
    LfoBank back; back.deserialize (bank.serialize());
    REQUIRE (back.slot (0).freeHz);
    REQUIRE (back.slot (0).hz == Approx (5.0f));
}
