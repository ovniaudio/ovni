// [supernova][preset] — PresetMorph (ease A→B, RF6) + PresetApplier / round-trip (el APVTS converge al destino)
// + VARIATION (randomización curada determinista).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cstring>
#include "presets/PresetMorph.h"
#include "presets/PresetTarget.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "params/ParamMapping.h"

using supernova::PresetMorph;
using supernova::MorphSnapshot;
namespace pid = supernova::params::id;

namespace
{
MorphSnapshot snap (float a, float b, float c, float d)   // sólo los 4 primeros para tests de ease
{
    MorphSnapshot s; s.v[0] = a; s.v[1] = b; s.v[2] = c; s.v[3] = d; return s;
}
void pump()   // drena el AsyncUpdater del PresetApplier (message thread)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
}
float rawv (supernova::SupernovaProcessor& p, const char* id)
{
    return p.apvts.getRawParameterValue (id)->load();
}
}

// ---- Grupo A: PresetMorph (puro) ----

TEST_CASE ("preset: el morph llega al destino dentro de su duración", "[supernova][preset]")
{
    PresetMorph m;
    m.syncTo (snap (0, 0, 0, 0));
    m.setTarget (snap (100, 80, 60, 40), 0.5);
    MorphSnapshot s;
    for (int i = 0; i < 40; ++i) s = m.tick (1.0 / 60.0);   // ~0.66 s > 0.5
    REQUIRE (s.v[0] == Catch::Approx (100.0f).margin (0.01));
    REQUIRE (s.v[1] == Catch::Approx (80.0f).margin (0.01));
    REQUIRE_FALSE (m.active());
}

TEST_CASE ("preset: el morph arranca cerca del origen, no del destino (sin corte seco)", "[supernova][preset]")
{
    PresetMorph m;
    m.syncTo (snap (0, 0, 0, 0));
    m.setTarget (snap (100, 100, 100, 100), 0.5);
    auto s = m.tick (0.016);          // primer paso chico
    REQUIRE (s.v[0] < 20.0f);         // cerca de 0, lejos de 100 → es un ease, no un salto
    REQUIRE (s.v[0] > 0.0f);
}

TEST_CASE ("preset: los valores intermedios quedan acotados (sin overshoot)", "[supernova][preset]")
{
    PresetMorph m;
    m.syncTo (snap (20, 20, 20, 20));
    m.setTarget (snap (80, 80, 80, 80), 0.3);
    for (int i = 0; i < 25; ++i)
    {
        auto s = m.tick (1.0 / 60.0);
        REQUIRE (s.v[0] >= 20.0f - 1e-3f);
        REQUIRE (s.v[0] <= 80.0f + 1e-3f);
    }
}

TEST_CASE ("preset: re-armar a mitad arranca desde la salida actual", "[supernova][preset]")
{
    PresetMorph m;
    m.syncTo (snap (0, 0, 0, 0));
    m.setTarget (snap (100, 0, 0, 0), 0.5);
    MorphSnapshot mid;
    for (int i = 0; i < 15; ++i) mid = m.tick (1.0 / 60.0);   // a mitad de camino
    const float midV = mid.v[0];
    REQUIRE (midV > 5.0f);
    REQUIRE (midV < 95.0f);
    m.setTarget (snap (0, 0, 0, 0), 0.5);                     // re-arma hacia atrás
    auto next = m.tick (1.0 / 60.0);
    REQUIRE (next.v[0] < midV);                               // baja desde el valor de mitad (sin jump)
    REQUIRE (next.v[0] > midV - 20.0f);
}

TEST_CASE ("preset: idle pasa de largo los knobs en vivo; dt gigante no da NaN", "[supernova][preset]")
{
    PresetMorph m;
    m.syncTo (snap (33, 44, 55, 66));
    REQUIRE_FALSE (m.active());
    auto s = m.tick (1.0 / 60.0);
    REQUIRE (s.v[0] == Catch::Approx (33.0f));

    m.setTarget (snap (10, 20, 30, 40), 0.5);
    auto big = m.tick (10.0);                                 // dt enorme → completa exacto, sin NaN
    REQUIRE (big.v[0] == Catch::Approx (10.0f).margin (0.01));
    REQUIRE (std::isfinite (big.v[3]));
}

// ---- Grupo B: PresetApplier + round-trip (processor real) ----

TEST_CASE ("preset: cambiar el param 'preset' converge los continuos del APVTS al destino", "[supernova][preset]")
{
    supernova::SupernovaProcessor proc;
    const int idx = 10;   // Colapso (int80 cha65 siz28 glw40 curl70 home40 grav20 mom60 blast55 shim60 brth40)
    auto* pp = proc.apvts.getParameter (pid::PRESET);
    pp->setValueNotifyingHost (proc.apvts.getParameterRange (pid::PRESET).convertTo0to1 ((float) idx));
    pump();

    REQUIRE (rawv (proc, pid::INTENSITY)  == Catch::Approx (80.0f).margin (0.1));
    REQUIRE (rawv (proc, pid::CHAOS)      == Catch::Approx (65.0f).margin (0.1));
    REQUIRE (rawv (proc, pid::CURL_SCALE) == Catch::Approx (70.0f).margin (0.1));
    REQUIRE (rawv (proc, pid::GRAVITY)    == Catch::Approx (20.0f).margin (0.1));
    // el propio 'preset' NO fue reseteado a 0 por el applier
    REQUIRE ((int) rawv (proc, pid::PRESET) == idx);
    // params no-continuos intactos
    REQUIRE (rawv (proc, pid::EXPLODE) == Catch::Approx (0.0f));
}

TEST_CASE ("preset: el applier no toca params ajenos (bypass)", "[supernova][preset]")
{
    supernova::SupernovaProcessor proc;
    proc.apvts.getParameter (pid::BYPASS)->setValueNotifyingHost (1.0f);
    proc.apvts.getParameter (pid::PRESET)->setValueNotifyingHost (
        proc.apvts.getParameterRange (pid::PRESET).convertTo0to1 (2.0f));   // Órbita
    pump();
    REQUIRE (rawv (proc, pid::BYPASS) >= 0.5f);   // sigue activo (no lo tocó el applier)
}

TEST_CASE ("preset: estado guardado a mitad de morph restaura el DESTINO", "[supernova][preset]")
{
    supernova::SupernovaProcessor a;
    a.apvts.getParameter (pid::PRESET)->setValueNotifyingHost (
        a.apvts.getParameterRange (pid::PRESET).convertTo0to1 (1.0f));   // Nebulosa
    pump();
    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    supernova::SupernovaProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    pump();
    REQUIRE (rawv (b, pid::INTENSITY) == Catch::Approx (30.0f).margin (0.1));   // Nebulosa int=30
    REQUIRE (rawv (b, pid::GRAVITY)   == Catch::Approx (-8.0f).margin (0.1));   // Nebulosa grav=-8
}

// ---- Grupo B2: la TABLA de fábrica es sana (30 mundos; un typo acá = un preset roto en silencio) ----

TEST_CASE ("preset: la tabla de fábrica referencia params reales, en rango y sin duplicados", "[supernova][preset]")
{
    supernova::SupernovaProcessor proc;
    const auto& presets = ovni::presets::factoryPresets();
    REQUIRE (presets.size() >= 28);                            // el sistema PRO: ~30 mundos

    juce::StringArray names;
    for (const auto& fp : presets)
    {
        const juce::String name = juce::String::fromUTF8 (fp.name);
        INFO ("preset: " << name);
        REQUIRE_FALSE (names.contains (name));                 // nombres únicos
        names.add (name);

        for (const auto& pv : fp.params)
        {
            INFO ("preset '" << name << "' param '" << pv.id << "' = " << pv.value);
            auto* param = proc.apvts.getParameter (pv.id);
            REQUIRE (param != nullptr);                        // id existe en el APVTS (sin typos)
            const auto range = proc.apvts.getParameterRange (pv.id);
            REQUIRE (pv.value >= range.start);
            REQUIRE (pv.value <= range.end);
        }
    }
}

TEST_CASE ("preset: los choices (motion/shape) del preset saltan al APVTS al elegirlo", "[supernova][preset]")
{
    supernova::SupernovaProcessor proc;
    // Púlsar (idx 3) declara motion=4 (Radial); Constelación (idx 8) declara shape=2 (Ring) + links 55.
    auto* pp = proc.apvts.getParameter (pid::PRESET);
    pp->setValueNotifyingHost (proc.apvts.getParameterRange (pid::PRESET).convertTo0to1 (3.0f));
    pump();
    REQUIRE ((int) rawv (proc, pid::MOTION) == 4);

    pp->setValueNotifyingHost (proc.apvts.getParameterRange (pid::PRESET).convertTo0to1 (8.0f));
    pump();
    REQUIRE ((int) rawv (proc, pid::SHAPE) == 2);
    REQUIRE (rawv (proc, pid::LINKS) == Catch::Approx (55.0f).margin (0.1));
    REQUIRE ((int) rawv (proc, pid::MOTION) == 0);             // Constelación no declara motion → default
}

// ---- Grupo C: VARIATION (motor de "miles de variaciones", puro) ----

namespace
{
// Los campos que applyVariation puede tocar, como clave comparable (memcmp del struct entero compararía
// bytes de PADDING no inicializados → falso negativo).
std::array<float, 15> varKey (const supernova::ParticleParams& p)
{
    return { p.curlScale, p.homeStrength, p.gravity, p.momentum, p.radialGain, p.jitterGain, p.breatheGain,
             p.chaos, p.intensity, p.particleSize, p.glow, p.trailAmt, p.linksAmt, p.hueShift, p.satAmt };
}
}

TEST_CASE ("variation: 0 = preset puro (identidad exacta)", "[supernova][preset][variation]")
{
    supernova::ParticleParams a;
    a.curlScale = 1.7f; a.trailAmt = 0.4f;
    supernova::ParticleParams b = a;
    supernova::applyVariation (b, 3, 0.0f);
    REQUIRE (varKey (a) == varKey (b));
}

TEST_CASE ("variation: determinista y distinta por semilla/valor", "[supernova][preset][variation]")
{
    supernova::ParticleParams base;
    base.trailAmt = 0.3f; base.linksAmt = 0.5f;

    auto v = [&] (int seed, float amt)
    {
        supernova::ParticleParams p = base;
        supernova::applyVariation (p, seed, amt);
        return varKey (p);
    };

    REQUIRE (v (2, 0.37f) == v (2, 0.37f));   // mismo (semilla, valor) → mismo mundo
    REQUIRE (v (2, 0.37f) != v (2, 0.62f));   // otro valor → otro mundo
    REQUIRE (v (2, 0.37f) != v (9, 0.37f));   // otra semilla → otro mundo
}

TEST_CASE ("variation: siempre dentro de los rangos automatizables; 0 relativo queda 0", "[supernova][preset][variation]")
{
    for (int seed = 0; seed < 24; ++seed)
        for (float amt : { 0.1f, 0.33f, 0.5f, 0.77f, 1.0f })
        {
            supernova::ParticleParams p;                                 // defaults (trails/links = 0)
            supernova::applyVariation (p, seed, amt);
            REQUIRE (p.curlScale    >= 0.30f); REQUIRE (p.curlScale    <= 2.70f);
            REQUIRE (p.homeStrength >= 0.20f); REQUIRE (p.homeStrength <= 2.60f);
            REQUIRE (p.momentum     >= 0.80f); REQUIRE (p.momentum     <= 0.99f);
            REQUIRE (p.gravity      >= -0.60f); REQUIRE (p.gravity     <= 0.60f);
            REQUIRE (p.radialGain   >= 0.20f); REQUIRE (p.radialGain   <= 2.20f);
            REQUIRE (p.satAmt       >= 0.00f); REQUIRE (p.satAmt       <= 2.00f);
            REQUIRE (p.trailAmt == 0.0f);      // relativo: un mundo sin estelas NUNCA las inventa
            REQUIRE (p.linksAmt == 0.0f);      // ídem conexiones (identidad del preset intacta)
            // Tier PRO: rangos automatizables + relativos (un mundo quieto no gira/deriva/dispersa solo).
            REQUIRE (p.densityAmt   >= 0.05f); REQUIRE (p.densityAmt <= 1.00f);
            REQUIRE (p.speedMul     >= 0.25f); REQUIRE (p.speedMul   <= 4.00f);
            REQUIRE (p.pumpAmt      >= 0.00f); REQUIRE (p.pumpAmt    <= 2.00f);
            REQUIRE (p.scatterAmt   == 0.0f);
            REQUIRE (p.rotateRate   == 0.0f);
            REQUIRE (p.hueCycleRate == 0.0f);
        }
}

TEST_CASE ("preset: el restore no re-clobberea un tweak del usuario (bracket suspend)", "[supernova][preset]")
{
    supernova::SupernovaProcessor a;
    a.apvts.getParameter (pid::PRESET)->setValueNotifyingHost (
        a.apvts.getParameterRange (pid::PRESET).convertTo0to1 (10.0f));   // Colapso (int=80)
    pump();
    // tweak manual post-preset
    a.apvts.getParameter (pid::INTENSITY)->setValueNotifyingHost (
        a.apvts.getParameterRange (pid::INTENSITY).convertTo0to1 (12.0f));
    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    supernova::SupernovaProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    pump();
    REQUIRE (rawv (b, pid::INTENSITY) == Catch::Approx (12.0f).margin (0.1));   // NO re-aplicado a 80
    REQUIRE ((int) rawv (b, pid::PRESET) == 10);
}
