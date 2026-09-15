// [telescope][kw] — el K-weighting de ITU-R BS.1770 (Anexo 1): shelf + paso-altos.
//
// La prueba de fuego: los coeficientes se recalculan por sample rate DESDE EL PROTOTIPO ANALÓGICO
// (bilineal + prewarp), y ese prototipo evaluado a 48 kHz tiene que reproducir la tabla PUBLICADA por la
// ITU. Si no la reproduce, el prototipo está mal — la tabla no se toca.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdio>
#include "analysis/modules/KWeighting.h"

using telescope::KWeighting;

TEST_CASE ("telescope: el prototipo analógico reproduce la tabla de BS.1770 a 48 kHz", "[telescope][kw]")
{
    const auto designed = KWeighting::designAt (48000.0);
    const auto table    = KWeighting::referenceAt48k();

    const double errs[] = {
        std::abs (designed.shelf.b0 - table.shelf.b0), std::abs (designed.shelf.b1 - table.shelf.b1),
        std::abs (designed.shelf.b2 - table.shelf.b2), std::abs (designed.shelf.a1 - table.shelf.a1),
        std::abs (designed.shelf.a2 - table.shelf.a2),
        std::abs (designed.hp.b0 - table.hp.b0), std::abs (designed.hp.b1 - table.hp.b1),
        std::abs (designed.hp.b2 - table.hp.b2), std::abs (designed.hp.a1 - table.hp.a1),
        std::abs (designed.hp.a2 - table.hp.a2),
    };
    double worst = 0.0;
    for (const double e : errs) worst = std::max (worst, e);

    std::printf ("KW_TABLE_MAX_ERR=%.3e\n", worst);
    REQUIRE (worst < 1.0e-6);
}

TEST_CASE ("telescope: la respuesta del K-weighting cae donde dice el estándar", "[telescope][kw]")
{
    const auto c = KWeighting::designAt (48000.0);

    const double at997  = KWeighting::magnitudeDb (c,   997.0, 48000.0);
    const double at10k  = KWeighting::magnitudeDb (c, 10000.0, 48000.0);
    const double at38   = KWeighting::magnitudeDb (c,    38.13, 48000.0);
    std::printf ("KW_48K 997Hz=%+.4f dB  10kHz=%+.4f dB  38.13Hz=%+.4f dB\n", at997, at10k, at38);

    // La constante -0.691 del estándar sale justo de esta ganancia a 997 Hz.
    REQUIRE_THAT (at997, Catch::Matchers::WithinAbs ( 0.691, 0.005));
    REQUIRE_THAT (at10k, Catch::Matchers::WithinAbs ( 4.04,  0.02));
    REQUIRE_THAT (at38,  Catch::Matchers::WithinAbs (-5.97,  0.05));
}

TEST_CASE ("telescope: el mismo prototipo a 44.1 y 96 kHz da la misma curva", "[telescope][kw]")
{
    for (const double fs : { 44100.0, 96000.0 })
    {
        const double at997 = KWeighting::magnitudeDb (KWeighting::designAt (fs), 997.0, fs);
        std::printf ("KW_%.0fK 997Hz=%+.4f dB\n", fs / 1000.0, at997);
        REQUIRE_THAT (at997, Catch::Matchers::WithinAbs (0.69, 0.02));
    }
}

// El test anterior mide la MATEMÁTICA; éste mide el FILTRO: un seno real por processSample.
TEST_CASE ("telescope: el filtro corrido da la ganancia que promete su respuesta", "[telescope][kw]")
{
    constexpr double fs = 48000.0;
    constexpr int    n  = 48000 * 2;   // 2 s: el transitorio de arranque se diluye

    KWeighting kw;
    kw.prepare (fs);

    double sumIn = 0.0, sumOut = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = std::sin (2.0 * M_PI * 997.0 * (double) i / fs);
        const double y = kw.processSample (x);
        if (i > 4800)   // saltear los primeros 100 ms
        {
            sumIn  += x * x;
            sumOut += y * y;
        }
    }
    const double gainDb = 10.0 * std::log10 (sumOut / sumIn);
    std::printf ("KW_MEASURED_997=%+.4f dB\n", gainDb);
    REQUIRE_THAT (gainDb, Catch::Matchers::WithinAbs (0.691, 0.01));
}
