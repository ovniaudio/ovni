#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [realbeat][horizon] — DIAGNÓSTICO con el BEAT REAL (no ruido rosa).
//
// Lección de la sesión pasada: el "arreglo" se midió con ruido rosa (energía en
// TODOS los bins → peak-lock engancha en todos lados y el spread decorrela algo)
// y dio FALSO VERDE. Sobre MÚSICA REAL (energía concentrada en kick/snare/hats/
// bajo) el plugin quedó MONO. ESTA medición lee tests/assets/realbeat_90.wav y lo
// procesa por el HorizonProcessor REAL.
//
// Reproduce LO QUE VE/OYE JOAQUÍN en el goniómetro de iZotope Insight:
//   (1) FREEZE OFF (su default): el wet es identity → CORR≈+1 (igual que el dry),
//       WIDTH≈igual al dry → el plugin NO HACE NADA al cargarlo.
//   (2) FREEZE OFF + WHISPER/SPREAD/MIX al 100: SIGUE igual al dry (los controles
//       no tocan el audio vivo).
//   (3) FREEZE ON + spread 100: recién acá abre (referencia del "antes").
//
// Mide sobre la SALIDA: CORR L/R (goniómetro: +1=mono, baja=ancho), WIDTH=
// RMS(side)/RMS(mid) y el espectro. Alinea el dry por la latencia declarada del
// plugin (PDC) antes de comparar.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr double kBpm = 90.0;   // el beat es 90 BPM

struct FixedPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (kBpm); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
    }
};

// Carga el beat real (juce::AudioFormatManager + createReaderFor) a un buffer estéreo @kSR.
juce::AudioBuffer<float> loadRealBeat()
{
    juce::AudioFormatManager fmt; fmt.registerBasicFormats();
    juce::File wav (HORIZON_REALBEAT_WAV);
    REQUIRE (wav.existsAsFile());
    auto stream = wav.createInputStream();
    REQUIRE (stream != nullptr);
    std::unique_ptr<juce::AudioFormatReader> rd (
        fmt.createReaderFor (std::unique_ptr<juce::InputStream> (stream.release())));
    REQUIRE (rd != nullptr);
    const int total = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> buf (2, total);
    rd->read (&buf, 0, total, 0, true, true);
    // archivo declarado @48k (afinfo): coincide con kSR → sin resample.
    REQUIRE (std::abs (rd->sampleRate - kSR) < 1.0);
    return buf;
}

struct Result
{
    StereoImage img;        // CORR/WIDTH/BAL/MONOSUM sobre la SALIDA
    StereoImage dryImg;     // lo MISMO sobre el dry ALINEADO (baseline del beat)
    double specLossDb = 0;  // pérdida de agudos wet vs dry (espectro >5 kHz), dB
};

// Procesa el beat real por el processor REAL con la config dada y mide la SALIDA contra
// el DRY alineado por la latencia declarada (PDC). freezeAtBlk<0 → nunca congela.
// monoSafe → activa IN PHASE (param inyectado por el chasis) desde el primer bloque.
Result run (float whisper01, float spread01, float mix01, int freezeAtBlk,
            float rate01 = 0.0f, bool monoSafe = false)
{
    const juce::AudioBuffer<float> beat = loadRealBeat();
    const int total = beat.getNumSamples();

    horizon::HorizonProcessor proc;
    setParam (proc, pid::RATESYNC, 0.0f);            // FREE
    setParam (proc, pid::RATE,     rate01);          // 0 = sostenido por default
    setParam (proc, pid::WHISPER,  whisper01);
    setParam (proc, "monoSafe",    monoSafe ? 1.0f : 0.0f);   // IN PHASE (chasis)
    setParam (proc, pid::SPREAD,   spread01);
    setParam (proc, pid::MIX,      mix01);
    proc.prepareToPlay (kSR, kBlk);
    FixedPlayHead ph; proc.setPlayHead (&ph);

    const int lat = proc.getLatencySamples();

    // Salida del plugin, bloque a bloque (camino REAL, processBlock).
    juce::AudioBuffer<float> out (2, total);
    bool froze = (freezeAtBlk < 0);
    int blk = 0;
    for (int off = 0; off < total; off += kBlk, ++blk)
    {
        if (! froze && freezeAtBlk >= 0 && blk >= freezeAtBlk)
        { setParam (proc, pid::FREEZE, 1.0f); froze = true; }
        const int len = juce::jmin (kBlk, total - off);
        juce::AudioBuffer<float> b (2, len);
        for (int ch = 0; ch < 2; ++ch) b.copyFrom (ch, 0, beat, ch, off, len);
        juce::MidiBuffer midi;
        proc.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, off, b, ch, 0, len);
        ph.ppq += (kBpm / 60.0) * ((double) len / kSR);
    }

    // Ventana de medición: saltear warmup (captura/cross-fade del freeze + latencia) y el final.
    // Comparamos la SALIDA[i] contra el DRY[i-lat] (dry alineado al PDC del plugin).
    const int warmSamp = juce::jmax (lat + 4 * kBlk, (int) (1.5 * kSR));
    const int measLo   = warmSamp;
    const int measHi   = total - kBlk;

    StereoImageMeter mOut, mDry;
    // FFT acumulada por banda para el espectro (wet vs dry).
    constexpr int kFftOrder = 11;            // 2048
    constexpr int kFftN     = 1 << kFftOrder;
    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> accWet (kFftN / 2, 0.0f), accDry (kFftN / 2, 0.0f);
    std::vector<float> fftBuf (2 * kFftN);
    juce::dsp::WindowingFunction<float> win (kFftN, juce::dsp::WindowingFunction<float>::hann);
    int fftFrames = 0;

    for (int i = measLo; i < measHi; ++i)
    {
        const float oL = out.getSample (0, i);
        const float oR = out.getSample (1, i);
        mOut.addSample (oL, oR);

        const int di = i - lat;
        const float dL = (di >= 0) ? beat.getSample (0, di) : 0.0f;
        const float dR = (di >= 0) ? beat.getSample (1, di) : 0.0f;
        mDry.addSample (dL, dR);
    }

    // Espectro: bloques Hann de 2048 sobre el MID, wet y dry alineado.
    for (int base = measLo; base + kFftN <= measHi; base += kFftN)
    {
        // wet
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int k = 0; k < kFftN; ++k)
            fftBuf[(size_t) k] = 0.5f * (out.getSample (0, base + k) + out.getSample (1, base + k));
        win.multiplyWithWindowingTable (fftBuf.data(), kFftN);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        for (int k = 0; k < kFftN / 2; ++k) accWet[(size_t) k] += fftBuf[(size_t) k];
        // dry alineado
        std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
        for (int k = 0; k < kFftN; ++k)
        {
            const int di = base + k - lat;
            fftBuf[(size_t) k] = (di >= 0) ? 0.5f * (beat.getSample (0, di) + beat.getSample (1, di)) : 0.0f;
        }
        win.multiplyWithWindowingTable (fftBuf.data(), kFftN);
        fft.performFrequencyOnlyForwardTransform (fftBuf.data());
        for (int k = 0; k < kFftN / 2; ++k) accDry[(size_t) k] += fftBuf[(size_t) k];
        ++fftFrames;
    }

    // Pérdida de agudos: energía wet vs dry en >5 kHz.
    double hiWet = 0.0, hiDry = 0.0;
    const int kHi = (int) (5000.0 / kSR * kFftN);
    for (int k = kHi; k < kFftN / 2; ++k) { hiWet += (double) accWet[(size_t) k] * accWet[(size_t) k]; hiDry += (double) accDry[(size_t) k] * accDry[(size_t) k]; }

    Result r;
    r.img        = mOut.finish();
    r.dryImg     = mDry.finish();
    r.specLossDb = 10.0 * std::log10 ((hiWet + 1e-20) / (hiDry + 1e-20));
    return r;
}
} // namespace

// ── (0) BASELINE: la imagen del beat real crudo (música centrada) ───────────────────────
TEST_CASE ("HORIZON realbeat: el beat crudo es música centrada (CORR alta)", "[realbeat][horizon]")
{
    // FREEZE OFF, controles en default-de-fábrica (whisper12/spread50/mix100). El dryImg es el
    // beat tal cual. Verifica que el material es lo que dice el brief: CORR L/R ~0.976.
    const Result r = run (0.12f, 0.50f, 1.0f, -1);
    std::printf ("REALBEAT[dry crudo]      CORR=%+.3f  WIDTH=%.3f\n", r.dryImg.corr, r.dryImg.width);
    REQUIRE (r.dryImg.corr > 0.90);   // música real centrada (brief: ~0.976)
}

// ── (1) FIX: FREEZE OFF (default) ABRE el beat al cargar (NO colapsa a mono) ──────────────
TEST_CASE ("HORIZON realbeat: FREEZE OFF default ENSANCHA el beat (carga sonando)", "[realbeat][horizon]")
{
    // El default de Joaquín: FREEZE OFF, whisper12/spread50/mix100. Tras el fix, el wet vivo es
    // un ESPACIALIZADOR ESPECTRAL (SPREAD+WHISPER sobre el espectro vivo del mid) → ABRE la
    // imagen al cargar y tocar, sin congelar. El goniómetro NO queda en línea 45°: se ensancha.
    // (Antes del fix esto salía CORR +1.000 / WIDTH 0.000 = mono perfecto — el bug de Joaquín.)
    const Result r = run (0.12f, 0.50f, 1.0f, -1);
    std::printf ("REALBEAT[FREEZE OFF def] out: CORR=%+.3f WIDTH=%.3f | dry: CORR=%+.3f WIDTH=%.3f\n",
                 r.img.corr, r.img.width, r.dryImg.corr, r.dryImg.width);
    REQUIRE (r.dryImg.width > 0.05);            // el beat real TIENE ancho (no es mono de origen)
    REQUIRE (r.img.width > r.dryImg.width);     // el default ENSANCHA vs el dry (se NOTA al cargar)
    REQUIRE (r.img.corr  < r.dryImg.corr);      // y des-correlaciona (CORR baja vs +0.975 del dry)
    REQUIRE (r.img.corr  < 0.95);               // claramente movido (no passthrough mono)
}

// ── (2) FIX: FREEZE OFF + SPREAD alto ABRE de verdad (control ya NO inerte) ───────────────
TEST_CASE ("HORIZON realbeat: FREEZE OFF + SPREAD alto abre fuerte (control vivo)", "[realbeat][horizon]")
{
    // Lo que probó Joaquín: subió SPREAD al 100 con FREEZE OFF. Ahora SÍ abre: SPREAD/WHISPER
    // actúan sobre el audio VIVO, no sólo dentro del frame congelado. Target del brief: CORR≤0.7.
    const Result r = run (1.0f, 1.0f, 1.0f, -1);
    std::printf ("REALBEAT[FREEZE OFF 100] out: CORR=%+.3f WIDTH=%.3f | dry: CORR=%+.3f WIDTH=%.3f\n",
                 r.img.corr, r.img.width, r.dryImg.corr, r.dryImg.width);
    REQUIRE (r.img.corr  <= 0.70);   // SPREAD alto abre CLARO (brief: CORR ≤ 0.7)
    REQUIRE (r.img.width >  0.40);   // y la imagen es ancha de verdad (no ancho-basura marginal)
}

// ── (2b) FIX: el SPREAD del vivo es MONÓTONO (sube ancho al subir el knob, no se da vuelta) ─
TEST_CASE ("HORIZON realbeat: FREEZE OFF — SPREAD monótono (0 mono → 50 medio → 100 ancho)", "[realbeat][horizon]")
{
    const Result r0  = run (0.12f, 0.00f, 1.0f, -1);   // spread 0 → mono-compatible
    const Result r50 = run (0.12f, 0.50f, 1.0f, -1);   // default
    const Result r100= run (0.12f, 1.00f, 1.0f, -1);   // máximo
    std::printf ("REALBEAT[SPREAD sweep] sp0:CORR=%+.3f W=%.3f  sp50:CORR=%+.3f W=%.3f  sp100:CORR=%+.3f W=%.3f\n",
                 r0.img.corr, r0.img.width, r50.img.corr, r50.img.width, r100.img.corr, r100.img.width);
    // SPREAD 0 → casi mono (sin des-correlación: L=R=mid). El WIDTH baja al del passthrough mono.
    REQUIRE (r0.img.corr  > 0.99);
    REQUIRE (r0.img.width < 0.02);
    // ancho MONÓTONO creciente: 0 < 50 < 100 (el knob nunca narrowea = honesto).
    REQUIRE (r50.img.width  > r0.img.width);
    REQUIRE (r100.img.width > r50.img.width);
    // correlación MONÓTONA decreciente.
    REQUIRE (r50.img.corr  < r0.img.corr);
    REQUIRE (r100.img.corr < r50.img.corr);
}

// ── (3) FIX: FREEZE ON + spread 100 → textura congelada sostenida + ancha (otra cosa) ─────
TEST_CASE ("HORIZON realbeat: FREEZE ON + spread 100 abre la textura congelada (CORR ≤ 0.6)", "[realbeat][horizon]")
{
    // El freeze SIGUE siendo dramático: congela el frame y lo abre (la textura suspendida es
    // claramente "otra cosa" que el dry). Brief: CORR ≤ 0.6 con la textura congelada + ancha.
    const Result r = run (0.12f, 1.0f, 1.0f, /*freezeAtBlk*/ 140);
    std::printf ("REALBEAT[FREEZE ON sp100] out: CORR=%+.3f WIDTH=%.3f specLossHi=%+.2f dB\n",
                 r.img.corr, r.img.width, r.specLossDb);
    REQUIRE (r.img.corr  <= 0.60);   // brief: la textura congelada abre fuerte
    REQUIRE (r.img.width >  0.60);   // y es ancha de verdad
}

// ── (4) FIX: IN PHASE (mono-safe) colapsa la des-correlación → mono-compatible ────────────
TEST_CASE ("HORIZON realbeat: IN PHASE colapsa a mono-compatible (escape mono)", "[realbeat][horizon]")
{
    // FREEZE OFF + SPREAD 100 abre fuerte; con IN PHASE el motor anula la des-correlación del
    // vivo → L=R = mono-compatible (CORR vuelve a ~+1). Honestidad: el mono-safe RECUPERA mono.
    const Result open = run (0.12f, 1.0f, 1.0f, -1, 0.0f, /*monoSafe*/ false);
    const Result safe = run (0.12f, 1.0f, 1.0f, -1, 0.0f, /*monoSafe*/ true);
    std::printf ("REALBEAT[IN PHASE] open:CORR=%+.3f W=%.3f  safe:CORR=%+.3f W=%.3f\n",
                 open.img.corr, open.img.width, safe.img.corr, safe.img.width);
    REQUIRE (open.img.corr <= 0.70);   // sin IN PHASE abre
    REQUIRE (safe.img.corr >  0.95);   // con IN PHASE recupera mono-compatibilidad (CORR sube)
    REQUIRE (safe.img.width <  0.10);  // y el ancho del vivo se colapsa
}

// ── (5) FIX: FREEZE OFF preserva los agudos del beat (no "encajonado") ────────────────────
TEST_CASE ("HORIZON realbeat: FREEZE OFF preserva los agudos (>5 kHz casi intacto)", "[realbeat][horizon]")
{
    // El "encajonado" del freeze (HF perdido al sostener UN frame) NO pasa con FREEZE OFF: el
    // wet vivo procesa el espectro VIVO → los hats/transientes >5 kHz sobreviven. specLoss ~0 dB.
    const Result r = run (0.12f, 0.50f, 1.0f, -1);
    std::printf ("REALBEAT[FREEZE OFF HF] specLossHi(>5k)=%+.2f dB\n", r.specLossDb);
    REQUIRE (r.specLossDb > -3.0);   // agudos preservados (no se pierden como en el freeze sostenido)
}
