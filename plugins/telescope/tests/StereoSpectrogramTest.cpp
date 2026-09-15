// [telescope][sgram-st] — la COLUMNA del espectrograma ESTÉREO y su anillo de dos bytes por celda.
//
// Es el sonograma del 50 con el color cambiado de significado: donde aquel pone NIVEL, éste pone FASE.
// Cada celda son dos bytes:
//
//   [0] coherencia   0 = −1 (fuera de fase) · 128 = 0 (ancho / sin definir) · 255 = +1 (mono)
//   [1] energía      el mismo mapeo de dB del 50 sobre [−rango, 0]
//
// Lo que se verifica acá es el DATO, no el dibujo (el dibujo tiene su foto y su presupuesto):
//   L = R          toda celda con energía en 255 — mono perfecto es +1, no "casi".
//   L = −R         toda celda con energía en 0.
//   INDEPENDIENTES media alrededor de 128 pero DISPERSA: un estimador de coherencia sobre ruido no da
//                  cero clavado, y si diera cero clavado sería porque algo lo está aplastando.
//   MIXTA          graves mono arriba de 240, agudos fuera de fase abajo de 15: el corte, byte a byte.
//   SILENCIO       energía 0 y coherencia 128 (sin definir), nunca 0 (que querría decir "fuera de fase").
//   DETERMINISMO   dos corridas de la misma señal dan anillos idénticos byte a byte, en bloques de 512 y 64.
//   UN SOLO CANAL  L con señal y R en silencio da EXACTAMENTE la misma columna que R con señal y L en
//                  silencio — y eso se afirma, no se lamenta (ver abajo).
//
// LO QUE ESTOS TESTS NO PUEDEN VER, Y POR QUÉ (nit del revisor del 51). Ninguno de ellos detectaría que
// alguien intercambiara L y R en el motor. Los dos números de la celda son SIMÉTRICOS por construcción:
// la coherencia es Σ LR / √(ΣLL·ΣRR) —el producto y el radical no distinguen quién es quién— y la energía
// es ΣLL + ΣRR. Intercambiar los canales deja los dos bytes idénticos, así que ningún caso de esta tabla
// puede ponerse rojo por eso. No es un agujero del espectrograma estéreo: es lo que la lente 10 mide.
// La red que SÍ atrapa un L↔R invertido es `balanceDb` —10·log10(ΣRR/ΣLL), que cambia de signo— y vive
// en [bands] (StereoBandsTest.cpp, el caso "R más fuerte"). El caso 8 de acá mide las dos cosas juntas:
// que la columna sea idéntica y que el balance sea el opuesto.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "TestSignals.h"
#include "analysis/SpectrogramRing.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/StereoBands.h"

using telescope::Spectrum;
using telescope::StereoBands;
using telescope::StereoSpectrogramRing;

namespace
{
constexpr double kSr = 48000.0;
constexpr int    kRows = StereoSpectrogramRing::kRows;

Spectrum::Settings baseSettings()
{
    Spectrum::Settings s;
    s.fftOrder        = 12;
    s.window          = Spectrum::hann;
    s.overlapIndex    = 1;                 // 75 % → 46.875 columnas por segundo
    s.channel         = Spectrum::left;    // el canal de la lente de espectro no toca a StereoBands
    s.avgMode         = Spectrum::avgNone;
    s.peakHold        = false;
    s.rangeDbIndex    = 1;                 // 90 dB
    s.historySecIndex = 0;                 // 10 s
    return s;
}

enum class Case { same, inverted, independent, silence, mixed, leftOnly, rightOnly };

const char* caseName (Case c)
{
    switch (c)
    {
        case Case::same:        return "L=R";
        case Case::inverted:    return "L=-R";
        case Case::independent: return "independientes";
        case Case::silence:     return "silencio";
        case Case::leftOnly:    return "solo L";
        case Case::rightOnly:   return "solo R";
        default:                return "mixta";
    }
}

struct Rig
{
    Rig()
    {
        ring   = std::make_unique<StereoSpectrogramRing>();
        module = std::make_unique<Spectrum>();
        bands  = std::make_unique<StereoBands>();
        bands->setSpectrogramRing (ring.get());
        bands->setWindowIndex (1);          // 1 s
        module->setFrameSink (bands.get());
        module->applySettings (baseSettings());
        module->prepare (kSr);
    }

    void feed (Case c, double seconds, int block = 512)
    {
        telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        telescope::test::MonoLowPhaseHigh mixed { kSr };
        std::vector<float> L ((size_t) block), R ((size_t) block);
        const auto total = (long long) std::llround (seconds * kSr);

        for (long long done = 0; done < total; )
        {
            const int k = (int) std::min ((long long) block, total - done);
            for (int i = 0; i < k; ++i)
            {
                float l = 0.0f, r = 0.0f;
                switch (c)
                {
                    case Case::same:        l = a.next(); r = l; break;
                    case Case::inverted:    l = a.next(); r = -l; break;
                    case Case::independent: l = a.next(); r = b.next(); break;
                    case Case::silence:     break;
                    // Espejo EXACTO: las mismas muestras, del otro lado. Si el motor tratara distinto a L
                    // y a R, las dos columnas no podrían salir iguales.
                    case Case::leftOnly:    l = a.next(); r = 0.0f; break;
                    case Case::rightOnly:   r = a.next(); l = 0.0f; break;
                    default: { const auto v = mixed.next(); l = v.first; r = v.second; } break;
                }
                L[(size_t) i] = l;
                R[(size_t) i] = r;
            }
            module->process (L.data(), R.data(), k);
            done += k;
        }
    }

    std::unique_ptr<StereoSpectrogramRing> ring;
    std::unique_ptr<Spectrum>              module;
    std::unique_ptr<StereoBands>           bands;
};

// Estadística de la ÚLTIMA columna: coherencia de las celdas cuya energía pasa el piso.
struct ColumnStats
{
    int   cells = 0, worstLow = 255, worstHigh = 0;
    double mean = 0.0, sd = 0.0;
    int   minCoh = 255, maxCoh = 0;
};

ColumnStats statsOf (const juce::uint8* col, int minEnergy = 1)
{
    ColumnStats st;
    std::vector<int> coh;
    for (int row = 0; row < kRows; ++row)
    {
        if ((int) col[2 * row + 1] < minEnergy) continue;
        const int c = (int) col[2 * row];
        coh.push_back (c);
        st.minCoh = std::min (st.minCoh, c);
        st.maxCoh = std::max (st.maxCoh, c);
    }
    st.cells = (int) coh.size();
    if (st.cells == 0) return st;

    for (const int c : coh) st.mean += (double) c;
    st.mean /= (double) st.cells;
    for (const int c : coh) st.sd += ((double) c - st.mean) * ((double) c - st.mean);
    st.sd = std::sqrt (st.sd / (double) st.cells);
    return st;
}

void report (Case c, const ColumnStats& st)
{
    std::printf ("SGRAM-ST[%-14s] %3d celdas con energia  ·  coherencia media = %6.2f  desvio = %5.2f  "
                 "min = %3d  max = %3d\n", caseName (c), st.cells, st.mean, st.sd, st.minCoh, st.maxCoh);
}
}

// ============================================================================================ 1 · L = R
TEST_CASE ("telescope: en el espectrograma estereo, L = R pinta mono en toda celda con energia", "[telescope][sgram-st]")
{
    Rig rig;
    rig.feed (Case::same, 4.0);

    const auto* col = rig.bands->column();
    const auto st = statsOf (col);
    report (Case::same, st);

    REQUIRE (st.cells > 300);
    REQUIRE (st.minCoh >= 254);       // +1 = 255, y ±1 de tolerancia
    REQUIRE (st.maxCoh == 255);
}

// =========================================================================================== 2 · L = -R
TEST_CASE ("telescope: en el espectrograma estereo, L = -R pinta fuera de fase", "[telescope][sgram-st]")
{
    Rig rig;
    rig.feed (Case::inverted, 4.0);

    const auto st = statsOf (rig.bands->column());
    report (Case::inverted, st);

    REQUIRE (st.cells > 300);
    REQUIRE (st.maxCoh <= 1);         // −1 = 0, y ±1 de tolerancia
    REQUIRE (st.minCoh == 0);
}

// ==================================================================================== 3 · independientes
TEST_CASE ("telescope: en el espectrograma estereo, dos fuentes independientes quedan al medio y dispersas", "[telescope][sgram-st]")
{
    Rig rig;
    rig.feed (Case::independent, 6.0);

    const auto st = statsOf (rig.bands->column());
    report (Case::independent, st);

    REQUIRE (st.cells > 300);
    REQUIRE (std::abs (st.mean - 128.0) <= 12.0);
    REQUIRE (st.sd > 12.0);           // dispersa: es lo que hace un estimador de coherencia sobre ruido
}

// =============================================================================================== 4 · mixta
// EL CORTE, byte a byte: graves mono (seno de 80 Hz en L = R) y agudos fuera de fase (rosa pasa-altos de
// 2 kHz con R = −L). Es la misma señal del módulo y de la foto (TestSignals.h).
TEST_CASE ("telescope: en el espectrograma estereo, la senal mixta se corta en la mitad del eje", "[telescope][sgram-st]")
{
    Rig rig;
    rig.feed (Case::mixed, 5.0);

    const auto* col = rig.bands->column();
    int low = 0, high = 0, worstLow = 255, worstHigh = 0;
    for (int row = 0; row < kRows; ++row)
    {
        if ((int) col[2 * row + 1] < 1) continue;          // sin energía no hay color que juzgar
        const double hz = StereoSpectrogramRing::rowFrequency (row);
        const int    c  = (int) col[2 * row];

        if (hz <= 160.0)       { ++low;  worstLow  = std::min (worstLow, c); }
        else if (hz >= 2500.0) { ++high; worstHigh = std::max (worstHigh, c); }
    }
    std::printf ("SGRAM-ST[mixta] filas <=160 Hz con energia = %d (peor coherencia = %d, criterio >= 240)  ·  "
                 "filas >=2.5 kHz = %d (peor = %d, criterio <= 15)\n", low, worstLow, high, worstHigh);

    REQUIRE (low > 10);
    REQUIRE (high > 100);
    REQUIRE (worstLow >= 240);
    REQUIRE (worstHigh <= 15);
}

// ============================================================================================ 5 · silencio
// En silencio la coherencia es 128 (SIN DEFINIR), no 0. Un 0 querría decir "fuera de fase", que sobre
// silencio sería una alarma inventada.
TEST_CASE ("telescope: en silencio el espectrograma estereo no dice nada, y lo dice bien", "[telescope][sgram-st]")
{
    Rig rig;
    rig.feed (Case::silence, 3.0);

    const auto* col = rig.bands->column();
    int worstEnergy = 0, worstCohError = 0;
    for (int row = 0; row < kRows; ++row)
    {
        worstEnergy    = std::max (worstEnergy, (int) col[2 * row + 1]);
        worstCohError  = std::max (worstCohError, std::abs ((int) col[2 * row] - 128));
    }
    std::printf ("SGRAM-ST[silencio] energia maxima = %d (criterio 0)  ·  desvio maximo de la coherencia "
                 "respecto de 128 = %d (criterio 0)\n", worstEnergy, worstCohError);
    REQUIRE (worstEnergy == 0);
    REQUIRE (worstCohError == 0);
}

// ======================================================================================= 6 · determinismo
TEST_CASE ("telescope: dos corridas dan el mismo espectrograma estereo byte a byte", "[telescope][sgram-st]")
{
    const auto run = [] (int block)
    {
        Rig rig;
        rig.feed (Case::mixed, 5.0, block);

        std::vector<juce::uint8> cols ((size_t) rig.ring->count() * (size_t) StereoSpectrogramRing::kCellBytes);
        const int n = rig.ring->copyLatest (cols.data(), rig.ring->count());
        cols.resize ((size_t) n * (size_t) StereoSpectrogramRing::kCellBytes);
        return cols;
    };

    const auto a = run (512);
    const auto b = run (512);
    const auto c = run (64);
    std::printf ("SGRAM-ST[determinismo] %zu bytes  (512 vs 512: %s · 512 vs 64: %s)\n",
                 a.size(), a == b ? "identicos" : "DISTINTOS", a == c ? "identicos" : "DISTINTOS");
    REQUIRE (! a.empty());
    REQUIRE (a == b);
    REQUIRE (a == c);
}

// ============================================================================ 7 · los dos mapeos byte
// Las fórmulas byte ↔ número viven en el módulo (las usan el módulo, la lente y estos tests): que el
// redondeo del centro caiga en 128 y no en 127 es lo que hace que el silencio se vea neutro y no
// levemente fuera de fase.
TEST_CASE ("telescope: los mapeos de coherencia y de dB del espectrograma estereo son reversibles", "[telescope][sgram-st]")
{
    REQUIRE (StereoBands::coherenceToByte (-1.0) == 0);
    REQUIRE (StereoBands::coherenceToByte (0.0) == 128);
    REQUIRE (StereoBands::coherenceToByte (1.0) == 255);
    REQUIRE (StereoBands::coherenceToByte (-2.0) == 0);     // clampeado, nunca fuera de rango
    REQUIRE (StereoBands::coherenceToByte (2.0) == 255);

    float worst = 0.0f;
    for (int b = 0; b <= 255; ++b)
    {
        const float c = StereoBands::byteToCoherence (b);
        REQUIRE (c >= -1.0f);
        REQUIRE (c <= 1.0f);
        worst = std::max (worst, std::abs ((float) StereoBands::coherenceToByte ((double) c) - (float) b));
    }
    std::printf ("SGRAM-ST[mapeos] ida y vuelta de los 256 valores de coherencia: error maximo = %.0f\n", worst);
    REQUIRE (worst == 0.0f);

    // dB: 0 dB arriba, −rango abajo, y el mapeo del 50 exacto.
    REQUIRE (StereoBands::dbToByte (0.0, 90.0) == 255);
    REQUIRE (StereoBands::dbToByte (-90.0, 90.0) == 0);
    REQUIRE (StereoBands::dbToByte (-200.0, 90.0) == 0);
    REQUIRE (StereoBands::dbToByte (-20.0, 90.0) == (juce::uint8) std::lround (255.0 * (1.0 - 20.0 / 90.0)));
    REQUIRE (std::abs (StereoBands::byteToDb (198, 90.0) + 20.12f) < 0.1f);
}

// ============================================================================ 8 · un solo canal
// LA PRUEBA DE QUE LA SIMETRÍA ES REAL, no una suposición del encabezado. Se alimenta la MISMA señal por
// L (con R mudo) y después por R (con L mudo), y se comparan las dos columnas byte a byte:
//
//   · iguales  → confirmado: la lente 10 no puede distinguir un L↔R invertido, y ningún test de este
//                archivo podría hacerlo. Queda escrito con un número, no con una opinión.
//   · el que SÍ lo distingue es `balanceDb`, que en un caso va al piso y en el otro al techo.
//
// Además fija lo que la celda dice de una señal de un solo canal: energía SÍ (hay algo sonando) y
// coherencia SIN DEFINIR (128), que es lo honesto — con un canal mudo no hay correlación que medir. Un
// 255 ahí diría "mono" y un 0 diría "fuera de fase"; las dos serían mentira.
TEST_CASE ("telescope: un solo canal da la misma columna de los dos lados (y el balance no)",
           "[telescope][sgram-st]")
{
    const auto run = [] (Case c)
    {
        Rig rig;
        rig.feed (c, 4.0);
        std::vector<juce::uint8> col ((size_t) StereoSpectrogramRing::kCellBytes);
        REQUIRE (rig.ring->copyColumn (rig.ring->writeIndex() - 1, col.data()));
        return std::pair<std::vector<juce::uint8>, StereoBands::Result> { col, rig.bands->result() };
    };

    const auto [colL, resL] = run (Case::leftOnly);
    const auto [colR, resR] = run (Case::rightOnly);

    const auto stL = statsOf (colL.data());
    report (Case::leftOnly,  stL);
    report (Case::rightOnly, statsOf (colR.data()));

    std::printf ("SGRAM-ST[solo L vs solo R] columnas identicas: %s  ·  balance solo-L = %+.2f dB  "
                 "solo-R = %+.2f dB  (la red del L<->R vive en [bands], no aca)\n",
                 colL == colR ? "SI" : "no", resL.wideBalanceDb, resR.wideBalanceDb);

    // 1 · la columna NO distingue de qué lado vino la señal: byte a byte, la misma.
    REQUIRE (colL == colR);

    // 2 · y sin embargo hay energía y la coherencia queda SIN DEFINIR, que es lo que corresponde.
    REQUIRE (stL.cells > 100);
    REQUIRE (stL.minCoh == 128);
    REQUIRE (stL.maxCoh == 128);

    // 3 · el balance por banda ancha SÍ los separa, y lo hace en los dos extremos de su escala.
    REQUIRE (resL.wideBalanceDb == StereoBands::kDbFloor);   // -60: todo en L
    REQUIRE (resR.wideBalanceDb == StereoBands::kDbCeil);    // +60: todo en R
}
