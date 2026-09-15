// [telescope][ebu][chain] — EL CABLEADO de los campos append-only del 57c, probado por el CAMINO REAL.
//
// Por qué existe: la mutación de la auditora sobre el 57c comentó la línea
//     f.shortTermPartial = r.shortTermPartial;
// de AnalysisThread::processHop() y `[ebu]`, `[visual]` y `[smoke]` siguieron VERDES. EBU[parcial] y
// TP[canal] prueban el MÓDULO (Loudness) a solas, y la captura `loudness_parcial` sólo imprime: las once
// líneas de AnalysisThread.cpp —la única desviación de alcance del 57c— no tenían un test con dientes.
// Este lo es: empuja audio por `TelescopeProcessor` y lee el `AnalysisFrame` publicado, que es lo que la
// lente lee. Si alguien borra una de las seis copias, el campo vale su default (−300) y esto se pone rojo.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "TestHelpers.h"

namespace
{
constexpr double kSr = 48000.0;
constexpr float  kNoReading = -100.0f;   // por debajo de esto el frame dice "sin medición" (kSilenceDb = −300)

// Empuja `seconds` de seno de 1 kHz a `dbfs` (pico) por el processor: L siempre, R sólo si `stereo`.
void pushSine (telescope::TelescopeProcessor& proc, double seconds, double dbfs, bool stereo)
{
    const auto amp = (float) std::pow (10.0, dbfs / 20.0);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const auto blocks = (int) std::ceil (seconds * kSr / 512.0);
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = (float) (amp * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) n / kSr));
            buf.setSample (0, i, v);
            buf.setSample (1, i, stereo ? v : 0.0f);
        }
        proc.processBlock (buf, midi);
    }
}
}

TEST_CASE ("telescope: los parciales y el true-peak por canal LLEGAN al AnalysisFrame (cableado del 57c)",
           "[telescope][ebu][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kLoudness);

    // 1.5 s: el momentary oficial ya existe (400 ms), el short-term oficial NO (3 s) — y su parcial sí.
    pushSine (proc, 1.5, -20.0, true);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 1.4; }, 8000));
    const auto f = proc.analysis().read();

    std::printf ("FRAME[cableado] mono -20 dBFS a 1.5 s  ·  momentary oficial %+.2f parcial %+.4f  ·  "
                 "short-term oficial %+.1f parcial %+.4f  ·  TP hop L %+.4f R %+.4f  ·  TP max L %+.4f R %+.4f "
                 "(conjunto %+.4f)\n",
                 f.loudness.momentary, f.momentaryPartial, f.loudness.shortTerm, f.shortTermPartial,
                 f.truePeakHopL, f.truePeakHopR, f.truePeakMaxL, f.truePeakMaxR, f.loudness.truePeakMax);

    // Los parciales llegan y valen lo que el medidor: un seno a −20 dBFS pico en L+R mide −20.0 LUFS.
    CHECK (f.loudness.shortTerm < kNoReading);          // el oficial todavía no existe…
    CHECK (f.shortTermPartial  > kNoReading);           // …y el parcial ya está cableado
    CHECK (std::abs (f.shortTermPartial + 20.0f) < 0.1f);
    CHECK (std::abs (f.momentaryPartial + 20.0f) < 0.1f);
    CHECK (f.loudness.momentary > kNoReading);
    CHECK (std::abs (f.momentaryPartial - f.loudness.momentary) < 1.0e-4f);   // con la ventana llena, el mismo número

    // El true-peak por canal llega y, con mono, los dos canales valen lo mismo que el conjunto.
    CHECK (std::abs (f.truePeakHopL + 20.0f) < 0.1f);
    CHECK (std::abs (f.truePeakHopR + 20.0f) < 0.1f);
    CHECK (f.truePeakMaxL == f.loudness.truePeakMax);
    CHECK (f.truePeakMaxR == f.loudness.truePeakMax);

    proc.releaseResources();
}

TEST_CASE ("telescope: con señal sólo en L, el frame dice R en silencio (cableado por canal del 57c)",
           "[telescope][ebu][chain]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setEnabledModules (telescope::kLoudness);

    pushSine (proc, 1.0, -6.0, false);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 0.9; }, 8000));
    const auto f = proc.analysis().read();

    std::printf ("FRAME[cableado] solo L -6 dBFS a 1.0 s  ·  TP hop L %+.4f R %+.1f  ·  TP max L %+.4f R %+.1f\n",
                 f.truePeakHopL, f.truePeakHopR, f.truePeakMaxL, f.truePeakMaxR);

    CHECK (std::abs (f.truePeakHopL + 6.0f) < 0.1f);
    CHECK (f.truePeakHopR < kNoReading);                // el canal en silencio NO hereda el pico del otro
    CHECK (std::abs (f.truePeakMaxL + 6.0f) < 0.1f);
    CHECK (f.truePeakMaxR < kNoReading);

    proc.releaseResources();
}
