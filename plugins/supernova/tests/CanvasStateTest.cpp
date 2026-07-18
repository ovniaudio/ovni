// [supernova][canvas] — CLEAR escribe el snapshot lienzo EXACTO y no toca preset/bypass. CPU puro.
// [supernova][hotknob] — los HOTKNOBS por dominio MUEVEN los params reales de su fila (y 0 restaura la base).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/CanvasState.h"
#include "params/ParameterIDs.h"

namespace pid = supernova::params::id;

namespace
{
void pumpUi()   // drena attachments/async del message thread
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
}
}

TEST_CASE ("canvas: applyClearState deja el lienzo exacto y respeta preset/bypass",
           "[supernova][canvas]")
{
    supernova::SupernovaProcessor proc;
    auto& apvts = proc.apvts;

    // Ensuciar el mundo (como un RANDOM): valores lejos del lienzo.
    auto setRaw = [&apvts] (const char* id, float v)
    {
        auto* p = apvts.getParameter (id);
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 (v));
    };
    setRaw (pid::INTENSITY, 92.0f);  setRaw (pid::CHAOS, 80.0f);   setRaw (pid::TRAILS, 70.0f);
    setRaw (pid::KALEIDO, 3.0f);     setRaw (pid::ROT_Y, -120.0f); setRaw (pid::PALETTE, 5.0f);
    setRaw (pid::PRESET, 12.0f);     setRaw (pid::VARIATION, 66.0f);

    supernova::canvas::applyClearState (apvts);

    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    for (const auto& pv : supernova::canvas::kClearState)
    {
        INFO ("param: " << pv.id);
        REQUIRE (raw (pv.id) == Catch::Approx (pv.value).margin (0.001));
    }

    REQUIRE (raw (pid::PRESET) == Catch::Approx (12.0f).margin (0.001));   // el mundo elegido QUEDA
}

TEST_CASE ("hotknob: mover el de MOVEMENT mueve SU fila, no las otras; 0 restaura la base",
           "[supernova][hotknob]")
{
    supernova::SupernovaProcessor proc;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (proc.createEditor());   // arma el ControlStrip
    REQUIRE (editor != nullptr);
    pumpUi();
    auto& apvts = proc.apvts;

    auto raw = [&apvts] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto setP = [&apvts] (const char* id, float v)
    {
        auto* p = apvts.getParameter (id);
        p->setValueNotifyingHost (apvts.getParameterRange (id).convertTo0to1 (v));
    };

    const float chaos0 = raw (pid::CHAOS);
    const float hue0   = raw (pid::HUE);

    setP (pid::VAR_MOVEMENT, 60.0f);   // attachment → slider → onValueChange → applyDomainTake
    pumpUi();
    const float chaosTake = raw (pid::CHAOS);
    REQUIRE (chaosTake != Catch::Approx (chaos0).margin (0.5));   // la fila MOVEMENT se movió DE VERDAD
    REQUIRE (raw (pid::HUE) == Catch::Approx (hue0).margin (0.001));   // COLOR intacto (otro dominio)

    // Determinista: mismo valor = misma toma (bajar y volver a 60 reproduce el mismo chaos).
    setP (pid::VAR_MOVEMENT, 25.0f);
    pumpUi();
    setP (pid::VAR_MOVEMENT, 60.0f);
    pumpUi();
    REQUIRE (raw (pid::CHAOS) == Catch::Approx (chaosTake).margin (0.5));

    setP (pid::VAR_MOVEMENT, 0.0f);    // 0 = volver a la base
    pumpUi();
    REQUIRE (raw (pid::CHAOS) == Catch::Approx (chaos0).margin (0.5));
}
