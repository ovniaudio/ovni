// [telescope][stereo] — el módulo Stereo de BANDA ANCHA (spec §5.3): las cinco sumas del hop
// (ΣLL ΣRR ΣLR ΣMM ΣSS), la ventana deslizante de 100/300/1000 ms, y los cuatro números que publica:
//
//   CORR     = ΣLR / √(ΣLL·ΣRR)                          +1 mono · 0 decorrelacionado · -1 fuera de fase
//   WIDTH    = √(ΣSS/ΣMM)          M=(L+R)/2, S=(L-R)/2  0 = mono · 1 = independientes · ↑ = side manda
//   BAL_dB   = 10·log10(ΣRR/ΣLL)                         + = R más fuerte, - = L más fuerte
//   MONOLOSS = 10·log10(ΣMM) - 10·log10((ΣLL+ΣRR)/2)     0 si L=R · -3.01 si independientes · -∞ si L=-R
//
// Es la matemática de ~/PLUGINS/orbita/tests/StereoMeasure.cpp:74-80 con el MISMO ruido rosa
// (tests/TestSignals.h), así los números de TELESCOPE se comparan directamente con los de ÓRBITA/PULSAR.
//
// Además: los buffers del osciloscopio/goniómetro (ScopeFrame) y la independencia del tamaño de bloque
// medida sobre el processor REAL (mismo esquema que casa-4 del medidor de loudness).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/modules/Stereo.h"

using telescope::Stereo;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;

enum class Case { same, inverted, independent, leftOnly, silence };

// Alimenta el módulo con `seconds` de ruido rosa de la casa según el caso, en bloques de `blockSize`.
void feedPink (Stereo& m, Case c, double seconds, double sr = kSr, int blockSize = 512)
{
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    std::vector<float> L ((size_t) blockSize, 0.0f), R ((size_t) blockSize, 0.0f);
    const auto total = (long long) std::llround (seconds * sr);

    for (long long done = 0; done < total; )
    {
        const int k = (int) std::min ((long long) blockSize, total - done);
        for (int i = 0; i < k; ++i)
        {
            const float x = a.next();
            const float y = b.next();
            switch (c)
            {
                case Case::same:        L[(size_t) i] = x;    R[(size_t) i] =  x;    break;
                case Case::inverted:    L[(size_t) i] = x;    R[(size_t) i] = -x;    break;
                case Case::independent: L[(size_t) i] = x;    R[(size_t) i] =  y;    break;
                case Case::leftOnly:    L[(size_t) i] = x;    R[(size_t) i] =  0.0f; break;
                case Case::silence:     L[(size_t) i] = 0.0f; R[(size_t) i] =  0.0f; break;
            }
        }
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// La tabla que lee la auditora.
void row (const char* name, const Stereo::Result& r)
{
    std::printf ("STEREO[%-13s] corr=%+7.4f  width=%7.4f  bal=%+8.3f dB  mono=%+8.3f dB  (W=%.2f s)\n",
                 name, r.corr, r.width, r.balanceDb, r.monoLossDb, r.windowSeconds);
}

Stereo::Result measure (Case c, double seconds = 10.0, int windowMs = 300, double sr = kSr, int block = 512)
{
    Stereo m;
    m.prepare (sr);
    m.setWindowMs (windowMs);
    feedPink (m, c, seconds, sr, block);
    return m.result();
}
}

// ============================================================================================ casos base
TEST_CASE ("telescope: L = R es mono perfecto", "[telescope][stereo]")
{
    const auto r = measure (Case::same);
    row ("L=R", r);
    REQUIRE_THAT (r.corr,       Catch::Matchers::WithinAbs (1.0f, 0.001f));
    REQUIRE_THAT (r.width,      Catch::Matchers::WithinAbs (0.0f, 0.001f));
    REQUIRE_THAT (r.balanceDb,  Catch::Matchers::WithinAbs (0.0f, 0.01f));
    REQUIRE_THAT (r.monoLossDb, Catch::Matchers::WithinAbs (0.0f, 0.01f));
}

TEST_CASE ("telescope: L = -R cancela al monoficar", "[telescope][stereo]")
{
    const auto r = measure (Case::inverted);
    row ("L=-R", r);
    REQUIRE_THAT (r.corr, Catch::Matchers::WithinAbs (-1.0f, 0.001f));
    REQUIRE (r.monoLossDb <= -60.0f);            // -∞ clampeado al piso
    REQUIRE (std::isfinite (r.monoLossDb));
    REQUIRE (std::isfinite (r.width));
}

TEST_CASE ("telescope: dos fuentes independientes dan corr 0, width 1 y -3 dB al monoficar", "[telescope][stereo]")
{
    const auto r = measure (Case::independent);
    row ("independientes", r);
    REQUIRE_THAT (r.corr,       Catch::Matchers::WithinAbs (0.0f, 0.05f));
    REQUIRE_THAT (r.width,      Catch::Matchers::WithinAbs (1.0f, 0.05f));
    REQUIRE_THAT (r.monoLossDb, Catch::Matchers::WithinAbs (-3.0f, 0.3f));
}

TEST_CASE ("telescope: sólo L da balance al piso y width 1", "[telescope][stereo]")
{
    const auto r = measure (Case::leftOnly);
    row ("sólo L", r);
    REQUIRE_THAT (r.corr,  Catch::Matchers::WithinAbs (0.0f, 0.0001f));   // ΣLR = 0 por definición
    REQUIRE (r.balanceDb <= -59.0f);
    // Con R = 0: M = L/2 y S = L/2 → ΣSS = ΣMM → width exactamente 1.
    REQUIRE_THAT (r.width, Catch::Matchers::WithinAbs (1.0f, 0.01f));
    REQUIRE (std::isfinite (r.balanceDb));
}

TEST_CASE ("telescope: en silencio no hay medición y nada es NaN", "[telescope][stereo]")
{
    const auto r = measure (Case::silence);
    row ("silencio", r);
    REQUIRE (r.corr  == 0.0f);
    REQUIRE (r.width == 0.0f);
    REQUIRE (std::isfinite (r.balanceDb));
    REQUIRE (std::isfinite (r.monoLossDb));
    REQUIRE_FALSE (r.hasSignal);
}

// ============================================================================================ la ventana
// Señal que alterna 200 ms L=R / 200 ms L=-R. Con la ventana LARGA los dos tramos se promedian y la
// correlación se va al medio; con la ventana CORTA cada hop cae entero dentro de un tramo y la correlación
// va de +1 a -1. Es la prueba de que la ventana es la que dice ser, no un número decorativo.
TEST_CASE ("telescope: la ventana de integración es la que dice", "[telescope][stereo]")
{
    const auto sweep = [] (int windowMs)
    {
        Stereo m;
        m.prepare (kSr);
        m.setWindowMs (windowMs);

        Pink p { telescope::test::kPinkSeedA };
        const int hop = m.hopSamples();
        std::vector<float> L ((size_t) hop), R ((size_t) hop);
        std::vector<float> corrs;

        for (int h = 0; h < 40; ++h)                       // 4 s = 10 alternancias de 200 ms
        {
            const bool inverted = ((h / 2) % 2) == 1;      // 200 ms = 2 hops
            for (int i = 0; i < hop; ++i)
            {
                const float x = p.next();
                L[(size_t) i] = x;
                R[(size_t) i] = inverted ? -x : x;
            }
            m.process (L.data(), R.data(), hop);
            if (h >= 10) corrs.push_back (m.result().corr);   // saltear el llenado de la ventana larga
        }
        return corrs;
    };

    const auto slow = sweep (1000);
    float slowWorst = 0.0f;
    for (const float c : slow) slowWorst = std::max (slowWorst, std::abs (c));
    std::printf ("STEREO[ventana 1000ms] |corr| peor = %.4f  (criterio < 0.30)\n", slowWorst);
    REQUIRE (slowWorst < 0.30f);

    const auto fast = sweep (100);
    int pos = 0, neg = 0, medio = 0;
    for (const float c : fast)
    {
        if (c > 0.9f) ++pos; else if (c < -0.9f) ++neg; else ++medio;
    }
    std::printf ("STEREO[ventana 100ms ] hops > +0.9 = %d   < -0.9 = %d   en el medio = %d\n", pos, neg, medio);
    REQUIRE (pos > 0);
    REQUIRE (neg > 0);
    REQUIRE (medio == 0);
}

// ======================================================================================= scope (xy / osc)
TEST_CASE ("telescope: el ScopeFrame trae 40 ms de onda y un trigger en cruce ascendente", "[telescope][stereo]")
{
    Stereo m;
    m.prepare (kSr);

    // Seno de 100 Hz idéntico en L y R → M = el mismo seno. Período 480 muestras: en los primeros 20 ms
    // (960 muestras) hay cruces ascendentes de sobra.
    const int hop = m.hopSamples();
    std::vector<float> L ((size_t) hop), R ((size_t) hop);
    long long n = 0;
    for (int h = 0; h < 5; ++h)
    {
        for (int i = 0; i < hop; ++i, ++n)
        {
            const auto v = (float) std::sin (2.0 * juce::MathConstants<double>::pi * 100.0 * (double) n / kSr);
            L[(size_t) i] = v;
            R[(size_t) i] = v;
        }
        m.process (L.data(), R.data(), hop);
    }

    const auto& s = m.scope();
    const int expectedOsc = juce::jmin (telescope::ScopeFrame::kMaxOsc, (int) std::llround (0.040 * kSr));
    std::printf ("STEREO[scope] xyCount=%d  oscCount=%d (esperado %d)  trigger=%d\n",
                 s.xyCount, s.oscCount, expectedOsc, s.trigger);

    REQUIRE (s.oscCount == expectedOsc);                       // 40 ms a la SR (1920 @ 48 k)
    REQUIRE (s.xyCount == telescope::ScopeFrame::kMaxXy);      // el hop (4800) decimado a 2048
    REQUIRE (s.trigger > 0);
    REQUIRE (s.trigger < (int) std::llround (0.020 * kSr));    // dentro de los primeros 20 ms
    REQUIRE (s.oscM[(size_t) s.trigger - 1] <  0.0f);          // cruce ASCENDENTE: anterior < 0 <= actual
    REQUIRE (s.oscM[(size_t) s.trigger]     >= 0.0f);

    // El goniómetro dibuja pares (L,R) reales del hop, no una copia del osciloscopio.
    for (int i = 0; i < s.xyCount; ++i)
    {
        REQUIRE (std::isfinite (s.xyL[(size_t) i]));
        REQUIRE (std::abs (s.xyL[(size_t) i] - s.xyR[(size_t) i]) < 1.0e-6f);   // L = R en esta señal
    }
}

// ============================================================================== independencia del bloque
// Mismo esquema que casa-4 del medidor: la misma señal tiene que dar los MISMOS números al bit venga en
// bloques de 1 o de 4096. Va por el processor REAL (el hop lo arma el AnalysisThread, no el módulo).
TEST_CASE ("telescope: los números de estéreo no dependen del tamaño de bloque", "[telescope][stereo]")
{
    constexpr int kSamples = 96000;   // 2 s exactos = 20 hops enteros a 48 k

    const auto run = [] (int block)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, block);
        proc.setEnabledModules (telescope::kStereo);

        Pink p { telescope::test::kPinkSeedA };
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        for (int done = 0; done < kSamples; done += block)
        {
            const int k = juce::jmin (block, kSamples - done);
            buf.clear();
            for (int i = 0; i < k; ++i)
            {
                const float x = p.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);   // L = R: números exactos, sin dispersión estadística
            }
            proc.processBlock (buf, midi);
        }

        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 1.95; }, 4000));
        const auto f = proc.analysis().read();
        proc.releaseResources();
        return f;
    };

    const auto ref = run (512);
    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto f = run (block);
        INFO ("bloque " << block);
        REQUIRE (f.corr       == ref.corr);
        REQUIRE (f.width      == ref.width);
        REQUIRE (f.balanceDb  == ref.balanceDb);
        REQUIRE (f.monoLossDb == ref.monoLossDb);
    }
    std::printf ("STEREO[bloque 1/7/64/4096] corr=%+.6f width=%.6f bal=%+.4f mono=%+.4f  (idénticos al bit)\n",
                 ref.corr, ref.width, ref.balanceDb, ref.monoLossDb);
}
