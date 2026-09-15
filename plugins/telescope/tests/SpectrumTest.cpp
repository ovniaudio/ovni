// [telescope][spectrum] — el módulo Spectrum (spec §5.2, prompt 50). Cada número contra una señal cuya
// respuesta se conoce por matemática, no por lo que salió:
//
//   REFERENCIA DE dB   dB_k = 20·log10( 2·|X_k| / (N·CG) )  con CG = Σw/N
//                      Un seno de amplitud A centrado en un bin lee EXACTAMENTE 20·log10(A). Se verifica
//                      con las tres ventanas: la corrección de ganancia coherente es exacta, no un ajuste.
//   SCALLOPING         Un seno ENTRE bins pierde: 1.42 dB como máximo con Hann, 0.83 con Blackman-Harris.
//   PARSEVAL           Σ potencia_k · CG²/(2·NPG) = media cuadrática temporal (NPG = Σw²/N).
//   FUGA               La respuesta de cada ventana medida por su propia DTFT (ancho de lóbulo principal
//                      y primer lóbulo lateral): es la tabla que va al README.
//   RESOLUCIÓN         Dos senos a 8 Hz de distancia: dos picos con bin de 1.46 Hz, uno con bin de 46.9.
//   BANDAS             ⅓ de octava ISO 266 sobre el ruido rosa de la casa (plano) y el blanco (+1 dB/banda).
//   SLOPE              El rosa con slope 3 se ve plano — que es por qué el default es 3.
//   HOLD / PROMEDIO    12 dB/s de caída; el exponencial llega al 63 % en τ.
//   CANALES            L = R ⇒ S en el piso; sólo L ⇒ R en el piso y M seis decibeles abajo.
//   BLOQUE             El frame 20 de la misma señal es idéntico AL BIT en bloques de 1, 7, 64 y 4 096.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "TestSignals.h"
#include "analysis/modules/Spectrum.h"

using telescope::Spectrum;
using telescope::SpectrumFrame;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

Spectrum::Settings baseSettings()
{
    Spectrum::Settings s;
    s.fftOrder     = 12;              // 4 096 → bin de 11.71875 Hz a 48 k
    s.window       = Spectrum::hann;
    s.overlapIndex = 1;               // 75 %
    s.channel      = Spectrum::left;
    s.avgMode      = Spectrum::avgNone;
    s.peakHold     = false;
    s.slopeDbPerOct = 0.0f;           // los tests miran los DATOS; el slope es display
    return s;
}

// Alimenta el módulo con `n` muestras generadas por `gen(i) -> {L, R}`, en bloques de `block`.
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

std::pair<float, float> sinePair (long long i, double freq, float amp, double sr = kSr)
{
    const auto v = (float) ((double) amp * std::sin (kTwoPi * freq * (double) i / sr));
    return { v, v };
}

int argMax (const float* a, int n)
{
    return (int) (std::max_element (a, a + n) - a);
}

// Respuesta de una ventana a un desplazamiento de `delta` bins, normalizada a su valor en delta = 0.
// Es la DTFT de la ventana evaluada donde uno quiera: así se mide el lóbulo principal y los laterales sin
// depender de dónde caiga un seno.
double windowResponse (const std::vector<float>& w, double delta)
{
    const int n = (int) w.size();
    double re = 0.0, im = 0.0, sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = -kTwoPi * delta * (double) i / (double) n;
        re  += (double) w[(size_t) i] * std::cos (ph);
        im  += (double) w[(size_t) i] * std::sin (ph);
        sum += (double) w[(size_t) i];
    }
    return std::sqrt (re * re + im * im) / sum;
}

struct WindowShape { double mainLobeBins, firstSidelobeDb; };

// Primer nulo (mínimo local) y máximo de los lóbulos laterales. El ancho de lóbulo principal se cuenta de
// nulo a nulo, que es la convención de Harris (1978) y la que dice la tabla del README.
WindowShape measureWindow (int windowType, int n = 1024)
{
    std::vector<float> w ((size_t) n);
    Spectrum::fillWindow (w.data(), n, windowType);

    constexpr double kStep = 0.01;
    double prev = windowResponse (w, 0.0);
    double firstNull = 0.0;
    double delta = kStep;
    for (; delta < 20.0; delta += kStep)
    {
        const double v = windowResponse (w, delta);
        if (v > prev) { firstNull = delta - kStep; break; }   // dejó de bajar: pasamos el nulo
        prev = v;
    }

    double side = 0.0;
    for (double d = firstNull + kStep; d < 24.0; d += kStep)
        side = std::max (side, windowResponse (w, d));

    return { 2.0 * firstNull, 20.0 * std::log10 (std::max (side, 1.0e-12)) };
}

const char* windowName (int t)
{
    return t == Spectrum::hann ? "Hann" : (t == Spectrum::blackmanHarris4 ? "BH4" : "Kaiser b=9");
}
}

// ==================================================================================== 1 · seno en el bin
TEST_CASE ("telescope: un seno centrado en un bin lee su amplitud exacta", "[telescope][spectrum]")
{
    constexpr int   kBin  = 85;
    const double    freq  = (double) kBin * kSr / 4096.0;      // 996.09375 Hz
    const float     amp   = std::pow (10.0f, -20.0f / 20.0f);  // -20 dBFS pico

    for (const int wtype : { (int) Spectrum::hann, (int) Spectrum::blackmanHarris4, (int) Spectrum::kaiser9 })
    {
        auto s = baseSettings();
        s.window = wtype;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (2.0 * kSr), [&] (long long i) { return sinePair (i, freq, amp); });

        const auto& f = m.frame();
        const int   peak = argMax (f.magDb[0], f.numBins);
        std::printf ("SPEC[bin exacto %-10s] f=%.5f Hz  argmax=%d  magDb=%+8.4f dB  (esperado -20.00)\n",
                     windowName (wtype), freq, peak, f.magDb[0][kBin]);

        REQUIRE (f.fftSize == 4096);
        REQUIRE (f.numBins == 2049);
        REQUIRE (peak == kBin);
        REQUIRE_THAT (f.magDb[0][kBin], Catch::Matchers::WithinAbs (-20.0f, 0.05f));
    }
}

// ============================================================================== 2 · seno fuera del centro
TEST_CASE ("telescope: un seno entre bins pierde sólo lo que dice la ventana", "[telescope][spectrum]")
{
    const float amp = std::pow (10.0f, -20.0f / 20.0f);
    // 1 000 Hz cae en el bin 85.333 con FFT de 4 096 a 48 k: a un tercio de bin del centro.
    const struct { int window; float maxLossDb; } cases[] = {
        { Spectrum::hann,            1.42f },   // scalloping máximo de Hann (a medio bin)
        { Spectrum::blackmanHarris4, 0.83f },   // el de Blackman-Harris de 4 términos
    };

    for (const auto& c : cases)
    {
        auto s = baseSettings();
        s.window = c.window;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (2.0 * kSr), [&] (long long i) { return sinePair (i, 1000.0, amp); });

        const auto& f = m.frame();
        const int   peak = argMax (f.magDb[0], f.numBins);
        const float db   = f.magDb[0][peak];
        std::printf ("SPEC[scalloping %-10s] argmax=%d  pico=%+8.4f dB  perdida=%.4f dB  (tope %.2f)\n",
                     windowName (c.window), peak, db, -20.0f - db, c.maxLossDb);

        REQUIRE (peak == 85);
        REQUIRE (db <= -20.0f + 1.0e-3f);          // nunca puede leer MÁS que la amplitud real
        REQUIRE (db >= -20.0f - c.maxLossDb);
    }
}

// ========================================================================================= 3 · Parseval
TEST_CASE ("telescope: la energía del espectro es la de la señal (Parseval)", "[telescope][spectrum]")
{
    auto s = baseSettings();
    s.avgMode = Spectrum::avgInfinite;   // media sobre ~230 frames: el periodograma de un frame solo tiene
    s.channel = Spectrum::left;          // 2.2 % de dispersión estadística, que no entra en el ±1 %

    Spectrum m;
    m.applySettings (s);
    m.prepare (kSr);

    Pink gen { telescope::test::kPinkSeedA };
    double sumSq = 0.0;
    long long count = 0;
    const long long total = (long long) (5.0 * kSr);
    feed (m, total, [&] (long long)
          {
              const float x = 0.25f * gen.white();
              sumSq += (double) x * x;
              ++count;
              return std::pair<float, float> { x, x };
          });

    const auto& f = m.frame();
    double sumPow = 0.0;
    for (int k = 0; k < f.numBins; ++k)
        sumPow += std::pow (10.0, (double) f.magDb[0][k] / 10.0);

    // La potencia del frame está normalizada para AMPLITUD de seno (×2/(N·CG)); para volver a la media
    // cuadrática temporal hay que deshacer ese ×2 y la ganancia de potencia de la ventana (NPG = Σw²/N).
    const double cg = m.coherentGain(), npg = m.noisePowerGain();
    const double estimated = sumPow * cg * cg / (2.0 * npg);
    const double actual    = sumSq / (double) count;

    std::printf ("SPEC[parseval] Sigma_k P = %.6e   estimada = %.6e   real = %.6e   error = %+.3f %%\n",
                 sumPow, estimated, actual, 100.0 * (estimated / actual - 1.0));
    REQUIRE (std::abs (estimated / actual - 1.0) < 0.01);
}

// ============================================================================================= 4 · fuga
TEST_CASE ("telescope: la fuga entre bins es la de la ventana", "[telescope][spectrum]")
{
    // (a) La tabla del README: ancho de lóbulo principal y primer lóbulo lateral, MEDIDOS.
    const struct { int window; double lobeMin, lobeMax, sideMin, sideMax; } expect[] = {
        { Spectrum::hann,            3.8,  4.2, -33.0, -30.0 },
        { Spectrum::blackmanHarris4, 7.8,  8.2, -94.0, -90.0 },
        { Spectrum::kaiser9,         5.8,  7.2, -70.0, -63.0 },
    };
    for (const auto& e : expect)
    {
        const auto w = measureWindow (e.window);
        std::printf ("SPEC[ventana %-10s] lobulo principal = %.2f bins   primer lateral = %.1f dB\n",
                     windowName (e.window), w.mainLobeBins, w.firstSidelobeDb);
        REQUIRE (w.mainLobeBins    >= e.lobeMin);
        REQUIRE (w.mainLobeBins    <= e.lobeMax);
        REQUIRE (w.firstSidelobeDb >= e.sideMin);
        REQUIRE (w.firstSidelobeDb <= e.sideMax);
    }

    // (b) Sobre la señal: un seno CENTRADO en un bin. Con la ventana periódica, la fuga lejos del pico es
    // cero en aritmética exacta (Hann ocupa exactamente 3 bins), así que lo que se mide es el piso
    // numérico de la FFT en float. El criterio del enunciado (-60 Hann / -90 BH4 a 8 bins o más) se
    // cumple con muchísimo margen, y eso es lo honesto de decir: quien quiera ver los lóbulos laterales
    // de verdad tiene que mirar la tabla de arriba, no un seno alineado.
    const double freq = 85.0 * kSr / 4096.0;
    const float  amp  = 1.0f;
    const struct { int window; float maxDb; } leak[] = {
        { Spectrum::hann,            -60.0f },
        { Spectrum::blackmanHarris4, -90.0f },
    };
    for (const auto& c : leak)
    {
        auto s = baseSettings();
        s.window = c.window;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (2.0 * kSr), [&] (long long i) { return sinePair (i, freq, amp); });

        const auto& f = m.frame();
        float worst = SpectrumFrame::kFloorDb;
        for (int k = 0; k < f.numBins; ++k)
            if (std::abs (k - 85) >= 8) worst = std::max (worst, f.magDb[0][k]);

        std::printf ("SPEC[fuga %-10s] peor bin a >= 8 de distancia = %.1f dB  (criterio %.0f)\n",
                     windowName (c.window), worst, c.maxDb);
        REQUIRE (worst <= c.maxDb);
    }
}

// ======================================================================================= 5 · resolución
TEST_CASE ("telescope: el tamaño de FFT decide si dos tonos cercanos se separan", "[telescope][spectrum]")
{
    const float amp = std::pow (10.0f, -20.0f / 20.0f);
    const auto  twoTones = [&] (long long i)
    {
        const double t = (double) i / kSr;
        const auto v = (float) ((double) amp * (std::sin (kTwoPi * 1000.0 * t) + std::sin (kTwoPi * 1008.0 * t)));
        return std::pair<float, float> { v, v };
    };

    const auto peaksBetween = [] (const SpectrumFrame& f, double loHz, double hiHz)
    {
        const double binHz = f.binHz();
        const int k0 = std::max (1, (int) std::floor (loHz / binHz));
        const int k1 = std::min (f.numBins - 2, (int) std::ceil (hiHz / binHz));
        float top = SpectrumFrame::kFloorDb;
        for (int k = k0; k <= k1; ++k) top = std::max (top, f.magDb[0][k]);

        int n = 0;
        for (int k = k0; k <= k1; ++k)
            if (f.magDb[0][k] > top - 20.0f && f.magDb[0][k] > f.magDb[0][k - 1] && f.magDb[0][k] >= f.magDb[0][k + 1])
                ++n;
        return n;
    };

    const struct { int order; int expectedPeaks; } cases[] = { { 15, 2 }, { 10, 1 } };
    for (const auto& c : cases)
    {
        auto s = baseSettings();
        s.fftOrder = c.order;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (3.0 * kSr), twoTones);

        const int n = peaksBetween (m.frame(), 950.0, 1060.0);
        std::printf ("SPEC[resolucion orden %d] bin = %.3f Hz   maximos locales entre 950 y 1060 Hz = %d"
                     "  (esperado %d)\n", c.order, m.frame().binHz(), n, c.expectedPeaks);
        REQUIRE (n == c.expectedPeaks);
    }
}

// ============================================================================= 6 · bandas de ⅓ de octava
TEST_CASE ("telescope: las bandas de un tercio de octava leen lo que tienen que leer", "[telescope][spectrum]")
{
    constexpr int kFirst = 3;    // 50 Hz
    constexpr int kLast  = 26;   // 10 kHz  → 24 bandas

    // ORDEN 15 y 20 s, no el default. No es para aflojar el criterio: es que una banda de ⅓ de octava en
    // 50 Hz mide 11.6 Hz de ancho, y con la FFT de 4 096 (bin de 11.72 Hz) esa banda entera cabe en UN bin
    // cuyo lóbulo principal, además, mide cuatro. Medir bandas de graves con esa resolución no es medir la
    // banda: es medir la ventana. Con orden 15 el bin baja a 1.46 Hz (8 bins en la banda de 50 Hz) y los
    // 20 s dejan suficientes frames para que la dispersión estadística del ruido no domine.
    // Es exactamente el consejo que la lente le da al usuario: para leer graves, FFT grande.
    const auto run = [] (bool pink)
    {
        auto s = baseSettings();
        s.avgMode  = Spectrum::avgInfinite;
        s.fftOrder = 15;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);

        Pink gen { telescope::test::kPinkSeedA };
        feed (m, (long long) (20.0 * kSr), [&] (long long)
              {
                  const float x = 0.25f * (pink ? gen.next() : gen.white());
                  return std::pair<float, float> { x, x };
              });
        return m.frame();
    };

    // (a) El rosa de la casa es plano por octava dentro de ±0.5 dB entre ~30 Hz y 16 kHz, así que en
    // ⅓ de octava tiene que leer plano.
    {
        const auto& f = run (true);
        double mean = 0.0;
        for (int b = kFirst; b <= kLast; ++b) mean += f.bands[0][b];
        mean /= (double) (kLast - kFirst + 1);

        float worst = 0.0f;
        for (int b = kFirst; b <= kLast; ++b)
        {
            worst = std::max (worst, std::abs (f.bands[0][b] - (float) mean));
            std::printf ("   1/3oct %7.1f Hz -> %+8.3f dB (desvio %+6.3f)\n",
                         telescope::kThirdOctaveHz[b], f.bands[0][b], f.bands[0][b] - (float) mean);
        }
        std::printf ("SPEC[1/3 oct rosa] media = %.2f dB   peor desvio = %.3f dB  (criterio 1.0)\n", mean, worst);
        REQUIRE (worst <= 1.0f);
    }

    // (b) El blanco sube 10·log10(2^(1/3)) = 1.003 dB por banda: cada banda de ⅓ de octava es 2^(1/3)
    // veces más ancha que la anterior, y el blanco reparte la misma densidad en toda ella.
    {
        const auto& f = run (false);
        constexpr int b0 = 9, b1 = 23;    // 200 Hz … 5 kHz
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        const double n = (double) (b1 - b0 + 1);
        for (int b = b0; b <= b1; ++b)
        {
            const double x = (double) (b - b0), y = (double) f.bands[0][b];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        std::printf ("SPEC[1/3 oct blanco] pendiente = %+.4f dB por banda  (esperado +1.003 +- 0.3)\n", slope);
        REQUIRE_THAT (slope, Catch::Matchers::WithinAbs (1.003, 0.3));
    }
}

// ============================================================================================ 7 · slope
TEST_CASE ("telescope: con slope 3 el ruido rosa se ve plano", "[telescope][spectrum]")
{
    auto s = baseSettings();
    s.avgMode = Spectrum::avgInfinite;

    Spectrum m;
    m.applySettings (s);
    m.prepare (kSr);

    Pink gen { telescope::test::kPinkSeedA };
    feed (m, (long long) (10.0 * kSr), [&] (long long)
          {
              const float x = 0.25f * gen.next();
              return std::pair<float, float> { x, x };
          });

    const auto&  f = m.frame();
    const double binHz = f.binHz();
    const int    k0 = (int) std::ceil (100.0 / binHz), k1 = (int) std::floor (10000.0 / binHz);

    // Regresión lineal de dB contra log2(f): la pendiente es directamente dB por octava.
    double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
    for (int k = k0; k <= k1; ++k)
    {
        const double fr = (double) k * binHz;
        const double x  = std::log2 (fr / 1000.0);
        const double y  = (double) telescope::spectrumDisplayDb (f.magDb[0][k], fr, 3.0f);
        sx += x; sy += y; sxx += x * x; sxy += x * y; n += 1.0;
    }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    const double mean  = sy / n;

    // Planitud sobre promedios de POTENCIA de 1/6 de octava, no bin a bin: aun con 10 s de promediado
    // infinito, un bin suelto de ruido conserva ~0.4 dB de dispersión estadística (χ² con ~117 grados de
    // libertad efectivos), y esa dispersión es de la SEÑAL, no del analizador. Un sexto de octava es,
    // además, la resolución a la que se mira un espectro en la pantalla.
    double worstCell = 0.0, worstBin = 0.0;
    const double ratio = std::pow (2.0, 1.0 / 6.0);
    for (double lo = 100.0; lo < 10000.0; lo *= ratio)
    {
        const double hi = std::min (lo * ratio, 10000.0);
        double p = 0.0; int cells = 0;
        for (int k = std::max (1, (int) std::ceil (lo / binHz)); k < (int) std::ceil (hi / binHz) && k < f.numBins; ++k)
        {
            const double fr = (double) k * binHz;
            p += std::pow (10.0, (double) telescope::spectrumDisplayDb (f.magDb[0][k], fr, 3.0f) / 10.0);
            ++cells;
        }
        if (cells == 0) continue;
        worstCell = std::max (worstCell, std::abs (10.0 * std::log10 (p / (double) cells) - mean));
    }
    for (int k = k0; k <= k1; ++k)
    {
        const double fr = (double) k * binHz;
        worstBin = std::max (worstBin, std::abs ((double) telescope::spectrumDisplayDb (f.magDb[0][k], fr, 3.0f) - mean));
    }

    std::printf ("SPEC[slope 3 sobre rosa] pendiente = %+.4f dB/oct   peor desvio 1/6 oct = %.2f dB   "
                 "peor bin suelto = %.2f dB\n", slope, worstCell, worstBin);
    REQUIRE (std::abs (slope) < 0.5);
    REQUIRE (worstCell <= 1.5);
    REQUIRE (worstBin  <= 1.5);   // con 10 s de promediado infinito hasta el bin suelto entra
}

// ========================================================================================= 8 · peak hold
TEST_CASE ("telescope: el peak hold cae a los dB por segundo que dice", "[telescope][spectrum]")
{
    auto s = baseSettings();
    s.peakHold = true;
    s.holdDecayDbPerSec = 12.0f;
    s.avgMode = Spectrum::avgNone;

    Spectrum m;
    m.applySettings (s);
    m.prepare (kSr);

    constexpr int kBin = 85;
    const double  freq = (double) kBin * kSr / 4096.0;
    const float   amp  = std::pow (10.0f, -20.0f / 20.0f);

    feed (m, (long long) (2.0 * kSr), [&] (long long i) { return sinePair (i, freq, amp); });
    const float atPeak = m.frame().holdDb[0][kBin];

    const auto silence = [] (long long) { return std::pair<float, float> { 0.0f, 0.0f }; };
    feed (m, (long long) (1.0 * kSr), silence);
    const float at1s = m.frame().holdDb[0][kBin];
    feed (m, (long long) (2.0 * kSr), silence);
    const float at3s = m.frame().holdDb[0][kBin];

    const float rate = (at1s - at3s) / 2.0f;
    std::printf ("SPEC[peak hold] pico=%+.3f dB  a 1 s=%+.3f  a 3 s=%+.3f  caida=%.3f dB/s  (criterio 12 +- 1)\n",
                 atPeak, at1s, at3s, rate);
    REQUIRE_THAT (atPeak, Catch::Matchers::WithinAbs (-20.0f, 0.05f));
    REQUIRE_THAT (rate,   Catch::Matchers::WithinAbs (12.0f, 1.0f));
}

// ================================================================================== 9 · promedio expon.
TEST_CASE ("telescope: el promedio exponencial llega al 63 % en tau", "[telescope][spectrum]")
{
    auto s = baseSettings();
    s.avgMode    = Spectrum::avgExp;
    s.avgSeconds = 1.0f;

    Spectrum m;
    m.applySettings (s);
    m.prepare (kSr);

    constexpr int kBin = 85;
    const double  freq = (double) kBin * kSr / 4096.0;
    const float   lo   = std::pow (10.0f, -40.0f / 20.0f);
    const float   hi   = std::pow (10.0f, -20.0f / 20.0f);

    // Cinco segundos a -40 dBFS: el promedio ya convergió (5 τ).
    feed (m, (long long) (5.0 * kSr), [&] (long long i) { return sinePair (i, freq, lo); });
    const double p0 = std::pow (10.0, (double) m.frame().magDb[0][kBin] / 10.0);

    // Escalón a -20 dBFS y exactamente un τ de audio.
    feed (m, (long long) (1.0 * kSr), [&] (long long i) { return sinePair (i, freq, hi); });
    const double p  = std::pow (10.0, (double) m.frame().magDb[0][kBin] / 10.0);
    const double p1 = (double) hi * (double) hi;   // el destino: potencia del seno de -20 dBFS

    const double fraction = (p - p0) / (p1 - p0);
    std::printf ("SPEC[promedio exp tau=1s] p0=%.4e  p(1s)=%.4e  destino=%.4e  fraccion=%.4f  (0.632 +- 0.05)\n",
                 p0, p, p1, fraction);
    REQUIRE_THAT (fraction, Catch::Matchers::WithinAbs (0.632, 0.05));
}

// ========================================================================================== 10 · canales
TEST_CASE ("telescope: los cinco modos de canal miden lo que dicen", "[telescope][spectrum]")
{
    constexpr int kBin = 85;
    const double  freq = (double) kBin * kSr / 4096.0;
    const float   amp  = std::pow (10.0f, -20.0f / 20.0f);

    const auto measure = [&] (int channel, bool onlyLeft)
    {
        auto s = baseSettings();
        s.channel = channel;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (2.0 * kSr), [&] (long long i)
              {
                  const auto v = sinePair (i, freq, amp);
                  return std::pair<float, float> { v.first, onlyLeft ? 0.0f : v.second };
              });

        // El peor bin del espectro y el bin del tono, que es lo que distingue "piso" de "medición".
        float top = SpectrumFrame::kFloorDb;
        for (int k = 0; k < m.frame().numBins; ++k) top = std::max (top, m.frame().magDb[0][k]);
        return std::pair<float, float> { m.frame().magDb[0][kBin], top };
    };

    // (a) L = R: el side es cero exacto → todo el espectro en el piso.
    {
        const auto r = measure (Spectrum::side, false);
        std::printf ("SPEC[canal S con L=R] bin del tono = %.1f dB   peor bin = %.1f dB  (criterio <= -100)\n",
                     r.first, r.second);
        REQUIRE (r.second <= -100.0f);
    }
    // (b) Sólo L: el canal derecho está mudo.
    {
        const auto r = measure (Spectrum::right, true);
        std::printf ("SPEC[canal R con solo L] bin del tono = %.1f dB   peor bin = %.1f dB  (criterio <= -100)\n",
                     r.first, r.second);
        REQUIRE (r.second <= -100.0f);
    }
    // (c) Sólo L: M = (L+R)/2 = L/2 → exactamente 6.0206 dB abajo.
    {
        const auto left = measure (Spectrum::left, true);
        const auto midC = measure (Spectrum::mid,  true);
        const float delta = left.first - midC.first;
        std::printf ("SPEC[canal M con solo L] L=%+.4f dB  M=%+.4f dB  delta=%.4f dB  (esperado 6.0206)\n",
                     left.first, midC.first, delta);
        REQUIRE_THAT (delta, Catch::Matchers::WithinAbs (6.0206f, 0.05f));
    }
    // (d) L+R llena los DOS espectros y el modo lo dice.
    {
        auto s = baseSettings();
        s.channel = Spectrum::leftRight;

        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);
        feed (m, (long long) (2.0 * kSr), [&] (long long i)
              {
                  const auto v = sinePair (i, freq, amp);
                  return std::pair<float, float> { v.first, v.second * 0.5f };
              });

        const auto& f = m.frame();
        std::printf ("SPEC[canal L+R] L=%+.4f dB  R=%+.4f dB  espectros=%d\n",
                     f.magDb[0][kBin], f.magDb[1][kBin], telescope::spectrumNumSpectra (f.channelMode));
        REQUIRE (telescope::spectrumNumSpectra (f.channelMode) == 2);
        REQUIRE_THAT (f.magDb[0][kBin], Catch::Matchers::WithinAbs (-20.0f, 0.05f));
        REQUIRE_THAT (f.magDb[1][kBin], Catch::Matchers::WithinAbs (-26.0206f, 0.05f));
    }
}

// ==================================================================================== 11 · el bloque
// Mismo contrato que casa-4 del medidor y que [stereo] bloque: el frame que sale en una posición del
// stream es el mismo AL BIT venga el audio en bloques de 1 o de 4 096. Se empuja EXACTAMENTE la cantidad
// de muestras que deja el contador en el frame 20 (N + 20·H), así que no hace falta atrapar nada al vuelo.
TEST_CASE ("telescope: el frame 20 no depende del tamaño de bloque", "[telescope][spectrum]")
{
    auto s = baseSettings();
    s.avgMode = Spectrum::avgExp;      // con estado: si el estado dependiera del bloque, se notaría acá
    s.peakHold = true;

    const long long samples = 4096 + 20LL * 1024LL;   // N + 20·H con 75 % de solape

    std::vector<float> reference;
    juce::uint32       refIndex = 0;

    for (const int block : { 512, 1, 7, 64, 4096 })
    {
        Spectrum m;
        m.applySettings (s);
        m.prepare (kSr);

        Pink gen { telescope::test::kPinkSeedA };
        feed (m, samples, [&] (long long)
              {
                  const float x = 0.25f * gen.next();
                  return std::pair<float, float> { x, x };
              }, block);

        const auto& f = m.frame();
        REQUIRE (f.frameIndex == 20u);

        std::vector<float> got (f.magDb[0], f.magDb[0] + f.numBins);
        if (reference.empty()) { reference = got; refIndex = f.frameIndex; }
        else
        {
            INFO ("bloque " << block);
            REQUIRE (f.frameIndex == refIndex);
            REQUIRE (got == reference);     // AL BIT, no "parecido"
        }
    }
    std::printf ("SPEC[bloque 1/7/64/4096] frame %u identico al bit (%zu bins)\n",
                 refIndex, reference.size());
}

// ============================================================================================ 13 · historia
// (1c del 50) Cambiar la HISTORIA del espectrograma NO es cambiar el análisis: la FFT, el promediado y el
// peak hold siguen exactamente donde estaban; lo único que se rehace es el anillo de columnas (que sí hay
// que limpiar, porque su capacidad cambia). Iba acoplado en `needsRestartVersus`, así que pasar de 10 a
// 60 segundos tiraba a la basura el promedio acumulado y el hold — un usuario que estira la historia para
// mirar más atrás perdía el número que estaba mirando.
TEST_CASE ("telescope: cambiar la historia del espectrograma no reinicia el analisis", "[telescope][spectrum]")
{
    telescope::SpectrogramRing ring;
    Spectrum m;
    m.setSpectrogramRing (&ring);

    auto s = baseSettings();
    s.historySecIndex = 0;                 // 10 s
    s.avgMode = Spectrum::avgInfinite;     // el promedio acumulado es lo que se perdía
    m.applySettings (s);
    m.prepare (kSr);

    feed (m, (long long) (2.0 * kSr), [&] (long long i) { return sinePair (i, 1000.0, 0.5f); });
    const auto framesBefore = m.framesEmitted();
    const int  colsBefore   = ring.count();
    REQUIRE (framesBefore > 10u);
    REQUIRE (colsBefore > 10);

    s.historySecIndex = 2;                 // 60 s
    m.applySettings (s);

    std::printf ("SPEC[historia] frames emitidos antes=%u despues=%u  ·  anillo %d -> %d columnas, "
                 "capacidad %d (%d s)\n",
                 framesBefore, m.framesEmitted(), colsBefore, ring.count(), ring.capacity(),
                 ring.historySeconds());

    REQUIRE (m.framesEmitted() == framesBefore);      // el análisis NO arrancó de cero
    REQUIRE (ring.count() == 0);                      // el anillo SÍ se limpió
    REQUIRE (ring.historySeconds() == 60);
    REQUIRE (ring.capacity() == (int) std::llround (m.emitRate() * 60.0));

    // Y sigue midiendo desde donde estaba: los frames siguen numerando hacia adelante.
    feed (m, (long long) (1.0 * kSr), [&] (long long i) { return sinePair (i + (long long) (2.0 * kSr), 1000.0, 0.5f); });
    REQUIRE (m.framesEmitted() > framesBefore);
    REQUIRE (ring.count() > 0);
}
