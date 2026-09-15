// [telescope][ref] — el módulo Reference (spec §5.6, prompt 54): el lado VIVO de TONAL BALANCE y la
// comparación contra una referencia, las dos normalizadas a su propia loudness.
//
//   norm[b]  = curva[b] − LUFS_integrado          (cada lado con el SUYO)
//   delta[b] = live_norm[b] − ref_norm[b]
//
// REF[nivel]   el MISMO material a dos niveles distintos da delta ≈ 0: la loudness se cancela, que es todo
//              el punto. Lo que queda es la varianza del ruido con 10 s.
// REF[tilt]    un tilt REAL (+6 dB arriba de 2 kHz) se ve como tilt. Y acá aparece la consecuencia de
//              normalizar por loudness, que es una propiedad del método y no un defecto: el delta es de
//              suma (ponderada) cero, así que +6 dB en los agudos NO se ve como "+6 arriba y 0 abajo" sino
//              como "+X arriba y −Y abajo" con X+Y = 6. Lo que la lente promete —y lo que se verifica
//              acá— es la DIFERENCIA entre las dos regiones.
// REF[sin-ref] sin referencia cargada: refValid en false, delta en 0, y el lado vivo sigue funcionando.
// REF[3s]      liveValid recién a los 3 s de audio promediado.
// REF[reset]   RESET limpia el acumulador vivo y NO la referencia.
// REF[bloque]  bloque 1 / 7 / 64 / 4096 → liveBands IDÉNTICAS AL BIT.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "TestShelf.h"
#include "TestSignals.h"
#include "analysis/FileAnalysis.h"
#include "analysis/ReferenceFrame.h"
#include "analysis/modules/Loudness.h"
#include "analysis/modules/Reference.h"
#include "analysis/modules/Spectrum.h"

using telescope::FileAnalysis;
using telescope::Loudness;
using telescope::Reference;
using telescope::ReferenceFrame;
using telescope::Spectrum;
using telescope::test::HighShelf;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;

// El generador de cada caso: devuelve {L, R} para la muestra i.
using Gen = std::function<std::pair<float, float> (long long)>;

// Ruido rosa estéreo a un pico dado. `seed` elige la realización.
Gen pinkGen (float peak, std::uint32_t seed)
{
    auto a = std::make_shared<Pink> (seed);
    auto b = std::make_shared<Pink> (seed ^ 0x9e3779b9u);
    return [peak, a, b] (long long) { return std::pair<float, float> { peak * a->next(), peak * b->next() }; };
}

Gen pinkPlusHighShelf (float peak, std::uint32_t seed, HighShelf& shelfL, HighShelf& shelfR)
{
    auto a = std::make_shared<Pink> (seed);
    auto b = std::make_shared<Pink> (seed ^ 0x9e3779b9u);
    return [peak, a, b, &shelfL, &shelfR] (long long)
    {
        return std::pair<float, float> { peak * shelfL.process (a->next()),
                                         peak * shelfR.process (b->next()) };
    };
}

// ------------------------------------------------------------------------------------------------------
// Un lado VIVO completo: el módulo Spectrum con Reference colgado del sink, más el medidor de loudness
// que le da el integrado. Es exactamente lo que arma el AnalysisThread, en chiquito.
// ------------------------------------------------------------------------------------------------------
struct LiveRun
{
    Spectrum  spectrum;
    Loudness  loudness;
    Reference reference;

    explicit LiveRun (double sr = kSr)
    {
        spectrum.setFrameSink (&reference);
        spectrum.applySettings (Spectrum::Settings{});
        spectrum.prepare (sr);
        spectrum.reset();
        loudness.prepare (sr);
        loudness.reset();
        reference.resetLive();
    }

    void feed (const Gen& gen, double seconds, int block = 512, double sr = kSr)
    {
        const auto total = (long long) std::llround (seconds * sr);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        for (long long done = 0; done < total; )
        {
            const int k = (int) std::min ((long long) block, total - done);
            for (int i = 0; i < k; ++i)
            {
                const auto v = gen (done + i);
                L[(size_t) i] = v.first;
                R[(size_t) i] = v.second;
            }
            spectrum.process (L.data(), R.data(), k);
            loudness.process (L.data(), R.data(), k);
            done += k;
        }
        const auto r = loudness.result();
        reference.setLiveLoudness (r.integrated, r.integratedValid);
    }

    ReferenceFrame frame()
    {
        ReferenceFrame f;
        reference.fill (f);
        return f;
    }
};

// Una FileAnalysis armada EN MEMORIA con el mismo motor (el camino del archivo ya lo prueba [file]).
FileAnalysis analysisOf (const Gen& gen, double seconds, const juce::String& name, double sr = kSr)
{
    LiveRun run { sr };
    run.feed (gen, seconds, 4096, sr);

    FileAnalysis a;
    a.ok = a.valid = true;
    a.name    = name;
    a.sr      = sr;
    a.channels = 2;
    a.seconds = seconds;

    const auto r = run.loudness.result();
    a.integratedLufs = r.integrated;
    a.lra            = r.lra;
    a.truePeakDbtp   = r.truePeakMax;

    ReferenceFrame f;
    run.reference.fill (f);
    std::copy (f.liveBands, f.liveBands + FileAnalysis::kNumBands, a.bandsDb);
    return a;
}

int bandOf (double hz)
{
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        if (std::abs (telescope::kThirdOctaveHz[b] - hz) < 0.51) return b;
    return -1;
}
}

// ================================================================================= 1 · la loudness se cancela
TEST_CASE ("telescope: el mismo material a dos niveles distintos da delta cero", "[telescope][ref]")
{
    constexpr double kSeconds = 10.0;

    // Vivo: rosa a ~-14 LUFS. Referencia: rosa de OTRA semilla a ~-20 LUFS (6 dB más abajo).
    LiveRun live;
    live.feed (pinkGen (std::pow (10.0f, -2.4f / 20.0f), telescope::test::kPinkSeedA), kSeconds);
    live.reference.setReference (analysisOf (pinkGen (std::pow (10.0f, -8.4f / 20.0f),
                                                      telescope::test::kPinkSeedB), kSeconds, "ref_pink.wav"));

    const auto f = live.frame();
    REQUIRE (f.liveValid);
    REQUIRE (f.refValid);
    REQUIRE (std::abs (f.liveIntegrated - f.refIntegrated - 6.0f) < 0.2f);   // los niveles SÍ difieren

    float worst = 0.0f;
    int   worstBand = -1, compared = 0;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        const double hz = telescope::kThirdOctaveHz[b];
        if (hz < 50.0 || hz > 10000.0) continue;
        if (! f.bandValid[b]) continue;
        ++compared;
        std::printf ("REF[nivel]   %6.0f Hz  vivo=%+8.3f (norm %+8.3f)  ref=%+8.3f (norm %+8.3f)  "
                     "delta=%+.3f dB\n", hz, f.liveBands[b], f.liveNorm[b], f.refBands[b], f.refNorm[b],
                     f.deltaDb[b]);
        if (std::abs (f.deltaDb[b]) > worst) { worst = std::abs (f.deltaDb[b]); worstBand = b; }
    }

    std::printf ("REF[nivel] vivo I=%+.3f LUFS  ·  ref I=%+.3f LUFS  ·  %d bandas entre 50 Hz y 10 kHz  ·  "
                 "peor delta: %.0f Hz %+.3f dB (tol 0.7)\n", f.liveIntegrated, f.refIntegrated, compared,
                 worstBand >= 0 ? telescope::kThirdOctaveHz[worstBand] : 0.0, worst);

    REQUIRE (compared >= 22);
    REQUIRE (worst <= 0.7f);
}

// ============================================================================================ 2 · un tilt
//
// LA PRUEBA COMPLETA, no dos regiones: el delta de CADA banda tiene que reproducir la respuesta del shelf,
// corrida por una constante. Y esa constante no es arbitraria — es exactamente cuánto subió la loudness:
//
//     live_norm[b] = (ref_b + shelf_b) − I_live
//     ref_norm [b] =  ref_b            − I_ref
//     delta    [b] =  shelf_b − (I_live − I_ref)
//
// Ésa es la propiedad que hay que entender de TONAL BALANCE, y este test la escribe como identidad: el
// delta NO es la ganancia que aplicaste, es la ganancia MENOS lo que esa ganancia le hizo a la loudness.
// Por eso un shelf de +6 dB en los agudos se ve como "+3.3 arriba y −2.7 abajo" y no como "+6 y 0": son
// la misma verdad, contada a igual volumen, que es como se compara una mezcla contra una referencia.
TEST_CASE ("telescope: un tilt real se ve como tilt", "[telescope][ref]")
{
    constexpr double kSeconds = 10.0;
    constexpr double kShelfHz = 2000.0, kShelfDb = 6.0;
    const float peak = std::pow (10.0f, -8.4f / 20.0f);

    HighShelf shelfL { kShelfHz, kShelfDb, kSr }, shelfR { kShelfHz, kShelfDb, kSr };

    LiveRun live;
    live.feed (pinkPlusHighShelf (peak, telescope::test::kPinkSeedA, shelfL, shelfR), kSeconds);
    // La MISMA realización de ruido sin el shelf: lo único que cambia entre los dos lados es el filtro.
    live.reference.setReference (analysisOf (pinkGen (peak, telescope::test::kPinkSeedA), kSeconds,
                                             "ref_plano.wav"));

    const auto f = live.frame();
    REQUIRE (f.liveValid);
    REQUIRE (f.refValid);

    const float deltaLoudness = f.liveIntegrated - f.refIntegrated;
    const HighShelf model { kShelfHz, kShelfDb, kSr };

    float worst = 0.0f;
    int   worstBand = -1, compared = 0;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        if (! f.bandValid[b]) continue;
        const double expected = model.bandDb (b, kSr);
        const double got      = (double) f.deltaDb[b] + (double) deltaLoudness;
        const double err      = std::abs (got - expected);
        const int    bins     = live.reference.binsInBand (b);

        std::printf ("REF[tilt]    %6.0f Hz  delta=%+7.3f  +ΔL=%+7.3f  shelf=%+7.3f  error=%.3f dB  "
                     "(%d bins)%s\n", telescope::kThirdOctaveHz[b], f.deltaDb[b], got, expected, err, bins,
                     bins < 4 ? "   (menos de 4 bins: informativa)" : "");

        if (bins < 4) continue;
        ++compared;
        if ((float) err > worst) { worst = (float) err; worstBand = b; }
    }

    const double tilt = model.bandDb (bandOf (16000.0), kSr) - model.bandDb (bandOf (100.0), kSr);
    std::printf ("REF[tilt] shelf +%.0f dB a %.0f Hz  ·  el shelf subio la loudness %+.3f dB  ·  "
                 "tilt del modelo 100 Hz -> 16 kHz = %+.3f dB  ·  peor error: %.0f Hz %.3f dB "
                 "(tol 0.5, %d bandas)\n", kShelfDb, kShelfHz, deltaLoudness, tilt,
                 worstBand >= 0 ? telescope::kThirdOctaveHz[worstBand] : 0.0, worst, compared);

    REQUIRE (compared >= 15);
    REQUIRE (worst <= 0.5f);                         // el delta ES la respuesta del shelf, banda por banda
    REQUIRE (std::abs (tilt - kShelfDb) <= 0.3);     // y el shelf del modelo es el shelf que se pidió
    REQUIRE (deltaLoudness > 0.5f);                  // subir los agudos SUBE la loudness: por eso el delta
                                                      // no puede ser "+6 arriba y 0 abajo"
}

// ==================================================================================== 3 · sin referencia
TEST_CASE ("telescope: sin referencia el delta es cero y el lado vivo sigue funcionando", "[telescope][ref]")
{
    LiveRun live;
    live.feed (pinkGen (0.3f, telescope::test::kPinkSeedA), 6.0);

    const auto f = live.frame();
    REQUIRE_FALSE (f.refValid);
    REQUIRE (f.liveValid);

    int measured = 0, nonZeroDelta = 0, anyBandValid = 0;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        if (f.liveBands[b] > telescope::SpectrumFrame::kFloorDb) ++measured;
        if (f.deltaDb[b] != 0.0f) ++nonZeroDelta;
        if (f.bandValid[b]) ++anyBandValid;
    }
    std::printf ("REF[sin-ref] refValid=%d  liveValid=%d  liveSeconds=%.3f  bandas vivas medidas=%d  "
                 "deltas != 0: %d  bandas comparables: %d  refName=\"%s\"\n",
                 (int) f.refValid, (int) f.liveValid, f.liveSeconds, measured, nonZeroDelta, anyBandValid,
                 f.refName);

    REQUIRE (measured >= 25);
    REQUIRE (nonZeroDelta == 0);
    REQUIRE (anyBandValid == 0);
    REQUIRE (juce::String (f.refName).isEmpty());
}

// ================================================================================ 4 · liveValid a los 3 s
TEST_CASE ("telescope: la curva viva no vale hasta los 3 segundos", "[telescope][ref]")
{
    LiveRun live;
    const auto gen = pinkGen (0.3f, telescope::test::kPinkSeedA);

    // De a 0.5 s, mirando en qué momento pasa a válida.
    double firstValidAt = -1.0;
    for (int step = 1; step <= 10; ++step)
    {
        live.feed (gen, 0.5);
        const auto f = live.frame();
        std::printf ("REF[3s] tras %.1f s empujados: liveSeconds=%.3f  liveValid=%d\n",
                     0.5 * step, f.liveSeconds, (int) f.liveValid);
        if (f.liveValid && firstValidAt < 0.0) firstValidAt = f.liveSeconds;
        // Nunca válida por debajo del mínimo declarado.
        if (f.liveSeconds < ReferenceFrame::kMinLiveSeconds) REQUIRE_FALSE (f.liveValid);
    }

    std::printf ("REF[3s] primera vez valida con %.3f s promediados (minimo %.1f)\n",
                 firstValidAt, ReferenceFrame::kMinLiveSeconds);
    REQUIRE (firstValidAt >= (double) ReferenceFrame::kMinLiveSeconds);
    REQUIRE (firstValidAt < (double) ReferenceFrame::kMinLiveSeconds + 0.6);
}

// ============================================================================================= 5 · RESET
TEST_CASE ("telescope: el reset limpia el acumulador vivo y no la referencia", "[telescope][ref]")
{
    LiveRun live;
    live.feed (pinkGen (0.3f, telescope::test::kPinkSeedA), 5.0);
    live.reference.setReference (analysisOf (pinkGen (0.3f, telescope::test::kPinkSeedB), 5.0, "ref.wav"));

    const auto before = live.frame();
    REQUIRE (before.liveValid);
    REQUIRE (before.refValid);

    live.reference.resetLive();
    const auto after = live.frame();

    std::printf ("REF[reset] antes: liveSeconds=%.3f liveValid=%d refValid=%d refName=\"%s\"  |  "
                 "despues: liveSeconds=%.3f liveValid=%d refValid=%d refName=\"%s\"\n",
                 before.liveSeconds, (int) before.liveValid, (int) before.refValid, before.refName,
                 after.liveSeconds, (int) after.liveValid, (int) after.refValid, after.refName);

    REQUIRE (after.liveSeconds == 0.0f);
    REQUIRE_FALSE (after.liveValid);
    REQUIRE (after.refValid);                                      // la referencia NO se toca
    REQUIRE (juce::String (after.refName) == juce::String (before.refName));
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        REQUIRE (after.refBands[b] == before.refBands[b]);
}

// ====================================================================================== 6 · bloque al bit
TEST_CASE ("telescope: la curva viva no depende del tamano de bloque", "[telescope][ref]")
{
    constexpr double kSeconds = 4.0;

    const auto bandsWithBlock = [] (int block)
    {
        LiveRun live;
        live.feed (pinkGen (0.3f, telescope::test::kPinkSeedA), kSeconds, block);
        return live.frame();
    };

    const auto ref = bandsWithBlock (4096);
    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto f = bandsWithBlock (block);
        int same = 0, measured = 0;
        for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        {
            if (ref.liveBands[b] <= telescope::SpectrumFrame::kFloorDb) continue;
            ++measured;
            if (f.liveBands[b] == ref.liveBands[b]) ++same;
        }
        std::printf ("REF[bloque] %5d → %d de %d bandas iguales AL BIT  ·  liveSeconds=%.6f  I=%+.6f\n",
                     block, same, measured, f.liveSeconds, f.liveIntegrated);
        REQUIRE (measured >= 25);
        REQUIRE (same == measured);
        REQUIRE (f.liveSeconds == ref.liveSeconds);
        REQUIRE (f.liveIntegrated == ref.liveIntegrated);
    }
}
