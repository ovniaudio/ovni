// Render de IMPULSE RESPONSES crudas de NÉBULA a WAV para análisis acústico externo (Python):
// T60 por banda (Schroeder EDC), T60(ω) vs TONE, tracking del knob DECAY vs la fórmula del motor,
// flutter/metal de la cola y pendiente del FREEZE. Es instrumentación de diagnóstico (tag [irrender]),
// NO corre en la suite por defecto de CI: se invoca a mano. Escribe a /tmp/ovni_nebula_ir_*.wav.
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "engines/fdn/FdnReverb.h"   // t60ForDecay (fórmula pública del motor)

namespace
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

void setNorm (nebula::NebulaProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}

// Impulso por el processor REAL con los macros dados; captura 'seconds' de cola (mix=100%, breath=0)
// y escribe el WAV estéreo 24-bit. Devuelve el peak por si hace falta sanity-check.
float renderIr (const char* path, float decay01, float tone01, double seconds, double sr = SR)
{
    nebula::NebulaProcessor proc;
    setNorm (proc, "size", 0.5f); setNorm (proc, "decay", decay01); setNorm (proc, "tone", tone01);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);
    proc.prepareToPlay (sr, N);

    const int numBlocks = (int) std::ceil (seconds * sr / N);
    juce::AudioBuffer<float> out (2, numBlocks * N);
    out.clear();

    float peak = 0.0f;
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi; buf.clear();
        if (blk == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }   // el impulso
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, blk * N, buf, ch, 0, N);
            auto* d = buf.getReadPointer (ch);
            for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        }
    }

    juce::File f (path);
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (os.release(), sr, (unsigned) out.getNumChannels(), 24, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
    return peak;
}
} // namespace

TEST_CASE ("NEBULA irrender: IRs crudas a WAV para analisis acustico externo", "[irrender][nebula]")
{
    // Targets de la fórmula pública del motor (los imprime para que el análisis externo compare).
    std::printf ("IRRENDER[targets] t60(0.25)=%.3f  t60(0.50)=%.3f  t60(0.75)=%.3f s\n",
                 ovni::engines::FdnReverb::t60ForDecay (0.25f),
                 ovni::engines::FdnReverb::t60ForDecay (0.50f),
                 ovni::engines::FdnReverb::t60ForDecay (0.75f));

    struct Job { const char* path; float decay, tone; double secs; };
    const Job jobs[] = {
        { "/tmp/ovni_nebula_ir_tone00.wav",  0.50f, 0.00f,  6.0 },   // T60 por banda, damping mínimo
        { "/tmp/ovni_nebula_ir_tone80.wav",  0.50f, 0.80f,  6.0 },   // T60(ω): TONE alto oscurece
        { "/tmp/ovni_nebula_ir_decay25.wav", 0.25f, 0.20f,  4.0 },   // tracking del knob (corto)
        { "/tmp/ovni_nebula_ir_decay75.wav", 0.75f, 0.20f, 10.0 },   // tracking del knob (largo)
        { "/tmp/ovni_nebula_ir_freeze.wav",  1.00f, 0.20f, 10.0 },   // pendiente del freeze
        { "/tmp/ovni_nebula_ir_frz_t0.wav",  1.00f, 0.00f, 10.0 },   // freeze con TONE 0 (¿la absorción actúa?)
        { "/tmp/ovni_nebula_ir_frz_t1.wav",  1.00f, 1.00f, 10.0 },   // freeze con TONE 1 (si difiere → absorción ACTIVA)
    };
    for (const auto& j : jobs)
    {
        const float peak = renderIr (j.path, j.decay, j.tone, j.secs);
        std::printf ("IRRENDER[wav] %s  decay=%.2f tone=%.2f secs=%.1f peak=%.4f\n",
                     j.path, j.decay, j.tone, j.secs, peak);
        REQUIRE (std::isfinite (peak));
        REQUIRE (peak > 1.0e-5f);   // hay cola
        REQUIRE (peak <= 1.0f);     // no clippea
    }
}

// Verificación 96 kHz de los fixes 2026-07-02 (difusor de entrada + freeze snap-a-entero): los primos
// del difusor y el snap escalan con el SR por construcción; acá se RINDE la prueba (densidad/T60/freeze
// medidos afuera). Tag [irrender96]: a mano, no en CI.
TEST_CASE ("NEBULA irrender96: IRs a 96 kHz (difusor + freeze)", "[irrender96][nebula]")
{
    struct Job { const char* path; float decay, tone; double secs; };
    const Job jobs[] = {
        { "/tmp/ovni_nebula_ir96_tone00.wav", 0.50f, 0.00f,  6.0 },
        { "/tmp/ovni_nebula_ir96_freeze.wav", 1.00f, 0.20f, 10.0 },
    };
    for (const auto& j : jobs)
    {
        const float peak = renderIr (j.path, j.decay, j.tone, j.secs, 96000.0);
        std::printf ("IRRENDER96[wav] %s peak=%.4f\n", j.path, peak);
        REQUIRE (std::isfinite (peak));
        REQUIRE (peak > 1.0e-5f);
        REQUIRE (peak <= 1.0f);
    }
}
