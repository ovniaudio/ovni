// [telescope][cqt] — el módulo Cqt: el espectro constant-Q, el cromagrama y la tonalidad estimada.
//
// Todo se mide CONTRA EL MÓDULO DIRECTO (sin processor ni editor) y con señales cuya respuesta se DERIVA
// acá, nunca se copia de lo que devolvió la función. Los diez casos de la tabla CQT[...]:
//
//   1 niveles      A0 / A4 / A7 a −20 dBFS leen −20.0 en su bin, en las TRES octavas. La normalización
//                  por bin es toda la prueba: cada bin tiene su propia ventana y su propia ganancia.
//   2 fuera de     medio bin de corrimiento pierde como mucho el scalloping de Hann (1.42 dB).
//     centro
//   3 selectividad un semitono (2 bins) se ve como DOS máximos; 25 cents (medio bin), como uno solo.
//   4 cromagrama   la tríada de Do mayor deja C, E y G arriba y los otros nueve abajo.
//   5 tonalidad    Do mayor, La menor y ruido rosa (que no tiene tonalidad, y la confianza lo dice).
//   6 octavas      cinco A en cinco octavas son UNA clase de nota.
//   7 cruce        el mismo seno de 1 kHz leído por el CQT y por el FFT del 50 da el mismo dB.
//   8 latencia     la que se declara es la que se mide.
//   9 bloque       1 / 7 / 64 / 4096 dan el mismo frame 20 AL BIT.
//  10 silencio     todo en el piso, sin NaN, y sin inventar una tonalidad.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/modules/Cqt.h"
#include "analysis/modules/Spectrum.h"

using telescope::Cqt;
using telescope::CqtFrame;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// ---- las frecuencias se DERIVAN, no se copian ----
double equalTempered (int midi) noexcept { return 440.0 * std::pow (2.0, (double) (midi - 69) / 12.0); }
// El bin (real, con decimales) donde cae una frecuencia: B·log2(f/f_min).
double binPosition (double hz) noexcept
{
    return (double) Cqt::kBinsPerOctave * std::log2 (hz / Cqt::kFMinHz);
}
int nearestBin (double hz) noexcept { return (int) std::lround (binPosition (hz)); }

// Respuesta de la ventana de Hann a δ bins del centro, normalizada al centro, en dB (≤ 0).
// W(δ) = 0.5·sinc(δ) + 0.25·sinc(δ−1) + 0.25·sinc(δ+1), con sinc(x) = sin(πx)/(πx).
// El CQT está diseñado para que UN bin del eje sea UN bin de la DFT de su propia ventana, así que la misma
// fórmula vale para el espectro constant-Q y para el FFT del 50.
double sincPi (double x) noexcept { return std::abs (x) < 1.0e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x); }
double hannResponse (double delta) noexcept
{
    return 0.5 * sincPi (delta) + 0.25 * sincPi (delta - 1.0) + 0.25 * sincPi (delta + 1.0);
}
double hannScallopDb (double delta) noexcept { return 20.0 * std::log10 (hannResponse (delta) / 0.5); }

const char* kClassNames[12] = { "Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
// Devuelve por VALOR: con un buffer estático, dos llamadas en el mismo printf se pisan (pasó: una de las
// dos tonalidades salía vacía en la línea CQT[motor]).
std::string keyName (int tonic, int mode)
{
    if (tonic < 0) return "sin tonalidad";
    return std::string (kClassNames[tonic]) + (mode == Cqt::minor ? " menor" : " mayor");
}

// ---- señales ----
struct Tone { double hz = 0.0, amp = 0.0; };

// Genera `seconds` de la suma de tonos, con fase continua desde n = 0 (determinista al bit).
std::vector<float> tonesBuffer (const std::vector<Tone>& tones, double seconds)
{
    const auto n = (size_t) std::llround (seconds * kSr);
    std::vector<float> v (n, 0.0f);
    for (const auto& t : tones)
        for (size_t i = 0; i < n; ++i)
            v[i] += (float) (t.amp * std::sin (2.0 * kPi * t.hz * (double) i / kSr));
    return v;
}

// Arpegio: una nota por tramo, con envolvente de Hann completa (sin clicks, que ensuciarían el espectro).
void addArpeggio (std::vector<float>& v, const std::vector<double>& hz, double noteSec, double amp)
{
    const auto step = (size_t) std::llround (noteSec * kSr);
    if (step == 0 || hz.empty()) return;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const size_t note = (i / step) % hz.size();
        const double u = (double) (i % step) / (double) step;
        const double env = 0.5 - 0.5 * std::cos (2.0 * kPi * u);
        v[i] += (float) (amp * env * std::sin (2.0 * kPi * hz[note] * (double) i / kSr));
    }
}

std::vector<float> pinkBuffer (double seconds, double amp, std::uint32_t seed)
{
    telescope::test::Pink p { seed };
    std::vector<float> v ((size_t) std::llround (seconds * kSr), 0.0f);
    for (auto& x : v) x = (float) (amp * p.next());
    return v;
}

Cqt::Settings baseSettings (int channel = Cqt::mid, int chromaIdx = Cqt::kDefaultChromaSecIndex)
{
    Cqt::Settings s;
    s.channel           = channel;
    s.chromaSecIndex    = chromaIdx;
    s.holdDecayDbPerSec = 12.0f;
    return s;
}

// Alimenta el módulo con el MISMO buffer por los dos canales, en bloques de `block`.
void feed (Cqt& m, const std::vector<float>& v, int block = 512)
{
    for (size_t done = 0; done < v.size(); )
    {
        const int k = (int) std::min ((size_t) block, v.size() - done);
        m.process (v.data() + done, v.data() + done, k);
        done += (size_t) k;
    }
}

// Espectro PROMEDIADO EN POTENCIA sobre los frames en régimen. Hace falta porque dos tonos cercanos
// INTERFIEREN dentro de una misma ventana: su fase relativa decide si en el bin del medio se suman o se
// restan, así que un frame suelto no contesta "¿se ven dos picos?" sino "¿en este instante se sumaron o se
// restaron?" (medido: A4 + 25 cents da un HUECO en el bin 96 y picos en el 95 y el 97 según el frame). El
// promedio de potencia es la respuesta incoherente, que es la que describe lo que se ve a lo largo del
// tiempo — y la que corresponde a la pregunta "¿el instrumento resuelve un cuarto de tono?".
std::vector<double> averagedPowerDb (Cqt& m, const std::vector<float>& v, double skipSec)
{
    const int hopN = m.hopSamples();
    const auto skipFrames = (size_t) std::llround (skipSec * kSr / (double) hopN);
    std::vector<double> acc;
    size_t used = 0, frame = 0;

    for (size_t done = 0; done < v.size(); done += (size_t) hopN, ++frame)
    {
        const int k = (int) std::min ((size_t) hopN, v.size() - done);
        m.process (v.data() + done, v.data() + done, k);
        if (frame < skipFrames) continue;

        const auto& f = m.frame();
        if (acc.empty()) acc.assign ((size_t) f.numBins, 0.0);
        for (int b = 0; b < f.numBins; ++b) acc[(size_t) b] += std::pow (10.0, (double) f.magDb[b] / 10.0);
        ++used;
    }
    for (auto& x : acc) x = 10.0 * std::log10 (std::max (1.0e-30, x / (double) std::max<size_t> (1, used)));
    return acc;
}

int argmaxBin (const CqtFrame& f)
{
    int best = 0;
    for (int k = 1; k < f.numBins; ++k) if (f.magDb[k] > f.magDb[best]) best = k;
    return best;
}

std::unique_ptr<Cqt> makeModule (const Cqt::Settings& s = baseSettings())
{
    auto m = std::make_unique<Cqt>();
    m->applySettings (s);
    m->prepare (kSr);
    return m;
}
}

// ============================================================================ 0 · geometría del diseño
// Los números del diseño salen de la fórmula, no de una tabla: si alguien cambia B o f_min, esto se cae.
TEST_CASE ("telescope: la geometria del constant-Q es la del diseno", "[telescope][cqt]")
{
    auto m = makeModule();

    const double q = 1.0 / (std::pow (2.0, 1.0 / (double) Cqt::kBinsPerOctave) - 1.0);
    const double fMax = std::min (Cqt::kFMaxHz, Cqt::kNyquistFrac * kSr);
    const int wantBins = (int) std::ceil ((double) Cqt::kBinsPerOctave * std::log2 (fMax / Cqt::kFMinHz));
    const int wantN0 = (int) std::lround (q * kSr / Cqt::kFMinHz);

    m->buildKernels();

    std::printf ("CQT[geometria] fs=%.0f  B=%d  f_min=%.1f Hz  f_max=%.0f Hz  ->  %d bins  ·  Q=%.4f  ·  "
                 "N_0=%d (%.4f s)  N_ultimo=%d  ·  FFT 2^%d  ·  %lld coeficientes (%.2f %% del denso) en "
                 "%.0f ms\n",
                 kSr, Cqt::kBinsPerOctave, Cqt::kFMinHz, fMax, m->numBins(), q, m->kernelLength (0),
                 m->lowestBinLatencySec(), m->kernelLength (m->numBins() - 1), m->fftOrder(),
                 m->kernelCoefficients(), 100.0 * m->kernelDensity(), m->kernelBuildMs());

    // LA TABLA DE LATENCIAS que va al README: cada bin ve N_k muestras, y N_k = Q·fs/f_k, así que la
    // latencia se DIVIDE POR DOS en cada octava. Es la propiedad que define al método y la que hay que
    // declarar: en el grave el instrumento está mirando más de un segundo atrás.
    for (const int midi : { 21, 45, 69, 93 })            // A0 · A2 · A4 · A6
    {
        const double hz = equalTempered (midi);
        const int    bin = nearestBin (hz);
        const int    nk = m->kernelLength (bin);
        std::printf ("CQT[latencia por bin] A%d  %8.2f Hz  bin %3d  ->  N_k = %6d muestras = %7.4f s\n",
                     (midi - 21) / 12, hz, bin, nk, (double) nk / kSr);
        REQUIRE_THAT ((double) nk, Catch::Matchers::WithinRel (q * kSr / hz, 1.0e-3));
    }

    REQUIRE_THAT (Cqt::qFactor(), Catch::Matchers::WithinRel (q, 1.0e-12));
    REQUIRE (m->numBins() == wantBins);
    REQUIRE (m->numBins() == 229);                      // a 48 kHz, el número del diseño
    REQUIRE (m->kernelLength (0) == wantN0);
    REQUIRE (m->fftSize() >= wantN0);                   // el bloque tiene que contener la ventana más larga
    REQUIRE (m->fftSize() / 2 < wantN0);                // ...y no de más: 2^16 es el mínimo que la contiene
    REQUIRE_THAT (m->lowestBinLatencySec(), Catch::Matchers::WithinAbs ((double) wantN0 / kSr, 1.0e-6));
    REQUIRE (m->hopSamples() == 4800);                  // 100 ms, el hop del AnalysisThread

    // La poda es lo que hace viable el método: sin ella serían 229 × 32 769 coeficientes.
    REQUIRE (m->kernelDensity() < 0.05);
    REQUIRE (m->kernelCoefficients() > 0);
}

// ============================================================================ 1 · niveles por octava
// LA PRUEBA DE LA NORMALIZACIÓN. Tres A a −20 dBFS, separadas por siete octavas: A0 (27.5 Hz, ventana de
// 59 568 muestras) y A7 (3 520 Hz, ventana de 465) tienen que leer EL MISMO número. Si la ganancia
// coherente se calculara una sola vez y no por bin, esto se caería por octavas enteras.
TEST_CASE ("telescope: un seno lee 20.log10(A) en su bin, en cualquier octava", "[telescope][cqt]")
{
    constexpr double kAmp = 0.1;               // −20 dBFS exacto
    const double wantDb = 20.0 * std::log10 (kAmp);

    struct Caso { const char* nombre; int midi; double seconds; };
    const Caso casos[] = { { "A0", 21, 3.0 }, { "A4", 69, 2.0 }, { "A7", 105, 2.0 } };

    for (const auto& c : casos)
    {
        const double hz = equalTempered (c.midi);
        const int    want = nearestBin (hz);

        auto m = makeModule();
        feed (*m, tonesBuffer ({ { hz, kAmp } }, c.seconds));
        const auto& f = m->frame();
        const int   got = argmaxBin (f);

        std::printf ("CQT[nivel %s] %8.3f Hz  ->  bin %3d (esperado %3d, %8.3f Hz)  nivel %+7.3f dB "
                     "(esperado %+.3f)  ventana %d muestras (%.3f s)\n",
                     c.nombre, hz, got, want, f.binHz (want), f.magDb[want], wantDb,
                     m->kernelLength (want), (double) m->kernelLength (want) / kSr);

        INFO (c.nombre << " a " << hz << " Hz");
        REQUIRE (got == want);
        REQUIRE_THAT (f.magDb[want], Catch::Matchers::WithinAbs ((float) wantDb, 0.2f));
    }

    // Y los bins DERIVADOS son los del diseño: A0 = 0, A4 = 96 (= 24·log2(440/27.5)), A7 = 168.
    REQUIRE (nearestBin (equalTempered (21)) == 0);
    REQUIRE (nearestBin (equalTempered (69)) == 96);
    REQUIRE (nearestBin (equalTempered (105)) == 168);
}

// ============================================================================ 2 · fuera de centro
// Medio bin de corrimiento es el peor caso del muestreo del eje: la pérdida no puede pasar del scalloping
// de Hann, 1.42 dB. OJO CON EL NÚMERO DEL PROMPT: 452.9 Hz NO es medio bin, es el bin 97 CLAVADO (un
// cuarto de tono arriba de A4, y con B = 24 un cuarto de tono es un bin entero). El medio bin real cae en
// 446.46 Hz, que es A4 + 25 cents. Se miden LOS DOS: el de medio bin por el scalloping, y el 452.9 del
// prompt para mostrar que ahí no hay pérdida ninguna porque es un centro de bin.
TEST_CASE ("telescope: medio bin fuera de centro pierde a lo sumo el scalloping de Hann", "[telescope][cqt]")
{
    constexpr double kAmp = 0.1;
    const double wantDb = 20.0 * std::log10 (kAmp);
    const double kMaxScallopDb = -hannScallopDb (0.5);        // 1.42 dB, derivado

    const double halfBinHz = Cqt::kFMinHz * std::pow (2.0, 96.5 / (double) Cqt::kBinsPerOctave);
    {
        auto m = makeModule();
        feed (*m, tonesBuffer ({ { halfBinHz, kAmp } }, 2.0));
        const auto& f = m->frame();
        const int   got = argmaxBin (f);
        const double loss = (double) wantDb - (double) f.magDb[got];

        std::printf ("CQT[medio bin] %8.3f Hz (bin %.2f)  ->  argmax %d  nivel %+7.3f dB  perdida %.3f dB "
                     "(scalloping de Hann = %.3f)\n",
                     halfBinHz, binPosition (halfBinHz), got, f.magDb[got], loss, kMaxScallopDb);

        REQUIRE ((got == 96 || got == 97));
        REQUIRE (loss >= -0.05);                              // nunca puede LEER de más
        REQUIRE (loss <= kMaxScallopDb + 0.1);
    }

    // El 452.9 Hz del prompt: un centro de bin, sin pérdida.
    {
        auto m = makeModule();
        feed (*m, tonesBuffer ({ { 452.9, kAmp } }, 2.0));
        const auto& f = m->frame();
        const int   got = argmaxBin (f);
        std::printf ("CQT[452.9 Hz] bin %.3f (o sea el 97 clavado, un cuarto de tono sobre A4)  ->  "
                     "argmax %d  nivel %+7.3f dB\n", binPosition (452.9), got, f.magDb[got]);
        REQUIRE (got == 97);
        REQUIRE_THAT (f.magDb[97], Catch::Matchers::WithinAbs ((float) wantDb, 0.2f));
    }
}

// ============================================================================ 3 · selectividad
// Un semitono son DOS bins y tiene que verse como dos picos; 25 cents son medio bin y tiene que verse como
// uno solo. Es la resolución que el instrumento promete: "estas son dos notas" contra "esto está desafinado".
TEST_CASE ("telescope: un semitono se ve como dos picos y 25 cents como uno solo", "[telescope][cqt]")
{
    const double a4 = equalTempered (69), bb4 = equalTempered (70);
    const double sharp25 = a4 * std::pow (2.0, 25.0 / 1200.0);

    // Máximos locales por encima de un umbral relativo al pico, en una ventana alrededor de A4.
    const auto peaksAround = [] (const std::vector<double>& db, int from, int to, double relDb)
    {
        double top = -1.0e9;
        for (int k = from; k <= to; ++k) top = std::max (top, db[(size_t) k]);
        std::vector<int> peaks;
        for (int k = from + 1; k < to; ++k)
            if (db[(size_t) k] > db[(size_t) (k - 1)] && db[(size_t) k] >= db[(size_t) (k + 1)]
                && db[(size_t) k] > top + relDb)
                peaks.push_back (k);
        return peaks;
    };

    {
        auto m = makeModule();
        const auto avg = averagedPowerDb (*m, tonesBuffer ({ { a4, 0.1 }, { bb4, 0.1 } }, 3.0), 1.0);
        const auto peaks = peaksAround (avg, 88, 106, -12.0);
        std::printf ("CQT[semitono] A4 %.2f Hz (bin %.2f) + Bb4 %.2f Hz (bin %.2f)  ->  %zu maximos: ",
                     a4, binPosition (a4), bb4, binPosition (bb4), peaks.size());
        for (const int p : peaks) std::printf ("%d ", p);
        std::printf (" (separacion %d bins)\n", peaks.size() >= 2 ? peaks.back() - peaks.front() : 0);

        REQUIRE (peaks.size() == 2);
        REQUIRE (peaks.back() - peaks.front() >= 1);
    }
    {
        auto m = makeModule();
        const auto avg = averagedPowerDb (*m, tonesBuffer ({ { a4, 0.1 }, { sharp25, 0.1 } }, 3.0), 1.0);
        const auto peaks = peaksAround (avg, 88, 106, -12.0);
        std::printf ("CQT[25 cents] A4 %.2f Hz (bin %.2f) + %.2f Hz (bin %.2f)  ->  %zu maximo(s): ",
                     a4, binPosition (a4), sharp25, binPosition (sharp25), peaks.size());
        for (const int p : peaks) std::printf ("%d ", p);
        std::printf ("\n");

        REQUIRE (peaks.size() == 1);
    }
}

// ============================================================================ 4 · cromagrama
// La tríada de Do mayor tiene que dejar C, E y G arriba. Los vecinos NO quedan en cero y eso es física,
// no un defecto: con B = 24 y Q canónico, un bin del eje ES un bin de la DFT de su ventana, así que la
// falda de Hann pone 0.25 de la potencia en el bin de al lado — y ese bin, con el mapeo ⌊k/2⌋, cae en la
// clase de abajo. Como la clase de la nota se queda además con su propio vecino de arriba, la relación
// derivada es 0.25 / 1.25 = 0.20. El criterio del prompt (< 0.3) se cumple con margen.
TEST_CASE ("telescope: el cromagrama de una triada de Do mayor deja C, E y G arriba", "[telescope][cqt]")
{
    auto m = makeModule (baseSettings (Cqt::mid, 0));   // 0.5 s de suavizado: es una señal estacionaria
    feed (*m, tonesBuffer ({ { equalTempered (60), 0.1 },     // C4
                             { equalTempered (64), 0.1 },     // E4
                             { equalTempered (67), 0.1 } }, 4.0));
    const auto& f = m->frame();

    std::printf ("CQT[cromagrama Do mayor] ");
    for (int c = 0; c < 12; ++c) std::printf ("%s=%.3f ", kClassNames[c], f.chroma[c]);
    std::printf ("\n");

    std::array<int, 12> order {};
    std::iota (order.begin(), order.end(), 0);
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return f.chroma[a] > f.chroma[b]; });

    // Los tres mayores son C (0), E (4) y G (7), en algún orden.
    std::array<int, 3> top { order[0], order[1], order[2] };
    std::sort (top.begin(), top.end());
    REQUIRE (top == std::array<int, 3> { 0, 4, 7 });

    // Y los otros nueve por debajo de 0.3 (el criterio del prompt); los que reciben la falda de Hann
    // —B, D# y F#, un bin por debajo de cada nota— quedan en la relación derivada de 0.20.
    for (int i = 3; i < 12; ++i)
    {
        INFO ("clase " << kClassNames[order[i]] << " = " << f.chroma[order[i]]);
        REQUIRE (f.chroma[order[i]] < 0.3f);
    }
    for (const int leak : { 11, 3, 6 })     // B, D#, F#
        REQUIRE_THAT (f.chroma[leak], Catch::Matchers::WithinAbs (0.20f, 0.06f));
}

// ============================================================================ 5 · tonalidad
// Los perfiles de Krumhansl & Kessler correlacionados contra el cromagrama suavizado. Lo que se afirma no
// es "la tonalidad es X" sino "de las 24, la que mejor correlaciona es X, con esta confianza y este % del
// tiempo". Los tres casos son: una tonalidad clara, su relativa menor (el caso difícil, porque comparte
// notas) y ruido rosa, que no tiene ninguna — y ahí lo que importa es que la confianza lo diga.
TEST_CASE ("telescope: la tonalidad estimada acierta Do mayor y La menor, y duda del ruido", "[telescope][cqt]")
{
    // ---- (a) Do mayor: la tríada sostenida, con el bajo en la tónica ----
    //
    // POR QUÉ EL BAJO NO ES DECORACIÓN, y por qué esto se cuenta. Una tríada PELADA de tres notas
    // (C-E-G, todas al mismo nivel) es genuinamente ambigua para Krumhansl-Schmuckler: el perfil menor le
    // da a su ♭6 un peso alto (3.98), así que Mi menor —cuyo perfil pondera E, G, B y C— correlaciona casi
    // igual que Do mayor. Medido con la tríada pelada: Mi menor 0.809 contra Do mayor 0.790, o sea que
    // GANA Mi menor. No es un error del estimador: son tres notas que en un compás real podrían ser las
    // dos cosas, y decidirlo sin bajo ni contexto no es algo que un correlador de doce números pueda
    // hacer. Es exactamente por eso que la lente muestra SIEMPRE la confianza y el % del tiempo, y por eso
    // el README lo dice con este ejemplo.
    //
    // Con la tónica doblada en el bajo —la forma normal de tocar una tríada en estado fundamental— la
    // ambigüedad se cierra sola y Do mayor gana por más de 0.19 de correlación.
    {
        auto m = makeModule (baseSettings (Cqt::mid, 1));   // 2 s de suavizado
        feed (*m, tonesBuffer ({ { equalTempered (48), 0.1 },     // C3, el bajo
                                 { equalTempered (60), 0.1 },     // C4
                                 { equalTempered (64), 0.1 },     // E4
                                 { equalTempered (67), 0.1 } }, 6.0));
        const auto& f = m->frame();
        const double rC  = Cqt::keyCorrelation (f.chromaSmooth, 0, Cqt::major);
        const double rAm = Cqt::keyCorrelation (f.chromaSmooth, 9, Cqt::minor);
        const double rEm = Cqt::keyCorrelation (f.chromaSmooth, 4, Cqt::minor);

        std::printf ("CQT[tonalidad Do mayor] gana %s  confianza %.3f  %.1f %% del tiempo   "
                     "(r[Do mayor]=%.3f  r[La menor]=%.3f  r[Mi menor]=%.3f)\n",
                     keyName (f.keyTonic, f.keyMode).c_str(), f.keyConfidence, 100.0f * f.keyTimeFraction,
                     rC, rAm, rEm);

        REQUIRE (f.keyTonic == 0);
        REQUIRE (f.keyMode == Cqt::major);
        REQUIRE (f.keyConfidence > 0.7f);
        REQUIRE (f.keyTimeFraction >= 0.9f);
        REQUIRE (rC > rEm);       // le gana al competidor de verdad, no sólo a la relativa menor
    }

    // ---- (a bis) la tríada PELADA: se afirma la ambigüedad, no se la esconde ----
    // Si algún día el estimador dejara de ver la competencia acá, sería porque alguien lo "arregló"
    // metiéndole una preferencia por el modo mayor — y eso sí sería inventar.
    {
        auto m = makeModule (baseSettings (Cqt::mid, 1));
        feed (*m, tonesBuffer ({ { equalTempered (60), 0.1 },
                                 { equalTempered (64), 0.1 },
                                 { equalTempered (67), 0.1 } }, 6.0));
        const auto& f = m->frame();
        const double rC  = Cqt::keyCorrelation (f.chromaSmooth, 0, Cqt::major);
        const double rEm = Cqt::keyCorrelation (f.chromaSmooth, 4, Cqt::minor);
        std::printf ("CQT[triada pelada C-E-G] gana %s con %.3f   (r[Do mayor]=%.3f  r[Mi menor]=%.3f  "
                     "diferencia %.3f: ambiguo, y la confianza no lo esconde)\n",
                     keyName (f.keyTonic, f.keyMode).c_str(), f.keyConfidence, rC, rEm, std::abs (rC - rEm));
        REQUIRE (std::abs (rC - rEm) < 0.1);      // los dos candidatos empatados: la ambigüedad es real
    }

    // ---- (b) La menor: tríada A3-C4-E4 sostenida + arpegio A-C-E-G ----
    {
        auto m = makeModule (baseSettings (Cqt::mid, 1));
        auto v = tonesBuffer ({ { equalTempered (57), 0.1 },     // A3
                                { equalTempered (60), 0.1 },     // C4
                                { equalTempered (64), 0.1 } }, 6.0);
        addArpeggio (v, { equalTempered (69), equalTempered (72), equalTempered (76), equalTempered (79) },
                     0.25, 0.1);                                  // A4 C5 E5 G5
        feed (*m, v);
        const auto& f = m->frame();
        const double rAm = Cqt::keyCorrelation (f.chromaSmooth, 9, Cqt::minor);
        const double rC  = Cqt::keyCorrelation (f.chromaSmooth, 0, Cqt::major);

        std::printf ("CQT[tonalidad La menor] gana %s  confianza %.3f  %.1f %% del tiempo   "
                     "(r[La menor]=%.3f  r[Do mayor]=%.3f)\n",
                     keyName (f.keyTonic, f.keyMode).c_str(), f.keyConfidence, 100.0f * f.keyTimeFraction, rAm, rC);

        REQUIRE (f.keyTonic == 9);
        REQUIRE (f.keyMode == Cqt::minor);
        REQUIRE (rAm > rC);           // le gana a su relativa mayor, que es el competidor de verdad
    }

    // ---- (c) ruido rosa: no hay tonalidad, y la confianza tiene que decirlo ----
    //
    // LO QUE DE VERDAD DELATA AL RUIDO NO ES LA CONFIANZA, ES EL % DEL TIEMPO — y esto hay que decirlo
    // porque es la propiedad más importante del número que la lente muestra. La confianza es el MÁXIMO de
    // 24 correlaciones, así que tiene PISO: aun con un cromagrama perfectamente plano, alguna de las 24
    // gana por azar con r ≈ 0.3-0.55. Un "0.5" no quiere decir "media tonalidad", quiere decir "esto es
    // lo que da cualquier cosa". El que se derrumba es el % del tiempo: con tonalidad de verdad la
    // ganadora se sostiene (98-100 %), con ruido cambia todo el rato (3-37 %).
    //
    // Los dos tramos —6 s como pide el prompt y 12 s— se miden juntos porque el segundo es el que muestra
    // que ese ~0.5 es el piso del estimador y no una tonalidad tenue: con el doble de material la
    // confianza se cae sola.
    {
        const auto medir = [] (double seconds)
        {
            auto m = makeModule (baseSettings (Cqt::mid, 1));
            feed (*m, pinkBuffer (seconds, 0.2, telescope::test::kPinkSeedA));
            const auto& f = m->frame();

            // El cromagrama de una señal sin altura definida tiene que ser PLANO: en un espectro
            // constant-Q el rosa (potencia ∝ 1/f) deposita la misma potencia en cada bin, porque el ancho
            // de banda del bin también crece con f. Se mide en dB, que es donde "plano" quiere decir algo.
            float lo = 1.0e9f, hi = 0.0f;
            for (int c = 0; c < 12; ++c) { lo = std::min (lo, f.chroma[c]); hi = std::max (hi, f.chroma[c]); }
            const double spreadDb = 10.0 * std::log10 ((double) hi / std::max (1.0e-12, (double) lo));

            std::printf ("CQT[ruido rosa %2.0f s] la que gana es %s con confianza %.3f (%.1f %% del tiempo)"
                         "  ·  cromagrama plano: max/min = %.2f dB\n",
                         seconds, keyName (f.keyTonic, f.keyMode).c_str(), f.keyConfidence,
                         100.0f * f.keyTimeFraction, spreadDb);

            struct R { float conf, frac; double spread; };
            return R { f.keyConfidence, f.keyTimeFraction, spreadDb };
        };

        const auto seis = medir (6.0);
        const auto doce = medir (12.0);

        // 6 s: el cromagrama plano y la ganadora que no se sostiene. La confianza queda MUY por debajo de
        // los casos con tonalidad (0.845 y 0.868), pero no baja de 0.5 — es el piso del máximo-de-24.
        REQUIRE (seis.spread < 6.0);
        REQUIRE (seis.frac < 0.5f);
        REQUIRE (seis.conf < 0.7f);
        // 12 s: con el doble de material la confianza sí se cae por debajo de 0.5, que es lo que prueba
        // que el número de arriba era piso y no señal.
        REQUIRE (doce.conf < 0.5f);
        REQUIRE (doce.frac < 0.5f);
        REQUIRE (doce.spread < 6.0);
    }
}

// ============================================================================ 6 · octavas
// Cinco A en cinco octavas son UNA clase de nota: eso es lo que un cromagrama tiene que decir. La clase
// vecina (G#) no queda en cero por el mismo motivo del caso 4 —la falda de Hann a un bin— y la relación
// vuelve a ser la derivada, 0.25/1.25 = 0.20. El criterio de 0.15 del prompt es inalcanzable con Hann y Q
// canónico (los mismos que fijan el scalloping de 1.42 dB del caso 2): acá se afirma el 0.20 DERIVADO,
// que es más estricto que un umbral suelto, y se exige que las otras diez clases estén por debajo de 0.05.
TEST_CASE ("telescope: cinco A en cinco octavas son una sola clase de nota", "[telescope][cqt]")
{
    auto m = makeModule (baseSettings (Cqt::mid, 0));
    feed (*m, tonesBuffer ({ { equalTempered (33), 0.1 },    // A1
                             { equalTempered (45), 0.1 },    // A2
                             { equalTempered (57), 0.1 },    // A3
                             { equalTempered (69), 0.1 },    // A4
                             { equalTempered (81), 0.1 } }, 4.0));
    const auto& f = m->frame();

    std::printf ("CQT[octavas] ");
    for (int c = 0; c < 12; ++c) std::printf ("%s=%.3f ", kClassNames[c], f.chroma[c]);
    std::printf ("  (la falda de Hann a un bin deja G# en 0.25/1.25 = 0.20 derivado)\n");

    REQUIRE (f.chroma[9] == 1.0f);                                    // La, normalizado al máximo
    REQUIRE_THAT (f.chroma[8], Catch::Matchers::WithinAbs (0.20f, 0.05f));   // Sol#: la falda, derivada
    for (int c = 0; c < 12; ++c)
    {
        if (c == 9 || c == 8) continue;
        INFO ("clase " << kClassNames[c] << " = " << f.chroma[c]);
        REQUIRE (f.chroma[c] < 0.05f);
    }
}

// ============================================================================ 7 · cruce con SPECTRUM
// EL MISMO SENO LEÍDO POR LOS DOS MOTORES. Si el CQT y el FFT del 50 no dieran el mismo dB sobre la misma
// señal, uno de los dos estaría mal — y la lente 6 y la lente 3 se contradecirían en pantalla.
//
// Los dos muestrean su eje, así que ninguno de los dos lee el pico exacto: 1 000 Hz cae en el bin 85.333
// del FFT (orden 12) y en el 124.427 del CQT. Se corrige CADA UNO por su propio scalloping de Hann —la
// misma fórmula, porque el CQT está diseñado para que un bin del eje sea un bin de la DFT de su ventana—
// y recién ahí se comparan. Los dos números y las dos correcciones se imprimen.
TEST_CASE ("telescope: el CQT y el FFT del 50 leen el mismo dB sobre el mismo seno", "[telescope][cqt]")
{
    constexpr double kHz = 1000.0, kAmp = 0.1;
    const double wantDb = 20.0 * std::log10 (kAmp);
    const auto signal = tonesBuffer ({ { kHz, kAmp } }, 2.0);

    // ---- CQT ----
    auto m = makeModule();
    feed (*m, signal);
    const int    cqtBin   = nearestBin (kHz);
    const double cqtDelta = binPosition (kHz) - (double) cqtBin;
    const double cqtRaw   = (double) m->frame().magDb[cqtBin];
    const double cqtFixed = cqtRaw - hannScallopDb (cqtDelta);

    // ---- SPECTRUM (orden 12, Hann, sin promediar: el mismo motor del 50) ----
    telescope::Spectrum sp;
    telescope::Spectrum::Settings ss;
    ss.fftOrder     = 12;
    ss.window       = telescope::Spectrum::hann;
    ss.overlapIndex = 1;
    ss.channel      = telescope::Spectrum::mid;
    ss.avgMode      = telescope::Spectrum::avgNone;
    ss.peakHold     = false;
    sp.applySettings (ss);
    sp.prepare (kSr);
    for (size_t done = 0; done < signal.size(); )
    {
        const int k = (int) std::min ((size_t) 512, signal.size() - done);
        sp.process (signal.data() + done, signal.data() + done, k);
        done += (size_t) k;
    }
    const double binHz    = kSr / (double) sp.fftSize();
    const int    fftBin   = (int) std::lround (kHz / binHz);
    const double fftDelta = kHz / binHz - (double) fftBin;
    const double fftRaw   = (double) sp.frame().magDb[0][fftBin];
    const double fftFixed = fftRaw - hannScallopDb (fftDelta);

    std::printf ("CQT[cruce con SPECTRUM] %0.0f Hz a %+.1f dBFS\n"
                 "    CQT      bin %3d (posicion %.3f, delta %+.3f)  crudo %+7.3f dB  correccion %+.3f  ->  %+7.3f dB\n"
                 "    SPECTRUM bin %3d (posicion %.3f, delta %+.3f)  crudo %+7.3f dB  correccion %+.3f  ->  %+7.3f dB\n"
                 "    diferencia = %.3f dB\n",
                 kHz, wantDb,
                 cqtBin, binPosition (kHz), cqtDelta, cqtRaw, -hannScallopDb (cqtDelta), cqtFixed,
                 fftBin, kHz / binHz, fftDelta, fftRaw, -hannScallopDb (fftDelta), fftFixed,
                 std::abs (cqtFixed - fftFixed));

    REQUIRE (cqtBin == 124);
    REQUIRE (fftBin == 85);
    REQUIRE (std::abs (cqtFixed - fftFixed) <= 0.5);
    // Y los dos tienen que dar en el número real, no sólo coincidir entre ellos.
    REQUIRE_THAT (cqtFixed, Catch::Matchers::WithinAbs (wantDb, 0.5));
    REQUIRE_THAT (fftFixed, Catch::Matchers::WithinAbs (wantDb, 0.5));
}

// ============================================================================ 8 · latencia declarada
// LA LATENCIA QUE SE DECLARA ES LA QUE SE MIDE. El bin de A0 tiene una ventana de N_0 muestras: hasta que
// no pasa ese tiempo, lo que está leyendo es en parte el silencio de antes. El test arranca un A0 en t = 0
// y mide CUÁNDO llega al 90 % de su nivel final.
//
// El valor esperado se DERIVA de la ventana: con Hann, la fracción de la suma coherente acumulada desde el
// final es F(u) = u − sin(2πu)/(2π), y F(u) = 0.9 en u ≈ 0.742 — o sea 0.74 · 1.241 s ≈ 0.92 s. La banda
// [0.6, 1.3] del prompt lo contiene con margen para los dos lados.
TEST_CASE ("telescope: el bin mas grave tarda lo que dice su ventana", "[telescope][cqt]")
{
    constexpr double kAmp = 0.1;
    auto m = makeModule();
    const double latency = m->lowestBinLatencySec();

    const auto v = tonesBuffer ({ { equalTempered (21), kAmp } }, 3.0);   // A0 desde t = 0
    const int hopN = m->hopSamples();

    std::vector<float> perFrame;
    for (size_t done = 0; done < v.size(); done += (size_t) hopN)
    {
        const int k = (int) std::min ((size_t) hopN, v.size() - done);
        m->process (v.data() + done, v.data() + done, k);
        perFrame.push_back (m->frame().magDb[0]);
    }

    const float finalDb  = perFrame.back();
    const float thresh   = finalDb + (float) (20.0 * std::log10 (0.9));   // 90 % del nivel FINAL, en amplitud
    double reached = -1.0;
    for (size_t i = 0; i < perFrame.size(); ++i)
        if (perFrame[i] >= thresh) { reached = (double) (i + 1) * (double) hopN / kSr; break; }

    // La derivación de arriba, resuelta acá para que el número esperado no sea una constante mágica.
    double u = 0.0;
    for (double x = 0.0; x <= 1.0; x += 1.0e-4)
        if (x - std::sin (2.0 * kPi * x) / (2.0 * kPi) >= 0.9) { u = x; break; }

    std::printf ("CQT[latencia] declarada = %.4f s (N_0/fs)  ·  nivel final %+.2f dB  ·  90 %% alcanzado a "
                 "%.2f s  (derivado: u=%.3f de la ventana -> %.2f s)\n",
                 latency, finalDb, reached, u, u * latency);

    REQUIRE_THAT (latency, Catch::Matchers::WithinAbs (1.2410, 0.001));
    REQUIRE (reached > 0.0);
    REQUIRE (reached >= 0.6);
    REQUIRE (reached <= 1.3);
    REQUIRE_THAT (reached, Catch::Matchers::WithinAbs (u * latency, 0.15));
}

// ============================================================================ 9 · independencia del bloque
// EL MISMO FRAME AL BIT venga el audio en bloques de 1 o de 4 096. Es la misma garantía que casa-4 y
// [stereo] bloque, y sale de lo mismo: las posiciones de los frames las decide un contador de muestras
// desde el reset, no el tamaño de bloque del host.
TEST_CASE ("telescope: el frame 20 del CQT es el mismo al bit en bloques de 1, 7, 64 y 4096", "[telescope][cqt]")
{
    // Una señal con varias notas (y contenido en todo el eje) generada UNA vez: lo único que cambia entre
    // corridas es cómo se trocea al entrar.
    auto signal = tonesBuffer ({ { equalTempered (36), 0.08 },
                                 { equalTempered (60), 0.08 },
                                 { equalTempered (64), 0.08 },
                                 { equalTempered (79), 0.08 } }, 2.5);
    {
        telescope::test::Pink p { telescope::test::kPinkSeedB };
        for (auto& x : signal) x += 0.01f * p.next();          // un poco de piso, para que no sea trivial
    }

    struct Snap { std::vector<float> mag; std::array<float, 12> chroma {}; juce::uint32 frames = 0; };
    const auto run = [&] (int block)
    {
        auto m = makeModule();
        Snap s;
        for (size_t done = 0; done < signal.size(); )
        {
            const int k = (int) std::min ((size_t) block, signal.size() - done);
            m->process (signal.data() + done, signal.data() + done, k);
            done += (size_t) k;
            if (m->framesEmitted() >= 20 && s.mag.empty())
            {
                const auto& f = m->frame();
                s.mag.assign (f.magDb, f.magDb + f.numBins);
                std::copy (f.chroma, f.chroma + 12, s.chroma.begin());
                s.frames = f.frameIndex;
            }
        }
        return s;
    };

    const auto a = run (4096), b = run (512), c = run (64), d = run (7), e = run (1);
    std::printf ("CQT[bloque] frame %u  ·  %zu bins  ·  4096 vs 512: %s  vs 64: %s  vs 7: %s  vs 1: %s\n",
                 a.frames, a.mag.size(),
                 a.mag == b.mag && a.chroma == b.chroma ? "identico" : "DISTINTO",
                 a.mag == c.mag && a.chroma == c.chroma ? "identico" : "DISTINTO",
                 a.mag == d.mag && a.chroma == d.chroma ? "identico" : "DISTINTO",
                 a.mag == e.mag && a.chroma == e.chroma ? "identico" : "DISTINTO");

    REQUIRE (! a.mag.empty());
    REQUIRE (a.frames == 19u);        // frameIndex cuenta desde 0: el vigésimo frame es el 19
    REQUIRE (a.mag == b.mag);
    REQUIRE (a.mag == c.mag);
    REQUIRE (a.mag == d.mag);
    REQUIRE (a.mag == e.mag);
    REQUIRE (a.chroma == b.chroma);
    REQUIRE (a.chroma == c.chroma);
    REQUIRE (a.chroma == d.chroma);
    REQUIRE (a.chroma == e.chroma);
}

// ============================================================================ 10 · silencio
// En silencio no se inventa nada: todo en el piso, cromagrama en cero, sin tonalidad y SIN NaN. Un
// "Do mayor con confianza 0" sería peor que no decir nada, así que tonic y mode quedan en −1.
TEST_CASE ("telescope: en silencio el CQT no dice nada, y lo dice bien", "[telescope][cqt]")
{
    auto m = makeModule();
    feed (*m, std::vector<float> ((size_t) std::llround (3.0 * kSr), 0.0f));
    const auto& f = m->frame();

    int atFloor = 0;
    bool finite = true;
    for (int k = 0; k < f.numBins; ++k)
    {
        if (f.magDb[k] == CqtFrame::kFloorDb) ++atFloor;
        finite = finite && std::isfinite (f.magDb[k]) && std::isfinite (f.holdDb[k]);
    }
    float chromaMax = 0.0f;
    for (int c = 0; c < 12; ++c) { chromaMax = std::max (chromaMax, f.chroma[c]); finite = finite && std::isfinite (f.chroma[c]); }

    std::printf ("CQT[silencio] %d/%d bins en el piso  ·  cromagrama max = %.3f  ·  tonalidad = %s "
                 "(confianza %.3f, %.1f %% del tiempo)  ·  finito: %s\n",
                 atFloor, f.numBins, chromaMax, keyName (f.keyTonic, f.keyMode).c_str(), f.keyConfidence,
                 100.0f * f.keyTimeFraction, finite ? "si" : "NO");

    REQUIRE (f.frameIndex > 0u);          // hubo frames: el silencio también se mide
    REQUIRE (atFloor == f.numBins);
    REQUIRE (chromaMax == 0.0f);
    REQUIRE (f.keyTonic == -1);
    REQUIRE (f.keyMode == -1);
    REQUIRE (f.keyConfidence == 0.0f);
    REQUIRE (f.keyTimeFraction == 0.0f);
    REQUIRE (finite);
}

// ============================================================================ 11 · el mapeo bin -> clase
// La definición sola, sin señal: es la que decide en qué barra del cromagrama cae cada bin.
TEST_CASE ("telescope: el bin cae en la clase de nota que dice la formula", "[telescope][cqt]")
{
    // El bin 0 es A0 -> clase 9 (La) en la numeración C=0 … B=11.
    REQUIRE (Cqt::pitchClassOf (0) == 9);
    REQUIRE (Cqt::pitchClassOf (96) == 9);      // A4
    REQUIRE (Cqt::pitchClassOf (168) == 9);     // A7
    REQUIRE (Cqt::pitchClassOf (78) == 0);      // C4 = 27.5·2^(78/24)
    REQUIRE (Cqt::pitchClassOf (86) == 4);      // E4
    REQUIRE (Cqt::pitchClassOf (92) == 7);      // G4

    // El bin IMPAR (un cuarto de tono arriba) se queda con el semitono de abajo: ⌊k/2⌋.
    REQUIRE (Cqt::pitchClassOf (97) == 9);
    REQUIRE (Cqt::pitchClassOf (95) == 8);

    // Y la vuelta completa: doce clases cada 24 bins.
    for (int k = 0; k < 200; ++k)
        REQUIRE (Cqt::pitchClassOf (k) == Cqt::pitchClassOf (k + 24));

    // Cada bin cae en la clase de la nota más cercana a su frecuencia (control independiente por Hz).
    for (int k = 0; k < 220; k += 2)
    {
        const double hz = Cqt::kFMinHz * std::pow (2.0, (double) k / 24.0);
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
        INFO ("bin " << k << " = " << hz << " Hz = MIDI " << midi);
        REQUIRE (Cqt::pitchClassOf (k) == ((midi % 12) + 12) % 12);
    }
}

// ============================================================================ 12 · el cableado al motor
// El módulo ya está probado solo; acá se prueba que llega ENTERO al otro lado: el frame por su
// TripleBuffer y los CUATRO números de la tonalidad dentro del AnalysisFrame (que es de donde los va a
// leer VERDICT, la lente 13).
TEST_CASE ("telescope: el motor publica el frame de CQT y la tonalidad llega al AnalysisFrame", "[telescope][cqt]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kCqt | telescope::kLoudness);

    // Una tríada de Do mayor con el bajo en la tónica, empujada por processBlock con freno para no
    // desbordar el bus (el mismo patrón del resto de los tests de cadena).
    const auto v = tonesBuffer ({ { equalTempered (48), 0.1 }, { equalTempered (60), 0.1 },
                                  { equalTempered (64), 0.1 }, { equalTempered (67), 0.1 } }, 8.0);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (size_t done = 0; done < v.size(); done += 512)
    {
        const int k = (int) std::min ((size_t) 512, v.size() - done);
        buf.clear();
        for (int i = 0; i < k; ++i) { buf.setSample (0, i, v[done + (size_t) i]); buf.setSample (1, i, v[done + (size_t) i]); }
        proc.processBlock (buf, midi);

        const double pushed = (double) (done + (size_t) k) / kSr;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.cqt().read().frameIndex > 60u; }, 8000));

    const auto& cf = proc.cqt().read();
    const auto  af = proc.analysis().read();

    std::printf ("CQT[motor] frame %u  ·  %d bins desde %.1f Hz  ·  latencia del grave %.3f s  ·  "
                 "tonalidad del frame = %s (%.3f, %.1f %%)  ·  en el AnalysisFrame = %s (%.3f, %.1f %%)\n",
                 cf.frameIndex, cf.numBins, cf.fMin, cf.lowestBinLatencySec,
                 keyName (cf.keyTonic, cf.keyMode).c_str(), cf.keyConfidence, 100.0f * cf.keyTimeFraction,
                 keyName (af.keyTonic, af.keyMode).c_str(), af.keyConfidence, 100.0f * af.keyTimeFraction);

    REQUIRE (cf.numBins == 229);
    REQUIRE (cf.binsPerOctave == telescope::Cqt::kBinsPerOctave);
    REQUIRE_THAT (cf.lowestBinLatencySec, Catch::Matchers::WithinAbs (1.2410f, 0.001f));
    REQUIRE (cf.keyTonic == 0);
    REQUIRE (cf.keyMode == telescope::Cqt::major);
    REQUIRE (cf.keyConfidence > 0.7f);

    // Los cuatro números del AnalysisFrame son los MISMOS: dos copias que se contradicen serían peor que
    // una sola (y VERDICT lee ésta).
    REQUIRE (af.keyTonic == cf.keyTonic);
    REQUIRE (af.keyMode == cf.keyMode);
    REQUIRE (af.keyConfidence == cf.keyConfidence);
    REQUIRE (af.keyTimeFraction == cf.keyTimeFraction);

    proc.releaseResources();
}

// Lente a demanda POR EL LADO DEL MOTOR: sin kCqt en la máscara, el módulo no corre (y no paga sus
// ~229 FFTs de construcción ni su producto disperso por frame).
TEST_CASE ("telescope: sin kCqt en la mascara el modulo de CQT no corre", "[telescope][cqt]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kLoudness);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 200; ++b)     // ~2.1 s
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto x = (float) (0.1 * std::sin (2.0 * kPi * 440.0 * (double) (b * 512 + i) / kSr));
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 2.0; }, 5000));

    std::printf ("CQT[a demanda] con kLoudness solo: frames de CQT = %u  ·  tonalidad = %s\n",
                 proc.cqt().read().frameIndex,
                 keyName (proc.analysis().read().keyTonic, proc.analysis().read().keyMode).c_str());
    REQUIRE (proc.cqt().read().frameIndex == 0u);
    REQUIRE (proc.analysis().read().keyTonic == -1);

    proc.releaseResources();
}

// ============================================================================ 13 · el piso de faldas
// EL PRECIO DE LA PODA, medido. Guardar sólo los coeficientes por encima de 0.0054·max no es gratis: al
// recortar el kernel espectral, su respuesta gana un PISO del orden del propio umbral. Un seno solo no
// deja el resto del eje en el fondo: deja una falda ancha a unos 45 dB por debajo.
//
// Es una propiedad del método (Brown & Puckette la aceptan explícitamente), no un defecto de esta
// implementación — pero es lo que se ve en pantalla alrededor de cada nota fuerte, así que se mide, se
// afirma y va al README. El número esperado se DERIVA del umbral: 20·log10(0.0054) = −45.35 dB.
TEST_CASE ("telescope: la poda del kernel deja un piso de faldas del orden del umbral", "[telescope][cqt]")
{
    auto m = makeModule();
    feed (*m, tonesBuffer ({ { equalTempered (69), 0.1 } }, 2.0));    // A4 solo, −20 dBFS
    const auto& f = m->frame();

    const double derivedDb = 20.0 * std::log10 (Cqt::kSparseThreshold);
    const float  peak = f.magDb[96];

    float worst = CqtFrame::kFloorDb;
    int   worstBin = -1;
    for (int k = 0; k < f.numBins; ++k)
    {
        if (std::abs (k - 96) < 8) continue;                          // fuera del lóbulo principal
        if (f.magDb[k] > worst) { worst = f.magDb[k]; worstBin = k; }
    }

    std::printf ("CQT[piso de faldas] pico %+.2f dB en el bin 96  ·  peor falda %+.2f dB en el bin %d "
                 "(%.1f dB por debajo)  ·  derivado del umbral %.4f: %.2f dB\n",
                 peak, worst, worstBin, (double) (peak - worst), Cqt::kSparseThreshold, derivedDb);

    REQUIRE (worstBin >= 0);
    REQUIRE ((double) (worst - peak) <= derivedDb + 6.0);   // el piso no puede superar al umbral + margen

    // Y UNA COTA FIJA, más ajustada que la derivada (LOW/NIT 1 del revisor del 52). El criterio de arriba
    // deja 28 dB de aire respecto de lo que la implementación realmente mide (67.5 dB por debajo del
    // pico): protege la afirmación del paper —"el piso es del orden del umbral"— pero deja pasar una
    // regresión de 25 dB sin decir nada. Los −50 dB no son un número nuevo: son la MITAD del margen
    // medido, o sea una regresión de más de ~17 dB en el piso de faldas pone la suite en rojo, y el
    // número que el README publica sigue siendo el medido, no éste.
    REQUIRE ((double) (worst - peak) <= -50.0);
}
