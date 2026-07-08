// DustEngineTests.cpp — batería [dustengine][dust] del MOTOR de DUST (multi-tap + banco binaural).
// Mide con el MECANISMO ENCENDIDO (banco HRIR activo, feedback real): nada de falsos verdes con
// el motor neutralizado. Etapas siguientes suman [gain]/[measure]/[alias]/etc. sobre el processor.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <chrono>
#include "template/tests/OvniTestHarness.h"   // Pink + StereoImageMeter (primitivas del sello)
#include "engine/DustEngine.h"

namespace
{
    constexpr double SR  = 48000.0;
    constexpr int    BLK = 512;

    void prep (dust::engine::DustEngine& e)
    {
        e.prepare ({ SR, (juce::uint32) BLK, 2 });
    }

    // Seno estéreo determinístico (misma señal en L/R) por fase global.
    void fillSine (juce::AudioBuffer<float>& buf, long start, float freqHz, float amp)
    {
        const double w = juce::MathConstants<double>::twoPi * (double) freqHz / SR;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const float x = amp * (float) std::sin (w * (double) (start + i));
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
    }

    bool blockFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (d[i])) return false;
        }
        return true;
    }

    double blockRms (const juce::AudioBuffer<float>& b)
    {
        double acc = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            const float* d = b.getReadPointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i) acc += (double) d[i] * (double) d[i];
        }
        return std::sqrt (acc / (2.0 * (double) b.getNumSamples()));
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 1. ESTABILIDAD — DENSIDAD 100 es el caso de feedback más caliente. La energía con señal sostenida
//    NO crece sin techo, y al cortar la entrada la cola DECAE (un lazo divergente no decae aunque
//    el limiter lo enmascare a 0.85: por eso se mide la cola, no sólo el pico).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST motor: DENSIDAD 100 + 60 s de señal -> energía acotada y cola que decae, sin NaN",
           "[dustengine][dust]")
{
    dust::engine::DustEngine eng;
    prep (eng);
    eng.setDensity01 (1.0f);     // nube "infinita": el piso de estabilidad la mantiene < 1
    eng.setRateMs (50.0f);       // lazo corto = más vueltas de feedback por segundo (peor caso)
    eng.setSpread01 (0.8f);
    eng.setVida01 (0.5f);

    juce::AudioBuffer<float> buf (2, BLK);
    bool   finite = true;
    float  peak   = 0.0f;
    double rmsEarly = 0.0, rmsLate = 0.0;
    int    nEarly = 0, nLate = 0;

    // Señal de programa BANDA ANCHA (ruido rosa). Nota: un seno con período que divide exacto al
    // RATE apila los 24 taps EN FASE (+13 dB de coherencia) y clava el limiter ~17 s: queda ACOTADO
    // por diseño (fb<1 + limiter techo 0.85) pero enmascara la medición del decay. Con material
    // descorrelacionado la normalización de energía del lazo es visible y el decay se mide.
    ovni::test::Pink pink;
    const int totalBlocks = (int) std::lround (60.0 * SR / BLK);
    for (int blk = 0; blk < totalBlocks && finite; ++blk)
    {
        for (int i = 0; i < BLK; ++i)
        {
            const float x = 0.5f * pink.next();
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        eng.process (buf, 2);
        finite = blockFinite (buf);
        peak   = juce::jmax (peak, buf.getMagnitude (0, BLK), buf.getMagnitude (1, BLK));
        const double t = (double) blk * BLK / SR;
        const double r = blockRms (buf);
        if (t >= 5.0 && t < 10.0)  { rmsEarly += r; ++nEarly; }
        if (t >= 55.0 && t < 60.0) { rmsLate  += r; ++nLate;  }
    }
    rmsEarly /= (double) juce::jmax (1, nEarly);
    rmsLate  /= (double) juce::jmax (1, nLate);

    REQUIRE (finite);
    REQUIRE (peak <= 0.86f);                       // limiter 0.85 estéreo-linked (+ epsilon)
    REQUIRE (rmsLate > 1.0e-4);                    // el mecanismo está ENCENDIDO (suena de verdad)
    REQUIRE (rmsLate <= rmsEarly * 2.0 + 1.0e-6);  // acotada: la nube no crece sin techo

    // Cola: 10 s de silencio. La energía tiene que BAJAR (fb < 1 garantizado por el piso).
    double tail1 = 0.0, tail10 = 0.0;
    const int tailBlocks = (int) std::lround (10.0 * SR / BLK);
    for (int blk = 0; blk < tailBlocks && finite; ++blk)
    {
        buf.clear();
        eng.process (buf, 2);
        finite = blockFinite (buf);
        const double t = (double) blk * BLK / SR;
        if (t >= 0.5 && t < 1.5) tail1  += blockRms (buf);
        if (t >= 9.0)            tail10 += blockRms (buf);
    }
    REQUIRE (finite);
    REQUIRE (tail10 < tail1 * 0.9);                // la cola decae: el lazo NO diverge
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 2. ESPACIADO — un impulso produce ecos detectables a múltiplos de RATE (±5%), con el banco HRIR
//    encendido (la fase mínima del anillo corre el pico apenas unos samples: entra holgado en ±5%).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST motor: impulso -> ecos a multiplos de RATE (+-5%)", "[dustengine][dust]")
{
    dust::engine::DustEngine eng;
    prep (eng);
    eng.setRateMs (250.0f);
    eng.setDensity01 (0.4f);    // default de la curaduría
    eng.setSpread01 (0.0f);     // todas las burbujas en el origen (frente): timing puro
    eng.setVida01 (0.0f);       // sin jitter ni deriva: la grilla limpia

    const int rateSamp  = (int) std::lround (0.250 * SR);
    const int total     = (int) std::lround (2.0 * SR);
    std::vector<float> env ((size_t) total, 0.0f);

    juce::AudioBuffer<float> buf (2, BLK);
    bool first = true;
    for (int pos = 0; pos < total; pos += BLK)
    {
        buf.clear();
        if (first) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); first = false; }
        eng.process (buf, 2);
        REQUIRE (blockFinite (buf));
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (1);
        for (int i = 0; i < BLK && pos + i < total; ++i)
            env[(size_t) (pos + i)] = std::abs (L[i]) + std::abs (R[i]);
    }

    for (int k = 1; k <= 5; ++k)
    {
        const int c  = k * rateSamp;
        const int w0 = c - (int) (0.4 * rateSamp);
        const int w1 = juce::jmin (total - 1, c + (int) (0.4 * rateSamp));
        int   best   = w0;
        float bestV  = -1.0f;
        for (int i = w0; i <= w1; ++i)
            if (env[(size_t) i] > bestV) { bestV = env[(size_t) i]; best = i; }

        INFO ("eco k=" << k << " esperado=" << c << " medido=" << best << " amp=" << bestV);
        REQUIRE (bestV > 0.02f);                                  // hay un eco real (mecanismo ON)
        REQUIRE (std::abs (best - c) <= (int) (0.05 * (double) c));   // ±5% del múltiplo
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 3. SPREAD — con 0 los taps colapsan al origen (corr L/R alta, BAL estable, toda la energía en un
//    bus); con 100 ocupan el campo (varianza angular medible en las energías de bus).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace
{
    struct SpreadStats
    {
        double corr = 0.0, balDb = 0.0;
        double topShare = 0.0;       // fracción del bus dominante
        int    busesOn  = 0;         // buses con > 2% de la energía
        double circR    = 0.0;       // resultante circular (1 = todo en un ángulo, ->0 = repartido)
    };

    SpreadStats measureSpread (float spread01)
    {
        dust::engine::DustEngine eng;
        prep (eng);
        eng.setRateMs (100.0f);
        eng.setDensity01 (0.5f);
        eng.setSpread01 (spread01);
        eng.setVida01 (0.0f);

        juce::AudioBuffer<float> buf (2, BLK);
        ovni::test::Pink pink;
        ovni::test::StereoImageMeter meter;

        const int warm = (int) std::lround (1.5 * SR / BLK);
        const int meas = (int) std::lround (3.0 * SR / BLK);
        for (int blk = 0; blk < warm + meas; ++blk)
        {
            for (int i = 0; i < BLK; ++i)
            {
                const float x = pink.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            eng.process (buf, 2);
            if (blk == warm) eng.dbgResetBusEnergy();   // medir el reparto ya asentado
            if (blk > warm)
                meter.addBlock (buf.getReadPointer (0), buf.getReadPointer (1), BLK);
        }

        SpreadStats st;
        const auto m = meter.finish();
        st.corr  = m.corr;
        st.balDb = m.balDb;

        double total = 0.0, sumCos = 0.0, sumSin = 0.0, top = 0.0;
        for (int b = 0; b < dust::engine::BubbleField::kNumBuses; ++b)
        {
            const double e  = (double) eng.dbgBusEnergy (b);
            const double az = (double) eng.dbgBusAzimuthRad (b);
            total  += e;
            top     = juce::jmax (top, e);
            sumCos += e * std::cos (az);
            sumSin += e * std::sin (az);
        }
        REQUIRE (total > 1.0e-9);   // el banco HRIR procesó energía de verdad
        st.topShare = top / total;
        st.circR    = std::sqrt (sumCos * sumCos + sumSin * sumSin) / total;
        for (int b = 0; b < dust::engine::BubbleField::kNumBuses; ++b)
            if ((double) eng.dbgBusEnergy (b) / total > 0.02) ++st.busesOn;
        return st;
    }
}

TEST_CASE ("DUST motor: SPREAD 0 colapsa al origen, SPREAD 100 ocupa el campo", "[dustengine][dust]")
{
    const auto s0   = measureSpread (0.0f);
    const auto s100 = measureSpread (1.0f);

    INFO ("spread0: corr=" << s0.corr << " bal=" << s0.balDb << " topShare=" << s0.topShare
          << " busesOn=" << s0.busesOn << " circR=" << s0.circR);
    INFO ("spread100: corr=" << s100.corr << " topShare=" << s100.topShare
          << " busesOn=" << s100.busesOn << " circR=" << s100.circR);

    // SPREAD 0: punto en el origen (frente) -> un solo bus, imagen correlacionada y centrada.
    REQUIRE (s0.topShare > 0.95);
    REQUIRE (s0.busesOn <= 2);
    REQUIRE (s0.corr > 0.7);
    REQUIRE (std::abs (s0.balDb) < 2.0);
    REQUIRE (s0.circR > 0.9);

    // SPREAD 100: el campo entero -> varios buses con energía y resultante circular baja.
    REQUIRE (s100.busesOn >= 5);
    REQUIRE (s100.topShare < 0.6);
    REQUIRE (s100.circR < 0.85);
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 3b. ITD POR BUS — la otra mitad del cue binaural (curaduría: "min-phase corto + ITD por bus";
//     fix de review: el dataset trae el delay field en cero y el ITD se sintetiza con Woodworth).
//     Regresión: 0 en el plano medio (frente/atrás), ~0.66 ms (≈31 samples @48k) a ±90°, y ningún
//     bus lateral sin ITD.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST motor: ITD por bus presente (0 al frente, ~0.66 ms a 90 grados)", "[dustengine][dust]")
{
    dust::engine::DustEngine eng;
    prep (eng);

    REQUIRE (eng.dbgBusItdSamples (0) == 0);   // frente (az 0): plano medio -> ITD 0

    // Bus 4 = ringDir 18 -> az 90°: Woodworth = a/c·(π/2+1) ≈ 0.656 ms ≈ 31 samples @48k.
    const int itd90 = eng.dbgBusItdSamples (4);
    INFO ("ITD @90° = " << itd90 << " samples (esperado ~31 @48k)");
    REQUIRE (itd90 >= 29);
    REQUIRE (itd90 <= 34);

    // Ningún bus claramente lateral se queda sin cue temporal.
    for (int b = 0; b < dust::engine::BubbleField::kNumBuses; ++b)
    {
        const float lat = std::abs (std::sin (eng.dbgBusAzimuthRad (b)));
        INFO ("bus " << b << " az=" << eng.dbgBusAzimuthRad (b) << " itd=" << eng.dbgBusItdSamples (b));
        if (lat > 0.2f)
            REQUIRE (eng.dbgBusItdSamples (b) > 0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 4. ANTI-CLICK — mover el ORIGIN de frente a lateral en 100 ms mientras suena, y barrer RATE
//    completo: la derivada máxima de la salida queda acotada (las ganancias de bus son rampas
//    por-sample y las lecturas fraccionales; nunca se conmuta un filtro).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST motor: mover ORIGIN (100 ms) y barrer RATE no chasquea", "[dustengine][dust]")
{
    dust::engine::DustEngine eng;
    prep (eng);
    eng.setRateMs (150.0f);
    eng.setDensity01 (0.5f);
    eng.setSpread01 (0.4f);
    eng.setVida01 (0.0f);

    juce::AudioBuffer<float> buf (2, BLK);
    long  g = 0;
    float prevL = 0.0f, prevR = 0.0f;
    bool  finite = true;

    auto runBlocks = [&] (int blocks, auto&& perBlock) -> float
    {
        float maxJump = 0.0f;
        for (int blk = 0; blk < blocks && finite; ++blk)
        {
            perBlock (blk, blocks);
            fillSine (buf, g, 220.0f, 0.4f);
            eng.process (buf, 2);
            finite = blockFinite (buf);
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            for (int i = 0; i < BLK; ++i)
            {
                maxJump = juce::jmax (maxJump, std::abs (L[i] - prevL), std::abs (R[i] - prevR));
                prevL = L[i];
                prevR = R[i];
            }
            g += BLK;
        }
        return maxJump;
    };

    // Asentar la nube (todos los nacimientos hechos) y descartar ese tramo (prev sigue continuo).
    runBlocks ((int) (3.0 * SR / BLK), [] (int, int) {});

    // Línea de base quieta (1 s): la derivada natural del material tonal eco-ado.
    const float steady = runBlocks ((int) (1.0 * SR / BLK), [] (int, int) {});

    // ORIGIN de frente (0,1) a lateral derecho (1,0) en 100 ms, sonando.
    const int moveBlocks = juce::jmax (1, (int) std::lround (0.100 * SR / BLK));
    const float moved = runBlocks (moveBlocks + (int) (0.3 * SR / BLK), [&] (int blk, int) {
        const float t = juce::jlimit (0.0f, 1.0f, (float) blk / (float) moveBlocks);
        eng.setOrigin (t, 1.0f - t);
    });

    INFO ("steady=" << steady << " moved=" << moved);
    REQUIRE (finite);
    REQUIRE (moved <= steady * 3.0f + 0.02f);   // sin saltos: rampa por-sample real

    // Barrido COMPLETO de RATE (150 -> 20 -> 2000 ms en ~6 s, como un drag de usuario). El
    // re-pitcheo tipo cinta de las lecturas fraccionales es contenido legítimo (sube la derivada);
    // un click duro saltaría MUY por encima del techo.
    const int sweepBlocks = (int) (6.0 * SR / BLK);
    const float swept = runBlocks (sweepBlocks, [&] (int blk, int blocks) {
        const float t = (float) blk / (float) blocks;            // 0..1
        const float ms = (t < 0.33f)
            ? juce::jmap (t / 0.33f, 150.0f, 20.0f)
            : juce::jmap ((t - 0.33f) / 0.67f, 20.0f, 2000.0f);
        eng.setRateMs (ms);
    });

    INFO ("swept=" << swept);
    REQUIRE (finite);
    REQUIRE (swept <= 0.45f);                   // acotado (techo del limiter 0.85; un click lo pasaría)
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// 5. DENORMALS — cola decayendo 30 s: el costo del process se mantiene estable (flush-to-zero por
//    ScopedNoDenormals). Sin la protección, la cola subnormal multiplica el costo por >10.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST motor: cola de 30 s sin explosion de CPU (denormals)", "[dustengine][dust]")
{
    dust::engine::DustEngine eng;
    prep (eng);
    eng.setRateMs (80.0f);
    eng.setDensity01 (0.7f);
    eng.setSpread01 (0.6f);
    eng.setVida01 (0.3f);

    juce::AudioBuffer<float> buf (2, BLK);
    long g = 0;

    // 1 s de señal para cargar el lazo.
    for (int blk = 0; blk < (int) (1.0 * SR / BLK); ++blk)
    {
        fillSine (buf, g, 220.0f, 0.5f);
        eng.process (buf, 2);
        g += BLK;
    }

    // 29 s de silencio (la cola decae hacia el rango subnormal) cronometrados por tramos de ~5 s.
    const int chunkBlocks = (int) (5.0 * SR / BLK);
    double chunkSec[6] = {};
    bool finite = true;
    for (int chunk = 0; chunk < 6 && finite; ++chunk)
    {
        const auto t0 = std::chrono::high_resolution_clock::now();
        for (int blk = 0; blk < chunkBlocks && finite; ++blk)
        {
            buf.clear();
            eng.process (buf, 2);
            finite = blockFinite (buf);
        }
        chunkSec[chunk] = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    }

    const double audioSec = (double) chunkBlocks * BLK / SR;
    std::printf ("DUSTENGINE_TAIL_CPU_PCT first=%.3f last=%.3f (informativo; el [bench] oficial llega con el processor)\n",
                 100.0 * chunkSec[0] / audioSec, 100.0 * chunkSec[5] / audioSec);

    REQUIRE (finite);
    for (int chunk = 1; chunk < 6; ++chunk)
    {
        INFO ("tramo " << chunk << " = " << chunkSec[chunk] << " s (tramo 0 = " << chunkSec[0] << " s)");
        REQUIRE (chunkSec[chunk] <= chunkSec[0] * 2.5 + 0.010);
    }
}
