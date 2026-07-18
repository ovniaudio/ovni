// =============================================================================
// [listen] — renders de ESCUCHA para la curaduría del catálogo (gate humano: el
// oído de Joaquín — Manifiesto #6). UN archivo compartido: se compila dentro del
// exe de CADA plugin (cada exe ya define su createPluginFilter() + factoryPresets(),
// así que acá no se incluye ningún header de plugin). Renderiza, sobre el BEAT REAL
// (tests/assets/realbeat_90.wav) y camas sintéticas (PAD sostenido / PLUCKs
// espaciados), el estado DEFAULT + los presets de firma del plan →
//   /tmp/ovni_<plug>_listen_<cama>_<preset>.wav  (+ _dry por cama)
// El plan por plugin (camas, presets, forces) vive en planFor(). Transport FALSO a
// 90 BPM (el tempo del beat) para que los presets SYNC caigan en la grilla.
// =============================================================================
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "presets/PresetTypes.h"
#include <cstdio>
#include <cmath>

// cada exe de plugin define la suya (símbolo per-plugin, igual que factoryPresets()):
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#ifndef OVNI_LISTEN_BEAT_WAV
    #define OVNI_LISTEN_BEAT_WAV "realbeat_90.wav"
#endif

namespace
{
constexpr double kBpm      = 90.0;
constexpr int    kBlock    = 512;
constexpr double kSynthSec = 8.0;

// ---------- plan de escucha por plugin (qué camas y qué presets lo muestran) ----------
struct Force { const char* id; float value; };            // valor en unidades del parámetro
struct Plan
{
    std::vector<const char*> presets;                     // 2-3 presets de firma (nombre exacto)
    bool beat = true, pad = false, pluck = false;         // camas que muestran a ESTE plugin
    std::vector<Force> preForce;                          // pisan al preset EN t=0 (p.ej. FREEZE=0: los presets
                                                          // de HORIZON latchean freeze y capturarían silencio)
    std::vector<Force> force;                             // se aplican DESPUÉS del preset…
    double forceAtSec = 0.0;                              // …a este tiempo (p.ej. FREEZE con señal ya sonando)
};

Plan planFor (juce::String name)
{
    name = name.toUpperCase();
    if (name.contains ("PULSAR"))  return { { "Drift", "Butterfly", "Comet" },              true,  true,  false, {}, {}, 0.0 };
    if (name.contains ("NEBULA"))  return { { "Short Room", "Cathedral", "Black Hole" },    true,  false, true,  {}, {}, 0.0 };
    if (name.contains ("DUST"))    return { { "Bubbles", "Cosmic Ping", "Dust Storm" },     true,  false, true,  {}, {}, 0.0 };
    if (name.contains ("HALO"))    return { { "Vastness", "Aura", "Cathedral of Light" },   false, true,  true,  {}, {}, 0.0 };
    if (name.contains ("HORIZON")) return { { "Event Horizon", "Pulse 1/8", "Ghost Choir" }, true, true,  false,
                                            { { "freeze", 0.0f } },                         // el preset latchea freeze: soltarlo en t=0
                                            { { "freeze", 1.0f } }, 1.2 };                  // el gesto: congelar CON señal sonando
    if (name.contains ("AURORA"))  return { { "First Light", "Fan 1/2", "Inverted Sky" },   true,  true,  false, {}, {}, 0.0 };
    return { {}, true, false, false, {}, {}, 0.0 };       // fallback: sólo default sobre el beat
}

// ---------- camas ----------
bool loadBeat (juce::AudioBuffer<float>& buf, double& srOut)
{
    juce::File f (juce::String (OVNI_LISTEN_BEAT_WAV));
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

// PAD sostenido (acorde con armónicos + vibrato lento, MONO centrado): la cama que
// delata movimiento/espectro — si el plugin no hace nada acá, no hace nada.
void makePad (juce::AudioBuffer<float>& buf, double SR)
{
    const int n = (int) (SR * kSynthSec);
    buf.setSize (2, n); buf.clear();
    const double freqs[] = { 110.0, 164.81, 220.0, 277.18 };
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / SR;
        const double vib = 1.0 + 0.003 * std::sin (juce::MathConstants<double>::twoPi * 4.7 * t);
        double s = 0.0;
        for (double f0 : freqs)
            for (int h = 1; h <= 5; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * f0 * h * vib * t) / (double) (h * h);
        s *= 0.10;
        double g = 1.0; const double fade = 0.4;
        if (t < fade) g = t / fade;
        else if (t > kSynthSec - fade) g = (kSynthSec - t) / fade;
        buf.setSample (0, i, (float) (s * g));
        buf.setSample (1, i, (float) (s * g));
    }
}

// PLUCKs espaciados (ataque + cola que decae, MONO centrado): la cama que delata
// colas/ecos/reverbs — entre pluck y pluck se ESCUCHA lo que el plugin agrega.
void makePluck (juce::AudioBuffer<float>& buf, double SR)
{
    const int n = (int) (SR * kSynthSec);
    buf.setSize (2, n); buf.clear();
    const double notes[] = { 220.0, 329.63, 277.18, 440.0, 220.0, 164.81, 329.63 };
    const double step = 1.1;
    for (int k = 0; k < 7; ++k)
    {
        const double t0 = 0.15 + k * step;
        const double f0 = notes[k];
        const int i0 = (int) (t0 * SR);
        for (int i = i0; i < n; ++i)
        {
            const double dt = (double) (i - i0) / SR;
            if (dt > 1.0) break;
            const double env = std::exp (-dt * 6.5) * juce::jmin (1.0, dt / 0.002);
            double s = 0.0;
            for (int h = 1; h <= 4; ++h)
                s += std::sin (juce::MathConstants<double>::twoPi * f0 * h * dt) * std::exp (-dt * 2.0 * h) / (double) h;
            buf.addSample (0, i, (float) (s * env * 0.32));
            buf.addSample (1, i, (float) (s * env * 0.32));
        }
    }
}

// ---------- infra ----------
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

// transport falso a 90 BPM: los presets SYNC caen EN la grilla del beat real.
struct FakePlayHead : juce::AudioPlayHead
{
    double sr = 48000.0; juce::int64 samplePos = 0;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo pi;
        pi.setBpm (kBpm);
        pi.setTimeInSamples (samplePos);
        pi.setTimeInSeconds ((double) samplePos / sr);
        pi.setPpqPosition ((double) samplePos / sr * (kBpm / 60.0));
        pi.setIsPlaying (true);
        return pi;
    }
};

void applyParam (juce::AudioProcessor& proc, const char* id, float value, bool required)
{
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID == juce::String (id))
            {
                rp->setValueNotifyingHost (rp->getNormalisableRange().convertTo0to1 (value));
                return;
            }
    if (required)
        FAIL ("param no encontrado: " << id);
}

bool applyFactoryPreset (juce::AudioProcessor& proc, const char* name)
{
    for (const auto& fp : ovni::presets::factoryPresets())
        if (juce::String (fp.name) == juce::String (name))
        {
            for (const auto& pp : fp.params)
                applyParam (proc, pp.id, pp.value, true);
            return true;
        }
    return false;
}

juce::String slug (juce::String s)
{
    s = s.toLowerCase().replaceCharacters (" /", "--");
    return s.retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789-");
}

// render offline: bloques de 512 con transport avanzando; forces al cruzar forceAtSec.
juce::AudioBuffer<float> renderThrough (const Plan& plan, const char* presetName,
                                        const juce::AudioBuffer<float>& dry, double SR)
{
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    REQUIRE (proc != nullptr);
    if (presetName != nullptr)
        REQUIRE (applyFactoryPreset (*proc, presetName));
    for (const auto& f : plan.preForce) applyParam (*proc, f.id, f.value, true);

    FakePlayHead ph; ph.sr = SR;
    proc->setPlayHead (&ph);
    proc->prepareToPlay (SR, kBlock);

    const int total = dry.getNumSamples();
    const int forceAt = (int) (plan.forceAtSec * SR);
    bool forced = plan.force.empty();

    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (dry);
    for (int off = 0; off < total; off += kBlock)
    {
        if (! forced && off >= forceAt)
        {
            for (const auto& f : plan.force) applyParam (*proc, f.id, f.value, true);
            forced = true;
        }
        const int len = juce::jmin (kBlock, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi;
        proc->processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
        ph.samplePos += len;
    }
    proc->setPlayHead (nullptr);
    proc->releaseResources();

    const float mag = juce::jmax (wet.getMagnitude (0, 0, total), wet.getMagnitude (1, 0, total));
    REQUIRE (std::isfinite (mag));
    return wet;
}
} // namespace

TEST_CASE ("listen: default + presets de firma -> WAV para curaduria", "[listen]")
{
    std::unique_ptr<juce::AudioProcessor> probe (createPluginFilter());
    REQUIRE (probe != nullptr);
    const juce::String plug = slug (probe->getName());
    const Plan plan = planFor (probe->getName());
    probe.reset();

    struct Source { juce::String name; juce::AudioBuffer<float> buf; double sr; };
    std::vector<Source> sources;

    if (plan.beat)
    {
        Source s { "beat", {}, 48000.0 };
        if (loadBeat (s.buf, s.sr)) sources.push_back (std::move (s));
        else std::printf ("LISTEN[%s] WARN: beat asset no encontrado (%s)\n", plug.toRawUTF8(), OVNI_LISTEN_BEAT_WAV);
    }
    if (plan.pad)   { Source s { "pad",   {}, 48000.0 }; makePad   (s.buf, s.sr); sources.push_back (std::move (s)); }
    if (plan.pluck) { Source s { "pluck", {}, 48000.0 }; makePluck (s.buf, s.sr); sources.push_back (std::move (s)); }
    REQUIRE (! sources.empty());

    for (const auto& src : sources)
    {
        const juce::String base = "/tmp/ovni_" + plug + "_listen_" + src.name;
        writeWav (base + "_dry.wav", src.buf, src.sr);

        auto renderOne = [&] (const char* presetName, const juce::String& tag)
        {
            const auto wet = renderThrough (plan, presetName, src.buf, src.sr);
            const float mag = juce::jmax (wet.getMagnitude (0, 0, wet.getNumSamples()),
                                          wet.getMagnitude (1, 0, wet.getNumSamples()));
            REQUIRE (mag > 0.01f);
            writeWav (base + "_" + tag + ".wav", wet, src.sr);
            std::printf ("LISTEN[%s] %s/%s peak=%.3f -> %s\n", plug.toRawUTF8(),
                         src.name.toRawUTF8(), tag.toRawUTF8(), mag, (base + "_" + tag + ".wav").toRawUTF8());
        };

        renderOne (nullptr, "default");
        for (const char* p : plan.presets)
            renderOne (p, slug (p));
    }
}
