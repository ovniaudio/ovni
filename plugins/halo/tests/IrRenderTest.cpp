// Render de diagnóstico acústico de HALO a WAV para análisis externo (Python):
// (a) IR de la base FDN con SHIMMER=0 (densidad de eco + T60 base), (b) seno 440 Hz sostenido con
// SHIMMER=100 (escalera de octavas: afinación del pitch en el lazo + ascenso del centroide),
// (c) cola con ORBIT=100 (simetría L/R de la órbita). Tag [irrender]: se invoca a mano, no en CI.
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

void setNorm (halo::HaloProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}

void writeWav (const char* path, const juce::AudioBuffer<float>& buf)
{
    juce::File f (path); f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (os.release(), SR, (unsigned) buf.getNumChannels(), 24, {}, 0));
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

// Corre el processor con los macros dados: 'sineSecs' de seno 440 Hz (0 = impulso) y luego cola en
// silencio hasta 'totalSecs'. Escribe WAV estéreo y devuelve el peak.
float render (const char* path, float shimmer, float decay, float orbit, double sineSecs, double totalSecs)
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    setNorm (proc, pid::MIX, 1.0f); setNorm (proc, pid::SIZE, 0.5f); setNorm (proc, pid::DECAY, decay);
    setNorm (proc, pid::SHIMMER, shimmer); setNorm (proc, pid::TONE, 0.5f);
    setNorm (proc, pid::ORBIT, orbit); setNorm (proc, pid::ORBITSYNC, 0.0f); setNorm (proc, pid::ORBITRATE, 0.5f);
    proc.prepareToPlay (SR, N);

    const int numBlocks = (int) std::ceil (totalSecs * SR / N);
    const long sineSamps = (long) (sineSecs * SR);
    juce::AudioBuffer<float> out (2, numBlocks * N);
    out.clear();

    float peak = 0.0f; long g = 0;
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi; buf.clear();
        if (sineSamps <= 0)
        {
            if (blk == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
        }
        else
        {
            for (int i = 0; i < N; ++i, ++g)
                if (g < sineSamps)
                {
                    const float x = 0.35f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) g / SR);
                    buf.setSample (0, i, x); buf.setSample (1, i, x);
                }
        }
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, blk * N, buf, ch, 0, N);
            auto* d = buf.getReadPointer (ch);
            for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        }
    }
    writeWav (path, out);
    return peak;
}
} // namespace


// Forense del NULL a MIX=0: ruido blanco determinístico por el processor con mix=0; escribe INPUT y
// OUTPUT para analizar el residuo afuera (¿delay oculto? ¿filtro? ¿no-linealidad?). Tag [nullforense].
TEST_CASE ("HALO nullforense: input+output a MIX=0 -> WAV", "[nullforense][halo]")
{
    namespace pid = halo::params::id;
    halo::HaloProcessor proc;
    setNorm (proc, pid::MIX, 0.0f); setNorm (proc, pid::FREEZE, 0.0f);
    proc.prepareToPlay (SR, N);
    std::uint32_t seed = 0x2545F491u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return ((float) (seed >> 9) * (1.0f / 4194304.0f)) - 1.0f; };
    const int numBlocks = (int) std::ceil (4.0 * SR / N);
    juce::AudioBuffer<float> inRec (2, numBlocks * N), outRec (2, numBlocks * N);
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int i = 0; i < N; ++i) { const float x = 0.25f * rnd(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        for (int ch = 0; ch < 2; ++ch) inRec.copyFrom (ch, blk * N, buf, ch, 0, N);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) outRec.copyFrom (ch, blk * N, buf, ch, 0, N);
    }
    writeWav ("/tmp/ovni_halo_null_in.wav", inRec);
    writeWav ("/tmp/ovni_halo_null_out.wav", outRec);
    std::printf ("NULLFORENSE[halo] escrito in/out\n");
    REQUIRE (true);
}

TEST_CASE ("HALO irrender: base FDN / escalera de octavas / orbita -> WAV", "[irrender][halo]")
{
    struct Job { const char* path; float shim, decay, orbit; double sine, total; };
    const Job jobs[] = {
        { "/tmp/ovni_halo_ir_base.wav",    0.0f, 0.5f, 0.0f, 0.0, 6.0 },   // FDN base: densidad + T60
        { "/tmp/ovni_halo_sine_shim.wav",  1.0f, 0.7f, 0.0f, 2.0, 10.0 },  // escalera de octavas
        { "/tmp/ovni_halo_orbit.wav",      0.5f, 0.7f, 1.0f, 2.0, 12.0 },  // simetría de la órbita
    };
    for (const auto& j : jobs)
    {
        const float peak = render (j.path, j.shim, j.decay, j.orbit, j.sine, j.total);
        std::printf ("IRRENDER[halo] %s  shim=%.2f decay=%.2f orbit=%.2f peak=%.4f\n",
                     j.path, j.shim, j.decay, j.orbit, peak);
        REQUIRE (std::isfinite (peak));
        REQUIRE (peak > 1.0e-5f);
        REQUIRE (peak <= 1.0f);
    }
}
