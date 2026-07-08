#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [render][aurora] — produce un WAV corto para el A/B de oído de Joaquín (el
// único gate humano). NO es un test de pase/falla del DSP (eso lo cubren
// [gain]/[measure]/[alias]/[real]); es un artefacto audible: una cama musical de
// banda ancha (acorde sostenido + capa de aire + transientes periódicos)
// procesada por el AuroraProcessor REAL en un preset de firma, escrita a
// /tmp/ovni_aurora_render_{dry,wet}.wav. El REQUIRE sólo verifica que el wet
// salió con energía (no es silencio) — para que un render roto no pase como ok.
// =============================================================================
namespace {
// Cama de prueba: 6 s, mono (igual en L/R a la entrada), 48 kHz. BANDA ANCHA de
// verdad (lección 2026-06-10): el A/B de oído tiene que mostrar el despliegue
// ESPECTRAL, y la decorrelación por bin necesita energía en MUCHOS bins. Un acorde
// grave/sparse la defeatea sola (física del material, NO del plugin) → el render
// medía casi mono y Joaquín oía "nada". La cama ahora suma:
//   · un acorde Am con armónicos (cuerpo musical, reconocible en el A/B),
//   · una capa de AIRE de banda ancha fuerte (pink, hasta agudos) — lo que hace
//     que el ancho se OIGA,
//   · transientes periódicos (mueven el duck/el gate).
// El timbre sigue siendo musical, pero ahora el efecto es claramente audible.
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
        // acorde Am con 2 armónicos por nota → contenido hasta ~2.6 kHz (no sólo graves)
        for (double f : { 220.0, 261.63, 329.63 })
        {
            s += 1.00 * std::sin (juce::MathConstants<double>::twoPi * f * vib * t);
            s += 0.35 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * f * vib * t);
            s += 0.18 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * f * vib * t);
        }
        s *= 0.085;
        pink = 0.965f * pink + 0.035f * noise();         // AIRE de banda ancha (fuerte: el ancho se oye acá)
        s += 0.80 * pink;
        s += 0.07 * std::sin (juce::MathConstants<double>::twoPi * 7200.0 * t);   // brillo fijo en agudos
        // transiente suave cada 0.5 s (ataque 2 ms, caída 80 ms)
        const double ph = std::fmod (t, 0.5);
        const double env = ph < 0.002 ? ph / 0.002 : std::exp (-(ph - 0.002) / 0.08);
        s += 0.18 * env * std::sin (juce::MathConstants<double>::twoPi * 660.0 * t);
        // fade in/out global de 50 ms para no clickear al inicio/fin
        double g = 1.0;
        const double fade = 0.05;
        if (t < fade) g = t / fade;
        else if (t > (double) n / SR - fade) g = ((double) n / SR - t) / fade;
        const float v = (float) (s * g * 0.7);
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

TEST_CASE ("render AURORA: cama musical -> WAV para A/B", "[render][aurora]")
{
    const double SR = 48000.0; const int N = 512;
    const int total = (int) (SR * 6.0);
    namespace pid = aurora::params::id;

    juce::AudioBuffer<float> dry (2, total);
    fillBed (dry, SR);
    writeWav ("/tmp/ovni_aurora_render_dry.wav", dry, SR);

    aurora::AuroraProcessor proc;
    auto set = [&] (const char* id, float v01) {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
    };
    // Preset de firma para el A/B: ancho cristalino, cuerpo (graves) al centro. MIX 100 y
    // DUCK 0 a propósito — el A/B tiene que mostrar el EFECTO, no una versión diluida (MIX 75
    // + DUCK 25 lo aplanaban: la lección 2026-06-10). SPREAD 70 + TILT +40 = el despliegue
    // claramente abierto que el fix de la causa raíz ahora entrega. Mono Safe 55 protege los
    // graves (sigue sumando mono sin cancelar).
    set (pid::SPREAD,      0.75f);
    set (pid::TILT,        0.70f);   // +40 en -100..100
    set (pid::MOTION,      0.0f);    // estático para el A/B: el MOTION promedia el ancho hacia abajo (su propio render lo cubre)
    set (pid::MONOSAFEAMT, 0.45f);   // protege graves pero deja abrir los medios/agudos del aire
    set (pid::DUCK,        0.0f);
    set (pid::MIX,         1.0f);

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
    writeWav ("/tmp/ovni_aurora_render_wet.wav", wet, SR);

    const float wetMag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    std::printf ("RENDER[aurora] wet_peak=%.4f -> /tmp/ovni_aurora_render_wet.wav\n", wetMag);
    REQUIRE (std::isfinite (wetMag));
    REQUIRE (wetMag > 0.01f);   // el wet salió con energía real (no silencio)
}

// Pliega a MONO (L+R)·0.5 y escribe un WAV mono → el A/B que prueba el fix 2026-06-13.
static void writeMonoFold (const juce::String& path, const juce::AudioBuffer<float>& st, double SR)
{
    const int n = st.getNumSamples();
    juce::AudioBuffer<float> mono (1, n);
    for (int i = 0; i < n; ++i) mono.setSample (0, i, 0.5f * (st.getSample (0, i) + st.getSample (1, i)));
    writeWav (path, mono, SR);
}

TEST_CASE ("render AURORA mono A/B: MOTION engaged, plegado a MONO (prueba del fix mono-audible)", "[render][aurora]")
{
    const double SR = 48000.0; const int N = 512;
    const int total = (int) (SR * 6.0);
    namespace pid = aurora::params::id;

    juce::AudioBuffer<float> dry (2, total);
    fillBed (dry, SR);

    aurora::AuroraProcessor proc;
    auto set = [&] (const char* id, float v01) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01); };
    // Firma del fix: la aurora ONDULA (MOTION) y se oye en mono. MIX 100, DUCK 0 (mostrar el efecto).
    set (pid::SPREAD, 0.70f); set (pid::TILT, 0.55f); set (pid::MOTION, 0.50f);
    set (pid::MONOSAFEAMT, 0.40f); set (pid::DUCK, 0.0f); set (pid::MIX, 1.0f);
    proc.prepareToPlay (SR, N);

    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (dry);
    for (int off = 0; off < total; off += N)
    {
        const int len = juce::jmin (N, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi; proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
    }
    writeWav      ("/tmp/ovni_aurora_motion_wet.wav", wet, SR);
    writeMonoFold ("/tmp/ovni_aurora_mono_dry.wav",   dry, SR);   // dry plegado a mono
    writeMonoFold ("/tmp/ovni_aurora_mono_wet.wav",   wet, SR);   // wet plegado a mono ← acá se oye el fix

    const float wetMag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    std::printf ("RENDER[aurora-mono] wet_peak=%.4f -> /tmp/ovni_aurora_mono_{dry,wet}.wav\n", wetMag);
    REQUIRE (wetMag > 0.01f);
}
