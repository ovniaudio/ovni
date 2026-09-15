// [telescope][bands] — el módulo StereoBands (spec §5.3, prompt 51): el estéreo POR BANDA DE FRECUENCIA.
// Es el diferencial (a) del producto: no "la mezcla está fuera de fase", sino EN QUÉ FRECUENCIAS lo está.
//
// La matemática es la del módulo Stereo llevada a los bins de la STFT (Parseval). Para la banda `b`
// (⅓ de octava ISO 266) sobre los `K` frames de la ventana y los bins `k` de la banda:
//
//   ΣLL = Σ|L_k|²   ΣRR = Σ|R_k|²   ΣLR = Σ Re(L_k·R_k*)   ΣMM = Σ|(L+R)/2|²   ΣSS = Σ|(L−R)/2|²
//
//   corr_b     = ΣLR / √(ΣLL·ΣRR)                          +1 mono · 0 sin correlación · −1 fuera de fase
//   width_b    = √(ΣSS / ΣMM)                              0 mono · 1 independientes · tope 10
//   balance_b  = 10·log10(ΣRR / ΣLL)                       + = R más fuerte
//   monoLoss_b = 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2)     0 si L=R · −3.01 indep · piso −60
//
// Y por BIN, para el espectrograma estéreo: coh_k = ΣLR_k / √(ΣLL_k·ΣRR_k) suavizada sobre la MISMA
// ventana (la de un frame solo es puro ruido), 0 por definición cuando un canal no tiene energía.
//
// EL CRUCE QUE NO SE NEGOCIA: la banda ancha calculada desde los bins (todos los bins, todos los frames de
// 1 s) tiene que coincidir con el módulo Stereo del dominio del TIEMPO con la misma ventana. Si no
// coinciden, uno de los dos está mal — Parseval no opina.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "TestSignals.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/Stereo.h"
#include "analysis/modules/StereoBands.h"

using telescope::Spectrum;
using telescope::SpectrumFrame;
using telescope::Stereo;
using telescope::StereoBands;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kTwoPi = 6.283185307179586476925286766559;

Spectrum::Settings baseSettings()
{
    Spectrum::Settings s;
    s.fftOrder     = 12;              // 4 096 → bin de 11.71875 Hz, 46.875 frames/s con 75 % de solape
    s.window       = Spectrum::hann;
    s.overlapIndex = 1;               // 75 %
    s.channel      = Spectrum::left;  // el canal de la LENTE de espectro: StereoBands no depende de él
    s.avgMode      = Spectrum::avgNone;
    s.peakHold     = false;
    s.rangeDbIndex = 1;               // 90 dB
    return s;
}

enum class Case { same, inverted, independent, leftOnly, silence, mixed };

const char* caseName (Case c)
{
    switch (c)
    {
        case Case::same:        return "L=R";
        case Case::inverted:    return "L=-R";
        case Case::independent: return "independientes";
        case Case::leftOnly:    return "solo L";
        case Case::silence:     return "silencio";
        default:                return "mixta";
    }
}

// Genera la señal del caso. `mixed` = seno de 80 Hz en L = R (graves MONO) + ruido rosa pasa-altos de
// 2 kHz con R = −L (agudos FUERA DE FASE): la señal que este módulo existe para delatar.
struct Signal
{
    explicit Signal (Case c) : kind (c) {}

    std::pair<float, float> next() noexcept
    {
        if (kind == Case::mixed) return mixed.next();

        const float x = a.next();
        const float y = b.next();
        switch (kind)
        {
            case Case::same:        return { x, x };
            case Case::inverted:    return { x, -x };
            case Case::independent: return { x, y };
            case Case::leftOnly:    return { x, 0.0f };
            default:                return { 0.0f, 0.0f };
        }
    }

    Case kind;
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    telescope::test::MonoLowPhaseHigh mixed { kSr };
};

// Arma módulo + señal y devuelve el resultado tras `seconds`. Devuelve también el módulo por referencia
// para los tests que además miran la coherencia por bin.
void feed (Spectrum& m, Signal& sig, double seconds, int block = 512)
{
    std::vector<float> L ((size_t) block), R ((size_t) block);
    const auto total = (long long) std::llround (seconds * kSr);
    for (long long done = 0; done < total; )
    {
        const int k = (int) std::min ((long long) block, total - done);
        for (int i = 0; i < k; ++i)
        {
            const auto v = sig.next();
            L[(size_t) i] = v.first;
            R[(size_t) i] = v.second;
        }
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// La tabla que lee la auditora: las 30 bandas con su número de bins.
void printBands (const char* label, const StereoBands::Result& r)
{
    for (int b = 0; b < StereoBands::kNumBands; ++b)
        std::printf ("BANDS[%-14s] %7.1f Hz  bins=%3d  corr=%+7.4f  width=%7.4f  bal=%+8.3f dB  "
                     "mono=%+8.3f dB  E=%8.2f dB\n",
                     label, telescope::kThirdOctaveHz[b], r.binsInBand[b], r.corr[b], r.width[b],
                     r.balanceDb[b], r.monoLossDb[b], r.energyDb[b]);
    std::printf ("BANDS[%-14s] ventana efectiva = %.3f s (%d frames)   bandas con energia = %d\n",
                 label, r.windowSeconds, r.windowFrames, r.valid);
}

// El índice de la banda ⅓ de octava cuyo centro es `hz` (las 30 de ISO 266).
int bandAt (double hz)
{
    for (int b = 0; b < StereoBands::kNumBands; ++b)
        if (std::abs (telescope::kThirdOctaveHz[b] / hz - 1.0) < 0.02) return b;
    return -1;
}
}

// ======================================================================================== 1 · L = R
TEST_CASE ("telescope: por banda, L = R es mono perfecto", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);   // 1 s

    Signal sig { Case::same };
    feed (m, sig, 5.0);

    const auto r = bands.result();
    printBands (caseName (Case::same), r);

    int checked = 0;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        if (r.binsInBand[b] == 0 || r.energyDb[b] < -140.0f) continue;
        INFO ("banda " << telescope::kThirdOctaveHz[b] << " Hz");
        REQUIRE_THAT (r.corr[b],       Catch::Matchers::WithinAbs (1.0f, 0.001f));
        REQUIRE_THAT (r.width[b],      Catch::Matchers::WithinAbs (0.0f, 0.001f));
        REQUIRE_THAT (r.monoLossDb[b], Catch::Matchers::WithinAbs (0.0f, 0.01f));
        REQUIRE_THAT (r.balanceDb[b],  Catch::Matchers::WithinAbs (0.0f, 0.01f));
        ++checked;
    }
    REQUIRE (checked >= 25);
    REQUIRE (r.valid == checked);
}

// ======================================================================================== 2 · L = -R
TEST_CASE ("telescope: por banda, L = -R cancela al monoficar", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);

    Signal sig { Case::inverted };
    feed (m, sig, 5.0);

    const auto r = bands.result();
    printBands (caseName (Case::inverted), r);

    int checked = 0;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        if (r.binsInBand[b] == 0 || r.energyDb[b] < -140.0f) continue;
        INFO ("banda " << telescope::kThirdOctaveHz[b] << " Hz");
        REQUIRE_THAT (r.corr[b], Catch::Matchers::WithinAbs (-1.0f, 0.001f));
        REQUIRE (r.monoLossDb[b] <= -60.0f);          // −∞ clampeado al piso
        REQUIRE (std::isfinite (r.monoLossDb[b]));
        REQUIRE (std::isfinite (r.width[b]));
        REQUIRE (r.width[b] <= StereoBands::kWidthMax);
        ++checked;
    }
    REQUIRE (checked >= 25);
}

// ================================================================================ 3 · independientes
// Dos fuentes decorrelacionadas: corr ~ 0 y −3.01 dB al monoficar, POR BANDA.
//
// LA TOLERANCIA SE DERIVA, NO SE AFLOJA. Un estimador de correlación sobre ruido no da 0 exacto: da 0 con
// una dispersión que fija la cantidad de grados de libertad que entraron en la cuenta. Para una banda de
// ancho `BW` y una ventana de `T` segundos son `BW·T` muestras complejas independientes (el tamaño de FFT
// no cambia eso: más bins por banda es exactamente lo mismo que menos ventanas independientes), y de ahí
//
//     σ(corr) ≈ 1 / √(2·BW·T)
//
// La banda de ⅓ de octava centrada en `fc` mide BW = fc·(2^(1/6) − 2^(−1/6)) = 0.2316·fc, así que con
// T = 1 s la de 10 kHz tiene 2 316 grados de libertad (σ = 0.015) y la de 50 Hz tiene 12 (σ = 0.21). El
// ±0.08 del criterio es alcanzable de 6.3 kHz para arriba y FÍSICAMENTE IMPOSIBLE en los graves: no es
// que el módulo mida peor abajo, es que abajo hay menos señal por segundo. Así que el criterio por banda
// es `max(0.08, 4σ)` — se imprimen los grados de libertad, el σ y el límite de cada banda para que el
// número se pueda auditar, y aparte se verifica el ±0.08 liso donde sí corresponde.
//
// Para monoLoss la propagación es exacta: monoLoss = 10·log10((1+corr)/2), o sea 4.34 dB por unidad de
// corr alrededor de 0 → el límite es `max(0.4, 4.34·límite_corr)`.
TEST_CASE ("telescope: por banda, dos fuentes independientes dan corr 0 y -3 dB al monoficar", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);   // 1 s

    Signal sig { Case::independent };
    feed (m, sig, 10.0);

    const auto r = bands.result();
    printBands (caseName (Case::independent), r);

    constexpr double kThirdOctBw = 1.1224620483093730 - 0.8908987181403393;   // 0.23156
    const double T = (double) r.windowSeconds;

    int checked = 0, strict = 0;
    float worstStrict = 0.0f;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        const double hz = telescope::kThirdOctaveHz[b];
        if (hz < 50.0 || hz > 10000.0) continue;

        // BW·T (producto ancho de banda × tiempo) y los GRADOS DE LIBERTAD, que son 2·BW·T: una señal
        // real aporta dos números independientes —parte real e imaginaria— por celda tiempo-frecuencia.
        // El nombre y el número tienen que coincidir (nit del revisor del 51): antes se imprimía BW·T
        // rotulado "gl", que es la mitad de los grados de libertad que usa la sigma de abajo.
        const double bwT   = kThirdOctBw * hz * T;
        const double dof   = 2.0 * bwT;
        const double sigma = 1.0 / std::sqrt (dof);
        const double tolC  = std::max (0.08, 4.0 * sigma);
        const double tolM  = std::max (0.4, 4.34 * tolC);

        std::printf ("BANDS[indep %7.1f Hz] bins=%3d  BW.T=%8.1f  gl=2.BW.T=%9.1f  sigma=%.4f  ->  "
                     "|corr|=%.4f <= %.4f   |mono+3|=%.4f <= %.4f dB\n",
                     hz, r.binsInBand[b], bwT, dof, sigma, std::abs (r.corr[b]), tolC,
                     std::abs (r.monoLossDb[b] + 3.0f), tolM);

        INFO ("banda " << hz << " Hz con " << r.binsInBand[b] << " bins, BW.T = " << bwT
                       << " y " << dof << " grados de libertad");
        REQUIRE_THAT (r.corr[b],       Catch::Matchers::WithinAbs (0.0f, (float) tolC));
        REQUIRE_THAT (r.monoLossDb[b], Catch::Matchers::WithinAbs (-3.0f, (float) tolM));
        ++checked;

        // Donde los grados de libertad alcanzan, el criterio liso del prompt tiene que cumplirse solo.
        if (4.0 * sigma <= 0.08)
        {
            worstStrict = std::max (worstStrict, std::abs (r.corr[b]));
            REQUIRE_THAT (r.corr[b],       Catch::Matchers::WithinAbs (0.0f, 0.08f));
            REQUIRE_THAT (r.monoLossDb[b], Catch::Matchers::WithinAbs (-3.0f, 0.4f));
            ++strict;
        }
    }
    std::printf ("BANDS[independientes] 50 Hz..10 kHz: %d bandas  ·  %d de ellas con grados de libertad de "
                 "sobra para el +-0.08 liso, |corr| peor ahi = %.4f\n", checked, strict, worstStrict);
    REQUIRE (checked >= 20);
    REQUIRE (strict >= 3);
}

// ========================================================================================== 4 · sólo L
TEST_CASE ("telescope: por banda, solo L manda el balance al piso y deja corr sin definir", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);

    Signal sig { Case::leftOnly };
    feed (m, sig, 5.0);

    const auto r = bands.result();
    printBands (caseName (Case::leftOnly), r);

    int checked = 0;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        if (r.binsInBand[b] == 0 || r.energyDb[b] < -140.0f) continue;
        INFO ("banda " << telescope::kThirdOctaveHz[b] << " Hz");
        REQUIRE (r.balanceDb[b] <= -59.0f);
        REQUIRE (std::isfinite (r.balanceDb[b]));
        REQUIRE_THAT (r.corr[b], Catch::Matchers::WithinAbs (0.0f, 0.0001f));   // no hay correlación DEFINIDA
        REQUIRE_THAT (r.width[b], Catch::Matchers::WithinAbs (1.0f, 0.01f));    // M = S = L/2
        ++checked;
    }
    REQUIRE (checked >= 25);

    // Coherencia por bin: con un canal mudo es 0 POR DEFINICIÓN (no hay dos fases que comparar).
    const auto& f = bands.frame();
    float worst = 0.0f;
    for (int k = 0; k < f.numBins; ++k) worst = std::max (worst, std::abs (f.coh[k]));
    std::printf ("BANDS[solo L] coherencia por bin: |coh| peor sobre %d bins = %.6f  (criterio 0)\n",
                 f.numBins, worst);
    REQUIRE (worst == 0.0f);
}

// ======================================================================================== 5 · silencio
TEST_CASE ("telescope: por banda, en silencio no hay medicion y nada es NaN", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);

    Signal sig { Case::silence };
    feed (m, sig, 3.0);

    const auto r = bands.result();
    printBands (caseName (Case::silence), r);

    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        INFO ("banda " << telescope::kThirdOctaveHz[b] << " Hz");
        REQUIRE (std::isfinite (r.corr[b]));
        REQUIRE (std::isfinite (r.width[b]));
        REQUIRE (std::isfinite (r.balanceDb[b]));
        REQUIRE (std::isfinite (r.monoLossDb[b]));
        REQUIRE (r.corr[b] == 0.0f);
    }
    REQUIRE (r.valid == 0);

    const auto& f = bands.frame();
    for (int k = 0; k < f.numBins; ++k)
    {
        REQUIRE (f.coh[k] == 0.0f);
        REQUIRE (std::isfinite (f.energyDb[k]));
    }
}

// =========================================================================================== 6 · mixta
// LA SEÑAL QUE IMPORTA, y lo que ningún medidor gratis muestra: graves MONO (seno de 80 Hz en L = R) con
// agudos FUERA DE FASE (ruido rosa pasa-altos de 2 kHz con R = −L). En banda ancha el correlímetro daría
// un número intermedio que no dice nada; por banda se ve el corte exacto.
TEST_CASE ("telescope: por banda, graves mono con agudos fuera de fase se ven separados", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);

    Signal sig { Case::mixed };
    feed (m, sig, 6.0);

    const auto r = bands.result();
    printBands (caseName (Case::mixed), r);

    int low = 0, high = 0;
    for (int b = 0; b < StereoBands::kNumBands; ++b)
    {
        const double hz = telescope::kThirdOctaveHz[b];
        if (r.binsInBand[b] == 0 || r.energyDb[b] < -140.0f) continue;

        if (hz <= 160.0)
        {
            INFO ("banda grave " << hz << " Hz");
            REQUIRE (r.corr[b] > 0.95f);          // los graves están en fase: se monofican sin perder nada
            REQUIRE (r.monoLossDb[b] > -0.5f);
            ++low;
        }
        else if (hz >= 2500.0)
        {
            INFO ("banda aguda " << hz << " Hz");
            REQUIRE (r.corr[b] < -0.9f);          // los agudos están dados vuelta
            REQUIRE (r.monoLossDb[b] <= -20.0f);  // y al monoficar se van
            ++high;
        }
    }
    std::printf ("BANDS[mixta] bandas graves en fase = %d   ·   bandas agudas fuera de fase = %d\n", low, high);
    REQUIRE (low >= 3);
    REQUIRE (high >= 8);

    // La banda ancha del mismo material NO dice esto: es el argumento del producto, medido.
    std::printf ("BANDS[mixta] banda ancha: corr=%+.4f  mono=%+.3f dB  (la banda de 80 Hz: corr=%+.4f, "
                 "la de 4 kHz: corr=%+.4f)\n",
                 r.wideCorr, r.wideMonoLossDb, r.corr[bandAt (80.0)], r.corr[bandAt (4000.0)]);
}

// ============================================================================== 7 · cruce con Stereo
// PARSEVAL NO NEGOCIA. La banda ancha calculada desde los bins (todos los bins, todos los frames de la
// ventana) tiene que dar lo mismo que el módulo Stereo del dominio del tiempo con la misma ventana. Si no
// coinciden, uno de los dos está mal — y los dos se publican.
TEST_CASE ("telescope: la banda ancha desde los bins coincide con el modulo Stereo del tiempo", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);      // 1 s

    Stereo wide;
    wide.prepare (kSr);
    wide.setWindowMs (1000);       // 1 s

    Signal sig { Case::independent };
    {
        constexpr int block = 512;
        std::vector<float> L ((size_t) block), R ((size_t) block);
        const auto total = (long long) std::llround (10.0 * kSr);
        for (long long done = 0; done < total; )
        {
            const int k = (int) std::min ((long long) block, total - done);
            for (int i = 0; i < k; ++i)
            {
                const auto v = sig.next();
                L[(size_t) i] = v.first;
                R[(size_t) i] = v.second;
            }
            m.process (L.data(), R.data(), k);
            wide.process (L.data(), R.data(), k);
            done += k;
        }
    }

    const auto fromBins = bands.result();
    const auto fromTime = wide.result();
    std::printf ("BANDS[cruce Stereo] bins: corr=%+.5f width=%.5f bal=%+.4f mono=%+.4f (W=%.3f s)\n"
                 "BANDS[cruce Stereo] tiempo: corr=%+.5f width=%.5f bal=%+.4f mono=%+.4f (W=%.3f s)\n"
                 "BANDS[cruce Stereo] delta: corr=%+.5f (criterio 0.02)  mono=%+.4f dB (criterio 0.1)\n",
                 fromBins.wideCorr, fromBins.wideWidth, fromBins.wideBalanceDb, fromBins.wideMonoLossDb,
                 fromBins.windowSeconds,
                 fromTime.corr, fromTime.width, fromTime.balanceDb, fromTime.monoLossDb,
                 fromTime.windowSeconds,
                 fromBins.wideCorr - fromTime.corr, fromBins.wideMonoLossDb - fromTime.monoLossDb);

    REQUIRE_THAT (fromBins.wideCorr,       Catch::Matchers::WithinAbs (fromTime.corr, 0.02f));
    REQUIRE_THAT (fromBins.wideMonoLossDb, Catch::Matchers::WithinAbs (fromTime.monoLossDb, 0.1f));
}

// ================================================================================ 8 · coherencia por bin
// La coherencia de UN frame es puro ruido; suavizada sobre la ventana es un número. Con L = R vale +1
// exacto en todo bin con energía; con fuentes independientes se queda alrededor de 0 pero DISPERSA — y
// esa dispersión no es un defecto: es lo que un estimador de coherencia hace sobre ruido.
TEST_CASE ("telescope: la coherencia por bin lee +1 en mono y disperso en decorrelacionado", "[telescope][bands]")
{
    const auto run = [] (Case c, double seconds)
    {
        auto m     = std::make_unique<Spectrum>();
        auto bands = std::make_unique<StereoBands>();
        m->setFrameSink (bands.get());
        m->applySettings (baseSettings());
        m->prepare (kSr);
        bands->setWindowIndex (1);

        Signal sig { c };
        feed (*m, sig, seconds);

        std::vector<float> coh;
        const auto& f = bands->frame();
        for (int k = 1; k < f.numBins; ++k)
            if (f.energyDb[k] > -100.0f) coh.push_back (f.coh[k]);
        return coh;
    };

    {
        const auto coh = run (Case::same, 4.0);
        REQUIRE (coh.size() > 500);
        float worst = 0.0f;
        for (const float c : coh) worst = std::max (worst, std::abs (c - 1.0f));
        std::printf ("BANDS[coh L=R] %zu bins con energia  ·  peor desvio de +1 = %.6f  (criterio 0.001)\n",
                     coh.size(), worst);
        REQUIRE (worst < 0.001f);
    }

    {
        const auto coh = run (Case::independent, 10.0);
        REQUIRE (coh.size() > 500);
        double mean = 0.0;
        for (const float c : coh) mean += (double) c;
        mean /= (double) coh.size();
        double var = 0.0;
        for (const float c : coh) var += ((double) c - mean) * ((double) c - mean);
        const double sd = std::sqrt (var / (double) coh.size());
        std::printf ("BANDS[coh indep] %zu bins  ·  media = %+.5f (criterio |.| <= 0.1)  ·  desvio = %.5f "
                     "(criterio > 0.1)\n", coh.size(), mean, sd);
        REQUIRE (std::abs (mean) <= 0.1);
        REQUIRE (sd > 0.1);
    }
}

// ============================================================================== 9 · independencia del bloque
// Las posiciones de frame son las del módulo Spectrum (conteo de muestras desde el reset), así que el
// determinismo se hereda: las 30 bandas y los 2 049 valores de coherencia tienen que salir IDÉNTICOS AL
// BIT venga el audio en bloques de 1 o de 4 096.
TEST_CASE ("telescope: los numeros por banda no dependen del tamano de bloque", "[telescope][bands]")
{
    const auto run = [] (int block)
    {
        auto m     = std::make_unique<Spectrum>();
        auto bands = std::make_unique<StereoBands>();
        m->setFrameSink (bands.get());
        m->applySettings (baseSettings());
        m->prepare (kSr);
        bands->setWindowIndex (1);

        Signal sig { Case::mixed };
        feed (*m, sig, 3.0, block);

        struct Out { StereoBands::Result r; std::vector<float> coh; juce::uint32 frames; };
        Out o { bands->result(), {}, bands->framesEmitted() };
        const auto& f = bands->frame();
        o.coh.assign (f.coh, f.coh + f.numBins);
        return o;
    };

    const auto ref = run (512);
    REQUIRE (ref.frames > 100u);
    REQUIRE (ref.coh.size() == 2049u);

    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto o = run (block);
        INFO ("bloque " << block);
        REQUIRE (o.frames == ref.frames);
        REQUIRE (o.coh == ref.coh);
        for (int b = 0; b < StereoBands::kNumBands; ++b)
        {
            REQUIRE (o.r.corr[b]       == ref.r.corr[b]);
            REQUIRE (o.r.width[b]      == ref.r.width[b]);
            REQUIRE (o.r.balanceDb[b]  == ref.r.balanceDb[b]);
            REQUIRE (o.r.monoLossDb[b] == ref.r.monoLossDb[b]);
        }
    }
    std::printf ("BANDS[bloque 1/7/64/4096] %u frames  ·  30 bandas y %zu coherencias identicas al bit\n",
                 ref.frames, ref.coh.size());
}

// ========================================================================================= 10 · ventana
// Las tres ventanas (0.3 / 1 / 3 s) son las que dicen ser: K = round(sec · frames por segundo), y lo que
// se publica es la ventana EFECTIVA (la que realmente se sumó), no la pedida.
TEST_CASE ("telescope: la ventana por banda es la que dice", "[telescope][bands]")
{
    for (int idx = 0; idx < StereoBands::kNumWindowOptions; ++idx)
    {
        Spectrum m;
        StereoBands bands;
        m.setFrameSink (&bands);
        m.applySettings (baseSettings());
        m.prepare (kSr);
        bands.setWindowIndex (idx);

        Signal sig { Case::independent };
        feed (m, sig, 6.0);

        const float want = StereoBands::kWindowSecOptions[idx];
        const auto  r = bands.result();
        const int   expectedFrames = (int) std::lround ((double) want * m.frameRate());

        std::printf ("BANDS[ventana %.1f s] %d frames de %.3f frames/s -> %.4f s efectivos (esperado %d frames)\n",
                     want, r.windowFrames, m.frameRate(), r.windowSeconds, expectedFrames);
        REQUIRE (r.windowFrames == expectedFrames);
        REQUIRE_THAT (r.windowSeconds,
                      Catch::Matchers::WithinAbs ((float) ((double) expectedFrames / m.frameRate()), 1.0e-4f));
    }
}

// ==================================================================================== 11 · reactivación
// Regla del 49 llevada a este módulo: al reactivarse, la ventana arranca LIMPIA. Si no, el primer número
// tras volver a la lente mezclaría hasta tres segundos de audio de antes de apagarse con el de ahora.
TEST_CASE ("telescope: al reiniciar, la ventana por banda arranca limpia", "[telescope][bands]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (2);   // 3 s: la ventana más larga, la que más arrastraría

    Signal same { Case::same };
    feed (m, same, 4.0);
    const int b1k = bandAt (1000.0);
    REQUIRE (bands.result().corr[b1k] > 0.99f);

    // El flanco de subida real (AnalysisThread::feedSpectrum) limpia LOS DOS: el ring de muestras del
    // espectro y la ventana de bandas. Las dos lentes que piden kStereoBands piden también kSpectrum, así
    // que se encienden juntas y se limpian juntas.
    m.reset();
    bands.reset();

    Signal inv { Case::inverted };
    feed (m, inv, 0.2);        // apenas unos frames de la señal NUEVA

    const auto r = bands.result();
    std::printf ("BANDS[reactivar] corr de la banda de 1 kHz tras 0.2 s de senal invertida = %+.4f  "
                 "(ventana efectiva %.3f s de los 3 s pedidos)\n", r.corr[b1k], r.windowSeconds);
    REQUIRE (r.corr[b1k] < -0.99f);
    REQUIRE (r.windowSeconds <= 0.15f);
}
