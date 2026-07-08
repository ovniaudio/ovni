// Render de diagnóstico acústico de PULSAR a WAV para análisis externo (Python):
// (a) seno ESTÁTICO (motion=0) → localizar dónde vive el piso de espurias (¿sidebands? ¿armónicos?),
// (b) seno con MOTION alto → física del Doppler (excursión de frecuencia instantánea, suavidad, simetría).
// Tag [irrender]: se invoca a mano, no en CI.
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"

namespace
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

void setNorm (pulsar::PulsarProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}

float render (const char* path, float motion, float rate, double secs)
{
    pulsar::PulsarProcessor proc;
    setNorm (proc, "mix", 1.0f); setNorm (proc, "motion", motion); setNorm (proc, "width", 0.5f);
    setNorm (proc, "smear", 0.0f); setNorm (proc, "rate", rate); setNorm (proc, "sync", 0.0f);
    proc.prepareToPlay (SR, N);

    const int numBlocks = (int) std::ceil (secs * SR / N);
    juce::AudioBuffer<float> out (2, numBlocks * N);
    out.clear();

    float peak = 0.0f; long g = 0;
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int i = 0; i < N; ++i, ++g)
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
    juce::File f (path); f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (os.release(), SR, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return peak;
}
} // namespace

TEST_CASE ("PULSAR irrender: seno estatico + seno con motion -> WAV", "[irrender][pulsar]")
{
    struct Job { const char* path; float motion, rate; double secs; };
    const Job jobs[] = {
        { "/tmp/ovni_pulsar_static.wav", 0.0f, 0.5f,  5.0 },
        { "/tmp/ovni_pulsar_motion.wav", 1.0f, 0.6f, 10.0 },
    };
    for (const auto& j : jobs)
    {
        const float peak = render (j.path, j.motion, j.rate, j.secs);
        std::printf ("IRRENDER[pulsar] %s  motion=%.2f peak=%.4f\n", j.path, j.motion, peak);
        REQUIRE (std::isfinite (peak));
        REQUIRE (peak > 1.0e-5f);
        REQUIRE (peak <= 1.0f);
    }
}
