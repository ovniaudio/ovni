#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [render][dust] — WAV para el A/B de oído de Joaquín (único gate humano).
// Usa el BEAT REAL (tests/assets/realbeat_90.wav) — la cama con la que se cazó el
// "encajonado" — procesado por el DustProcessor REAL en el preset de firma "Burbujas"
// más presente, escrito a /tmp/ovni_dust_render_{dry,wet}.wav. Si el asset no está,
// cae a una cama sintética de banda ancha. El REQUIRE verifica que el wet salió con
// energía (no silencio).
// =============================================================================
#ifndef OVNI_DUST_ASSET_DIR
    #define OVNI_DUST_ASSET_DIR "."
#endif
namespace {
// Devuelve true si cargó el beat real (estéreo) a 'buf' a SR; deja el largo en samples del archivo.
bool loadRealBeat (juce::AudioBuffer<float>& buf, double& srOut)
{
    juce::File f (juce::String (OVNI_DUST_ASSET_DIR) + "/realbeat_90.wav");
    if (! f.existsAsFile())
        f = juce::File (OVNI_DUST_ASSET_DIR).getChildFile ("realbeat_90.wav");
    if (! f.existsAsFile()) return false;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return false;
    srOut = rd->sampleRate;
    const int total = (int) rd->lengthInSamples;
    buf.setSize (2, total); buf.clear();
    rd->read (&buf, 0, total, 0, true, true);
    if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, total);
    return true;
}
void fillBed (juce::AudioBuffer<float>& buf, double SR)
{
    const int n = buf.getNumSamples();
    uint32_t rng = 0x1234567u;
    auto noise = [&] { rng = rng * 1664525u + 1013904223u; return (float) ((int32_t) rng) / 2.147483648e9f; };
    float pink = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / SR;
        const double vib = 1.0 + 0.004 * std::sin (juce::MathConstants<double>::twoPi * 5.0 * t);
        double s = 0.0;
        for (double f : { 220.0, 261.63, 329.63 })
            s += std::sin (juce::MathConstants<double>::twoPi * f * vib * t);
        s *= 0.18;
        pink = 0.98f * pink + 0.02f * noise();
        s += 0.06 * pink;
        const double ph = std::fmod (t, 0.5);
        const double env = ph < 0.002 ? ph / 0.002 : std::exp (-(ph - 0.002) / 0.08);
        s += 0.22 * env * std::sin (juce::MathConstants<double>::twoPi * 660.0 * t);
        double g = 1.0; const double fade = 0.05;
        if (t < fade) g = t / fade;
        else if (t > (double) n / SR - fade) g = ((double) n / SR - t) / fade;
        const float v = (float) (s * g * 0.8);
        buf.setSample (0, i, v);
        buf.setSample (1, i, v);
    }
}

void writeWav (const juce::String& path, const juce::AudioBuffer<float>& buf, double SR)
{
    juce::File f (path);
    f.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    REQUIRE (os != nullptr);
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (os.release(), SR, (unsigned) buf.getNumChannels(), 24, {}, 0));
    REQUIRE (w != nullptr);
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}
} // namespace

TEST_CASE ("render DUST: cama musical -> WAV para A/B", "[render][dust]")
{
    double SR = 48000.0; const int N = 512;
    namespace pid = dust::params::id;

    // BEAT REAL primero (la cama con la que se cazó el "encajonado"); fallback a la sintética.
    juce::AudioBuffer<float> dry;
    if (loadRealBeat (dry, SR))
        std::printf ("RENDER[dust] cama = BEAT REAL (realbeat_90.wav, %d samp @ %.0f)\n",
                     dry.getNumSamples(), SR);
    else
    {
        SR = 48000.0;
        dry.setSize (2, (int) (SR * 6.0));
        fillBed (dry, SR);
        std::printf ("RENDER[dust] cama = sintetica (asset no encontrado)\n");
    }
    const int total = dry.getNumSamples();
    writeWav ("/tmp/ovni_dust_render_dry.wav", dry, SR);

    dust::DustProcessor proc;
    auto set01 = [&] (const char* id, float v01) {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
    };
    auto setVal = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (v));
    };
    // Preset de firma "Burbujas" más presente para que el efecto se oiga en el render.
    setVal (pid::RATE,    260.0f);  // ~260 ms entre ecos
    setVal (pid::DENSITY, 50.0f);
    setVal (pid::SPREAD,  75.0f);
    setVal (pid::VIDA,    40.0f);
    setVal (pid::DUCK,    30.0f);
    setVal (pid::MIX,     45.0f);   // por encima del default 35 para destacar las burbujas

    proc.prepareToPlay (SR, N);

    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (dry);
    for (int off = 0; off < total; off += N)
    {
        const int len = juce::jmin (N, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi;
        proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
    }
    writeWav ("/tmp/ovni_dust_render_wet.wav", wet, SR);

    const float wetMag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    std::printf ("RENDER[dust] wet_peak=%.4f -> /tmp/ovni_dust_render_wet.wav\n", wetMag);
    REQUIRE (std::isfinite (wetMag));
    REQUIRE (wetMag > 0.01f);
}
