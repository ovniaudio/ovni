// [telescope][tp] — true-peak de ITU-R BS.1770 Anexo 2: sobremuestreo 4× por el FIR polifásico de 48 taps
// (4 fases × 12) cuya tabla está publicada en el Anexo. El número que importa es el PICO ENTRE MUESTRAS:
// el que el medidor de picos de muestra NO ve y el que hace clipear el conversor del oyente.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include <vector>
#include <juce_core/juce_core.h>
#include "analysis/modules/TruePeakFir.h"

using telescope::TruePeakFir;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// dBTP de un seno de `freq` a `amp` (lineal) con fase inicial `phase`, medido por el FIR.
//
// Se saltean las primeras kTaps salidas: hasta que la línea de retardo se llena, el FIR está convolucionando
// contra ceros. Y con fase inicial ±90° la señal ARRANCA en su pico, o sea que el test le está metiendo un
// escalón — cuyo pico real SÍ sobrepasa la amplitud del seno (medido: +1 dB con fase 90°). El filtro no se
// equivoca; lo que no es un seno continuo es la señal. Un medidor real hace lo mismo tras un reset.
double measureDbtp (double fs, double freq, double amp, double phase, int n)
{
    TruePeakFir fir;
    fir.prepare (fs);
    float mx = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto x = (float) (amp * std::sin (2.0 * kPi * freq * (double) i / fs + phase));
        const float y = fir.processSample (x);
        if (i >= TruePeakFir::kTaps) mx = std::max (mx, y);
    }
    return mx > 0.0f ? 20.0 * std::log10 ((double) mx) : -300.0;
}
}

// TP-1 — el true-peak de un seno NO depende de dónde caigan las muestras. El pico de muestra sí.
TEST_CASE ("telescope: TP-1 · seno de 997 Hz a -6 dBFS, 16 fases iniciales", "[telescope][tp]")
{
    const double amp = std::pow (10.0, -6.0 / 20.0);

    for (const double fs : { 44100.0, 48000.0 })
    {
        double worst = 0.0, first = 0.0;
        for (int k = 0; k < 16; ++k)
        {
            const double dbtp = measureDbtp (fs, 997.0, amp, 2.0 * kPi * (double) k / 16.0, (int) (fs / 10.0));
            if (k == 0) first = dbtp;
            worst = std::max (worst, std::abs (dbtp + 6.0));
        }
        std::printf ("TP1_%.0fk dBTP=%+.4f  peor_delta=%.4f\n", fs / 1000.0, first, worst);
        REQUIRE (worst < 0.1);
    }
}

// TP-2 — el caso de manual: un seno a fs/4 con fase 45° cae SIEMPRE en ±0.707·A. El pico de muestra
// lee -9 dBFS; el pico real está 3 dB más arriba, entre dos muestras. Ése es el número que se publica.
TEST_CASE ("telescope: TP-2 · seno a fs/4 con fase 45° — el pico que la muestra no ve", "[telescope][tp]")
{
    constexpr double fs = 44100.0;
    const double amp = std::pow (10.0, -6.0 / 20.0);
    const int n = 4410;

    float samplePeak = 0.0f;
    for (int i = 0; i < n; ++i)
        samplePeak = std::max (samplePeak,
                               (float) std::abs (amp * std::sin (2.0 * kPi * (fs / 4.0) * (double) i / fs + kPi / 4.0)));

    const double sampleDb = 20.0 * std::log10 ((double) samplePeak);
    const double dbtp     = measureDbtp (fs, fs / 4.0, amp, kPi / 4.0, n);
    std::printf ("TP2 pico_de_muestra=%+.4f dBFS  dBTP=%+.4f\n", sampleDb, dbtp);

    REQUIRE_THAT (sampleDb, Catch::Matchers::WithinAbs (-9.0, 0.05));
    REQUIRE_THAT (dbtp,     Catch::Matchers::WithinAbs (-6.0, 0.15));
}

// TP-3 — invariante: el true-peak NUNCA puede quedar por debajo del pico de muestra.
TEST_CASE ("telescope: TP-3 · dBTP >= pico de muestra sobre 30 s de ruido rosa", "[telescope][tp]")
{
    constexpr double fs = 48000.0;
    const int n = (int) (30.0 * fs);

    // Ruido rosa por el filtro de Voss-McCartney económico (Paul Kellett): -3 dB/oct, determinista.
    juce::Random rng (20260907);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;

    TruePeakFir fir;
    fir.prepare (fs);
    float samplePeak = 0.0f, truePeak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const double w = rng.nextDouble() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11;
        b6 = w * 0.115926;

        const auto x = (float) std::clamp (pink, -1.0, 1.0);
        samplePeak = std::max (samplePeak, std::abs (x));
        truePeak   = std::max (truePeak, fir.processSample (x));
    }

    const double sampleDb = 20.0 * std::log10 ((double) samplePeak);
    const double tpDb     = 20.0 * std::log10 ((double) truePeak);
    std::printf ("TP3 pico_de_muestra=%+.4f dBFS  dBTP=%+.4f  (dBTP - muestra = %+.4f)\n",
                 sampleDb, tpDb, tpDb - sampleDb);
    REQUIRE (truePeak >= samplePeak);
}

// TP-4 — la tasa de sobremuestreo por sample rate. El estándar pide llegar a >= 192 kHz: 4× hasta 48 k,
// 2× desde 96 k, y a 192 k o más las muestras YA están a esa resolución.
TEST_CASE ("telescope: TP-4 · el sobremuestreo se adapta al sample rate", "[telescope][tp]")
{
    REQUIRE (TruePeakFir::phasesForRate (44100.0)  == 4);
    REQUIRE (TruePeakFir::phasesForRate (48000.0)  == 4);
    REQUIRE (TruePeakFir::phasesForRate (88200.0)  == 4);
    REQUIRE (TruePeakFir::phasesForRate (96000.0)  == 2);
    REQUIRE (TruePeakFir::phasesForRate (176400.0) == 2);
    REQUIRE (TruePeakFir::phasesForRate (192000.0) == 1);
    REQUIRE (TruePeakFir::phasesForRate (384000.0) == 1);

    // Y a 96 kHz (2×) el seno de -6 dBFS sigue leyendo -6 dBTP.
    const double amp = std::pow (10.0, -6.0 / 20.0);
    double worst = 0.0;
    for (int k = 0; k < 16; ++k)
        worst = std::max (worst, std::abs (measureDbtp (96000.0, 997.0, amp, 2.0 * kPi * k / 16.0, 9600) + 6.0));
    std::printf ("TP4_96k peor_delta=%.4f\n", worst);
    REQUIRE (worst < 0.1);
}

// La tabla del Anexo 2 no es cualquier juego de 48 números: es simétrica (fase 0 = fase 3 al revés,
// fase 1 = fase 2 al revés). Si alguien la re-tipea mal, esto lo agarra.
TEST_CASE ("telescope: TP-5 · la tabla del Anexo 2 conserva su simetría polifásica", "[telescope][tp]")
{
    const auto& t = TruePeakFir::table();
    for (int i = 0; i < TruePeakFir::kTaps; ++i)
    {
        REQUIRE (t[0][i] == t[3][TruePeakFir::kTaps - 1 - i]);
        REQUIRE (t[1][i] == t[2][TruePeakFir::kTaps - 1 - i]);
    }
}

// ---------------------------------------------------------------------------------------------
// Los vectores de true-peak de EBU Tech 3341-2023 §2.9 Tabla 1 (tests 15-19), leídos del documento
// original el 2026-09-07. Tolerancia del propio documento: +0.2 / -0.4 dBTP (asimétrica: sub-leer es
// más grave que sobre-leer).
//   15  fs/4, 0.50 FFS, fase  0.0°  → -6.0        18  fs/8, 0.50 FFS, fase 67.5° → -6.0
//   16  fs/4, 0.50 FFS, fase 45.0°  → -6.0        19  fs/4, 1.41 FFS, fase 45.0° → +3.0
//   17  fs/6, 0.50 FFS, fase 60.0°  → -6.0
// Los tests 20-23 (mismo tono resampleado con offsets) quedan para el prompt 49: piden un resampler
// aparte para SINTETIZAR la señal, no para medirla.
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: vectores de true-peak de EBU Tech 3341 (15-19)", "[telescope][tp]")
{
    constexpr double fs = 48000.0;
    const struct { const char* id; double divisor; double ffs; double phaseDeg; double expected; } cases[] = {
        { "3341-15", 4.0, 0.50, 0.0,  -6.0 },
        { "3341-16", 4.0, 0.50, 45.0, -6.0 },
        { "3341-17", 6.0, 0.50, 60.0, -6.0 },
        { "3341-18", 8.0, 0.50, 67.5, -6.0 },
        { "3341-19", 4.0, 1.41, 45.0, +3.0 },
    };

    for (const auto& c : cases)
    {
        const double dbtp  = measureDbtp (fs, fs / c.divisor, c.ffs, c.phaseDeg * kPi / 180.0, 4800);
        const double delta = dbtp - c.expected;
        const bool   ok    = delta <= 0.2 && delta >= -0.4;
        std::printf ("TP[%s] esperado=%+6.2f  medido=%+7.4f  delta=%+7.4f  tol=+0.2/-0.4  %s\n",
                     c.id, c.expected, dbtp, delta, ok ? "OK" : "FALLA");
        REQUIRE (ok);
    }
}

// ========================================================================================================
// ===== 57c · EL TRUE-PEAK POR CANAL, Y LAS VENTANAS PARCIALES =====
//
// «Sólo es M–S, también debería poder ser L y R, y que no haya retraso al cargar el medidor» (Joaquín,
// 12-sep). Las dos cosas son campos nuevos del medidor, APPEND-ONLY, y las dos salen de números que el
// módulo YA calculaba:
//
//   · `truePeakHopL/R` y `truePeakMaxL/R` — `process` calculaba `tpL` y `tpR` por muestra y se quedaba
//     con el máximo de los dos. Ahora los guarda por separado. Lo que se verifica es que el canal en
//     silencio NO herede el pico del otro, que es el único modo en que un medidor por canal puede mentir;
//   · `momentaryPartial` / `shortTermPartial` — la misma media de `msL`/`msR` sobre `min (hops, N)` hops.
//     Lo que se verifica es que sea la MISMA cuenta y no una aproximación: cuando la ventana se llena,
//     idéntica AL BIT a la oficial.
// ========================================================================================================
#include "analysis/modules/Loudness.h"

TEST_CASE ("telescope: el true-peak por canal no hereda el pico del otro", "[telescope][tp]")
{
    constexpr double sr = 48000.0;
    telescope::Loudness m;
    m.prepare (sr);

    // Un seno de 1 kHz a -6 dBFS SÓLO EN L, un segundo.
    const auto amp = (float) std::pow (10.0, -6.0 / 20.0);
    std::vector<float> L (512, 0.0f), R (512, 0.0f);
    long long n = 0;
    for (int blk = 0; blk < (int) (1.0 * sr / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
            L[(size_t) i] = (float) (amp * std::sin (2.0 * kPi * 1000.0 * (double) n / sr));
        m.process (L.data(), R.data(), 512);
    }

    const auto r = m.result();
    std::printf ("TP[canal] seno 1 kHz -6 dBFS solo en L  ·  hop L=%+.4f dBTP  R=%+.1f  ·  "
                 "max L=%+.4f  R=%+.1f  ·  junto hop=%+.4f max=%+.4f\n",
                 r.truePeakHopL, r.truePeakHopR, r.truePeakMaxL, r.truePeakMaxR,
                 r.truePeakHop, r.truePeakMax);

    CHECK (r.truePeakHopL >= -6.10f);
    CHECK (r.truePeakHopL <= -5.90f);
    CHECK (r.truePeakHopR == telescope::Loudness::kSilenceFloor);
    CHECK (r.truePeakMaxL >= -6.10f);
    CHECK (r.truePeakMaxL <= -5.90f);
    CHECK (r.truePeakMaxR == telescope::Loudness::kSilenceFloor);
    // Y el de siempre no se movió: es el máximo de los dos.
    CHECK (r.truePeakHop == r.truePeakHopL);
    CHECK (r.truePeakMax == r.truePeakMaxL);
}

TEST_CASE ("telescope: las ventanas parciales valen desde el primer hop y coinciden al bit al llenarse",
           "[telescope][ebu]")
{
    constexpr double sr = 48000.0;
    telescope::Loudness m;
    m.prepare (sr);

    const auto amp = (float) std::pow (10.0, -20.0 / 20.0);
    std::vector<float> L (480, 0.0f), R (480, 0.0f);   // 10 ms por llamada: 10 llamadas = 1 hop
    long long n = 0;

    float partialAt1s = 0.0f, officialAt1s = 0.0f;
    const auto push = [&] (double seconds)
    {
        for (int blk = 0; blk < (int) std::lround (seconds * sr / 480.0); ++blk)
        {
            for (int i = 0; i < 480; ++i, ++n)
            {
                const auto v = (float) (amp * std::sin (2.0 * kPi * 997.0 * (double) n / sr));
                L[(size_t) i] = v;
                R[(size_t) i] = v;
            }
            m.process (L.data(), R.data(), 480);
        }
    };

    // A 0.1 s (UN hop) el parcial ya existe y el oficial todavía no.
    push (0.1);
    {
        const auto r = m.result();
        std::printf ("EBU[parcial] a 0.1 s (1 hop): momentary oficial %+.1f  parcial %+.4f  ·  "
                     "short-term oficial %+.1f  parcial %+.4f\n",
                     r.momentary, r.momentaryPartial, r.shortTerm, r.shortTermPartial);
        CHECK (r.momentary == telescope::Loudness::kSilenceFloor);
        CHECK (r.shortTerm == telescope::Loudness::kSilenceFloor);
        CHECK (r.momentaryPartial > -25.0f);
        CHECK (r.shortTermPartial > -25.0f);
    }

    push (0.9);                                   // 1.0 s
    partialAt1s = m.result().shortTermPartial;
    push (2.0);                                   // 3.0 s: la ventana de short-term se llenó
    const auto r3 = m.result();
    officialAt1s = r3.shortTerm;

    std::printf ("EBU[parcial] short-term parcial a 1.0 s = %+.6f  ·  oficial a 3.0 s = %+.6f  ·  "
                 "diferencia %.6f LU (tol 0.1)  ·  a 3.0 s parcial = %+.6f (igual al bit: %s)\n",
                 partialAt1s, officialAt1s, std::abs (partialAt1s - officialAt1s),
                 r3.shortTermPartial, r3.shortTermPartial == r3.shortTerm ? "si" : "NO");

    CHECK (std::abs (partialAt1s - officialAt1s) <= 0.1f);
    CHECK (r3.shortTermPartial == r3.shortTerm);      // AL BIT
    CHECK (r3.momentaryPartial == r3.momentary);      // ídem la de 400 ms
}
