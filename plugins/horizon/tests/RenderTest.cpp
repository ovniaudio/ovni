#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [render][horizon] — WAV corto para el A/B de oído de Joaquín (único gate humano).
// Usa el BEAT REAL (tests/assets/realbeat_90.wav) — el A/B que importa es sobre material
// real, no ruido/cama sintética (la lección del falso verde). Estructura del wet:
//   · 1ª mitad FREEZE OFF (default whisper12/spread50/mix100): el ESPACIALIZADOR vivo
//     ensancha el beat al cargar y tocar — esto es lo que faltaba.
//   · 2ª mitad FREEZE ON (gate FREE ~3 Hz): la textura congelada suspendida + ancha.
// Escribe /tmp/ovni_horizon_render_{dry,wet}.wav. El REQUIRE verifica energía (no silencio).
// =============================================================================
namespace {
// Carga el beat real a un buffer estéreo @SR (mismo path que RealBeatTest).
juce::AudioBuffer<float> loadBeat (double SR)
{
    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    juce::File wav (HORIZON_REALBEAT_WAV);
    REQUIRE (wav.existsAsFile());
    auto stream = wav.createInputStream();
    REQUIRE (stream != nullptr);
    std::unique_ptr<juce::AudioFormatReader> rd (
        fmt.createReaderFor (std::unique_ptr<juce::InputStream> (stream.release())));
    REQUIRE (rd != nullptr);
    REQUIRE (std::abs (rd->sampleRate - SR) < 1.0);
    const int total = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> buf (2, total);
    rd->read (&buf, 0, total, 0, true, true);
    return buf;
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

TEST_CASE ("render HORIZON: BEAT REAL -> WAV para A/B", "[render][horizon]")
{
    const double SR = 48000.0; const int N = 512;
    namespace pid = horizon::params::id;

    const juce::AudioBuffer<float> dry = loadBeat (SR);
    const int total = dry.getNumSamples();
    writeWav ("/tmp/ovni_horizon_render_dry.wav", dry, SR);

    horizon::HorizonProcessor proc;
    auto set01 = [&] (const char* id, float v01) {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
    };
    auto setVal = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (v));
    };
    // Default de firma: arranca con FREEZE OFF (el espacializador vivo ya ensancha el beat al
    // cargar). RATE FREE ~3 Hz para que el gate del freeze se oiga cuando se enganche.
    set01 (pid::RATESYNC, 0.0f);
    setVal (pid::RATE,    3.0f);
    setVal (pid::WHISPER, 12.0f);   // default de firma (vidrioso vivo)
    setVal (pid::SPREAD,  50.0f);   // default de firma
    setVal (pid::DUCK,    25.0f);
    setVal (pid::MIX,    100.0f);   // default de firma (wet pleno)

    proc.prepareToPlay (SR, N);

    // FREEZE OFF la 1ª mitad (espacializador vivo sobre el beat), FREEZE ON la 2ª (textura
    // suspendida). El A/B de Joaquín oye: (a) el beat ensanchado SIN congelar, (b) el freeze.
    const int freezeAtSample = total / 2;

    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (dry);
    bool freezeEngaged = false;
    for (int off = 0; off < total; off += N)
    {
        if (! freezeEngaged && off >= freezeAtSample)
        {
            set01 (pid::FREEZE, 1.0f);   // a mitad: suspender el instante
            freezeEngaged = true;
        }
        const int len = juce::jmin (N, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi;
        proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
    }
    writeWav ("/tmp/ovni_horizon_render_wet.wav", wet, SR);

    const float wetMag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    std::printf ("RENDER[horizon] BEAT REAL wet_peak=%.4f -> /tmp/ovni_horizon_render_wet.wav (1ª mitad FREEZE OFF vivo, 2ª FREEZE ON)\n", wetMag);
    REQUIRE (std::isfinite (wetMag));
    REQUIRE (wetMag > 0.01f);
}

static void writeMonoFold (const juce::String& path, const juce::AudioBuffer<float>& st, double SR)
{
    const int n = st.getNumSamples();
    juce::AudioBuffer<float> mono (1, n);
    for (int i = 0; i < n; ++i) mono.setSample (0, i, 0.5f * (st.getSample (0, i) + st.getSample (1, i)));
    writeWav (path, mono, SR);
}

TEST_CASE ("render HORIZON mono A/B: shimmer vivo + freeze, plegado a MONO (prueba del fix mono-audible)", "[render][horizon]")
{
    const double SR = 48000.0; const int N = 512;
    namespace pid = horizon::params::id;

    const juce::AudioBuffer<float> dry = loadBeat (SR);
    const int total = dry.getNumSamples();

    horizon::HorizonProcessor proc;
    auto set01 = [&] (const char* id, float v01) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01); };
    auto setVal = [&] (const char* id, float v) {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (proc.apvts.getParameterRange (id).convertTo0to1 (v)); };
    // Firma del fix: WHISPER arriba (wash difuso glassy mono-audible), spread 50, mix 100.
    set01 (pid::RATESYNC, 0.0f); setVal (pid::RATE, 0.0f);
    setVal (pid::WHISPER, 45.0f); setVal (pid::SPREAD, 50.0f); setVal (pid::DUCK, 0.0f); setVal (pid::MIX, 100.0f);
    proc.prepareToPlay (SR, N);

    // 1ª mitad: VIVO (shimmer difuso, sin congelar) · 2ª mitad: FREEZE (textura suspendida).
    const int freezeAtSample = total / 2;
    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (dry);
    bool frozen = false;
    for (int off = 0; off < total; off += N)
    {
        if (! frozen && off >= freezeAtSample) { set01 (pid::FREEZE, 1.0f); frozen = true; }
        const int len = juce::jmin (N, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi; proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
    }
    writeWav      ("/tmp/ovni_horizon_shimmer_wet.wav", wet, SR);
    writeMonoFold ("/tmp/ovni_horizon_mono_dry.wav",    dry, SR);
    writeMonoFold ("/tmp/ovni_horizon_mono_wet.wav",    wet, SR);   // ← acá se oye el fix sumado a mono

    const float wetMag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    std::printf ("RENDER[horizon-mono] wet_peak=%.4f -> /tmp/ovni_horizon_mono_{dry,wet}.wav\n", wetMag);
    REQUIRE (wetMag > 0.01f);
}
