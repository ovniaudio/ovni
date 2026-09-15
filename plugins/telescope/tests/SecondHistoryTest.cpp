// [telescope][history] — LA HISTORIA POR SEGUNDO: el archivo y el vivo dan LAS MISMAS FILAS, al bit.
//
// Es la promesa que sostiene el "dónde" de VERDICT. Si las filas del análisis offline no fueran las
// mismas que las del análisis en vivo, "el hueco está entre 0:20 y 0:35" querría decir una cosa mirando
// el archivo y otra escuchándolo, y la lente 13 no podría afirmar ninguna de las dos.
//
// LA SEÑAL, con dos defectos EN POSICIONES CONOCIDAS (60 s, ruido rosa estéreo a ~−14 LUFS):
//   · un HUECO espectral entre 0:20 y 0:35 — se le saca la región de 400 a 1 000 Hz, así que las bandas
//     de ⅓ de octava de 500, 630 y 800 Hz caen muy por debajo de su propio nivel en el resto del tema;
//   · un TRAMO BAJO entre 0:40 y 0:50 — 16 dB menos, o sea alrededor de −30 LUFS.
//
// Lo que se verifica:
//   HISTORY[identidad]  las 60 filas del archivo == las 60 del vivo, IGUALES AL BIT (huella hexadecimal
//                       campo por campo: 30 bandas × 3 + loudness + estéreo + continua + tonalidad).
//   HISTORY[hueco]      las bandas 500/630/800 están ≥ 6 dB por debajo de su media del resto en las filas
//                       20 a 34 y NO en las demás; las bandas lejanas no se mueven.
//   HISTORY[tramo]      el short-term de las filas 40 a 49 está ≥ 10 LU por debajo del resto.
//   HISTORY[bloque]     bloques de 1 / 7 / 64 / 4096 muestras dan las MISMAS filas al bit.
//   HISTORY[determinismo] dos corridas del mismo archivo dan la misma huella, byte por byte.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestDefectSignal.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalyzer.h"
#include "analysis/SecondHistory.h"

using telescope::FileAnalysis;
using telescope::FileAnalyzer;
using telescope::SecondRow;
using telescope::test::Pink;

namespace
{
constexpr double kSr        = telescope::test::kDefectSr;
constexpr double kSeconds   = telescope::test::kDefectSeconds;
constexpr int    kHoleFrom  = telescope::test::kDefectHoleFrom,  kHoleTo  = telescope::test::kDefectHoleTo;
constexpr int    kQuietFrom = telescope::test::kDefectQuietFrom, kQuietTo = telescope::test::kDefectQuietTo;

// La señal con defectos vive en TestDefectSignal.h: la comparten [history] y [verdict].
using telescope::test::makeDefectSignal;

juce::String hexOf (float v)
{
    juce::uint32 bits;
    std::memcpy (&bits, &v, sizeof (bits));
    return juce::String::toHexString ((int) bits);
}

juce::String rowPrint (const SecondRow& r)
{
    return FileAnalysis::secondFingerprint (r, [] (float v) { return hexOf (v); });
}

void pushThrough (telescope::TelescopeProcessor& proc, const juce::AudioBuffer<float>& audio, int block)
{
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int n = audio.getNumSamples();

    for (int done = 0; done < n; done += block)
    {
        const int k = juce::jmin (block, n - done);
        buf.clear();
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, audio, c, done, k);
        buf.setSize (2, k, true, false, true);
        proc.processBlock (buf, midi);
        buf.setSize (2, block, false, false, true);

        const double pushed = (double) (done + k) / kSr;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 60000));
    }
}

// Corre el motor entero y devuelve las filas que quedaron en el ring.
std::vector<SecondRow> liveRows (const juce::AudioBuffer<float>& audio, int block)
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, juce::jmax (block, 32));
    // Exactamente los módulos de los que depende una fila: lo mismo que declara el archivo.
    proc.setEnabledModules (telescope::kLoudness | telescope::kReference | telescope::kCqt);

    pushThrough (proc, audio, block);

    const double want = (double) audio.getNumSamples() / kSr - 0.25;
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= want; }, 120000));
    REQUIRE (telescope::test::waitStable (
        [&] { return (double) proc.analysis().read().secondsAnalysed; }, 300, 60000));

    std::vector<SecondRow> rows;
    proc.secondHistory().copyLatest (rows, telescope::SecondHistory::kCapacity);
    proc.releaseResources();
    return rows;
}
}

TEST_CASE ("telescope: la historia por segundo del archivo es la del vivo, al bit", "[telescope][history]")
{
    const auto signal = makeDefectSignal();
    const auto wav = telescope::test::writeWav ("history_60s.wav", kSr, 2,
                                                (juce::int64) signal.getNumSamples(),
                                                [&] (juce::int64 i)
                                                {
                                                    return std::pair<float, float> {
                                                        signal.getSample (0, (int) i),
                                                        signal.getSample (1, (int) i) };
                                                });
    double srRead = 0.0;
    const auto audio = telescope::test::readWavStereo (wav, srRead);   // los MISMOS float en los dos lados
    REQUIRE (srRead == kSr);

    // ---------- el archivo ----------
    FileAnalyzer fa;
    fa.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 180000));
    const auto off = fa.result();
    REQUIRE (off.ok);
    REQUIRE (off.valid);
    REQUIRE ((int) off.secondRows.size() == (int) kSeconds);

    // ---------- el vivo ----------
    const auto live = liveRows (audio, 512);
    REQUIRE ((int) live.size() == (int) kSeconds);

    // ---------- HISTORY[identidad] ----------
    int same = 0, firstDiff = -1;
    for (int i = 0; i < (int) kSeconds; ++i)
    {
        if (rowPrint (off.secondRows[(size_t) i]) == rowPrint (live[(size_t) i])) ++same;
        else if (firstDiff < 0) firstDiff = i;
    }
    std::printf ("HISTORY[identidad] %d de %d filas IGUALES AL BIT%s\n", same, (int) kSeconds,
                 firstDiff >= 0 ? (juce::String ("   primera distinta: fila ")
                                   + juce::String (firstDiff)).toRawUTF8() : "");
    if (firstDiff >= 0)
    {
        std::printf ("HISTORY[identidad] archivo: %s\n", rowPrint (off.secondRows[(size_t) firstDiff]).toRawUTF8());
        std::printf ("HISTORY[identidad] vivo   : %s\n", rowPrint (live[(size_t) firstDiff]).toRawUTF8());
    }
    REQUIRE (same == (int) kSeconds);
    REQUIRE (live[0].measuredModules == telescope::kSecondRowModules);

    // ---------- HISTORY[hueco] ----------
    // Para cada banda: media en las filas del hueco vs media en las filas SANAS (fuera del hueco y fuera
    // del tramo bajo, que también baja el nivel de todas).
    const auto bandOf = [] (double hz)
    {
        for (int b = 0; b < SecondRow::kNumBands; ++b)
            if (std::abs (telescope::kThirdOctaveHz[b] - hz) < 0.51) return b;
        return -1;
    };
    const auto meanBand = [&] (int band, int from, int to)
    {
        double sum = 0.0; int n = 0;
        for (int i = from; i < to; ++i)
            if (live[(size_t) i].bandMeasured (band)) { sum += live[(size_t) i].bandsDb[band]; ++n; }
        return n > 0 ? sum / (double) n : 0.0;
    };
    // Las filas sanas: 2..19 (se saltean las dos primeras, donde el promedio del segundo todavía arranca)
    // y 52..59.
    const auto meanHealthy = [&] (int band)
    {
        double sum = 0.0; int n = 0;
        for (int i = 2; i < kHoleFrom; ++i)  { sum += live[(size_t) i].bandsDb[band]; ++n; }
        for (int i = 52; i < (int) kSeconds; ++i) { sum += live[(size_t) i].bandsDb[band]; ++n; }
        return sum / (double) n;
    };

    // EL HUECO EN FRECUENCIA, banda por banda. El filtro que lo abre (cuatro biquads en cascada a 400 Hz
    // y a 1 kHz) tiene una TRANSICIÓN ancha —una cascada de cuatro Butterworth de segundo orden corre el
    // punto de −3 dB bastante hacia afuera—, así que las bandas de 200 a 2 kHz también se mueven algo.
    // Eso es física del filtro, no del medidor: lo que el test exige es que las tres bandas del hueco
    // caigan ≥ 6 dB y que FUERA de la transición (≤ 125 Hz y ≥ 3.15 kHz) no se mueva nada.
    std::printf ("HISTORY[hueco] perfil por banda (sano -> en el hueco):\n");
    for (int b = 0; b < SecondRow::kNumBands; ++b)
    {
        const double healthy = meanHealthy (b);
        const double hole    = meanBand (b, kHoleFrom, kHoleTo);
        const double hz = telescope::kThirdOctaveHz[b];
        const bool   inNotch = hz >= 499.0 && hz <= 801.0;
        const bool   farOut  = hz <= 125.1 || hz >= 3149.0;
        if (! live[2].bandMeasured (b)) continue;
        std::printf ("HISTORY[hueco] %6.0f Hz  %+7.2f -> %+7.2f  caida %6.2f dB   %s\n",
                     hz, healthy, hole, healthy - hole,
                     inNotch ? "HUECO (>= 6)" : (farOut ? "lejos (< 1)" : "transicion del filtro"));
        if (inNotch) REQUIRE (healthy - hole >= 6.0);
        if (farOut)  REQUIRE (std::abs (healthy - hole) < 1.0);
    }

    // Y el hueco está SÓLO donde tiene que estar: fila a fila, la banda de 630 Hz cae > 6 dB en 20..34 y
    // en ninguna otra.
    {
        const int b = bandOf (630.0);
        const double healthy = meanHealthy (b);
        int inside = 0, outside = 0;
        for (int i = 2; i < (int) kSeconds; ++i)
        {
            if (i >= kQuietFrom && i < kQuietTo) continue;   // ahí baja TODO, no sólo esta banda
            const bool low = (healthy - live[(size_t) i].bandsDb[b]) >= 6.0;
            if (i >= kHoleFrom && i < kHoleTo) { if (low) ++inside; }
            else if (low) ++outside;
        }
        std::printf ("HISTORY[hueco] 630 Hz por fila: %d de %d filas del hueco caidas  ·  %d fuera del "
                     "hueco (esperado 0)\n", inside, kHoleTo - kHoleFrom, outside);
        REQUIRE (inside == kHoleTo - kHoleFrom);
        REQUIRE (outside == 0);
    }

    // ---------- HISTORY[tramo] ----------
    {
        double loud = 0.0, quiet = 0.0;
        int nl = 0, nq = 0;
        for (int i = 2; i < (int) kSeconds; ++i)
        {
            const float st = live[(size_t) i].shortTermMax;
            if (i >= kQuietFrom + 1 && i < kQuietTo) { quiet += st; ++nq; }
            else if (i < kQuietFrom - 1 || i > kQuietTo + 3) { loud += st; ++nl; }
        }
        loud /= (double) nl;
        quiet /= (double) nq;
        std::printf ("HISTORY[tramo] short-term fuera %+7.2f LUFS  ·  en 0:40-0:50 %+7.2f LUFS  ·  "
                     "caida %6.2f LU (criterio 10)\n", loud, quiet, loud - quiet);
        REQUIRE (loud - quiet >= 10.0);
        REQUIRE (nq >= 8);
    }

    // ---------- HISTORY[determinismo] ----------
    FileAnalyzer fa2;
    fa2.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa2.busy(); }, 180000));
    const auto off2 = fa2.result();
    std::printf ("HISTORY[determinismo] dos corridas del archivo: huella %s  (%d caracteres)\n",
                 off.fingerprint() == off2.fingerprint() ? "IDENTICA" : "DISTINTA",
                 off.fingerprint().length());
    REQUIRE (off.fingerprint() == off2.fingerprint());

    wav.deleteFile();
}

TEST_CASE ("telescope: las filas por segundo no dependen del tamano de bloque", "[telescope][history]")
{
    // Seis segundos alcanzan y sobran: lo que se prueba es que el ARMADO de la fila no mira el bloque.
    // Con bloques de 1 muestra, sesenta segundos serían casi tres millones de llamadas a processBlock.
    constexpr double kShort = 6.0;
    const auto full = makeDefectSignal();
    juce::AudioBuffer<float> audio (2, (int) std::llround (kShort * kSr));
    for (int c = 0; c < 2; ++c) audio.copyFrom (c, 0, full, c, 0, audio.getNumSamples());

    const auto ref = liveRows (audio, 512);
    REQUIRE ((int) ref.size() == (int) kShort);

    for (const int block : { 1, 7, 64, 4096 })
    {
        const auto rows = liveRows (audio, block);
        int same = 0;
        const int n = juce::jmin ((int) rows.size(), (int) ref.size());
        for (int i = 0; i < n; ++i)
            if (rowPrint (rows[(size_t) i]) == rowPrint (ref[(size_t) i])) ++same;

        std::printf ("HISTORY[bloque] %5d -> %d de %d filas iguales AL BIT (referencia: bloque 512)\n",
                     block, same, (int) ref.size());
        REQUIRE ((int) rows.size() == (int) ref.size());
        REQUIRE (same == (int) ref.size());
    }
}
