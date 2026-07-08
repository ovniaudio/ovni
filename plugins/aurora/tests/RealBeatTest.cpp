#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// ★ [realbeat][aurora] — DIAGNOSTICO SOBRE EL BEAT REAL (no ruido rosa).
//
// Joaquin mira el GONIOMETRO de iZotope Insight en Ableton con un beat 80s
// 90bpm y AURORA queda MONO (linea vertical) aun con SPREAD/MOTION al 100%.
// La sesion pasada se "arreglo" con RUIDO ROSA (energia en TODOS los bins) y dio
// falso verde; sobre musica real CONCENTRADA (kick/snare/hats/bajo, CORR ~0.976)
// el efecto no decorrelaciona nada.
//
// Esta medicion LEE tests/assets/realbeat_90.wav (AudioFormatManager +
// createReaderFor), lo procesa por el AuroraProcessor REAL, ALINEA el dry por la
// latencia declarada del plugin, y mide sobre la SALIDA:
//   · CORR L/R  (= lo que muestra el goniometro: +1 mono/vertical, baja = ancho)
//   · WIDTH = RMS(side)/RMS(mid)
//   · tilt espectral grueso (low/high band ratio) — sanity de agudos.
// Sobre el INPUT mide lo mismo (la referencia de "lo que entra").
//
// NO es pass/fail del fix (eso vendra en la etapa 2). Es la REPRODUCCION del
// problema: imprime los numeros del beat real para confirmar que reproducimos lo
// que ve/oye Joaquin (queda casi tan mono a la salida como a la entrada).
// =============================================================================
namespace
{
using namespace ovni::test;
namespace pid = aurora::params::id;

constexpr int kBlk = 512;

// Lee el WAV del asset a un buffer estereo (resuelve a 2 canales). Devuelve SR real.
struct LoadedWav { juce::AudioBuffer<float> buf; double sr = 0.0; };

LoadedWav loadRealBeat()
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    juce::File f (OVNI_AURORA_REALBEAT_WAV);
    REQUIRE (f.existsAsFile());
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    REQUIRE (rd != nullptr);

    const int total = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> src ((int) juce::jmax (1u, rd->numChannels), total);
    rd->read (&src, 0, total, 0, true, true);

    LoadedWav out;
    out.sr = rd->sampleRate;
    out.buf.setSize (2, total);
    if (src.getNumChannels() >= 2)
    {
        out.buf.copyFrom (0, 0, src, 0, 0, total);
        out.buf.copyFrom (1, 0, src, 1, 0, total);
    }
    else
    {
        out.buf.copyFrom (0, 0, src, 0, 0, total);
        out.buf.copyFrom (1, 0, src, 0, 0, total);
    }
    return out;
}

// Mide CORR/WIDTH de un buffer estereo en [from, from+len).
StereoImage measure (const juce::AudioBuffer<float>& b, int from, int len)
{
    StereoImageMeter m;
    m.addBlock (b.getReadPointer (0) + from, b.getReadPointer (1) + from, len);
    return m.finish();
}

// Energia low/high para un canal (banda ~<300 Hz vs >3 kHz) — tilt espectral grueso.
struct SpectralTilt { double lowRms = 0.0, highRms = 0.0; };
SpectralTilt spectralTilt (const float* x, int from, int len, double sr)
{
    // 2x one-pole LP @300 Hz (low) y su complemento HP @3 kHz (high) — grueso pero suficiente
    // para detectar perdida de agudos / comb (el caso DUST; aca solo sanity).
    const double aL = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 300.0 / sr);
    const double aH = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 3000.0 / sr);
    double lp1 = 0, lp2 = 0, hp = 0;
    double sLow = 0, sHigh = 0;
    for (int i = from; i < from + len; ++i)
    {
        const double v = (double) x[i];
        lp1 += aL * (v - lp1); lp2 += aL * (lp1 - lp2);
        hp  += aH * (v - hp);
        const double hi = v - hp;            // residuo = agudos
        sLow  += lp2 * lp2;
        sHigh += hi * hi;
    }
    return { std::sqrt (sLow / (double) len), std::sqrt (sHigh / (double) len) };
}

struct Cfg { float spread, tilt, motion, monoSafe, duck, mix; bool sync; const char* name; bool inPhase = false; };
struct BeatResult { double inCorr, outCorr, inWidth, outWidth, hiBandDb, monoSumDb; float peak; };

// Corre el beat real por el processor REAL, alinea el dry por la latencia declarada, mide.
BeatResult runBeat (const LoadedWav& wav, const Cfg& c)
{
    aurora::AuroraProcessor proc;
    setParam (proc, pid::SPREAD,      c.spread);
    setParam (proc, pid::TILT,        c.tilt);
    setParam (proc, pid::MOTION,      c.motion);
    setParam (proc, pid::MONOSAFEAMT, c.monoSafe);
    setParam (proc, pid::DUCK,        c.duck);
    setParam (proc, pid::MIX,         c.mix);
    setParam (proc, pid::MOTIONSYNC,  c.sync ? 1.0f : 0.0f);
    setParam (proc, "monoSafe",       c.inPhase ? 1.0f : 0.0f);   // IN PHASE del chasis (escape mono-safe)

    proc.prepareToPlay (wav.sr, kBlk);
    const int latency = proc.getLatencySamples();

    const int total = wav.buf.getNumSamples();
    juce::AudioBuffer<float> wet (2, total);
    wet.makeCopyOf (wav.buf);
    for (int off = 0; off < total; off += kBlk)
    {
        const int len = juce::jmin (kBlk, total - off);
        juce::AudioBuffer<float> blk (2, len);
        for (int ch = 0; ch < 2; ++ch) blk.copyFrom (ch, 0, wet, ch, off, len);
        juce::MidiBuffer midi;
        proc.processBlock (blk, midi);
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, off, blk, ch, 0, len);
    }

    // Ventana de medicion: saltar el warm-up del OLA y la latencia; alinear dry.
    const int warm = latency + kBlk;
    const int from = warm;
    const int len  = total - warm - latency;
    REQUIRE (len > wav.sr * 0.5);   // al menos 0.5 s de material valido

    // INPUT (lo que entra): mismo material sobre la misma ventana, sin desplazar.
    const StereoImage inImg  = measure (wav.buf, from, len);
    // OUTPUT (lo que sale): el wet, corrido +latency para alinearse con el dry de la ventana.
    const StereoImage outImg = measure (wet, from + latency, len);

    // Tilt espectral del wet sobre el MID (mono sum L+R)/2 — el "boxed in" PERCEPTUAL es
    // perder agudos en la suma, no en un canal suelto (un widener mueve energia al side y el
    // L-canal solo MIENTE perdida que en realidad fue al R). Construimos el mid y medimos.
    std::vector<float> midIn (len), midOut (len);
    for (int i = 0; i < len; ++i)
    {
        midIn[(size_t) i]  = 0.5f * (wav.buf.getSample (0, from + i)        + wav.buf.getSample (1, from + i));
        midOut[(size_t) i] = 0.5f * (wet.getSample (0, from + latency + i)  + wet.getSample (1, from + latency + i));
    }
    const SpectralTilt stIn  = spectralTilt (midIn.data(),  0, len, wav.sr);
    const SpectralTilt stOut = spectralTilt (midOut.data(), 0, len, wav.sr);
    const double hiDeltaDb = 20.0 * std::log10 ((stOut.highRms + 1e-12) / (stIn.highRms + 1e-12));

    // PEAK del wet (techo del limiter 0.85) + mono-sum dB del OUT (compatibilidad mono).
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < len; ++i)
            peak = juce::jmax (peak, std::abs (wet.getSample (ch, from + latency + i)));

    std::printf ("REALBEAT[aurora] %-22s  IN  corr=%.4f width=%.4f  ->  OUT corr=%.4f width=%.4f   dCORR=%+.4f  dWIDTH=%+.4f  hiBand=%+.2f dB  monoSum=%+.2f dB  peak=%.3f\n",
                 c.name, inImg.corr, inImg.width, outImg.corr, outImg.width,
                 outImg.corr - inImg.corr, outImg.width - inImg.width, hiDeltaDb, outImg.monoSumDb, peak);

    return { inImg.corr, outImg.corr, inImg.width, outImg.width, hiDeltaDb, outImg.monoSumDb, peak };
}
} // namespace

TEST_CASE ("AURORA beat real: el ENSANCHADOR abre de verdad en el goniometro (regresion con MATERIAL REAL)", "[realbeat][aurora]")
{
    const LoadedWav wav = loadRealBeat();
    std::printf ("REALBEAT[aurora] WAV cargado: sr=%.0f samples=%d (%.2f s)\n",
                 wav.sr, wav.buf.getNumSamples(), wav.buf.getNumSamples() / wav.sr);

    // Referencia de entrada: el beat real es musica centrada (CORR ~0.976 esperado).
    const StereoImage inRef = measure (wav.buf, 0, wav.buf.getNumSamples());
    std::printf ("REALBEAT[aurora] INPUT global  corr=%.4f width=%.4f\n", inRef.corr, inRef.width);
    REQUIRE (inRef.rms > 1e-4);                 // hay material de verdad (no silencio)
    REQUIRE (inRef.corr > 0.95);                // la ENTRADA es musica centrada (el caso que mentia mono)

    // ─────────────────────────────────────────────────────────────────────────────────────
    // GATE PERMANENTE CON MATERIAL REAL (2026-06-10): el falso verde del ruido rosa NO vuelve.
    // El bug de Joaquin: con todo al 100 el goniometro quedaba VERTICAL (CORR 0.978 = mas mono
    // que la entrada). Estos REQUIRE clavan el fix sobre el BEAT REAL (no ruido rosa).
    // ─────────────────────────────────────────────────────────────────────────────────────

    // 1) DEFAULT (Spread 55) — "carga sonando": el goniometro ya se NOTA.
    const BeatResult def = runBeat (wav, { 0.55f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "default" });
    REQUIRE (def.outCorr  < 0.85);              // claramente abierto vs dry 0.976 (objetivo <=0.85)
    REQUIRE (def.outWidth > def.inWidth + 0.10);// WIDTH sube de verdad
    REQUIRE (def.hiBandDb > -3.0);              // NO "encajonado": la suma mono conserva agudos
    REQUIRE (def.peak    <= 0.85f);             // techo del limiter del sello

    // 2) Lo que uso Joaquin: Spread+Motion al 100 — antes quedaba MAS mono (0.978); ahora abre.
    const BeatResult all100 = runBeat (wav, { 1.00f, 0.50f, 1.0f, 0.0f, 0.0f, 1.0f, false, "joaquin_all100" });
    REQUIRE (all100.outCorr < 0.85);            // ya NO es mas mono que la entrada (era el bug exacto)

    // 3) SPREAD barrido (Motion 0, Mix 100): honesto y MONOTONO sobre musica real.
    const BeatResult s0   = runBeat (wav, { 0.00f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "spread_0" });
    const BeatResult s25  = runBeat (wav, { 0.25f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "spread_25" });
    const BeatResult s50  = runBeat (wav, { 0.50f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "spread_50" });
    const BeatResult s75  = runBeat (wav, { 0.75f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "spread_75" });
    const BeatResult s100 = runBeat (wav, { 1.00f, 0.50f, 0.0f, 0.0f, 0.0f, 1.0f, false, "spread_100" });
    REQUIRE (s0.outCorr   > 0.99);              // SPREAD 0 = identidad (mono, sin efecto) — honesto
    REQUIRE (s0.outWidth  < 0.02);
    // CORR cae MONOTONA al subir SPREAD (el knob hace lo que dice, sin zona muerta):
    REQUIRE (s25.outCorr  < s0.outCorr  - 0.005);
    REQUIRE (s50.outCorr  < s25.outCorr - 0.02);
    REQUIRE (s75.outCorr  < s50.outCorr - 0.02);
    REQUIRE (s100.outCorr < s75.outCorr - 0.02);
    REQUIRE (s100.outCorr < 0.50);              // SPREAD 100 = DRAMATICO (objetivo <=0.5, como PULSAR)
    REQUIRE (s100.outWidth > 0.55);             // WIDTH alto y visible en el goniometro
    REQUIRE (s100.peak   <= 0.85f);             // sin pasar el techo (anti-clip)

    // 4) IN PHASE ON (chasis monoSafe): el ensanchador COLAPSA → vuelve ~al dry (escape mono-safe).
    const BeatResult inPh = runBeat (wav, { 1.00f, 0.50f, 1.0f, 0.0f, 0.0f, 1.0f, false, "all100_INPHASE", true });
    REQUIRE (inPh.outCorr   > 0.95);            // CORR vuelve a ~dry (mono-compatible)
    REQUIRE (inPh.monoSumDb > -0.5);            // suma mono ~0 dB (recupera mono de verdad)

    // 5) Mono Safe ALTO mantiene el cuerpo grave correlado (kick/bajo mas al centro que con red 0).
    const BeatResult msHi = runBeat (wav, { 1.00f, 0.50f, 0.0f, 1.0f, 0.0f, 1.0f, false, "spread100_msHi" });
    REQUIRE (msHi.outCorr > s100.outCorr);      // con la red ALTA el goniometro queda mas centrado (graves protegidos)
}
