// [telescope][pan] + [telescope][field] — el PANEO POR ENERGÍA por bin y el módulo Field (lente 11).
//
// LO QUE ESTE ARCHIVO DEFIENDE, y por qué importa más que el resto:
//
// FIELD es la lente que más fácil miente. Dibuja manchas repartidas de izquierda a derecha y cualquiera
// que la mire va a leer "ahí está el violín". No: lo único que se mide de una mezcla estéreo terminada es
// CUÁNTA ENERGÍA hay en cada canal por frecuencia. De dónde venía la fuente en la sala es un problema sin
// solución única (la trampa del ITD de ORBIT, informe 21). Así que lo que se verifica acá es exactamente
// lo que la lente promete y nada más:
//
//   pan_k = (ΣRR_k − ΣLL_k) / (ΣRR_k + ΣLL_k) ∈ [−1, +1]      (energía, no amplitud; ver StereoBandsFrame.h)
//
// Con la ley de potencia constante L = cos θ·x, R = sin θ·x el número es EXACTO y redondo:
//
//   pan = (sin²θ − cos²θ) / (sin²θ + cos²θ) = −cos 2θ
//   θ =  0° → −1      22.5° → −0.7071      45° → 0      67.5° → +0.7071      90° → +1
//
// No es una aproximación: como R = tan θ · L muestra a muestra, también R_k = tan θ · L_k bin a bin, y el
// cociente sale sin error salvo el redondeo del float. La tolerancia de ±0.02 del prompt es holgadísima
// a propósito — si algún día alguien cambia la fórmula a la de amplitud, el número se corre a −0.414 en
// 22.5° y este test lo agarra.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "TestSignals.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Spectrum.h"
#include "analysis/modules/StereoBands.h"

using telescope::Spectrum;
using telescope::SpectrumFrame;
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
    s.channel      = Spectrum::left;  // el canal de la LENTE: ni StereoBands ni Field dependen de él
    s.avgMode      = Spectrum::avgNone;
    s.peakHold     = false;
    s.rangeDbIndex = 1;               // 90 dB
    return s;
}

// Una fuente MONO paneada con ley de potencia constante: L = cos θ·x, R = sin θ·x.
struct Panned
{
    explicit Panned (double thetaRad) : gl ((float) std::cos (thetaRad)), gr ((float) std::sin (thetaRad)) {}
    std::pair<float, float> next() noexcept { const float x = p.next(); return { gl * x, gr * x }; }
    float gl, gr;
    Pink  p { telescope::test::kPinkSeedA };
};

template <typename Gen>
void feed (Spectrum& m, Gen& gen, double seconds, int block = 512)
{
    std::vector<float> L ((size_t) block), R ((size_t) block);
    const auto total = (long long) std::llround (seconds * kSr);
    for (long long done = 0; done < total; )
    {
        const int k = (int) std::min ((long long) block, total - done);
        for (int i = 0; i < k; ++i)
        {
            const auto v = gen.next();
            L[(size_t) i] = v.first;
            R[(size_t) i] = v.second;
        }
        m.process (L.data(), R.data(), k);
        done += k;
    }
}

// Los bins con energía DE VERDAD en el frame publicado: los que están a menos de `belowDb` del más fuerte.
// Un bin en el piso tiene un paneo que es puro redondeo, y promediarlo con los demás sería ruido.
std::vector<int> loudBins (const telescope::StereoBandsFrame& f, double loHz, double hiHz, float belowDb)
{
    float best = SpectrumFrame::kFloorDb;
    const int k0 = std::max (1, (int) std::ceil (loHz / f.binHz));
    const int k1 = std::min (f.numBins, (int) std::ceil (hiHz / f.binHz));
    for (int k = k0; k < k1; ++k) best = std::max (best, f.energyDb[k]);

    std::vector<int> out;
    for (int k = k0; k < k1; ++k)
        if (f.energyDb[k] > best - belowDb) out.push_back (k);
    return out;
}
}

// ================================================================================== 1 · la ley de paneo
TEST_CASE ("telescope: el paneo por bin sigue la ley de potencia constante", "[telescope][pan]")
{
    struct Row { double deg, want; };
    const Row rows[] = { { 0.0, -1.0 }, { 22.5, -0.70710678 }, { 45.0, 0.0 },
                         { 67.5, 0.70710678 }, { 90.0, 1.0 } };

    for (const auto& row : rows)
    {
        Spectrum m;
        StereoBands bands;
        m.setFrameSink (&bands);
        m.applySettings (baseSettings());
        m.prepare (kSr);
        bands.setWindowIndex (1);   // 1 s

        Panned sig { row.deg * kTwoPi / 360.0 };
        feed (m, sig, 3.0);

        const auto& f = bands.frame();
        const auto ks = loudBins (f, 100.0, 8000.0, 30.0f);
        REQUIRE (ks.size() > 100u);

        double sum = 0.0, worst = 0.0;
        for (const int k : ks)
        {
            sum += (double) f.pan[k];
            worst = std::max (worst, std::abs ((double) f.pan[k] - row.want));
            REQUIRE (std::isfinite (f.pan[k]));
        }
        const double mean = sum / (double) ks.size();

        std::printf ("PAN[theta=%5.1f deg] esperado %+.4f  ·  medio %+.4f  ·  peor error %.2e  (%zu bins)\n",
                     row.deg, row.want, mean, worst, ks.size());
        REQUIRE (std::abs (mean - row.want) < 0.02);
        REQUIRE (worst < 0.02);
    }
}

// ================================================================== 2 · independientes: disperso, no cero
// Dos fuentes independientes no están "en el centro": están REPARTIDAS. El promedio da ~0 pero cada bin
// tiene su propio paneo, y esa dispersión es justo lo que la lente tiene que dibujar como "ancho". Un
// estimador que devolviera 0 en todos los bins pasaría un test de promedio y dibujaría una raya al medio.
TEST_CASE ("telescope: con fuentes independientes el paneo queda disperso alrededor de 0", "[telescope][pan]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);
    bands.setWindowIndex (1);

    struct Indep
    {
        std::pair<float, float> next() noexcept { return { a.next(), b.next() }; }
        Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    } sig;
    feed (m, sig, 3.0);

    const auto& f = bands.frame();
    const auto ks = loudBins (f, 100.0, 8000.0, 30.0f);
    REQUIRE (ks.size() > 100u);

    double sum = 0.0, sum2 = 0.0, cohAbs = 0.0;
    for (const int k : ks)
    {
        sum  += (double) f.pan[k];
        sum2 += (double) f.pan[k] * (double) f.pan[k];
        cohAbs += std::abs ((double) f.coh[k]);
    }
    const double n = (double) ks.size();
    const double mean = sum / n, spread = std::sqrt (std::max (0.0, sum2 / n - mean * mean));

    std::printf ("PAN[independientes] medio %+.4f  ·  dispersion %.4f  ·  |coh| medio %.4f  (%zu bins)\n",
                 mean, spread, cohAbs / n, ks.size());
    REQUIRE (std::abs (mean) < 0.15);   // no está corrido a un lado
    REQUIRE (spread > 0.05);            // y NO es cero: está repartido
    REQUIRE (cohAbs / n < 0.5);         // sin fase común
}

// =========================================================================================== 3 · silencio
TEST_CASE ("telescope: en silencio el paneo es 0 y no hay NaN", "[telescope][pan]")
{
    Spectrum m;
    StereoBands bands;
    m.setFrameSink (&bands);
    m.applySettings (baseSettings());
    m.prepare (kSr);

    struct Mute { std::pair<float, float> next() noexcept { return { 0.0f, 0.0f }; } } sig;
    feed (m, sig, 2.0);

    const auto& f = bands.frame();
    REQUIRE (f.numBins == 2049);
    int nonZero = 0;
    for (int k = 0; k < f.numBins; ++k)
    {
        REQUIRE (std::isfinite (f.pan[k]));
        if (f.pan[k] != 0.0f) ++nonZero;
    }
    std::printf ("PAN[silencio] %d bins  ·  %d con paneo distinto de 0\n", f.numBins, nonZero);
    REQUIRE (nonZero == 0);
}

// ======================================================================== 4 · independiente del bloque
// Las posiciones de los frames las decide el contador de muestras del módulo Spectrum, no el tamaño del
// bloque del host: el paneo por bin tiene que salir IDÉNTICO AL BIT con bloques de 1 o de 4 096.
TEST_CASE ("telescope: el paneo por bin no depende del tamano de bloque", "[telescope][pan]")
{
    const auto run = [] (int block)
    {
        auto m     = std::make_unique<Spectrum>();
        auto bands = std::make_unique<StereoBands>();
        m->setFrameSink (bands.get());
        m->applySettings (baseSettings());
        m->prepare (kSr);
        bands->setWindowIndex (1);

        telescope::test::MonoLowPhaseHigh mixed { kSr };
        struct Wrap
        {
            telescope::test::MonoLowPhaseHigh& s;
            std::pair<float, float> next() noexcept { return s.next(); }
        } sig { mixed };
        feed (*m, sig, 3.0, block);

        const auto& f = bands->frame();
        return std::vector<float> (f.pan, f.pan + f.numBins);
    };

    const auto ref = run (512);
    REQUIRE (ref.size() == 2049u);
    for (const int block : { 1, 7, 64, 4096 })
    {
        INFO ("bloque " << block);
        REQUIRE (run (block) == ref);
    }
    std::printf ("PAN[bloque 1/7/64/4096] %zu paneos por bin identicos al bit\n", ref.size());
}

// ========================================================================================================
// [telescope][field] — el MÓDULO Field: la grilla dirección × frecuencia con decaimiento y estela.
//
// Se prueba contra el módulo DIRECTO (Spectrum → StereoBands → Field), no contra la lente: lo que acá
// tiene que ser cierto es la MEDICIÓN, y una foto linda de una medición equivocada sigue estando mal.
// ========================================================================================================
#include "analysis/FieldFrame.h"
#include "analysis/modules/Field.h"

using telescope::Field;
using telescope::FieldFrame;

namespace
{
// El motor entero de la lente 11, cableado como lo cablea el AnalysisThread.
struct Rig
{
    explicit Rig (int windowIdx = 1, int decayIdx = 1)
    {
        m.setFrameSink (&bands);
        bands.setFieldSink (&field);
        m.applySettings (baseSettings());
        m.prepare (kSr);
        bands.setWindowIndex (windowIdx);
        field.setDecayIndex (decayIdx);
    }

    Spectrum    m;
    StereoBands bands;
    Field       field;
};

// La energía TOTAL por columna de dirección (sumando todas las frecuencias).
std::vector<double> columnTotals (const FieldFrame& f)
{
    std::vector<double> t ((size_t) FieldFrame::kDir, 0.0);
    for (int r = 0; r < FieldFrame::kRows; ++r)
        for (int c = 0; c < FieldFrame::kDir; ++c) t[(size_t) c] += (double) f.grid[r][c];
    return t;
}

int argMax (const std::vector<double>& v)
{
    return (int) (std::max_element (v.begin(), v.end()) - v.begin());
}

double gridTotal (const FieldFrame& f)
{
    double t = 0.0;
    for (int r = 0; r < FieldFrame::kRows; ++r)
        for (int c = 0; c < FieldFrame::kDir; ++c) t += (double) f.grid[r][c];
    return t;
}

// La celda más fuerte entre las filas cuyo centro cae a menos de un ±12 % de `hz`. Se busca por RANGO y
// no por "la fila de hz" porque un tono no cae en una sola fila: con FFT de 4 096 a 48 kHz, 100 Hz se
// reparte entre los bins 8 y 9, que caen en filas distintas de una grilla de 32 filas por década.
struct Cell { int row = -1, col = -1; double value = 0.0, rowMax = 0.0, centre = 0.0; };

Cell strongestNear (const FieldFrame& f, double hz)
{
    Cell best;
    for (int r = 0; r < FieldFrame::kRows; ++r)
    {
        const double fr = FieldFrame::rowFrequency (r);
        if (fr < hz / 1.12 || fr > hz * 1.12) continue;
        for (int c = 0; c < FieldFrame::kDir; ++c)
            if ((double) f.grid[r][c] > best.value) { best = { r, c, (double) f.grid[r][c], 0.0, 0.0 }; }
    }
    if (best.row >= 0)
    {
        for (int c = 0; c < FieldFrame::kDir; ++c) best.rowMax = std::max (best.rowMax, (double) f.grid[best.row][c]);
        // Las dos columnas del centro: pan = 0 cae ENTRE la 31 y la 32 (64 columnas, ver FieldFrame.h).
        best.centre = std::max ((double) f.grid[best.row][FieldFrame::kDir / 2 - 1],
                                (double) f.grid[best.row][FieldFrame::kDir / 2]);
    }
    return best;
}
}

// ========================================================================================= 1 · ley de paneo
TEST_CASE ("telescope: la grilla del campo pone la energia donde dice la ley de paneo", "[telescope][field]")
{
    struct Row { double deg, wantPan; };
    const Row rows[] = { { 0.0, -1.0 }, { 22.5, -0.70710678 }, { 45.0, 0.0 }, { 90.0, 1.0 } };

    for (const auto& row : rows)
    {
        Rig rig;
        Panned sig { row.deg * kTwoPi / 360.0 };
        feed (rig.m, sig, 3.0);

        const auto& f = rig.field.frame();
        const auto totals = columnTotals (f);
        const int got  = argMax (totals);
        const int want = (int) std::lround (FieldFrame::columnPos (row.wantPan));

        double near = 0.0;
        for (int c = std::max (0, got - 1); c <= std::min (FieldFrame::kDir - 1, got + 1); ++c)
            near += totals[(size_t) c];

        std::printf ("FIELD[paneo theta=%5.1f] columna esperada %2d (pan %+.4f)  ·  argmax %2d (pan %+.4f)"
                     "  ·  %.1f %% de la energia en +-1 columna\n",
                     row.deg, want, row.wantPan, got, FieldFrame::columnPan (got),
                     100.0 * near / std::max (1.0e-30, gridTotal (f)));
        REQUIRE (std::abs (got - want) <= 1);
        REQUIRE (f.maxCell > 0.0f);
    }
}

// ================================================================================== 2 · dos fuentes
// Tono de 100 Hz SÓLO en L y tono de 5 kHz SÓLO en R: dos manchas en esquinas opuestas de la grilla, y el
// centro de esas dos filas vacío. Es la foto que la lente existe para dar, y la que un promedio de banda
// ancha (corr = 0, width = 1) no puede dar: los dos números de banda ancha de esta señal son idénticos a
// los del ruido decorrelacionado del test 3, y las dos grillas no se parecen en nada.
TEST_CASE ("telescope: dos fuentes en canales opuestos dan dos maximos y nada en el centro", "[telescope][field]")
{
    Rig rig;
    struct TwoTones
    {
        std::pair<float, float> next() noexcept
        {
            const auto t = (double) n++ / kSr;
            return { (float) (0.5 * std::sin (kTwoPi * 100.0 * t)),
                     (float) (0.5 * std::sin (kTwoPi * 5000.0 * t)) };
        }
        long long n = 0;
    } sig;
    feed (rig.m, sig, 3.0);

    const auto& f = rig.field.frame();
    const auto lo = strongestNear (f, 100.0);
    const auto hi = strongestNear (f, 5000.0);

    REQUIRE (lo.row >= 0);
    REQUIRE (hi.row >= 0);
    std::printf ("FIELD[dos fuentes] 100 Hz -> fila %d (%.0f Hz) columna %d (pan %+.3f), centro %.2f %% de la fila"
                 "  ·  5 kHz -> fila %d (%.0f Hz) columna %d (pan %+.3f), centro %.2f %% de la fila\n",
                 lo.row, FieldFrame::rowFrequency (lo.row), lo.col, FieldFrame::columnPan (lo.col),
                 100.0 * lo.centre / std::max (1.0e-30, lo.rowMax),
                 hi.row, FieldFrame::rowFrequency (hi.row), hi.col, FieldFrame::columnPan (hi.col),
                 100.0 * hi.centre / std::max (1.0e-30, hi.rowMax));

    REQUIRE (lo.col <= 1);                          // sólo L → el borde izquierdo
    REQUIRE (hi.col >= FieldFrame::kDir - 2);       // sólo R → el borde derecho
    REQUIRE (lo.centre < 0.05 * lo.rowMax);         // y nada en el medio
    REQUIRE (hi.centre < 0.05 * hi.rowMax);
}

// ============================================================================== 3 · decorrelado = ANCHO
// Ruido estéreo INDEPENDIENTE: la energía tiene que quedar REPARTIDA, no apilada en una columna. Eso es lo
// que "ancho" tiene que parecer, y es la diferencia visible contra el test 2.
//
// Se miran sólo las filas por encima de 2 kHz Y ADEMÁS eso está bien: por debajo, una fila de la grilla
// (7.5 % de ancho relativo) puede contener UN SOLO bin de la STFT, y el paneo de un bin es un número
// solo — esa fila cae entera en una columna por RESOLUCIÓN de la grilla, no porque la señal sea angosta.
// Afirmar lo contrario sería un test que mide la geometría de la grilla y la llama propiedad de la señal.
TEST_CASE ("telescope: con fuentes independientes la energia queda repartida en direccion", "[telescope][field]")
{
    Rig rig;
    struct Indep
    {
        std::pair<float, float> next() noexcept { return { a.next(), b.next() }; }
        Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    } sig;
    feed (rig.m, sig, 3.0);

    const auto& f = rig.field.frame();
    int checked = 0;
    double worstShare = 0.0;
    int worstRow = -1;

    for (int r = 0; r < FieldFrame::kRows; ++r)
    {
        if (FieldFrame::rowFrequency (r) < 2000.0) continue;
        double total = 0.0, best = 0.0;
        for (int c = 0; c < FieldFrame::kDir; ++c)
        {
            total += (double) f.grid[r][c];
            best = std::max (best, (double) f.grid[r][c]);
        }
        if (total <= 1.0e-4 * (double) f.maxCell) continue;   // fila sin energía: no hay nada que repartir
        ++checked;
        const double share = best / total;
        if (share > worstShare) { worstShare = share; worstRow = r; }
    }

    std::printf ("FIELD[independientes] %d filas por encima de 2 kHz  ·  peor concentracion %.1f %% "
                 "(fila %d, %.0f Hz)  ·  criterio < 15 %%\n",
                 checked, 100.0 * worstShare, worstRow, worstRow >= 0 ? FieldFrame::rowFrequency (worstRow) : 0.0);
    REQUIRE (checked >= 20);
    REQUIRE (worstShare < 0.15);
}

// ============================================================================================ 4 · decaimiento
// Sin señal nueva la grilla se multiplica por exp(−dt/τ) frame a frame: la energía total cae a 1/e en
// exactamente τ. La medición arranca DESPUÉS de vaciar la ventana de la FFT (4 096 muestras = 85 ms): en
// ese tramo todavía entra cola de la señal y el total sube, que es correcto y no es decaimiento.
TEST_CASE ("telescope: la energia del campo cae a 1/e en la constante de tiempo", "[telescope][field]")
{
    for (int idx = 0; idx < Field::kNumDecayOptions; ++idx)
    {
        Rig rig { 1, idx };
        struct Src { std::pair<float, float> next() noexcept { const float x = p.next(); return { x, 0.6f * x }; }
                     Pink p { telescope::test::kPinkSeedA }; } sig;
        feed (rig.m, sig, 3.0);

        struct Mute { std::pair<float, float> next() noexcept { return { 0.0f, 0.0f }; } } silence;
        feed (rig.m, silence, 0.25);                    // vaciar la ventana de la FFT

        const double rate = rig.field.emitRate();
        REQUIRE (rate > 0.0);
        const auto  idx0 = rig.field.frame().frameIndex;
        const double e0  = gridTotal (rig.field.frame());
        REQUIRE (e0 > 0.0);

        double measured = -1.0;
        for (int step = 0; step < 4000 && measured < 0.0; ++step)
        {
            feed (rig.m, silence, 1.0 / kSr * 256.0, 256);
            if (gridTotal (rig.field.frame()) <= e0 / std::exp (1.0))
                measured = (double) (rig.field.frame().frameIndex - idx0) / rate;
        }

        const double want = (double) Field::kDecaySecOptions[idx];
        std::printf ("FIELD[decaimiento tau=%.1f s] medido %.4f s  ·  error %+.2f %%  ·  %.3f frames/s"
                     "  ·  decaySec publicado %.2f\n",
                     want, measured, 100.0 * (measured / want - 1.0), rate, rig.field.frame().decaySec);
        REQUIRE (measured > 0.0);
        REQUIRE (std::abs (measured / want - 1.0) < 0.10);
        REQUIRE (rig.field.frame().decaySec == Field::kDecaySecOptions[idx]);
    }
}

// ================================================================================================ 5 · estela
// `trail` guarda la HISTORIA, nunca la grilla de ahora: tras el frame n tiene las 8 anteriores, y la más
// vieja es la del frame n−8. Se verifica CONTRA LAS GRILLAS REALES, decimadas igual que las guarda el
// módulo — comparar sólo el contador diría que hay ocho láminas sin decir si son las que corresponden.
TEST_CASE ("telescope: la estela del campo guarda las 8 grillas anteriores en orden", "[telescope][field]")
{
    Rig rig;
    struct Src
    {
        // Una señal que CAMBIA frame a frame (barrido), para que dos grillas consecutivas no sean iguales
        // y el test no pueda pasar por casualidad.
        std::pair<float, float> next() noexcept
        {
            const double t = (double) n++ / kSr;
            const double f = 200.0 * std::pow (10.0, 1.2 * t);
            phase += kTwoPi * f / kSr;
            const auto v = (float) (0.5 * std::sin (phase));
            return { v, 0.5f * v };
        }
        long long n = 0;
        double phase = 0.0;
    } sig;

    const auto decimate = [] (const FieldFrame& f)
    {
        std::vector<float> d ((size_t) (FieldFrame::kTrailRows * FieldFrame::kTrailDir), 0.0f);
        for (int r = 0; r < FieldFrame::kTrailRows; ++r)
            for (int c = 0; c < FieldFrame::kTrailDir; ++c)
                d[(size_t) (r * FieldFrame::kTrailDir + c)] =
                    std::max (std::max (f.grid[2 * r][2 * c], f.grid[2 * r][2 * c + 1]),
                              std::max (f.grid[2 * r + 1][2 * c], f.grid[2 * r + 1][2 * c + 1]));
        return d;
    };

    // Bloques de 256 muestras: con hop de 1 024 sale a lo sumo UN frame por llamada, así que lo grabado
    // va uno a uno con los frames del módulo (y se verifica que así sea).
    std::vector<std::vector<float>> perFrame;
    juce::uint32 last = 0;
    for (int i = 0; i < 4000 && perFrame.size() < 20u; ++i)
    {
        feed (rig.m, sig, 256.0 / kSr, 256);
        const auto now = rig.field.framesEmitted();
        if (now == last) continue;
        REQUIRE (now == last + 1);          // un frame por llamada: la grabación es uno a uno
        last = now;
        perFrame.push_back (decimate (rig.field.frame()));
    }
    REQUIRE (perFrame.size() >= 20u);

    const auto& f = rig.field.frame();
    const int n = (int) f.frameIndex;
    REQUIRE (f.trailCount == FieldFrame::kTrail);

    // trail[0] es la más vieja = la grilla del frame n−8; trail[7] la del frame n−1.
    int matched = 0;
    for (int i = 0; i < FieldFrame::kTrail; ++i)
    {
        const auto& want = perFrame[(size_t) (n - FieldFrame::kTrail + i)];
        const float* got = &f.trail[i][0][0];
        bool same = true;
        for (size_t j = 0; j < want.size(); ++j) if (got[j] != want[j]) { same = false; break; }
        INFO ("lamina " << i << " (frame " << (n - FieldFrame::kTrail + i) << ")");
        REQUIRE (same);
        if (same) ++matched;
    }
    // Y las láminas NO son todas iguales (si lo fueran, el test de arriba no probaría nada).
    bool differ = false;
    for (int j = 0; j < FieldFrame::kTrailRows * FieldFrame::kTrailDir && ! differ; ++j)
        if ((&f.trail[0][0][0])[j] != (&f.trail[FieldFrame::kTrail - 1][0][0])[j]) differ = true;

    std::printf ("FIELD[estela] frame %d  ·  trailCount %d  ·  %d/%d laminas identicas al bit a los frames "
                 "%d..%d  ·  la mas vieja y la mas nueva difieren: %s\n",
                 n, f.trailCount, matched, FieldFrame::kTrail, n - FieldFrame::kTrail, n - 1,
                 differ ? "si" : "NO");
    REQUIRE (differ);
}

// =========================================================================================== 6 · silencio
TEST_CASE ("telescope: en silencio la grilla del campo queda en cero y sin NaN", "[telescope][field]")
{
    Rig rig;
    struct Mute { std::pair<float, float> next() noexcept { return { 0.0f, 0.0f }; } } sig;
    feed (rig.m, sig, 2.0);

    const auto& f = rig.field.frame();
    REQUIRE (rig.field.framesEmitted() > 50u);
    int bad = 0;
    for (int r = 0; r < FieldFrame::kRows; ++r)
        for (int c = 0; c < FieldFrame::kDir; ++c)
            if (! std::isfinite (f.grid[r][c]) || f.grid[r][c] != 0.0f) ++bad;

    std::printf ("FIELD[silencio] %u frames  ·  %d celdas distintas de 0 o no finitas  ·  maxCell %.3e\n",
                 rig.field.framesEmitted(), bad, (double) f.maxCell);
    REQUIRE (bad == 0);
    REQUIRE (f.maxCell == 0.0f);
    REQUIRE (std::isfinite (f.maxCell));
}

// ================================================================== 7 · independiente del tamano de bloque
// 34 816 muestras = el frame 30 exacto (el primero sale en t = 4 096 y después uno cada hop de 1 024).
TEST_CASE ("telescope: la grilla del campo no depende del tamano de bloque", "[telescope][field]")
{
    constexpr long long kToFrame30 = 4096 + 30 * 1024;

    const auto run = [] (int block)
    {
        auto rig = std::make_unique<Rig>();
        telescope::test::MonoLowPhaseHigh mixed { kSr };
        struct Wrap
        {
            telescope::test::MonoLowPhaseHigh& s;
            std::pair<float, float> next() noexcept { return s.next(); }
        } sig { mixed };
        feed (rig->m, sig, (double) kToFrame30 / kSr, block);

        struct Out { std::vector<float> grid; juce::uint32 frames, index; float maxCell; };
        const auto& f = rig->field.frame();
        Out o { {}, rig->field.framesEmitted(), f.frameIndex, f.maxCell };
        o.grid.assign (&f.grid[0][0], &f.grid[0][0] + FieldFrame::kRows * FieldFrame::kDir);
        return o;
    };

    const auto ref = run (512);
    REQUIRE (ref.frames == 31u);
    REQUIRE (ref.index  == 30u);
    REQUIRE (ref.maxCell > 0.0f);

    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto o = run (block);
        INFO ("bloque " << block);
        REQUIRE (o.frames == ref.frames);
        REQUIRE (o.index  == ref.index);
        REQUIRE (o.maxCell == ref.maxCell);
        REQUIRE (o.grid == ref.grid);
    }
    std::printf ("FIELD[bloque 1/7/64/4096] frame %u  ·  %zu celdas identicas al bit  ·  maxCell %.6e\n",
                 ref.index, ref.grid.size(), (double) ref.maxCell);
}

// ========================================================================================================
// LA LENTE FIELD: el tope de puntos, la estela bajo reduced-motion, la lectura, el estado y las fotos.
// ========================================================================================================
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "lenses/FieldLens.h"
#include "lenses/LensIds.h"
#include "ui-kit/Theme.h"

using telescope::FieldLens;

namespace
{
constexpr int kLensW = 1025, kLensH = 702;   // el área de la lente en tamaño L (820×562 × 1.25)

// La captura a PNG vive en TestHelpers.h, en UNA sola copia (LOW de los tres revisores).
using telescope::test::writePng;

// Empuja `seconds` de una señal por el processor con freno (para no desbordar el bus).
template <typename Gen>
void push (telescope::TelescopeProcessor& proc, Gen& gen, double seconds)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int blk = 0; blk < (int) (seconds * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = gen.next();
            buf.setSample (0, i, v.first);
            buf.setSample (1, i, v.second);
        }
        proc.processBlock (buf, midi);
        const double pushed = (double) n / 48000.0;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
}

struct TwoSources
{
    std::pair<float, float> next() noexcept
    {
        const auto t = (double) n++ / 48000.0;
        return { (float) (0.5 * std::sin (kTwoPi * 100.0 * t)),
                 (float) (0.5 * std::sin (kTwoPi * 5000.0 * t)) };
    }
    long long n = 0;
};

struct IndepPink
{
    std::pair<float, float> next() noexcept { return { 0.3f * a.next(), 0.3f * b.next() }; }
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
};

// ========================================================================================================
// 57c — EL UMBRAL DE "BORDE DURO", Y POR QUÉ NO ES EL DEL PROMPT.
//
// El prompt pedía contar saltos de luma > 16/255 y esperaba «≈ 31 bordes por fila ≈ 2–3 % de los pares».
// Los dos números no pueden salir a la vez, y medirlo lo confirma: con la lámina de estela estirada por
// vecino más cercano (la base `d99c140`) los 31 bordes de columna ESTÁN, pero su salto de luma vive entre
// 2 y 23 — porque la rampa inferno en la zona oscura y con el alpha de la estela (fade ≈ 0.5) cambia poco
// de LUMA entre dos celdas vecinas, aunque cambie de color a la vista. Con el gate en 16 el mosaico medía
// 0.015 % y el test habría estado VERDE sobre el defecto que existe para atrapar.
//
// Medido sobre la misma lámina, base contra bilineal (M4 de la casa, 2026-09-12):
//
//     umbral    base (vecino más cercano)        bilineal (57c)
//      > 2        1.038 %  · peor fila 25          0.019 %
//      > 4        0.686 %  · peor fila 19          0.006 %
//      > 8        0.230 %  · peor fila  8          0.000 %
//      >16        0.015 %  · peor fila  2          0.000 %
//
// El umbral es 4/255: está por ENCIMA del percentil 99.9 de una rampa de verdad (1.0/255 medido sobre la
// lámina bilineal, donde una celda de ~50 px sube de a menos de un nivel por píxel) y por DEBAJO de los
// escalones que deja el vecino más cercano. Con el criterio de 0.3 % del prompt, la base falla 2.3 veces
// y la lámina bilineal pasa con 49 veces de margen: la separación es de la métrica, no del criterio.
// ========================================================================================================
constexpr double kHardEdgeLuma   = 4.0;      // saltos de luma por encima de esto = borde, no rampa
constexpr double kBeforePct      = 0.686;
constexpr int    kBeforeWorstRow = 19;
}

// ==================================================================== 8 · el tope de puntos y la estela
TEST_CASE ("telescope: FIELD nunca dibuja mas de 4096 puntos y con reduced-motion no dibuja estela",
           "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (1);

    IndepPink sig;                       // la señal más DENSA: llena la grilla de lado a lado
    push (proc, sig, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 8000));

    const auto render = [&] (bool reduced)
    {
        telescope::Lens::setReducedMotion (reduced);
        auto lens = std::make_unique<FieldLens> (proc);
        lens->setSize (kLensW, kLensH);

        juce::Image img (juce::Image::ARGB, kLensW, kLensH, true);
        for (int i = 0; i < 4; ++i)
        {
            lens->pumpFrames (1);
            img.clear (img.getBounds());
            juce::Graphics g (img);
            lens->paintEntireComponent (g, false);
        }

        // Píxeles DE LA PALETA en la franja a la que sólo puede llegar la estela.
        //
        // Se cuenta "verde" y no "distinto del fondo" porque en esa franja también entran las aristas de
        // fuga de la caja de alambre, que se dibujan siempre (con estela y sin ella). La paleta de la
        // lente es la familia Espectral —verde por encima del azul— y las hairlines del Theme son
        // azuladas (a0c0e0), así que el criterio separa las dos cosas sin ambigüedad.
        const auto area = lens->trailOnlyArea();
        int lit = 0;
        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                if (c.getAlpha() > 0 && (int) c.getGreen() > (int) c.getBlue() + 4) ++lit;
            }

        // Y lo simétrico: la franja de abajo a la que sólo puede llegar el plano de AHORA. Es la mitad del
        // test que faltaba — sin ella "la lente dibuja" lo cumplían las tres láminas de estela solas, que
        // es exactamente lo que pasaba en cff1ec1.
        //
        // 57c — SE CUENTA SOBRE LA CACHÉ Y CONTRA EL FONDO, no "píxeles verdes" sobre el componente. Ahí
        // abajo vive la fila de 20 Hz, que con ruido rosa está cerca del piso: la superficie la pinta con
        // el alpha de piso (kSurfaceFloorAlpha) sobre el extremo oscuro de la rampa, y si ese color queda
        // apenas del lado azul de la comparación —cosa que depende de la paleta y del suavizado, no de si
        // la lente dibujó— el test se cae sin que nada esté roto. Contra el fondo la pregunta es la que
        // el test quiere hacer: ¿ESCRIBIÓ la superficie acá? Y sobre la caché, además, no entran las
        // hairlines de la caja de alambre, que es por lo que el criterio del color existía.
        const auto front = lens->frontOnlyArea();
        const auto plot  = lens->cacheAreaForTest();
        const auto bg    = ovni::ui::theme::bg1.withAlpha (1.0f);
        int litFront = 0;
        {
            const auto& raster = lens->cacheImageForTest();
            REQUIRE (raster.isValid());
            for (int y = front.getY(); y < front.getBottom(); ++y)
                for (int x = front.getX(); x < front.getRight(); ++x)
                {
                    const int cx = x - plot.getX(), cy = y - plot.getY();
                    if (cx < 0 || cy < 0 || cx >= raster.getWidth() || cy >= raster.getHeight()) continue;
                    const auto c = raster.getPixelAt (cx, cy);
                    if (std::abs ((int) c.getRed()   - (int) bg.getRed())   > 1
                        || std::abs ((int) c.getGreen() - (int) bg.getGreen()) > 1
                        || std::abs ((int) c.getBlue()  - (int) bg.getBlue())  > 1) ++litFront;
                }
        }

        struct Out { int lit, drawn, layers, areaH, live, litFront, frontH; };
        Out o { lit, lens->pointsDrawn(), lens->trailLayersDrawn(), area.getHeight(),
                lens->liveCellsDrawn(), litFront, front.getHeight() };
        telescope::Lens::setReducedMotion (false);
        return o;
    };

    const auto full = render (false);
    const auto red  = render (true);

    std::printf ("FIELD[lente] libre: %d celdas (%d del plano de AHORA), %d estelas, %d px de paleta en la\n"
                 "             franja de solo-estela (%d px de alto) y %d en la de solo-AHORA (%d px)\n"
                 "FIELD[lente] reduced-motion: %d celdas (%d vivas), %d estelas, %d px arriba, %d px abajo\n",
                 full.drawn, full.live, full.layers, full.lit, full.areaH, full.litFront, full.frontH,
                 red.drawn, red.live, red.layers, red.lit, red.litFront);

    // ===== 56 ===== FIELD pasó de nube de puntos a SUPERFICIE DE CALOR, así que lo que se cuenta cambió
    // de sentido: `drawn` son celdas de grilla con energía, no cuadraditos. El tope de 4 096 puntos dejó
    // de aplicar —y de hacer falta— porque el costo dejó de depender de la señal: se dibujan siempre los
    // mismos nueve planos con las mismas celdas. Lo que se verifica ahora es esa COTA ESTRUCTURAL, que es
    // más fuerte que un tope: un tope hay que acordarse de aplicarlo, una cota no se puede olvidar.
    REQUIRE (full.drawn <= FieldLens::kMaxCells);
    REQUIRE (red.drawn  <= FieldLens::kMaxCells);
    REQUIRE (full.drawn > FieldLens::kDenseCells);   // con la señal densa la superficie enciende de verdad
    REQUIRE (full.layers == telescope::FieldFrame::kTrail);
    REQUIRE (full.lit > 0);                          // sin reduced-motion la estela se ve…
    REQUIRE (red.layers == 0);                       // …y con reduced-motion no hay estela…
    REQUIRE (red.lit == 0);                          // …ni un solo píxel encendido ahí arriba

    // ===== 56b ===== EL PLANO DE AHORA. Con trailCount saturado en kTrail = 8 y kMaxTrailPlanes = 3 el
    // paso valía (8+2)/3 = 3 y el bucle recorría 8 → 5 → 2 → −1: nunca visitaba layer == 0, así que la
    // grilla de AHORA —con su piso kSurfaceFloorAlpha y su bilineal de alta calidad— no se dibujaba
    // NUNCA. Lo que se veía en uso normal eran tres parches de estela 48×32 sueltos. Nada lo atrapaba
    // porque `drawn > kDenseCells` lo cumplen las estelas solas. Estas cuatro líneas son ese agujero.
    REQUIRE (full.live > 0);                         // el presente se dibuja…
    REQUIRE (full.litFront > 0);                     // …y llega a píxeles donde SÓLO él puede pintar
    REQUIRE (red.live  > 0);                         // también con reduced-motion, que es sólo el presente
    REQUIRE (red.litFront > 0);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 56c ===== EL PLANO DE AHORA SE DIBUJA CON *CUALQUIER* CANTIDAD DE ESTELA
//
// El test de arriba cubre los dos extremos: trailCount saturado en 8 y reduced-motion, que es 0. El bug
// del 56b —el bucle de capas saltando por encima de layer == 0— aparecía justamente cuando trailLayers no
// era múltiplo del paso, o sea en las INTERMEDIAS, y esas nadie las miraba. Con 8 y paso 3 el recorrido
// iba 8 → 5 → 2 → −1; con 5 y con 7 pasaba lo mismo. Se arregló y quedó verificado a mano, que es
// exactamente el tipo de verificación que no sobrevive al próximo cambio.
//
// Acá se recorren los nueve valores posibles inyectando `trailCount` en el TripleBuffer: se captura UN
// frame real —con energía de verdad, no una grilla fabricada— y se vuelve a publicar nueve veces con
// distinta cantidad de láminas válidas. Lo que se exige de cada uno es que el PRESENTE se dibuje
// (`liveCellsDrawn() > 0`) y que se dibujen las láminas que corresponden.
// ========================================================================================================
TEST_CASE ("telescope: FIELD dibuja el plano de AHORA con 0 a 8 laminas de estela", "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (1);

    IndepPink sig;
    push (proc, sig, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 8000));

    // EL HILO DE ANÁLISIS TIENE QUE ESTAR QUIETO ANTES DE INYECTAR. Si publica DESPUÉS de la inyección,
    // lo que la lente lee es SU frame y no el de la prueba — y el test mide otra cosa sin decirlo. Pasó
    // en la primera corrida: con n = 0 la lente reportó las 8 láminas del backlog. Se espera a que el
    // frameIndex deje de moverse.
    const auto analisisQuieto = [&]
    {
        juce::uint32 previo = proc.field().read().frameIndex;
        for (int i = 0; i < 120; ++i)
        {
            juce::Thread::sleep (25);
            const auto ahora = proc.field().read().frameIndex;
            if (ahora == previo) return true;
            previo = ahora;
        }
        return false;
    };
    REQUIRE (analisisQuieto());

    // El frame REAL, copiado antes de tocar nada. Va al heap: son ~73 KB de POD.
    auto captured = std::make_unique<telescope::FieldFrame> (proc.field().read());
    REQUIRE (captured->maxCell > 0.0f);
    REQUIRE (captured->trailCount == telescope::FieldFrame::kTrail);

    FieldLens lens (proc);
    lens.setSize (kLensW, kLensH);
    juce::Image img (juce::Image::ARGB, kLensW, kLensH, true);

    for (int n = 0; n <= telescope::FieldFrame::kTrail; ++n)
    {
        auto& slot = proc.field().writeSlot();
        slot = *captured;
        slot.trailCount = n;
        slot.frameIndex = 9000u + (juce::uint32) n;   // que advanceFrame() lo vea como nuevo
        proc.field().publish();

        lens.rebuildOnNextPaint();
        for (int i = 0; i < 2; ++i)   // la primera pasada hornea la capa estática; la segunda es la que vale
        {
            img.clear (img.getBounds());
            juce::Graphics g (img);
            lens.paintEntireComponent (g, false);
        }

        const int esperadas = juce::jmin (n, telescope::FieldLens::kMaxTrailPlanes);
        std::printf ("FIELD[lente] trailLayers=%d  celdas=%5d  del plano de AHORA=%4d  laminas dibujadas="
                     "%d de %d  %s\n",
                     n, lens.pointsDrawn(), lens.liveCellsDrawn(), esperadas, lens.trailLayersDrawn(),
                     lens.liveCellsDrawn() > 0 ? "OK" : "SIN PRESENTE");

        CHECK (lens.trailLayersDrawn() == n);
        CHECK (lens.liveCellsDrawn() > 0);   // el presente se dibuja SIEMPRE, sea cual sea la estela
        CHECK (lens.pointsDrawn() <= FieldLens::kMaxCells);
    }

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57c · punto 1c: LA LÁMINA DE ESTELA NO TIENE BORDES DUROS =====
//
// «Se ven como montañas y arriba sigue habiendo como píxeles, no se entiende bien» (Joaquín, 12-sep,
// mirando TELESCOPE en su DAW al lado de Insight). El "arriba" es exacto: la franja de la lente a la que
// sólo llega la estela (`trailOnlyArea`) mostraba RECTÁNGULOS con borde duro, porque la lámina de estela se
// estiraba por VECINO MÁS CERCANO desde la grilla decimada de 48 filas × 32 direcciones. A escala 2 esa
// lámina mide ~1 600 px de ancho, así que cada celda ocupaba ~50 px seguidos del MISMO color y la frontera
// entre dos celdas era un escalón de golpe — 31 escalones por fila, uno por cada borde de columna.
//
// LO QUE SE MIDE, y por qué así: los pares de píxeles HORIZONTALMENTE ADYACENTES con un salto de luma
// mayor a 16/255. Una superficie continua no puede producir uno: una rampa que cruza una celda de ~50 px
// sube ~1/255 por píxel. Un BORDE sí. Contarlos es contar bordes, y el porcentaje sobre el total de pares
// dice qué fracción de la lámina es escalón en vez de rampa.
//
// SE MIDE SOBRE LA CACHÉ, no sobre el componente pintado. En el componente la caja de alambre, la línea
// del centro y las aristas de fuga se dibujan ENCIMA de la imagen, en el plano lógico (ver drawStage):
// son líneas de 1 px que producen sus propios saltos y no tienen nada que ver con la estela. La caché es
// la lámina sola, que es lo que este test existe para vigilar.
// ========================================================================================================
TEST_CASE ("telescope: la lamina de estela de FIELD no tiene bordes duros", "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (0);   // 0.3 s: la misma señal ancha de `field_wide` y del presupuesto
    proc.setFieldDecayIndex (2);

    IndepPink sig;
    push (proc, sig, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 10000));

    telescope::Lens::setReducedMotion (false);
    FieldLens lens (proc);
    lens.setSize (kLensW, kLensH);

    // A escala 2, como una Retina: es donde una celda de la grilla ocupa ~50 px y el escalón se ve.
    constexpr float kScale = 2.0f;
    juce::Image img (juce::Image::ARGB, (int) std::lround (kLensW * (double) kScale),
                     (int) std::lround (kLensH * (double) kScale), true);
    for (int i = 0; i < 6; ++i)
    {
        lens.pumpFrames (1);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (kScale));
        lens.paintEntireComponent (g, false);
    }
    REQUIRE (lens.trailLayersDrawn() == telescope::FieldFrame::kTrail);

    // La franja donde SÓLO puede haber estela, en píxeles de la caché (= de dispositivo).
    const auto  cache  = lens.cacheAreaForTest();     // el rectángulo LÓGICO que cubre la imagen
    const auto  band   = lens.trailOnlyArea();        // la franja LÓGICA de la estela sola
    const auto& raster = lens.cacheImageForTest();
    REQUIRE (raster.isValid());
    REQUIRE (std::abs (lens.cacheScaleForTest() - kScale) < 1.0e-4f);

    const auto toDev = [] (int v) { return (int) std::lround ((double) v * (double) kScale); };
    const int x0 = juce::jlimit (0, raster.getWidth()  - 1, toDev (band.getX()      - cache.getX()) + 1);
    const int x1 = juce::jlimit (0, raster.getWidth()  - 1, toDev (band.getRight()  - cache.getX()) - 1);
    const int y0 = juce::jlimit (0, raster.getHeight() - 1, toDev (band.getY()      - cache.getY()) + 1);
    const int y1 = juce::jlimit (0, raster.getHeight() - 1, toDev (band.getBottom() - cache.getY()) - 1);
    REQUIRE (x1 > x0 + 16);
    REQUIRE (y1 > y0 + 16);

    const juce::Image::BitmapData bd (raster, juce::Image::BitmapData::readOnly);
    const auto luma = [&bd] (int x, int y)
    {
        const auto c = bd.getPixelColour (x, y);
        return 0.2126 * (double) c.getRed() + 0.7152 * (double) c.getGreen() + 0.0722 * (double) c.getBlue();
    };

    long long hard = 0, pairs = 0, worstRow = 0;
    for (int y = y0; y <= y1; ++y)
    {
        long long rowHard = 0;
        double prev = luma (x0, y);
        for (int x = x0 + 1; x <= x1; ++x)
        {
            const double v = luma (x, y);
            if (std::abs (v - prev) > kHardEdgeLuma) ++rowHard;
            prev = v;
            ++pairs;
        }
        hard += rowHard;
        worstRow = std::max (worstRow, rowHard);
    }

    const double pct = pairs > 0 ? 100.0 * (double) hard / (double) pairs : 0.0;
    std::printf ("FIELD[estela] bordes duros en la lamina: %lld de %lld pares (%.3f %%)  ·  peor fila %lld "
                 "bordes sobre %d px  ·  criterio 0.300 %%  ·  ANTES (vecino mas cercano, base d99c140): "
                 "%.3f %% con %d bordes en la peor fila\n",
                 hard, pairs, pct, worstRow, x1 - x0, kBeforePct, kBeforeWorstRow);

    CHECK (pct <= 0.300);

    proc.releaseResources();
}

// ========================================================================================================
// ===== 57d · punto 2c: FIELD[brillo] — EL PLANO DE AHORA TIENE MÁS LUZ =====
//
// «Habría que suavizarlo un poco más y darle más brillo» (Joaquín, 14-sep, con el 57c en su DAW). Lo que
// cambia es CÓMO se pinta el relieve —la luz de la cara en sombra, la curva del índice de paleta y el piso de
// alpha—, no lo que se mide: `FieldFrame`, la lectura bajo el cursor y FIELD[lente] quedan como estaban.
//
// Lo que se mide acá es el resultado, sobre la CACHÉ (la superficie sola, sin la caja de alambre ni los ejes,
// que se dibujan encima en el plano lógico): la luma media de la zona del plano de AHORA —toda la caché por
// debajo de la franja a la que sólo llega la estela, `trailOnlyArea()`—, con la señal de `field_wide` (ruido
// estéreo independiente, ventana 0.3 s, τ = 1 s) y a escala 2, que es donde lo mira Joaquín.
//
// El criterio es relativo a b777c08 y el número de b777c08 se midió con ESTE MISMO test antes de tocar la
// lente (ver `kLumaB777c08`): un criterio absoluto de luma dependería de la paleta y del fondo del Theme, y
// no diría si la lente ganó luz o si cambió otra cosa.
// ========================================================================================================
namespace
{
// Medido con este test sobre el código de FIELD de b777c08 (2026-09-14, M4 de la casa), tres corridas: 32.250,
// 32.250, 32.250 — idénticas, porque la señal es determinista y se espera a que el frame deje de moverse.
constexpr double kLumaB777c08       = 32.250;
constexpr double kMinBrightnessGain = 1.20;
}

TEST_CASE ("telescope: FIELD tiene mas luz en el plano de AHORA", "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (0);   // la señal de `field_wide_M`: ventana de 0.3 s…
    proc.setFieldDecayIndex (1);    // …y τ = 1 s

    IndepPink sig;
    push (proc, sig, 5.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 10000));
    // LA CONDICIÓN, NO LA QUIETUD (lección del intermitente de TONAL BALANCE): el hilo de análisis tiene que
    // haber digerido lo empujado, y recién ahí se espera a que el frame del campo deje de moverse. Si no, la
    // lente pinta un frame a medio camino y la luma cambia de corrida en corrida. "Digerido" es con medio
    // segundo de holgura: el último tramo que no completa una ventana de análisis se queda en la cola hasta
    // que llegue más audio, y acá no llega más (medido: con 50 ms de holgura la espera no se cumple nunca).
    const double pushed = (double) (int) (5.0 * 48000.0 / 512.0) * 512.0 / 48000.0;
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= pushed - 0.5; },
                                         20000));
    REQUIRE (telescope::test::waitStable ([&] { return (double) proc.field().read().frameIndex; }, 150, 20000));

    telescope::Lens::setReducedMotion (false);
    FieldLens lens (proc);
    lens.setSize (kLensW, kLensH);

    constexpr float kScale = 2.0f;
    juce::Image img (juce::Image::ARGB, (int) std::lround (kLensW * (double) kScale),
                     (int) std::lround (kLensH * (double) kScale), true);
    for (int i = 0; i < 6; ++i)
    {
        lens.pumpFrames (1);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (kScale));
        lens.paintEntireComponent (g, false);
    }

    const auto  cache  = lens.cacheAreaForTest();
    const auto  band   = lens.trailOnlyArea();
    const auto& raster = lens.cacheImageForTest();
    REQUIRE (raster.isValid());
    REQUIRE (std::abs (lens.cacheScaleForTest() - kScale) < 1.0e-4f);

    // La zona del plano de AHORA: la caché entera por debajo de la franja de sólo-estela.
    const int y0 = juce::jlimit (0, raster.getHeight() - 1,
                                 (int) std::lround ((double) (band.getBottom() - cache.getY()) * (double) kScale) + 1);
    const juce::Image::BitmapData bd (raster, juce::Image::BitmapData::readOnly);
    double sum = 0.0;
    long long pixels = 0;
    for (int y = y0; y < raster.getHeight(); ++y)
        for (int x = 0; x < raster.getWidth(); ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            sum += 0.2126 * (double) c.getRed() + 0.7152 * (double) c.getGreen() + 0.0722 * (double) c.getBlue();
            ++pixels;
        }
    const double luma  = pixels > 0 ? sum / (double) pixels : 0.0;
    const double ratio = kLumaB777c08 > 0.0 ? luma / kLumaB777c08 : 0.0;

    std::printf ("FIELD[brillo] luma media del plano de AHORA: %.3f  (%lld px de cache desde la fila %d; la franja de "
                 "solo-estela afuera)  ·  b777c08: %.3f  ·  razon %.3f (criterio >= %.2f)  ·  paleta %s  ·  "
                 "lambert %.2f + %.2f  ·  gamma %.2f  ·  piso %.2f   [b777c08: 0.45 + 0.55 · gamma 1.00 · piso 0.16]\n",
                 luma, pixels, y0, kLumaB777c08, ratio, kMinBrightnessGain,
                 juce::String (telescope::look::paletteName (telescope::look::paletteFromIndex (proc.paletteIndex()))).toRawUTF8(),
                 (double) FieldLens::kReliefAmbient, (double) FieldLens::kReliefDiffuse,
                 (double) FieldLens::kReliefGamma, (double) FieldLens::kSurfaceFloorAlpha);

    REQUIRE (pixels > 100000);
    REQUIRE (kLumaB777c08 > 0.0);                     // el número de b777c08 está medido y escrito
    REQUIRE (luma >= kMinBrightnessGain * kLumaB777c08);

    proc.releaseResources();
}

// ================================================================================= 9 · la lectura
TEST_CASE ("telescope: FIELD lee la direccion y la frecuencia bajo el cursor", "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kLoudness);
    proc.setBandsWindowIndex (1);

    TwoSources sig;
    push (proc, sig, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().maxCell > 0.0f; }, 8000));

    FieldLens lens (proc);
    lens.setSize (kLensW, kLensH);
    lens.pumpFrames (3);
    { juce::Image img (juce::Image::ARGB, kLensW, kLensH, true); juce::Graphics g (img);
      lens.paintEntireComponent (g, false); }   // pintar fija la normalización que usa la lectura

    // Se barre el plot y se busca, POR SEPARADO, la lectura más fuerte cerca de cada tono. Buscar un solo
    // máximo global no serviría, y el porqué vale la pena anotarlo: los dos tonos tienen la MISMA
    // amplitud pero el de 100 Hz lee más bajo. No es un error de la medición — es la resolución de la
    // grilla. Con FFT de 4 096 a 48 kHz un bin mide 11.72 Hz, y a 100 Hz una fila de la grilla mide 7.5 %
    // de ancho relativo, o sea ~7.5 Hz: MENOS que un bin. El tono cae entre los bins 8 y 9, que van a
    // parar a filas distintas, y su energía queda repartida en dos. A 5 kHz, en cambio, la fila cubre
    // ~375 Hz y se lleva los dos bins juntos. Está en el README.
    struct Best { double hz = 0.0, pan = 0.0; float db = -1000.0f; };
    Best low, high;
    int  valid = 0;
    for (int y = 0; y < kLensH; y += 2)
        for (int x = 0; x < kLensW; x += 2)
        {
            const auto r = lens.readoutAt ({ x, y });
            if (! r.valid) continue;
            ++valid;
            auto& slot = (r.freqHz < 500.0) ? low : high;
            if (r.db > slot.db) slot = { r.freqHz, r.panPercent, r.db };
        }

    std::printf ("FIELD[lectura] %d px con dato  ·  grave: %.0f Hz paneo %+.0f %% (%.1f dB rel)"
                 "  ·  agudo: %.0f Hz paneo %+.0f %% (%.1f dB rel)\n",
                 valid, low.hz, low.pan, low.db, high.hz, high.pan, high.db);
    REQUIRE (valid > 1000);
    REQUIRE (std::abs (low.hz  /  100.0 - 1.0) < 0.15);    // el tono de 100 Hz…
    REQUIRE (low.pan  < -90.0);                            // …sólo en L
    REQUIRE (low.db   > -12.0f);                           // y bien por encima del piso de −45
    REQUIRE (std::abs (high.hz / 5000.0 - 1.0) < 0.15);    // el tono de 5 kHz…
    REQUIRE (high.pan > 90.0);                             // …sólo en R
    REQUIRE (high.db  > -3.0f);                            // es el más fuerte de la grilla

    // Fuera del plano de adelante no hay lectura (no se inventa un número en la profundidad).
    REQUIRE_FALSE (lens.readoutAt ({ kLensW / 2, 4 }).valid);
    REQUIRE_FALSE (lens.readoutAt ({ -5, kLensH / 2 }).valid);

    proc.releaseResources();
}

// ============================================================================== 10 · estado y demanda
TEST_CASE ("telescope: el setting de FIELD sobrevive al round-trip", "[telescope][field]")
{
    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        REQUIRE (proc.fieldDecayIndex() == Field::kDefaultDecayIndex);
        proc.setFieldDecayIndex (2);   // 2 s
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    REQUIRE (proc.fieldDecayIndex() == Field::kDefaultDecayIndex);
    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    std::printf ("FIELD[round-trip] decaimiento %.1f s (indice %d)\n", proc.fieldDecaySec(),
                 proc.fieldDecayIndex());
    REQUIRE (proc.fieldDecayIndex() == 2);
    REQUIRE (proc.fieldDecaySec() == Field::kDecaySecOptions[2]);

    proc.setFieldDecayIndex (99);
    REQUIRE (proc.fieldDecayIndex() == Field::kNumDecayOptions - 1);
}

TEST_CASE ("telescope: lente a demanda — kField solo con FIELD arriba", "[telescope][field]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    const auto select = [&] (telescope::LensId id)
    {
        auto* p = proc.apvts.getParameter ("lens");
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) id));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    };

    select (telescope::LensId::stereoSpectrogram);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kField)       == 0u);   // la 10 no paga el campo

    select (telescope::LensId::field);
    REQUIRE ((proc.enabledModules() & telescope::kField)       != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) != 0u);   // el campo come del paneo por bin
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum)    != 0u);   // …que come de la STFT
    REQUIRE ((proc.enabledModules() & telescope::kLoudness)    != 0u);

    select (telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kField) == 0u);

    select (telescope::LensId::field);
    ed.reset();
    std::printf ("FIELD[a demanda] mascara tras cerrar = 0x%02x\n", (unsigned) proc.enabledModules());
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    proc.releaseResources();
}

// ============================================================================================ 11 · fotos
TEST_CASE ("telescope: snapshot del editor con la lente FIELD en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setBandsWindowIndex (1);
    proc.setFieldDecayIndex (1);   // 1 s

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::field));

    // El EDITOR primero: es el que enciende kField (lente a demanda).
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kField) != 0u);

    // (a) DOS FUENTES en canales opuestos: dos manchas en esquinas opuestas.
    TwoSources two;
    push (proc, two, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 8000));
    {
        const auto& f = proc.field().read();
        std::printf ("UISNAP field (dos fuentes): maxCell %.3e  ·  %d estelas  ·  decaimiento %.1f s\n",
                     (double) f.maxCell, f.trailCount, f.decaySec);
        REQUIRE (f.maxCell > 0.0f);
    }

    tel->pumpLensFrames (6);
    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_field_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_field_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_field_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (3);
        writePng (*tel, s.path);
    }

    proc.releaseResources();

    // (b) RUIDO INDEPENDIENTE, en un processor NUEVO: la misma lente con lo contrario — la energía
    // repartida en dirección en vez de apilada en dos esquinas. Los cuatro números de banda ancha de (a) y
    // (b) son casi los mismos (corr ≈ 0, width ≈ 1) y las dos fotos no se parecen en nada: ése es el punto
    // entero de la lente.
    //
    // Processor NUEVO y no el mismo: con τ = 1 s los dos tonos de (a) siguen siendo, decaídos, las celdas
    // más fuertes de la grilla, y como la normalización es el máximo, el ruido se dibujaría 20 dB por
    // debajo de lo que le toca. La foto mostraría "no hay casi nada" cuando lo que pasa es otra cosa.
    telescope::TelescopeProcessor wideProc;
    wideProc.prepareToPlay (48000.0, 512);
    wideProc.setBandsWindowIndex (0);   // 0.3 s: con la ventana corta la dispersión del paneo se ve mejor
    wideProc.setFieldDecayIndex (1);

    auto* wideParam = wideProc.apvts.getParameter ("lens");
    REQUIRE (wideParam != nullptr);
    wideParam->setValueNotifyingHost (wideParam->convertTo0to1 ((float) (int) telescope::LensId::field));

    std::unique_ptr<juce::AudioProcessorEditor> wideEd (wideProc.createEditor());
    auto* wideTel = dynamic_cast<telescope::TelescopeEditor*> (wideEd.get());
    REQUIRE (wideTel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((wideProc.enabledModules() & telescope::kField) != 0u);

    IndepPink wide;
    push (wideProc, wide, 5.0);
    REQUIRE (telescope::test::waitUntil ([&] { return wideProc.field().read().trailCount
                                                      == telescope::FieldFrame::kTrail; }, 8000));
    std::printf ("UISNAP field (independientes): maxCell %.3e  ·  %d estelas\n",
                 (double) wideProc.field().read().maxCell, wideProc.field().read().trailCount);

    wideTel->applyZoom (ovni::PluginEditorBase::Zoom::medium);
    wideTel->pumpLensFrames (8);
    writePng (*wideTel, "/tmp/ovni_telescope_field_wide_M.png");

    wideProc.releaseResources();
}
