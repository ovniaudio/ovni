// RealWorldTest.cpp — ★ [real][dust]: REGRESIÓN EN CONDICIONES REALES del DustProcessor.
//
// Nada de medir con el mecanismo neutralizado: acá DUST corre como lo usaría un productor —
// MIX 35 %, DENSIDAD 60, SYNC 1/8 con BPM del host (playhead stub), ráfagas de banda ancha — y se
// verifica que:
//   (a) los ecos caen EN GRILLA (espaciado == división del tempo ±5 %),
//   (b) DUCK 100 agacha el wet ≥ 6 dB durante la ráfaga y lo DEVUELVE en el hueco,
//   (c) el eco es AUDIBLE en la salida total a MIX realista (no un wet enterrado).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    constexpr double SR  = 48000.0;
    constexpr int    BLK = 512;
    constexpr double BPM = 100.0;   // BPM de test (no 120: caza hardcodeos del fallback)

    namespace pid = dust::params::id;

    // PlayHead con BPM fijo + isPlaying (patrón del MeasureTest de HALO).
    struct FixedBpmPlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (BPM);
            p.setIsPlaying (true);
            p.setPpqPosition (ppq);
            return p;
        }
    };

    // Config "condiciones reales": MIX 35 · DENSIDAD 60 · SYNC 1/8 · SPREAD 60 · VIDA 0 (la grilla
    // se mide limpia; el jitter de VIDA es contenido legítimo que la emborracharía a propósito).
    void setupRealConditions (dust::DustProcessor& proc, float duck01)
    {
        using ovni::test::setParam;
        setParam (proc, pid::MIX, 0.35f);
        setParam (proc, pid::DENSITY, 0.60f);
        setParam (proc, pid::SPREAD, 0.60f);
        setParam (proc, pid::VIDA, 0.0f);
        setParam (proc, pid::DUCK, duck01);
        setParam (proc, pid::RATESYNC, 1.0f);
        // División "1/8" = índice 1 de la tabla (choice normalizado = idx / (kCount-1)).
        setParam (proc, pid::RATEDIV, 1.0f / (float) (dust::params::sync::kCount - 1));
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// (a) + (c) — UNA ráfaga de banda ancha: los ecos aparecen a múltiplos de la división (1/8 @ 100
// BPM = 300 ms) ±5 %, y son AUDIBLES en la salida total a MIX 35 (el hueco tras la ráfaga es del
// wet: ahí el eco se mide sin el dry encima).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST real: SYNC 1/8 @ 100 BPM -> ecos en grilla y AUDIBLES a MIX 35", "[real][dust]")
{
    dust::DustProcessor proc;
    setupRealConditions (proc, 0.0f);
    proc.prepareToPlay (SR, BLK);

    FixedBpmPlayHead ph;
    proc.setPlayHead (&ph);

    // La división manda el espaciado: 1/8 @ 100 BPM = 60000·0.5/100 = 300 ms (una sola fuente de
    // verdad con processAudio vía el getter de diagnóstico).
    const float rateMs = proc.debugEffectiveRateMs (BPM);
    REQUIRE (rateMs == Catch::Approx (300.0f).margin (0.01));

    const int rateSamp = (int) std::lround ((double) rateMs * 0.001 * SR);   // 14400
    const int total    = (int) std::lround (2.0 * SR);
    std::vector<float> env ((size_t) total, 0.0f);

    // Ráfaga de banda ancha: 10 ms de ruido blanco a 0.8 en t=0, después silencio.
    ovni::test::White noise;
    const int burstLen = (int) std::lround (0.010 * SR);

    juce::AudioBuffer<float> buf (2, BLK);
    for (int pos = 0; pos < total; pos += BLK)
    {
        buf.clear();
        for (int i = 0; i < BLK; ++i)
        {
            const int g = pos + i;
            if (g < burstLen)
            {
                const float x = 0.8f * noise.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        ph.ppq += (BPM / 60.0) * ((double) BLK / SR);

        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < BLK && pos + i < total; ++i)
            env[(size_t) (pos + i)] = std::abs (L[i]) + std::abs (R[i]);
    }

    // (a) Ecos en grilla: pico en cada ventana k·300 ms ±40 %, ubicado a ±5 % del múltiplo.
    // (c) Audibles: amplitud real a MIX 35 (no un wet enterrado bajo el piso).
    for (int k = 1; k <= 4; ++k)
    {
        const int c  = k * rateSamp;
        const int w0 = c - (int) (0.4 * rateSamp);
        const int w1 = juce::jmin (total - 1, c + (int) (0.4 * rateSamp));
        int   best  = w0;
        float bestV = -1.0f;
        for (int i = w0; i <= w1; ++i)
            if (env[(size_t) i] > bestV) { bestV = env[(size_t) i]; best = i; }

        INFO ("eco k=" << k << " esperado=" << c << " medido=" << best << " amp=" << bestV);
        REQUIRE (bestV > 0.02f);                                      // eco AUDIBLE a MIX realista
        REQUIRE (std::abs (best - c) <= (int) (0.05 * (double) c));   // en grilla: ±5 % de la división
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// (a') — Grilla con VIDA al DEFAULT (25). El jitter temporal de VIDA (±0.30·VIDA·RATE, fijado al
// nacer) es contenido legítimo del carácter (spec: VIDA = jitter temporal), pero la grilla del
// DEFAULT no puede quedar sin gate (hallazgo de review: el caso de arriba mide con VIDA 0).
// Ventana honesta: ±(5 % del múltiplo + 7.5 % del RATE) — el jitter es por-tap, NO acumulativo.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST real: SYNC 1/8 con VIDA default (25) -> la grilla aguanta el jitter declarado",
           "[real][dust]")
{
    dust::DustProcessor proc;
    setupRealConditions (proc, 0.0f);
    ovni::test::setParam (proc, pid::VIDA, 0.25f);   // default de curaduría: jitter ±7.5 % del RATE
    proc.prepareToPlay (SR, BLK);

    FixedBpmPlayHead ph;
    proc.setPlayHead (&ph);

    const float rateMs   = proc.debugEffectiveRateMs (BPM);
    const int   rateSamp = (int) std::lround ((double) rateMs * 0.001 * SR);
    const int   total    = (int) std::lround (2.0 * SR);
    std::vector<float> env ((size_t) total, 0.0f);

    ovni::test::White noise;
    const int burstLen = (int) std::lround (0.010 * SR);

    juce::AudioBuffer<float> buf (2, BLK);
    for (int pos = 0; pos < total; pos += BLK)
    {
        buf.clear();
        for (int i = 0; i < BLK; ++i)
            if (pos + i < burstLen)
            {
                const float x = 0.8f * noise.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        ph.ppq += (BPM / 60.0) * ((double) BLK / SR);

        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < BLK && pos + i < total; ++i)
            env[(size_t) (pos + i)] = std::abs (L[i]) + std::abs (R[i]);
    }

    const int jitterMax = (int) std::lround (0.30 * 0.25 * (double) rateSamp);   // ±7.5 % del RATE
    for (int k = 1; k <= 4; ++k)
    {
        const int c  = k * rateSamp;
        const int w0 = c - (int) (0.4 * rateSamp);
        const int w1 = juce::jmin (total - 1, c + (int) (0.4 * rateSamp));
        int   best  = w0;
        float bestV = -1.0f;
        for (int i = w0; i <= w1; ++i)
            if (env[(size_t) i] > bestV) { bestV = env[(size_t) i]; best = i; }

        INFO ("eco k=" << k << " esperado=" << c << " medido=" << best << " amp=" << bestV
              << " tol=" << (int) (0.05 * (double) c) + jitterMax);
        REQUIRE (bestV > 0.02f);                                                  // audible a MIX 35
        REQUIRE (std::abs (best - c) <= (int) (0.05 * (double) c) + jitterMax);   // grilla + jitter declarado
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// (b) — DUCK: con ráfagas periódicas, el wet (estimado restando el dry conocido de la salida) cae
// ≥ 6 dB durante la ráfaga respecto de DUCK 0, y en el hueco VUELVE (no queda agachado).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace
{
    struct DuckLevels { double burstRms = 0.0, gapRms = 0.0; };

    DuckLevels measureDuck (float duck01)
    {
        dust::DustProcessor proc;
        setupRealConditions (proc, duck01);
        proc.prepareToPlay (SR, BLK);

        FixedBpmPlayHead ph;
        proc.setPlayHead (&ph);

        // gDry exacto del MIX 35 (ley de potencia): wet = out − gDry·dry (el dry pasa intacto:
        // IN/OUT 0 dB, sin bass-mono). Así se mide el WET REAL dentro de la salida total.
        const float gDry = std::cos (0.35f * juce::MathConstants<float>::halfPi);

        // Ráfagas de 60 ms a 0.7 cada 1.05 s (período NO múltiplo de la grilla de 300 ms: los ecos
        // pueblan los huecos). Ventanas: ráfaga = [inicio, +210 ms] (60 ms + release 150 ms del
        // duck); hueco = [+500 ms, +1000 ms].
        const int period   = (int) std::lround (1.05 * SR);
        const int burstLen = (int) std::lround (0.060 * SR);
        const int bWin     = (int) std::lround (0.210 * SR);
        const int gWin0    = (int) std::lround (0.500 * SR);
        const int gWin1    = (int) std::lround (1.000 * SR);
        const int nBursts  = 14, skip = 4;
        const int total    = nBursts * period;

        ovni::test::White noise;
        juce::AudioBuffer<float> buf (2, BLK);
        juce::AudioBuffer<float> dry (2, BLK);

        double accB = 0.0, accG = 0.0;
        long   cntB = 0,  cntG = 0;
        for (int pos = 0; pos < total; pos += BLK)
        {
            buf.clear();
            for (int i = 0; i < BLK; ++i)
            {
                const int g = pos + i;
                if (g % period < burstLen)
                {
                    const float x = 0.7f * noise.next();
                    buf.setSample (0, i, x);
                    buf.setSample (1, i, x);
                }
            }
            dry.makeCopyOf (buf, true);
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            ph.ppq += (BPM / 60.0) * ((double) BLK / SR);

            for (int i = 0; i < BLK; ++i)
            {
                const int g = pos + i;
                if (g < skip * period) continue;   // warmup: la nube se asienta
                const int phase = g % period;
                const float wL = buf.getSample (0, i) - gDry * dry.getSample (0, i);
                const float wR = buf.getSample (1, i) - gDry * dry.getSample (1, i);
                const double e = (double) wL * wL + (double) wR * wR;
                if (phase < bWin)                      { accB += e; ++cntB; }
                else if (phase >= gWin0 && phase < gWin1) { accG += e; ++cntG; }
            }
        }

        DuckLevels lv;
        lv.burstRms = std::sqrt (accB / (double) juce::jmax (1L, cntB));
        lv.gapRms   = std::sqrt (accG / (double) juce::jmax (1L, cntG));
        return lv;
    }
}

TEST_CASE ("DUST real: DUCK 100 agacha el wet >= 6 dB en la rafaga y lo devuelve en el hueco",
           "[real][dust]")
{
    const auto d0   = measureDuck (0.0f);
    const auto d100 = measureDuck (1.0f);

    const double ratio0   = d0.burstRms   / juce::jmax (1.0e-12, d0.gapRms);
    const double ratio100 = d100.burstRms / juce::jmax (1.0e-12, d100.gapRms);

    std::printf ("DUCK[dust] duck0: burst=%.5f gap=%.5f (ratio=%.3f) | duck100: burst=%.5f gap=%.5f (ratio=%.3f)\n",
                 d0.burstRms, d0.gapRms, ratio0, d100.burstRms, d100.gapRms, ratio100);
    INFO ("ratio duck0=" << ratio0 << "  ratio duck100=" << ratio100);

    REQUIRE (d0.gapRms   > 1.0e-4);                 // el wet existe (mecanismo encendido)
    REQUIRE (d100.gapRms > 1.0e-4);

    // (b1) En la ráfaga el wet cae ≥ 6 dB MÁS que sin duck (relativo: aísla el efecto del DUCK del
    // patrón natural ráfaga/hueco, idéntico en ambos casos).
    REQUIRE (ratio100 <= ratio0 * 0.5);

    // (b2) En el hueco el wet VUELVE: el nivel del hueco con DUCK 100 no queda agachado (release real).
    REQUIRE (d100.gapRms >= d0.gapRms * 0.7);
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// Cableado SYNC/FREE: la tabla de divisiones produce los espaciados correctos y FREE honra la
// perilla (el RATE nunca es inerte — honestidad-dsp.md).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST real: la division elegida fija el espaciado y FREE honra la perilla", "[real][dust]")
{
    namespace sd = dust::params::sync;

    // Helper puro: 1/16·1/8·1/4·1/2 @ 100 BPM = 150·300·600·1200 ms.
    REQUIRE (sd::rateMsForDiv (BPM, 0) == Catch::Approx (150.0f));
    REQUIRE (sd::rateMsForDiv (BPM, 1) == Catch::Approx (300.0f));
    REQUIRE (sd::rateMsForDiv (BPM, 2) == Catch::Approx (600.0f));
    REQUIRE (sd::rateMsForDiv (BPM, 3) == Catch::Approx (1200.0f));

    dust::DustProcessor proc;
    using ovni::test::setParam;

    // SYNC on: cada división cambia el rate efectivo (choice -> ms).
    setParam (proc, pid::RATESYNC, 1.0f);
    for (int idx = 0; idx < sd::kCount; ++idx)
    {
        setParam (proc, pid::RATEDIV, (float) idx / (float) (sd::kCount - 1));
        REQUIRE (proc.debugEffectiveRateMs (BPM) == Catch::Approx (sd::rateMsForDiv (BPM, idx)).margin (0.01));
    }

    // FREE: la perilla manda (700 ms del preset "Satélites").
    setParam (proc, pid::RATESYNC, 0.0f);
    if (auto* r = proc.apvts.getParameter (pid::RATE))
        r->setValueNotifyingHost (proc.apvts.getParameterRange (pid::RATE).convertTo0to1 (700.0f));
    REQUIRE (proc.debugEffectiveRateMs (BPM) == Catch::Approx (700.0f).margin (0.5));
}
