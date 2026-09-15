// [telescope][dyn] — los datos que come la lente DYNAMICS (spec §2 fila 2):
//
//   PSR  = true-peak máximo de los últimos 3 s  -  short-term      (la lectura "en vivo")
//   PLR  = true-peak máximo desde el reset      -  integrado       (AES TD1004: peak-to-loudness ratio)
//   histograma de short-term desde el reset: 61 bins de 1 LU (-60…0)
//   eventos de clip por encima de un umbral en dBTP (default -1.0), con su ring de 1 Hz para la línea
//   de tiempo de 10 min
//
// Con un seno estable los DOS valen 0.0: para un seno de 997 Hz el true-peak en dBTP y el loudness en
// LUFS dan el mismo número, porque el K-weighting aporta ahí exactamente los +0.691 dB que la constante
// de BS.1770 resta. Es la identidad que verificó el prompt 48 y es la que hace que este test signifique algo.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/modules/Loudness.h"

using telescope::Loudness;

namespace
{
constexpr double kSr    = 48000.0;
constexpr double kPi    = 3.14159265358979323846;
constexpr float  kBurstPeakDb = -0.5f;

void row (const char* name, const char* what, double value)
{
    std::printf ("DYN[%-16s] %-14s = %+9.3f\n", name, what, value);
}

// ---- señal de RÁFAGAS: 10 ráfagas de 50 ms de seno 997 Hz a -0.5 dBFS pico, una por segundo, sobre un
// lecho de ruido rosa a -40 dBFS. Las ráfagas llevan 1 ms de subida y de bajada: un corte abrupto mete
// un click de banda ancha cuyo pico REAL puede pasarse de la muestra, y entonces el test mediría el
// click y no la ráfaga. Todo en aritmética entera para que no dependa de dónde cae un fmod.
struct Bursts
{
    explicit Bursts (double sampleRate) : sr (sampleRate) {}

    float next()
    {
        const long long inSecond   = n % (long long) std::llround (sr);
        const long long burstStart = (long long) std::llround (0.20 * sr);
        const long long burstLen   = (long long) std::llround (0.05 * sr);
        const long long fade       = (long long) std::llround (0.001 * sr);

        float v = 0.01f * bed.next();     // lecho: pico <= 0.01 = -40 dBFS

        const long long k = inSecond - burstStart;
        if (k >= 0 && k < burstLen)
        {
            double env = 1.0;
            if (k < fade)                 env = 0.5 - 0.5 * std::cos (kPi * (double) k / (double) fade);
            else if (k >= burstLen - fade) env = 0.5 - 0.5 * std::cos (kPi * (double) (burstLen - k) / (double) fade);
            v += (float) (std::pow (10.0, kBurstPeakDb / 20.0) * env
                          * std::sin (2.0 * kPi * 997.0 * (double) n / sr));
        }
        ++n;
        return v;
    }

    double sr;
    long long n = 0;
    telescope::test::Pink bed { telescope::test::kPinkSeedA };
};

// Empuja `samples` de un seno de 997 Hz a `peakDb` por el módulo, en bloques de `block`.
void feedSine (Loudness& m, double sr, long long samples, double peakDb, long long& n, int block = 512)
{
    const double amp = std::pow (10.0, peakDb / 20.0);
    std::vector<float> L ((size_t) block), R ((size_t) block);
    for (long long done = 0; done < samples; )
    {
        const int k = (int) std::min ((long long) block, samples - done);
        for (int i = 0; i < k; ++i, ++n)
            L[(size_t) i] = R[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * 997.0 * (double) n / sr));
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// Empuja la señal de ráfagas por el PROCESSOR real, en bloques de `block`, exactamente `samples` muestras.
//
// CON FRENO: el AnalysisBus guarda 8 s a 48 kHz, así que soltarle 10 s de una a toda velocidad lo
// DESBORDA y el análisis pierde muestras — y con ellas, ráfagas enteras. En un DAW esto no pasa (el audio
// llega en tiempo real); en un test que empuja a la velocidad de la CPU, sí. Se deja al worker no más de
// 2 s atrás. El test verifica después que droppedSamples == 0, que es lo que hace válida la cuenta.
void feedBursts (telescope::TelescopeProcessor& proc, long long samples, int block)
{
    Bursts sig { kSr };
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    long long pushed = 0;

    for (long long done = 0; done < samples; done += block)
    {
        const int k = (int) std::min ((long long) block, samples - done);
        buf.clear();
        for (int i = 0; i < k; ++i)
        {
            const float v = sig.next();
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
        pushed += k;

        const double pushedSec = (double) pushed / kSr;
        if (pushedSec - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushedSec - proc.analysis().read().timeSeconds <= 1.0; }, 5000));
    }
}
}

// ================================================================================= seno estable: 0 y 0
TEST_CASE ("telescope: con un seno estable PSR y PLR valen 0", "[telescope][dyn]")
{
    Loudness m;
    m.prepare (kSr);
    long long n = 0;
    feedSine (m, kSr, (long long) (10.0 * kSr), -6.0, n);

    const auto r = m.result();
    row ("seno -6 dBFS", "TP max", r.truePeakMax);
    row ("seno -6 dBFS", "TP hop",  r.truePeakHop);
    row ("seno -6 dBFS", "short-term", r.shortTerm);
    row ("seno -6 dBFS", "integrado",  r.integrated);
    row ("seno -6 dBFS", "PSR", r.psr);
    row ("seno -6 dBFS", "PLR", r.plr);

    REQUIRE (r.psrValid);
    REQUIRE (r.plrValid);
    REQUIRE_THAT (r.psr, Catch::Matchers::WithinAbs (0.0f, 0.15f));
    REQUIRE_THAT (r.plr, Catch::Matchers::WithinAbs (0.0f, 0.15f));
    REQUIRE_THAT (r.truePeakHop, Catch::Matchers::WithinAbs (-6.0f, 0.2f));
}

// ================================================================================= eventos de clip
TEST_CASE ("telescope: cuenta un evento de clip por ráfaga, y el umbral manda", "[telescope][dyn]")
{
    const auto run = [] (float thresholdDbtp)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, 512);
        proc.setClipThresholdDbtp (thresholdDbtp);

        feedBursts (proc, (long long) (10.0 * kSr), 512);
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 9.95; }, 5000));

        const auto f = proc.analysis().read();
        REQUIRE (f.droppedSamples == 0u);   // si el bus descartó, la cuenta de ráfagas no vale nada
        std::vector<juce::uint32> ring;
        proc.clipHistory().copyLatest (ring, telescope::ClipHistory::kCapacity);
        proc.releaseResources();
        return std::pair { f, ring };
    };

    const auto [strict, ringStrict] = run (-1.0f);
    row ("ráfagas u=-1.0", "clipEvents", (double) strict.clipEvents);
    row ("ráfagas u=-1.0", "TP max",     strict.loudness.truePeakMax);
    REQUIRE (strict.clipEvents == 10u);

    // El ring de 1 Hz: exactamente 10 segundos con 1 evento y ninguno con más.
    int secondsWithOne = 0, secondsWithMore = 0;
    for (const auto v : ringStrict) { if (v == 1u) ++secondsWithOne; else if (v > 1u) ++secondsWithMore; }
    std::printf ("DYN[ráfagas u=-1.0 ] ring 1 Hz: %d segundos con 1 evento, %d con más, %d segundos en total\n",
                 secondsWithOne, secondsWithMore, (int) ringStrict.size());
    REQUIRE (secondsWithOne == 10);
    REQUIRE (secondsWithMore == 0);

    // Con el techo en 0 dBTP la misma señal (pico -0.5 dBFS) no clipea NADA.
    const auto [loose, ringLoose] = run (0.0f);
    row ("ráfagas u=0.0", "clipEvents", (double) loose.clipEvents);
    REQUIRE (loose.clipEvents == 0u);
    for (const auto v : ringLoose) REQUIRE (v == 0u);

    // PLR sobre la misma señal: MUY por encima del 0.0 del seno estable, que es el punto de la métrica.
    //
    // El enunciado del prompt 49 pedía PLR > 30 dB "porque el integrado lo domina el lecho". No puede
    // pasar, y no por un error de implementación sino por cómo funciona BS.1770: el integrado tiene
    // COMPUERTA RELATIVA a -10 LU, que existe justamente para sacar del promedio las partes calladas.
    // Con este material: los bloques de 400 ms que contienen una ráfaga miden ~-9.5 LUFS y los de puro
    // lecho ~-51; la media de los que pasan la compuerta absoluta da ~-13.4, la relativa queda en -23.4,
    // y los -51 quedan AFUERA. El integrado termina siendo el de las ráfagas (~-9.5), no el del lecho, y
    // PLR = -0.5 - (-9.5) ≈ 9 dB. Para que diera 30 habría que romper la compuerta del estándar.
    // Se afirma lo que la métrica sí dice — que con material de picos el PLR se despega del 0.0 del seno
    // — y se IMPRIME el número medido.
    row ("ráfagas u=-1.0", "integrado", strict.loudness.integrated);
    row ("ráfagas u=-1.0", "PLR",       strict.plr);
    REQUIRE (strict.plrValid);
    REQUIRE (strict.plr > 5.0f);
}

// ================================================================================= histograma
TEST_CASE ("telescope: el histograma de short-term acumula un bin por hop válido", "[telescope][dyn]")
{
    Loudness m;
    m.prepare (kSr);
    long long n = 0;
    feedSine (m, kSr, (long long) (20.0 * kSr), -20.0, n);
    feedSine (m, kSr, (long long) (20.0 * kSr), -30.0, n);

    const auto r = m.result();
    const int bin20 = 60 - 20, bin30 = 60 - 30;   // el bin i está centrado en (i - 60) LUFS

    juce::uint32 total = 0;
    for (const auto c : r.histogram) total += c;

    std::printf ("DYN[histograma    ] bin -20 = %u   bin -30 = %u   Σ bins = %u   hops con S válido = %lld\n",
                 r.histogram[bin20], r.histogram[bin30], total, r.shortTermHops);

    REQUIRE (r.histogram[bin20] > 100u);
    REQUIRE (std::abs ((int) r.histogram[bin20] - (int) r.histogram[bin30]) <= 3);
    REQUIRE (total == (juce::uint32) r.shortTermHops);
}

// ================================================================================= reset
TEST_CASE ("telescope: el reset borra PSR, PLR, histograma y clips", "[telescope][dyn]")
{
    Loudness m;
    m.prepare (kSr);
    m.setClipThresholdDbtp (-1.0f);
    long long n = 0;
    feedSine (m, kSr, (long long) (10.0 * kSr), -0.2, n);   // por encima del umbral: clipea

    REQUIRE (m.result().psrValid);
    REQUIRE (m.result().plrValid);
    REQUIRE (m.result().clipEvents > 0u);

    m.reset();
    const auto r = m.result();
    REQUIRE_FALSE (r.psrValid);
    REQUIRE_FALSE (r.plrValid);
    REQUIRE (r.clipEvents == 0u);
    for (const auto c : r.histogram) REQUIRE (c == 0u);
}

// ================================================================================= bloque
TEST_CASE ("telescope: PSR, PLR y los clips no dependen del tamaño de bloque", "[telescope][dyn]")
{
    constexpr long long kSamples = (long long) (4.0 * kSr);   // 4 s = 40 hops enteros, 4 ráfagas

    const auto run = [] (int block)
    {
        telescope::TelescopeProcessor proc;
        proc.prepareToPlay (kSr, block);
        proc.setClipThresholdDbtp (-1.0f);
        feedBursts (proc, kSamples, block);
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 3.95; }, 5000));
        const auto f = proc.analysis().read();
        proc.releaseResources();
        return f;
    };

    const auto ref = run (512);
    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto f = run (block);
        INFO ("bloque " << block);
        REQUIRE (f.psr        == ref.psr);
        REQUIRE (f.plr        == ref.plr);
        REQUIRE (f.clipEvents == ref.clipEvents);
        REQUIRE (f.truePeakHop == ref.truePeakHop);
    }
    std::printf ("DYN[bloque 1/7/64/4096] PSR=%+.6f  PLR=%+.6f  clips=%u  (idénticos al bit)\n",
                 ref.psr, ref.plr, ref.clipEvents);
}
