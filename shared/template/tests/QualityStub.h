#pragma once

// ========================================================================================================
// QualityStub.h — TEST_CASE [quality] PARAMETRIZADO: la señal NUNCA pierde calidad. Cuatro gates de
// transparencia que un plugin "calidad UAD" tiene que pasar sí o sí:
//   1. PISO DE SILENCIO: tras cargar la cola y callar la entrada, la salida MUERE de verdad
//      (sin cola zombie, sin self-noise, sin denormals audibles). Gate: < −80 dBFS al final.
//   2. DC OFFSET: un seno (sin DC) no genera DC a la salida. Gate: |media| < −60 dB rel señal.
//   3. NULL DEL DRY (MIX=0): con la perilla en 0 el plugin NO toca la señal — la salida es el dry
//      (alineado por la latencia reportada). Gate: residuo < −60 dB rel entrada. Caza filtros,
//      ganancias o color escondidos en el camino "seco".
//   4. LINEALIDAD DE NIVEL: −20 dB de entrada = exactamente −20 dB de salida wet (±0.5 dB) a niveles
//      nominales. Caza saturación/compresión NO declarada (los limiters del sello viven arriba, no acá).
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG.
//   OVNI_QUALITY_WET(proc)   — macros representativos del wet (sin freeze). Puede ser vacío (defaults).
//   OVNI_QUALITY_SHORT(proc) — variante de cola CORTA para el piso de silencio (reverbs largas).
//   Opcional OVNI_QUALITY_MIX_ID si el id del mix no es "mix".
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "QualityStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG"
#endif
#ifndef OVNI_QUALITY_MIX_ID
  #define OVNI_QUALITY_MIX_ID "mix"
#endif
#ifndef OVNI_QUALITY_WET
  #define OVNI_QUALITY_WET(proc)   do {} while (0)
#endif
#ifndef OVNI_QUALITY_SHORT
  #define OVNI_QUALITY_SHORT(proc) OVNI_QUALITY_WET (proc)
#endif

namespace ovni_quality_detail
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

// Ruido blanco determinístico (LCG, LA MISMA semilla en cada corrida → linealidad comparable).
struct WhiteQ
{
    std::uint32_t s = 0x2545F491u;
    float next() noexcept { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

inline double db (double x) noexcept { return 20.0 * std::log10 (x + 1.0e-30); }
} // namespace ovni_quality_detail

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": calidad de senal (piso/DC/null mix0/linealidad)", "[quality]" OVNI_PLUGIN_TAG)
{
    using namespace ovni_quality_detail;
    using ovni::test::setParam;

    // ── 1. PISO DE SILENCIO: cargar 0.3 s de ruido, callar 10 s, medir el ÚLTIMO segundo. ──────────
    double floorDb = 0.0;
    {
        OVNI_PLUGIN_PROCESSOR proc;
        OVNI_QUALITY_SHORT (proc);
        setParam (proc, OVNI_QUALITY_MIX_ID, 1.0f);
        proc.prepareToPlay (SR, N);
        WhiteQ w;
        for (int blk = 0; blk < (int) std::ceil (0.3 * SR / N); ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i) { const float x = 0.4f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
            proc.processBlock (buf, midi);
        }
        const int tailBlocks = (int) std::ceil (10.0 * SR / N);
        const int measFrom   = tailBlocks - (int) std::ceil (1.0 * SR / N);
        double acc = 0.0; long cnt = 0;
        for (int blk = 0; blk < tailBlocks; ++blk)
        {
            juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
            proc.processBlock (z, midi);
            if (blk >= measFrom)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = z.getReadPointer (ch); for (int i = 0; i < N; ++i) { acc += (double) d[i] * d[i]; ++cnt; } }
        }
        floorDb = db (std::sqrt (acc / juce::jmax (1L, cnt)));
    }

    // ── 2. DC OFFSET: seno 468.75 Hz (períodos enteros por ventana) 5 s, media de los últimos 3 s. ──
    double dcDb = 0.0;
    {
        OVNI_PLUGIN_PROCESSOR proc;
        OVNI_QUALITY_WET (proc);
        setParam (proc, OVNI_QUALITY_MIX_ID, 1.0f);
        proc.prepareToPlay (SR, N);
        const int total = (int) std::ceil (5.0 * SR / N);
        const int from  = (int) std::ceil (2.0 * SR / N);
        double sum = 0.0; long cnt = 0; long g = 0;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i, ++g)
            {
                const float x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 468.75 * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            if (blk >= from)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { sum += (double) d[i]; ++cnt; } }
        }
        dcDb = db (std::abs (sum / juce::jmax (1L, cnt)) / 0.25);
    }

    // ── 3. NULL DEL DRY (MIX=0): ruido por el processor, restar el dry alineado por la latencia. ───
    double nullDb = 0.0; int latency = 0;
    {
        OVNI_PLUGIN_PROCESSOR proc;
        OVNI_QUALITY_WET (proc);
        setParam (proc, OVNI_QUALITY_MIX_ID, 0.0f);
        proc.prepareToPlay (SR, N);
        latency = proc.getLatencySamples();
        WhiteQ w;
        const int total = (int) std::ceil (4.0 * SR / N);
        std::vector<float> inHist, outHist;
        inHist.reserve ((size_t) total * N); outHist.reserve ((size_t) total * N);
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i) { const float x = 0.25f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); inHist.push_back (x); }
            proc.processBlock (buf, midi);
            auto* d = buf.getReadPointer (0);
            for (int i = 0; i < N; ++i) outHist.push_back (d[i]);
        }
        const long from = (long) std::ceil (1.5 * SR);            // saltar el settle de los smoothers
        double res = 0.0, ref = 0.0; long cnt = 0;
        for (long n = from; n < (long) outHist.size(); ++n)
        {
            const long m = n - (long) latency;
            if (m < 0 || m >= (long) inHist.size()) continue;
            const double e = (double) outHist[(size_t) n] - (double) inHist[(size_t) m];
            res += e * e; ref += (double) inHist[(size_t) m] * inHist[(size_t) m]; ++cnt;
        }
        nullDb = db (std::sqrt (res / juce::jmax (1L, cnt)) / (std::sqrt (ref / juce::jmax (1L, cnt)) + 1.0e-30));
    }

    // ── 4. LINEALIDAD: mismo ruido a −26 dBFS y a −46 dBFS → el wet debe bajar EXACTAMENTE 20 dB. ──
    double linDeltaDb = 0.0;
    {
        auto wetRms = [] (float amp) -> double
        {
            OVNI_PLUGIN_PROCESSOR proc;
            OVNI_QUALITY_WET (proc);
            setParam (proc, OVNI_QUALITY_MIX_ID, 1.0f);
            proc.prepareToPlay (SR, N);
            WhiteQ w;   // misma semilla → misma señal, sólo cambia el nivel
            const int total = (int) std::ceil (4.0 * SR / N);
            const int from  = (int) std::ceil (1.5 * SR / N);
            double acc = 0.0; long cnt = 0;
            for (int blk = 0; blk < total; ++blk)
            {
                juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
                for (int i = 0; i < N; ++i) { const float x = amp * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
                proc.processBlock (buf, midi);
                if (blk >= from)
                    for (int ch = 0; ch < 2; ++ch)
                    { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { acc += (double) d[i] * d[i]; ++cnt; } }
            }
            return std::sqrt (acc / juce::jmax (1L, cnt));
        };
        const double hi = wetRms (0.05f);
        const double lo = wetRms (0.005f);
        linDeltaDb = db (hi) - db (lo);   // esperado: +20.0 exacto si el camino es lineal
    }

    std::printf ("QUALITY[" OVNI_PLUGIN_SLUG "] floor=%.1f dBFS  DC=%.1f dB  null(mix0)=%.1f dB (lat=%d)  lin=%.2f dB (esp. 20.00)\n",
                 floorDb, dcDb, nullDb, latency, linDeltaDb);

    REQUIRE (std::isfinite (floorDb));
    REQUIRE (floorDb < -80.0);                    // la cola MUERE: sin zombie/self-noise/denormals
    REQUIRE (dcDb    < -60.0);                    // sin DC generado
    REQUIRE (nullDb  < -60.0);                    // MIX=0 = dry intacto (alineado por PDC)
    REQUIRE (std::abs (linDeltaDb - 20.0) < 0.5); // lineal a niveles nominales (sin saturación oculta)
}
