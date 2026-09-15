// [telescope][ebu] — los vectores de conformidad de la EBU para el medidor de loudness.
//
// Las señales se generan EN MEMORIA (no hay WAV en el repo) siguiendo al pie de la letra las tablas
// publicadas, que la obrera del prompt 48 leyó de los documentos originales el 2026-09-07:
//   · EBU Tech 3341-2023 §2.9 Tabla 1 "Minimum requirements test signals" (tech.ebu.ch/docs/tech/tech3341.pdf)
//   · EBU Tech 3342-2023 §4  Tabla 1 "Minimum requirements test signals" (tech.ebu.ch/docs/tech/tech3342.pdf)
//
// OJO CON LAS DURACIONES: el enunciado del prompt daba los tests 3341-3 y 3341-4 con segmentos de 20 s.
// La tabla real dice 10 s / 60 s / 10 s (y 10/10/60/10/10). Con 20/20/20 la respuesta correcta NO es
// -23.0 sino -27.4, porque los tonos de -36 quedan a sólo 8.6 LU de la media y la compuerta relativa
// (-10 LU) no los saca. Con las duraciones del documento la media sube a -24.2 y sí los saca: ése es
// justamente el punto del test. Se implementan las del documento, y la tabla imprime las dos.
//
// Todas las señales son seno de 1000 Hz (el documento dice 1000 Hz, no 997) al nivel de PICO por canal,
// en fase en ambos canales.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include "analysis/modules/Loudness.h"

using telescope::Loudness;

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSilent = -std::numeric_limits<double>::infinity();

struct Segment { double seconds; double dbfs; };   // dbfs = kSilent → silencio digital

enum class Channels { stereo, leftOnly };

// Alimenta el medidor con la concatenación de los segmentos, en bloques de `blockSize` muestras.
// La fase del seno es continua entre segmentos (un cambio de nivel no es un cambio de tono).
void feed (Loudness& m, double sr, const std::vector<Segment>& segs,
           int blockSize = 512, Channels ch = Channels::stereo)
{
    std::vector<float> L ((size_t) blockSize, 0.0f), R ((size_t) blockSize, 0.0f);
    long long n = 0;

    for (const auto& seg : segs)
    {
        const auto  total = (long long) std::llround (seg.seconds * sr);
        const float amp   = (seg.dbfs == kSilent) ? 0.0f : (float) std::pow (10.0, seg.dbfs / 20.0);

        for (long long done = 0; done < total; )
        {
            const int k = (int) std::min ((long long) blockSize, total - done);
            for (int i = 0; i < k; ++i, ++n)
            {
                const auto v = (float) (amp * std::sin (2.0 * kPi * 1000.0 * (double) n / sr));
                L[(size_t) i] = v;
                R[(size_t) i] = (ch == Channels::leftOnly) ? 0.0f : v;
            }
            m.process (L.data(), R.data(), k);
            done += k;
        }
    }
}

// La tabla que lee la auditora.
void row (const char* test, double expected, double measured, double tol)
{
    const double delta = measured - expected;
    std::printf ("EBU[%-8s] esperado=%+8.3f  medido=%+8.3f  delta=%+7.3f  tol=%.2f  %s\n",
                 test, expected, measured, delta, tol, std::abs (delta) <= tol ? "OK" : "FALLA");
}

Loudness::Result measure (double sr, const std::vector<Segment>& segs,
                          int blockSize = 512, Channels ch = Channels::stereo)
{
    Loudness m;
    m.prepare (sr);
    feed (m, sr, segs, blockSize, ch);
    return m.result();
}
}

// ---------------------------------------------------------------------------------------------
// EBU Tech 3341 — loudness momentánea, short-term e integrada
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: EBU Tech 3341 tests 1-5 — la doble compuerta", "[telescope][ebu]")
{
    constexpr double sr = 48000.0;

    // 3341-1: 20 s a -23.0 dBFS. M = S = I = -23.0 ±0.1 LUFS.
    {
        const auto r = measure (sr, { { 20.0, -23.0 } });
        row ("3341-1 M", -23.0, r.momentary,  0.1);
        row ("3341-1 S", -23.0, r.shortTerm,  0.1);
        row ("3341-1 I", -23.0, r.integrated, 0.1);
        REQUIRE (r.integratedValid);
        REQUIRE (std::abs (r.momentary  + 23.0) <= 0.1);
        REQUIRE (std::abs (r.shortTerm  + 23.0) <= 0.1);
        REQUIRE (std::abs (r.integrated + 23.0) <= 0.1);
    }

    // 3341-2: igual al 1 pero a -33.0 dBFS.
    {
        const auto r = measure (sr, { { 20.0, -33.0 } });
        row ("3341-2 I", -33.0, r.integrated, 0.1);
        REQUIRE (std::abs (r.integrated + 33.0) <= 0.1);
    }

    // 3341-3: 10 s a -36 · 60 s a -23 · 10 s a -36. Los -36 caen bajo la compuerta RELATIVA.
    {
        const auto r = measure (sr, { { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } });
        row ("3341-3 I", -23.0, r.integrated, 0.1);
        REQUIRE (std::abs (r.integrated + 23.0) <= 0.1);
    }

    // 3341-4: + dos tonos de -72 que caen bajo la compuerta ABSOLUTA (-70 LUFS).
    {
        const auto r = measure (sr, { { 10.0, -72.0 }, { 10.0, -36.0 }, { 60.0, -23.0 },
                                      { 10.0, -36.0 }, { 10.0, -72.0 } });
        row ("3341-4 I", -23.0, r.integrated, 0.1);
        REQUIRE (std::abs (r.integrated + 23.0) <= 0.1);
    }

    // 3341-5: 20 s a -26 · 20.1 s a -20 · 20 s a -26. Acá NADA se compuerta: es la media energética.
    {
        const auto r = measure (sr, { { 20.0, -26.0 }, { 20.1, -20.0 }, { 20.0, -26.0 } });
        row ("3341-5 I", -23.0, r.integrated, 0.1);
        REQUIRE (std::abs (r.integrated + 23.0) <= 0.1);
    }
}

// Lo que el enunciado del prompt pedía con segmentos de 20 s: se mide y se muestra, pero el valor
// correcto para ESA señal no es -23.0. Queda documentado para que nadie "arregle" el medidor.
TEST_CASE ("telescope: la variante 20/20/20 del 3341-3 NO da -23 (y está bien)", "[telescope][ebu]")
{
    const auto r = measure (48000.0, { { 20.0, -36.0 }, { 20.0, -23.0 }, { 20.0, -36.0 } });
    row ("3341-3v20", -27.36, r.integrated, 0.2);
    REQUIRE (std::abs (r.integrated + 27.36) <= 0.2);
}

// ---------------------------------------------------------------------------------------------
// EBU Tech 3342 — Loudness Range
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: EBU Tech 3342 tests 1-4 — Loudness Range", "[telescope][ebu]")
{
    constexpr double sr = 48000.0;

    { const auto r = measure (sr, { { 20.0, -20.0 }, { 20.0, -30.0 } });
      row ("3342-1", 10.0, r.lra, 1.0); REQUIRE (std::abs (r.lra - 10.0) <= 1.0); }

    { const auto r = measure (sr, { { 20.0, -20.0 }, { 20.0, -15.0 } });
      row ("3342-2",  5.0, r.lra, 1.0); REQUIRE (std::abs (r.lra -  5.0) <= 1.0); }

    { const auto r = measure (sr, { { 20.0, -40.0 }, { 20.0, -20.0 } });
      row ("3342-3", 20.0, r.lra, 1.0); REQUIRE (std::abs (r.lra - 20.0) <= 1.0); }

    { const auto r = measure (sr, { { 20.0, -50.0 }, { 20.0, -35.0 }, { 20.0, -20.0 },
                                    { 20.0, -35.0 }, { 20.0, -50.0 } });
      row ("3342-4", 15.0, r.lra, 1.0); REQUIRE (std::abs (r.lra - 15.0) <= 1.0); }
}

// ---------------------------------------------------------------------------------------------
// Los de casa: lo que el catálogo necesita y la EBU no cubre
// ---------------------------------------------------------------------------------------------
TEST_CASE ("telescope: casa-1 · un canal solo pesa 3 LU menos", "[telescope][ebu]")
{
    // -23 dBFS SÓLO en L (R en silencio): con G_L = G_R = 1 la suma cae a la mitad → -26.0 LUFS.
    const auto r = measure (48000.0, { { 20.0, -23.0 } }, 512, Channels::leftOnly);
    row ("casa-1", -26.0, r.integrated, 0.1);
    REQUIRE (std::abs (r.integrated + 26.0) <= 0.1);
}

TEST_CASE ("telescope: casa-2 · el silencio digital no produce NaN ni infinitos", "[telescope][ebu]")
{
    const auto r = measure (48000.0, { { 20.0, kSilent } });
    row ("casa-2", 0.0, r.integratedValid ? 1.0 : 0.0, 0.0);   // 0 = inválido, que es lo esperado

    REQUIRE_FALSE (r.integratedValid);
    for (const float v : { r.momentary, r.shortTerm, r.integrated, r.lra,
                           r.truePeakMax, r.momentaryMax, r.shortTermMax })
    {
        REQUIRE (std::isfinite (v));
    }
}

TEST_CASE ("telescope: casa-3 · el mismo -23.0 a 44.1 y 96 kHz", "[telescope][ebu]")
{
    for (const double sr : { 44100.0, 96000.0 })
    {
        const auto r = measure (sr, { { 20.0, -23.0 } });
        row (sr < 50000.0 ? "casa-3a" : "casa-3b", -23.0, r.integrated, 0.1);
        REQUIRE (std::abs (r.integrated + 23.0) <= 0.1);
    }
}

TEST_CASE ("telescope: casa-4 · los números no dependen del tamaño de bloque", "[telescope][ebu]")
{
    const std::vector<Segment> sig { { 20.0, -23.0 } };
    const auto ref = measure (48000.0, sig, 512);

    double worst = 0.0;
    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto r = measure (48000.0, sig, block);
        // AL BIT: la misma señal tiene que dar exactamente los mismos números.
        REQUIRE (r.momentary  == ref.momentary);
        REQUIRE (r.shortTerm  == ref.shortTerm);
        REQUIRE (r.integrated == ref.integrated);
        REQUIRE (r.lra        == ref.lra);
        REQUIRE (r.truePeakMax == ref.truePeakMax);
        worst = std::max (worst, (double) std::abs (r.integrated - ref.integrated));
    }
    row ("casa-4", 0.0, worst, 0.0);
    REQUIRE (worst == 0.0);
}
