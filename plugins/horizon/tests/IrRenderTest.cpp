// Render de diagnóstico acústico de HORIZON a WAV para análisis externo (Python):
// fidelidad de AFINACIÓN del freeze — congela un seno de 440 Hz y deja sonar el frame congelado 6 s.
// Un freeze STFT con phase-lock correcto sostiene la frecuencia analizada sin deriva; uno roto deriva
// o gorjea. Tag [irrender]: se invoca a mano, no en CI.
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

void setNorm (horizon::HorizonProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}
} // namespace

TEST_CASE ("HORIZON irrender: freeze de un seno 440 -> WAV (fidelidad de afinacion)", "[irrender][horizon]")
{
    namespace pid = horizon::params::id;
    horizon::HorizonProcessor proc;
    setNorm (proc, pid::MIX, 1.0f);          // wet pleno: medimos el congelado
    setNorm (proc, pid::WHISPER, 0.0f);      // sin shimmer: frame puro
    setNorm (proc, pid::SPREAD, 0.0f);
    setNorm (proc, pid::RATE, 0.0f);         // sin gate: sustain continuo
    setNorm (proc, pid::DUCK, 0.0f);
    setNorm (proc, pid::FREEZE, 0.0f);
    proc.prepareToPlay (SR, N);

    const double sineSecs = 1.0, totalSecs = 8.0;
    const int numBlocks = (int) std::ceil (totalSecs * SR / N);
    const long sineSamps = (long) (sineSecs * SR);
    juce::AudioBuffer<float> out (2, numBlocks * N);
    out.clear();

    float peak = 0.0f; long g = 0; bool frozen = false;
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        // Congelar a los 0.7 s (con el seno todavía sonando, para capturar un frame lleno).
        if (! frozen && (double) g / SR >= 0.7) { setNorm (proc, pid::FREEZE, 1.0f); frozen = true; }
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi; buf.clear();
        for (int i = 0; i < N; ++i, ++g)
            if (g < sineSamps)
            {
                const float x = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, blk * N, buf, ch, 0, N);
            auto* d = buf.getReadPointer (ch);
            for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        }
    }

    juce::File f ("/tmp/ovni_horizon_freeze440.wav"); f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (os.release(), SR, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());

    std::printf ("IRRENDER[horizon] /tmp/ovni_horizon_freeze440.wav  peak=%.4f\n", peak);
    REQUIRE (std::isfinite (peak));
    REQUIRE (peak > 1.0e-5f);
    REQUIRE (peak <= 1.0f);
}
