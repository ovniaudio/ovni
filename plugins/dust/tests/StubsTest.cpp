// StubsTest.cpp — [alias][latency][bench][dust]: stubs parametrizados del template sobre el
// DustProcessor REAL (contrato de 3 defines, shared/template/tests/_README.md).
//
//  · [alias]  — gate fino < −96 dBFS (house-standard §2, lo evalúa validate.sh sobre ALIAS_DBFS=).
//               QUÉ se mide y por qué este setup (MEDIDO con instrumentación, no asumido): el
//               camino de señal de DUST es LINEAL (lecturas Lagrange + banco FIR HRIR + scatter por
//               ley de potencia + limiter EN CIRCUITO, que a −8 dBFS no actúa: minLimGain=1.0
//               instrumentado). Medido así da −109…−112 dBFS = el piso de la PROPIA ventana FFT
//               (un seno puro por la misma matemática da −112): cero foldback.
//               RATE=20 ms (el tren de ecos y su transitorio de encendido terminan ANTES del
//               warmup fijo del stub, 40 bloques) y DENSIDAD=0 / VIDA=0 porque su contenido es
//               TIEMPO-VARIANTE LEGÍTIMO, no alias: el lazo re-circula el ESCALÓN de encendido del
//               seno de prueba (−16.5 dB por vuelta: con RATE default tarda ~3 s en caer bajo el
//               piso, mucho más que la ventana del stub) y la deriva de VIDA es AM del propio
//               efecto (sidebands = la burbuja moviéndose, igual que medir un chorus). Con 3
//               burbujas reales sonando por el banco HRIR el MECANISMO del camino queda encendido;
//               el comportamiento con feedback/deriva lo miden [real]/[measure]/[stability].
//  · [latency]— 0 declarada == 0 real ±1 (el dry no se retarda; MIX=0 aísla el camino directo).
//  · [bench]  — CPU% de una instancia con los DEFAULTS del plugin (preset de carga realista).
//               Budget de la familia Movimiento: ≤ 3% (WARN en validate vía CPU_BUDGET).
//  · [null]   — bypass = pass-through BIT-EXACT (qa-listening-checklist §2: ganancia unitaria).
//  · [staterecall] — getState→setState restaura el MISMO sonido (round-trip a nivel señal).
//  · [transient]   — true-peak inter-sample ≤ 0.97 con transitorios full-scale y el PEOR caso de
//               nube (DENSIDAD 100, wet pleno): el limiter 0.85 deja margen inter-sample real.
#define OVNI_PLUGIN_PROCESSOR dust::DustProcessor
#define OVNI_PLUGIN_SLUG      "dust"
#define OVNI_PLUGIN_TAG       "[dust]"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// Setup del [alias] (ver bloque de arriba): wet pleno, tren corto asentado, sin lazo ni deriva.
#define OVNI_ALIAS_SETUP(proc)                                                       \
    do {                                                                             \
        ovni::test::setParam ((proc), dust::params::id::MIX,     1.0f);              \
        ovni::test::setParam ((proc), dust::params::id::RATE,    0.0f);  /* 20 ms */ \
        ovni::test::setParam ((proc), dust::params::id::DENSITY, 0.0f);              \
        ovni::test::setParam ((proc), dust::params::id::VIDA,    0.0f);              \
    } while (false)

// Setup del [transient]: el peor caso de pico (como el [gain]) — wet pleno + nube infinita + campo
// y deriva al máximo + RATE 100 ms (norm log ≈ 0.35: ln(100/20)/ln(2000/20)). Mecanismo ENCENDIDO.
#define OVNI_TRANSIENT_SETUP(proc)                                                   \
    do {                                                                             \
        ovni::test::setParam ((proc), dust::params::id::MIX,     1.0f);              \
        ovni::test::setParam ((proc), dust::params::id::DENSITY, 1.0f);              \
        ovni::test::setParam ((proc), dust::params::id::SPREAD,  1.0f);              \
        ovni::test::setParam ((proc), dust::params::id::VIDA,    1.0f);              \
        ovni::test::setParam ((proc), dust::params::id::RATE,    0.35f);             \
    } while (false)

#include "template/tests/AliasStub.h"        // [alias][dust]       -> ALIAS_DBFS=
#include "template/tests/LatencyStub.h"      // [latency][dust]     -> LATENCY_REPORTED= / LATENCY_REAL=
#include "template/tests/BenchStub.h"        // [bench][dust]       -> CPU_PCT=
#include "template/tests/NullStub.h"         // [null][dust]        -> bypass bit-exact
#include "template/tests/StateRecallStub.h"  // [staterecall][dust] -> round-trip de estado
#include "template/tests/TransientStub.h"    // [transient][dust]   -> true-peak inter-sample ≤ 0.97

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// [bench] PEOR CASO (hallazgo de review: el budget no puede medirse SOLO en el caso cómodo).
// El CPU_PCT= oficial mide los DEFAULTS (convención del harness: costo realista de carga). Acá se
// publica además el PEOR caso real — DENSIDAD 100 (24 taps vivos), SPREAD 100 (los 16 buses
// encendidos, sin skip de cola), VIDA 100 (deriva activa) y wet pleno: por ENCIMA del preset de
// fábrica más caro ("Tormenta de Polvo", Dens 88). Imprime CPU_WORST_PCT= (clave DISTINTA de
// CPU_PCT=: measure-check.sh grepea la oficial; ésta se declara en el QA doc).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST: CPU del PEOR caso (DENSIDAD/SPREAD/VIDA 100, wet pleno)", "[bench][dust]")
{
    using namespace ovni::test;
    namespace pid = dust::params::id;
    dust::DustProcessor proc;
    const double SR = 48000.0; const int N = 512;

    setParam (proc, pid::MIX,     1.0f);
    setParam (proc, pid::DENSITY, 1.0f);
    setParam (proc, pid::SPREAD,  1.0f);
    setParam (proc, pid::VIDA,    1.0f);
    proc.prepareToPlay (SR, N);

    auto fill = [&] (juce::AudioBuffer<float>& buf, int blk)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
                d[i] = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (blk * N + i) / (float) SR);
        }
    };

    // Pre-carga larga: a DENSIDAD 100 la nube tarda en poblarse (1 nacimiento por bloque).
    for (int blk = 0; blk < 200; ++blk)
    {
        juce::AudioBuffer<float> b (2, N); juce::MidiBuffer m;
        fill (b, blk);
        proc.processBlock (b, m);
    }

    constexpr int kBlocks = 10000;   // misma metodología que el [bench] oficial (comparable 1:1)
    juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int blk = 0; blk < kBlocks; ++blk) { fill (buf, blk + 200); proc.processBlock (buf, midi); }
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double cpuPct = 100.0 * std::chrono::duration<double> (t1 - t0).count()
                        / ((double) kBlocks * N / SR);
    std::printf ("CPU_WORST_PCT=%.3f\n", cpuPct);   // número DECLARADO de peor caso (QA doc)
    REQUIRE (std::isfinite (cpuPct));
    REQUIRE (cpuPct < 80.0);   // cordura (el budget 3% es del caso default; el peor caso se DECLARA)
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// [alias] con la REGENERACIÓN ENCENDIDA (hallazgo de review: el número oficial se mide con
// DENSIDAD 0 — razonamiento válido y documentado arriba, pero el escéptico merece el número con el
// mecanismo más característico SONANDO). DENSIDAD 60 + RATE 20 ms + VIDA 0: en régimen el camino es
// LTI (ganancias/feedback asentados, sin deriva) y el lazo recircula el seno SIN transponer
// frecuencia → el piso esperado es el de la propia ventana FFT, igual que el oficial. Warmup largo
// (600 bloques ≈ 6.4 s; el escalón de encendido recirculado cae ~−9.5 dB por vuelta de ~340 ms →
// < −140 dB al capturar). Imprime ALIAS_FB_DBFS= (clave distinta: el público sigue siendo
// ALIAS_DBFS= del stub).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace
{
    // Réplica local de alias_detail::spuriaFloorDb con lazo encendido + warmup configurable
    // (la del stub fija 40 bloques: insuficiente con feedback).
    double spuriaFloorDbConLazo (double sr, float fHz, int warmupBlocks)
    {
        dust::DustProcessor proc;
        ovni::test::setParam (proc, dust::params::id::MIX,     1.0f);
        ovni::test::setParam (proc, dust::params::id::RATE,    0.0f);   // 20 ms
        ovni::test::setParam (proc, dust::params::id::DENSITY, 0.6f);   // lazo ENCENDIDO (fb ≈ 0.33)
        ovni::test::setParam (proc, dust::params::id::VIDA,    0.0f);   // sin deriva: régimen LTI
        const int N = 512;
        proc.prepareToPlay (sr, N);

        const float amp = juce::Decibels::decibelsToGain (-8.0f);       // house-standard §2
        constexpr int kFftOrder = 14;
        constexpr int kFftSize  = 1 << kFftOrder;
        fHz = (float) (std::round ((double) fHz * (double) kFftSize / sr) * sr / (double) kFftSize);

        std::vector<float> cap ((size_t) kFftSize, 0.0f);
        int filled = 0, produced = 0; long phase = 0;
        while (filled < kFftSize)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < N; ++i)
                    d[i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi
                                                   * (double) fHz * (double) (phase + i) / sr);
            }
            phase += N;
            proc.processBlock (buf, midi);
            ++produced;
            if (produced < warmupBlocks) continue;   // ventana TARDÍA: el lazo ya está en régimen
            const float* a = buf.getReadPointer (0);
            for (int i = 0; i < N && filled < kFftSize; ++i, ++filled)
                cap[(size_t) filled] = a[i];
        }

        juce::dsp::FFT fft (kFftOrder);
        std::vector<float> fd ((size_t) kFftSize * 2, 0.0f);
        for (int i = 0; i < kFftSize; ++i)
        {
            const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                      * (float) i / (float) (kFftSize - 1));
            fd[(size_t) i] = cap[(size_t) i] * win;
        }
        fft.performRealOnlyForwardTransform (fd.data());

        const double tol = 4.0 * sr / (double) kFftSize;   // ±4 bins de la fundamental = legítimo
        double fund = 1e-20, worst = 1e-20;
        for (int k = 1; k < kFftSize / 2; ++k)
        {
            const double hz = (double) k * sr / (double) kFftSize;
            const float  re = fd[(size_t) (2 * k)], im = fd[(size_t) (2 * k + 1)];
            const double m  = std::sqrt ((double) re * re + (double) im * im);
            if (std::abs (hz - (double) fHz) <= tol) { fund = std::max (fund, m); continue; }
            if (hz < 20.0) continue;                        // DC / sub-graves: no es espuria audible
            worst = std::max (worst, m);
        }
        return 20.0 * std::log10 (worst / fund);
    }
}

TEST_CASE ("DUST: piso de espurias con la regeneración ENCENDIDA (DENSIDAD 60, ventana tardía)",
           "[alias][dust]")
{
    double worst = -300.0;
    for (float f : { 440.0f, 4000.0f })
        worst = std::max (worst, spuriaFloorDbConLazo (48000.0, f, 600));

    std::printf ("ALIAS_FB_DBFS=%.2f\n", worst);
    INFO ("piso de espurias con el lazo encendido = " << worst
          << " dBFS (esperado: el mismo piso de la ventana FFT que el oficial — camino lineal)");
    REQUIRE (std::isfinite (worst));
    REQUIRE (worst < -96.0);   // el piso del sello se sostiene también con la regeneración sonando
}
