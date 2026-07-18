// [supernova][chain] — evidencia end-to-end de la REACTIVIDAD con "música" (no señales de laboratorio):
// processor real + AnalysisThread real (timing real) + un consumidor que lee el TripleBuffer a ~60Hz como el
// render. Música sintética: kick (seno 55Hz con pitch-drop y decay) cada 0.5s + hats + pad. Mide cuántos de
// los kicks LLEGAN al consumidor como onset y qué rango dinámico tiene el bass. Reproduce el reporte de campo
// de Joaquín: "no late con el ritmo".
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include "PluginProcessor.h"

namespace
{
// Un loop de "música" determinista: kick cada medio segundo + hat en corcheas + pad grave continuo.
void fillMusic (juce::AudioBuffer<float>& buf, long startSample, double sr, float gain = 1.0f);

void fillMusic (juce::AudioBuffer<float>& buf, long startSample, double sr, float gain)
{
    const int n = buf.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const long   s = startSample + i;
        const double t = (double) s / sr;

        // kick: cada 0.5s, seno 90→45Hz con decay exponencial (~120ms), fuerte
        const double tk = std::fmod (t, 0.5);
        const double kickEnv = std::exp (-tk * 18.0);
        const double kickF   = 45.0 + 45.0 * std::exp (-tk * 30.0);
        const float  kick    = (float) (0.9 * kickEnv * std::sin (2.0 * juce::MathConstants<double>::pi * kickF * tk));

        // hat: cada 0.25s, ruido corto
        const double th = std::fmod (t, 0.25);
        const float  hat = (th < 0.02) ? 0.25f * ((float) std::sin (s * 12.9898) * 43758.5453f
                                                  - std::floor ((float) std::sin (s * 12.9898) * 43758.5453f) - 0.5f)
                                       : 0.0f;

        // pad: seno 220Hz suave continuo
        const float pad = 0.12f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t);

        const float sample = gain * juce::jlimit (-1.0f, 1.0f, kick + hat + pad);
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            buf.setSample (ch, i, sample);
    }
}

// Corre 6s de música por el processor real (thread real) con un consumidor estilo render (60Hz-ish).
struct ChainResult { int onsets = 0; float bassMax = 0, bassMin = 1, rmsMax = 0; };
ChainResult runChain (float gain)
{
    supernova::SupernovaProcessor proc;
    const double sr = 48000.0; const int blk = 512;
    proc.prepareToPlay (sr, blk);

    const int totalBlocks = (int) (6.0 * sr / blk);
    juce::AudioBuffer<float> buf (2, blk);
    juce::MidiBuffer midi;
    ChainResult r; bool lastOnset = false;

    for (int b = 0; b < totalBlocks; ++b)
    {
        fillMusic (buf, (long) b * blk, sr, gain);
        proc.processBlock (buf, midi);
        const auto f = proc.analysis().read();
        if (f.timeSeconds > 0.2)
        {
            if (f.onset && ! lastOnset) ++r.onsets;
            lastOnset = f.onset;
            r.bassMax = juce::jmax (r.bassMax, f.bass);
            r.bassMin = juce::jmin (r.bassMin, f.bass);
            r.rmsMax  = juce::jmax (r.rmsMax, f.rms);
        }
        std::this_thread::sleep_for (std::chrono::microseconds (2500));
    }
    return r;
}
}

TEST_CASE ("chain: los kicks llegan como onsets al consumidor (render) y el bass tiene dinámica",
           "[supernova][chain]")
{
    const auto r = runChain (1.0f);
    std::printf ("CHAIN full: onsets=%d (de ~11 kicks) bass=[%.3f..%.3f] rmsMax=%.3f\n",
                 r.onsets, r.bassMin, r.bassMax, r.rmsMax);
    REQUIRE (r.onsets >= 6);                      // ≥ la mitad de los kicks llegan
    REQUIRE (r.bassMax > 0.35f);                  // el kick enciende la banda grave
    REQUIRE (r.bassMax - r.bassMin > 0.25f);      // y PULSA (dinámica, no continuo)
}

TEST_CASE ("chain: música a −13dB también late (independencia de nivel — reporte de campo)",
           "[supernova][chain]")
{
    // Nadie reproduce a 0dBFS: el track de Joaquín suena a nivel de mezcla normal. La reactividad NO puede
    // depender del fader (gate absoluto / sumas crudas): a −13dB los kicks tienen que seguir llegando y el
    // bass tiene que seguir pulsando con rango comparable.
    const auto r = runChain (0.22f);
    std::printf ("CHAIN quiet: onsets=%d (de ~11 kicks) bass=[%.3f..%.3f] rmsMax=%.3f\n",
                 r.onsets, r.bassMin, r.bassMax, r.rmsMax);
    REQUIRE (r.onsets >= 6);
    REQUIRE (r.bassMax > 0.35f);
    REQUIRE (r.bassMax - r.bassMin > 0.25f);
}
