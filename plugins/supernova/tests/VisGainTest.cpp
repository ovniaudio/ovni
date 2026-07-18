// [supernova][visgain] — el fader IN de la TopBar (param visGain) es SENSIBILIDAD VISUAL: escala SOLO la
// mezcla mono que alimenta el análisis (FIFO → AnalysisThread). El audio del insert sale bit-exacto SIEMPRE
// (RNF1), incluso con visGain a ±24 dB — a diferencia del inGain del chasis (drive real), que por eso la UI
// de SUPERNOVA no expone. Espejo del patrón de AnalysisChainTest: processor real + thread real + consumidor.
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstring>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
// Bombea `seconds` de seno 220 Hz (amplitud 0.25) por el processor real y devuelve el rms MÁXIMO que vio
// el consumidor (el contrato de AnalysisFrame.rms es CRUDO: sigue la amplitud, sin AGC).
float pumpAndMaxRms (float visGainDb, double seconds = 2.0)
{
    supernova::SupernovaProcessor proc;
    const double sr = 48000.0; const int blk = 512;
    proc.prepareToPlay (sr, blk);

    auto* p = proc.apvts.getParameter (supernova::params::id::VIS_GAIN);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (p->convertTo0to1 (visGainDb));

    juce::AudioBuffer<float> buf (2, blk);
    juce::MidiBuffer midi;
    const int totalBlocks = (int) (seconds * sr / blk);
    float maxRms = 0.0f;

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int i = 0; i < blk; ++i)
        {
            const double t = ((double) b * blk + i) / sr;
            const float s = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t);
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }
        proc.processBlock (buf, midi);
        const auto f = proc.analysis().read();
        if (f.timeSeconds > 0.2)
            maxRms = juce::jmax (maxRms, f.rms);
        std::this_thread::sleep_for (std::chrono::microseconds (2500));   // deja correr al AnalysisThread
    }
    return maxRms;
}
}

TEST_CASE ("visgain: escala el ANÁLISIS (sensibilidad visual del fader IN)", "[supernova][visgain]")
{
    const float atZero  = pumpAndMaxRms (0.0f);
    const float atMinus = pumpAndMaxRms (-12.0f);

    INFO ("rms máx a 0 dB = " << atZero << " · a -12 dB = " << atMinus);
    REQUIRE (atZero > 0.05f);                       // la señal se ve
    REQUIRE (atMinus > 0.0f);
    const float ratio = atZero / juce::jmax (1.0e-6f, atMinus);
    CHECK (ratio > 2.0f);                           // -12 dB ≈ ×0.25 en amplitud (tolerancia amplia: timing real)
    CHECK (ratio < 8.0f);
}

TEST_CASE ("visgain: el audio del insert sigue BIT-EXACTO con visGain al mango (RNF1)", "[supernova][visgain]")
{
    supernova::SupernovaProcessor proc;
    const double sr = 48000.0; const int blk = 512;
    proc.prepareToPlay (sr, blk);

    auto* p = proc.apvts.getParameter (supernova::params::id::VIS_GAIN);
    REQUIRE (p != nullptr);
    p->setValueNotifyingHost (1.0f);                // +24 dB (tope del rango)

    juce::AudioBuffer<float> buf (2, blk), ref (2, blk);
    juce::MidiBuffer midi;
    juce::Random rng (4242);

    for (int b = 0; b < 8; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < blk; ++i)
                buf.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);
        ref.makeCopyOf (buf);

        proc.processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (std::memcmp (buf.getReadPointer (ch), ref.getReadPointer (ch),
                                  sizeof (float) * (size_t) blk) == 0);
    }
}
