// [telescope][spectrogram] — la COLUMNA del espectrograma y el ring donde vive la historia.
//
// La columna son 512 filas log-espaciadas de 20 Hz a 20 kHz con el dB mapeado a 0-255 sobre [-rango, 0].
// Lo que se verifica acá:
//
//   BARRIDO      un barrido logarítmico de 100 Hz a 10 kHz tiene que SUBIR fila a fila, columna tras
//                columna (Spearman > 0.99 y ≥ 95 % de pares consecutivos no decrecientes).
//   SILENCIO     todo en 0. Un espectrograma que "nieva" sobre silencio es un espectrograma que miente.
//   ESCALA       un tono de -20 dBFS con rango 90 vale round(255·(1 - 20/90)) = 198 en su fila.
//   ANILLO       tras 2× la historia, count == capacity y la columna 0 es la de hace exactamente
//                `historySec` segundos: un tono que cambia cada segundo la delata.
//   DETERMINISMO dos corridas de la misma señal dan rings IDÉNTICOS byte a byte.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "TestSignals.h"
#include "analysis/SpectrogramRing.h"
#include "analysis/modules/Spectrum.h"

using telescope::SpectrogramRing;
using telescope::Spectrum;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

Spectrum::Settings baseSettings()
{
    Spectrum::Settings s;
    s.fftOrder       = 12;
    s.window         = Spectrum::hann;
    s.overlapIndex   = 1;                 // 75 % → 46.875 columnas por segundo a 48 k
    s.channel        = Spectrum::left;
    s.avgMode        = Spectrum::avgNone;
    s.peakHold       = false;
    s.rangeDbIndex   = 1;                 // 90 dB
    s.historySecIndex = 0;                // 10 s: los tests del anillo no tardan medio minuto
    return s;
}

template <typename Gen>
void feed (Spectrum& m, long long n, Gen gen, int block = 512)
{
    std::vector<float> L ((size_t) block), R ((size_t) block);
    for (long long done = 0; done < n; )
    {
        const int k = (int) std::min ((long long) block, n - done);
        for (int i = 0; i < k; ++i)
        {
            const auto v = gen (done + i);
            L[(size_t) i] = v.first;
            R[(size_t) i] = v.second;
        }
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// La fila cuya frecuencia es la más cercana a `hz`.
int rowFor (double hz)
{
    int best = 0;
    double bestErr = 1.0e30;
    for (int r = 0; r < SpectrogramRing::kRows; ++r)
    {
        const double e = std::abs (std::log (SpectrogramRing::rowFrequency (r) / hz));
        if (e < bestErr) { bestErr = e; best = r; }
    }
    return best;
}

int argMaxRow (const juce::uint8* col)
{
    return (int) (std::max_element (col, col + SpectrogramRing::kRows) - col);
}

// Coeficiente de Spearman de una serie contra su índice (mide si SUBE, no cuánto).
double spearmanAgainstIndex (const std::vector<int>& y)
{
    const int n = (int) y.size();
    std::vector<int> order (y.size());
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return y[(size_t) a] < y[(size_t) b]; });

    std::vector<double> rank (y.size());
    for (int i = 0; i < n; )        // rangos promediados para los empates
    {
        int j = i;
        while (j + 1 < n && y[(size_t) order[(size_t) (j + 1)]] == y[(size_t) order[(size_t) i]]) ++j;
        const double r = 0.5 * (double) (i + j) + 1.0;
        for (int k = i; k <= j; ++k) rank[(size_t) order[(size_t) k]] = r;
        i = j + 1;
    }

    double sd2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double d = rank[(size_t) i] - (double) (i + 1);
        sd2 += d * d;
    }
    return 1.0 - 6.0 * sd2 / ((double) n * ((double) n * (double) n - 1.0));
}
}

// ============================================================================================ 1 · barrido
TEST_CASE ("telescope: un barrido logarítmico sube monótonamente en el espectrograma", "[telescope][spectrogram]")
{
    SpectrogramRing ring;
    Spectrum m;
    m.setSpectrogramRing (&ring);
    m.applySettings (baseSettings());
    m.prepare (kSr);

    // Barrido LOG de 100 Hz a 10 kHz en 5 s: fase = 2π·f0·(k^t - 1)/(t·ln k) con k = f1/f0.
    constexpr double kSeconds = 5.0, f0 = 100.0, f1 = 10000.0;
    const double lnK = std::log (f1 / f0);
    feed (m, (long long) (kSeconds * kSr), [&] (long long i)
          {
              const double t  = (double) i / kSr;
              const double ph = kTwoPi * f0 * kSeconds * (std::exp (lnK * t / kSeconds) - 1.0) / lnK;
              const auto v = (float) (0.5 * std::sin (ph));
              return std::pair<float, float> { v, v };
          });

    std::vector<juce::uint8> cols ((size_t) ring.count() * SpectrogramRing::kRows);
    const int n = ring.copyLatest (cols.data(), ring.count());
    REQUIRE (n > 100);

    std::vector<int> peaks;
    peaks.reserve ((size_t) n);
    for (int c = 0; c < n; ++c) peaks.push_back (argMaxRow (cols.data() + (size_t) c * SpectrogramRing::kRows));

    int nonDecreasing = 0;
    for (int c = 1; c < n; ++c) if (peaks[(size_t) c] >= peaks[(size_t) (c - 1)]) ++nonDecreasing;
    const double frac = (double) nonDecreasing / (double) (n - 1);
    const double rho  = spearmanAgainstIndex (peaks);

    std::printf ("SGRAM[barrido] %d columnas  fila inicial=%d (%.0f Hz)  final=%d (%.0f Hz)  "
                 "Spearman=%.5f  no decrecientes=%.1f %%\n",
                 n, peaks.front(), SpectrogramRing::rowFrequency (peaks.front()),
                 peaks.back(), SpectrogramRing::rowFrequency (peaks.back()), rho, 100.0 * frac);

    REQUIRE (rho > 0.99);
    REQUIRE (frac >= 0.95);
    REQUIRE (std::abs (SpectrogramRing::rowFrequency (peaks.front()) / f0 - 1.0) < 0.25);
    REQUIRE (std::abs (SpectrogramRing::rowFrequency (peaks.back())  / f1 - 1.0) < 0.10);
}

// =========================================================================================== 2 · silencio
TEST_CASE ("telescope: en silencio el espectrograma es todo cero", "[telescope][spectrogram]")
{
    SpectrogramRing ring;
    Spectrum m;
    m.setSpectrogramRing (&ring);
    m.applySettings (baseSettings());
    m.prepare (kSr);

    feed (m, (long long) (3.0 * kSr), [] (long long) { return std::pair<float, float> { 0.0f, 0.0f }; });

    std::vector<juce::uint8> cols ((size_t) ring.count() * SpectrogramRing::kRows);
    const int n = ring.copyLatest (cols.data(), ring.count());
    REQUIRE (n > 50);

    int worst = 0;
    for (const auto v : cols) worst = std::max (worst, (int) v);
    std::printf ("SGRAM[silencio] %d columnas  valor maximo = %d  (criterio 0)\n", n, worst);
    REQUIRE (worst == 0);
}

// ============================================================================================= 3 · escala
TEST_CASE ("telescope: la escala de la columna es dB sobre el rango elegido", "[telescope][spectrogram]")
{
    SpectrogramRing ring;
    Spectrum m;
    m.setSpectrogramRing (&ring);
    m.applySettings (baseSettings());   // rango 90 dB
    m.prepare (kSr);

    const double freq = 85.0 * kSr / 4096.0;                 // 996.09375 Hz, centro de bin exacto
    const float  amp  = std::pow (10.0f, -20.0f / 20.0f);
    feed (m, (long long) (2.0 * kSr), [&] (long long i)
          {
              const auto v = (float) ((double) amp * std::sin (kTwoPi * freq * (double) i / kSr));
              return std::pair<float, float> { v, v };
          });

    const auto* col = m.spectrogramColumn();
    const int   row = rowFor (freq);
    const int   expected = (int) std::lround (255.0 * (1.0 - 20.0 / 90.0));

    std::printf ("SGRAM[escala] tono %.3f Hz a -20 dBFS, rango 90 -> fila %d (%.1f Hz) vale %d  (esperado %d +- 2)\n",
                 freq, row, SpectrogramRing::rowFrequency (row), (int) col[row], expected);
    REQUIRE (std::abs ((int) col[row] - expected) <= 2);
    REQUIRE (argMaxRow (col) == row);
}

// ============================================================================================= 4 · anillo
// El ring guarda `historySec × columnas/s`. Tras el doble de la historia tiene que estar lleno y la columna
// más vieja tiene que ser la de hace exactamente `historySec` segundos. Un tono que cambia de frecuencia
// cada segundo lo delata: si el ring guardara de más o de menos, la columna 0 tendría OTRA frecuencia.
TEST_CASE ("telescope: el anillo guarda exactamente la historia que dice", "[telescope][spectrogram]")
{
    SpectrogramRing ring;
    Spectrum m;
    m.setSpectrogramRing (&ring);
    auto s = baseSettings();
    s.historySecIndex = 0;   // 10 s
    m.applySettings (s);
    m.prepare (kSr);

    REQUIRE (ring.historySeconds() == 10);
    REQUIRE (ring.columnsPerSecond() == m.emitRate());
    const int capacity = ring.capacity();
    REQUIRE (capacity == (int) std::llround (m.emitRate() * 10.0));

    // Un tono por segundo, subiendo por octavas desde 125 Hz. 20.5 s: el medio segundo de más pone la
    // columna 0 en el MEDIO de un tono y no encima del cambio, donde la ventana de 85 ms pisa los dos.
    constexpr double kTotalSec = 20.5;
    const auto toneHz = [] (int second) { return 125.0 * std::pow (2.0, (double) (second % 6) * 0.5); };

    double phase = 0.0;
    feed (m, (long long) (kTotalSec * kSr), [&] (long long i)
          {
              const int second = (int) ((double) i / kSr);
              phase += kTwoPi * toneHz (second) / kSr;      // fase continua: sin clicks en los cambios
              return std::pair<float, float> { (float) (0.5 * std::sin (phase)),
                                               (float) (0.5 * std::sin (phase)) };
          });

    REQUIRE (ring.count() == capacity);

    std::vector<juce::uint8> cols ((size_t) capacity * SpectrogramRing::kRows);
    REQUIRE (ring.copyLatest (cols.data(), capacity) == capacity);

    // Tiempo de audio de la columna 0: la última columna está en el final, y hay capacity-1 pasos atrás.
    const double tCol0 = kTotalSec - (double) (capacity - 1) / ring.columnsPerSecond();
    const int    expectedSecond = (int) tCol0;
    const double expectedHz = toneHz (expectedSecond);
    const int    got = argMaxRow (cols.data());

    std::printf ("SGRAM[anillo] capacidad=%d (%.3f col/s x 10 s)  columna 0 en t=%.3f s -> tono esperado "
                 "%.1f Hz, medido %.1f Hz\n", capacity, ring.columnsPerSecond(), tCol0, expectedHz,
                 SpectrogramRing::rowFrequency (got));

    // La ventana de la FFT (85 ms) tiene que caer ENTERA dentro del tono, si no el test sería ambiguo.
    REQUIRE (tCol0 - 4096.0 / kSr > (double) expectedSecond);
    REQUIRE (std::abs (SpectrogramRing::rowFrequency (got) / expectedHz - 1.0) < 0.05);

    // Y la última columna es la del último tono.
    const int last = argMaxRow (cols.data() + (size_t) (capacity - 1) * SpectrogramRing::kRows);
    REQUIRE (std::abs (SpectrogramRing::rowFrequency (last) / toneHz ((int) (kTotalSec - 0.01)) - 1.0) < 0.05);
}

// ======================================================================================= 5 · determinismo
TEST_CASE ("telescope: dos corridas de la misma señal dan el mismo espectrograma", "[telescope][spectrogram]")
{
    const auto run = [] (int block)
    {
        auto ring = std::make_unique<SpectrogramRing>();
        Spectrum m;
        m.setSpectrogramRing (ring.get());
        m.applySettings (baseSettings());
        m.prepare (kSr);

        telescope::test::Pink gen { telescope::test::kPinkSeedA };
        feed (m, (long long) (6.0 * kSr), [&] (long long)
              {
                  const float x = 0.3f * gen.next();
                  return std::pair<float, float> { x, x };
              }, block);

        std::vector<juce::uint8> cols ((size_t) ring->count() * SpectrogramRing::kRows);
        const int n = ring->copyLatest (cols.data(), ring->count());
        cols.resize ((size_t) n * SpectrogramRing::kRows);
        return cols;
    };

    const auto a = run (512);
    const auto b = run (512);
    const auto c = run (64);     // y tampoco depende del tamaño de bloque
    std::printf ("SGRAM[determinismo] %zu bytes  (512 vs 512: %s · 512 vs 64: %s)\n",
                 a.size(), a == b ? "identicos" : "DISTINTOS", a == c ? "identicos" : "DISTINTOS");
    REQUIRE (! a.empty());
    REQUIRE (a == b);
    REQUIRE (a == c);
}
