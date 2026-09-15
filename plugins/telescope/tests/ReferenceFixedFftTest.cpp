// [telescope][ref][fixedfft] — LA REFERENCIA MIDE CON SU PROPIA FFT, no con la de la lente SPECTRUM.
//
// El problema (MEDIUM del revisor del 54). Hasta el 54, el lado VIVO de TONAL BALANCE colgaba del
// FrameSink de la instancia de `Spectrum` que corre con los settings que el usuario eligió en la lente 3
// (tamaño de FFT, ventana, solape, canal). El lado ARCHIVO, en cambio, se analiza SIEMPRE con settings
// fijos (orden 12 · Hann · 75 % · L+R). O sea: tocar la FFT del analizador de espectro hacía que las dos
// curvas de la comparación quedaran medidas con dos resoluciones distintas, y nada en pantalla lo decía.
//
// Desde el 55 (1c) el módulo tiene su PROPIA instancia clavada en esos settings fijos. Este test lo prueba
// por el camino de verdad —processBlock → bus → AnalysisThread → refSpectrum → Reference— con la lente
// SPECTRUM puesta en la geometría MÁS distinta que el motor admite:
//
//   REF[fija-igual]   el mismo audio con SPECTRUM en orden 12/Hann/75 %/L+R y en orden 15/BH4/87.5 %/M
//                     da las MISMAS bandas vivas, IGUALES AL BIT.
//   REF[fija-file]    y esas bandas siguen siendo IGUALES AL BIT a las del análisis OFFLINE del mismo
//                     material (la promesa que sostiene FILE[identidad], ahora bajo settings exóticos).
//   REF[fija-nivel]   con la referencia cargada y el programa 6 dB más bajo, el delta sigue dentro de
//                     ±0.7 dB en las bandas comparables — el criterio de REF[nivel], con la FFT movida.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "TestWav.h"
#include "analysis/FileAnalyzer.h"
#include "analysis/modules/Spectrum.h"

using telescope::FileAnalysis;
using telescope::ReferenceFrame;
using telescope::Spectrum;
using telescope::test::Pink;

namespace
{
constexpr double kSr = 48000.0;
constexpr double kSeconds = 10.0;

// La geometría MÁS lejana a la fija que el motor admite: 32 768 puntos, Blackman-Harris de 4 términos,
// 87.5 % de solape y el canal M en vez de L+R. Si la referencia dependiera de la lente, acá se rompe todo.
Spectrum::Settings exoticSettings()
{
    Spectrum::Settings s;
    s.fftOrder     = 15;
    s.window       = Spectrum::blackmanHarris4;
    s.overlapIndex = 2;                 // 87.5 %
    s.channel      = Spectrum::mid;
    return s;
}

void pushBuffer (telescope::TelescopeProcessor& proc, const juce::AudioBuffer<float>& audio, int block)
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
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 30000));
    }
}

// Corre el motor entero con los settings pedidos y devuelve el ReferenceFrame publicado.
ReferenceFrame liveBandsWith (const Spectrum::Settings& st, const juce::AudioBuffer<float>& audio,
                              float gain = 1.0f)
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setSpectrumSettings (st);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);

    if (gain == 1.0f)
    {
        pushBuffer (proc, audio, 512);
    }
    else
    {
        juce::AudioBuffer<float> scaled (2, audio.getNumSamples());
        for (int c = 0; c < 2; ++c) scaled.copyFrom (c, 0, audio, c, 0, audio.getNumSamples());
        scaled.applyGain (gain);
        pushBuffer (proc, scaled, 512);
    }

    REQUIRE (telescope::test::waitUntil (
        [&] { return proc.analysis().read().timeSeconds >= kSeconds - 0.25; }, 30000));
    REQUIRE (telescope::test::waitUntil ([&] { return proc.reference().read().liveValid; }, 8000));
    // Y ADEMÁS que el worker haya drenado TODO: el promedio infinito sigue creciendo mientras queden
    // muestras en el bus, y comparar dos corridas que promediaron distinta cantidad de audio no probaría
    // nada sobre la geometría (es lo que hacía fallar este test con la suite entera al lado).
    REQUIRE (telescope::test::waitStable ([&] { return (double) proc.reference().read().liveSeconds; },
                                          250, 30000));

    const auto f = proc.reference().read();
    proc.releaseResources();
    return f;
}
}

TEST_CASE ("telescope: la referencia mide con su propia FFT y no con la de la lente SPECTRUM",
           "[telescope][ref][fixedfft]")
{
    // El mismo audio para todos los caminos: rosa estéreo a ~-14 LUFS, escrito además a WAV para el
    // análisis offline.
    const float peak = std::pow (10.0f, -2.4f / 20.0f);
    Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    const auto gen = [&] (juce::int64) { return std::pair<float, float> { peak * a.next(), peak * b.next() }; };

    const auto wav = telescope::test::writeWav ("ref_fixed_fft.wav", kSr, 2,
                                                (juce::int64) std::llround (kSeconds * kSr), gen);
    double srRead = 0.0;
    const auto audio = telescope::test::readWavStereo (wav, srRead);
    REQUIRE (srRead == kSr);

    // ---- 1 · dos geometrías de SPECTRUM, las MISMAS bandas vivas ----
    const auto def = liveBandsWith (Spectrum::Settings{}, audio);
    const auto exo = liveBandsWith (exoticSettings(),     audio);

    int measured = 0, same = 0;
    float worst = 0.0f;
    for (int i = 0; i < ReferenceFrame::kNumBands; ++i)
    {
        if (def.liveBands[i] <= telescope::SpectrumFrame::kFloorDb) continue;
        ++measured;
        if (def.liveBands[i] == exo.liveBands[i]) ++same;
        worst = juce::jmax (worst, std::abs (def.liveBands[i] - exo.liveBands[i]));
    }
    std::printf ("REF[fija-igual] SPECTRUM orden 12/Hann/75 %%/L+R vs orden 15/BH4/87.5 %%/M  ->  %d de %d "
                 "bandas iguales AL BIT (peor diferencia %.9f dB)  ·  %.6f s vs %.6f s promediados\n",
                 same, measured, worst, def.liveSeconds, exo.liveSeconds);
    REQUIRE (def.liveSeconds == exo.liveSeconds);   // si no promediaron lo mismo, comparar no diría nada
    REQUIRE (measured >= 25);
    REQUIRE (same == measured);
    REQUIRE (def.liveIntegrated == exo.liveIntegrated);

    // ---- 2 · y siguen siendo las del análisis OFFLINE, al bit ----
    telescope::FileAnalyzer fa;
    fa.start (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! fa.busy(); }, 60000));
    const auto off = fa.result();
    REQUIRE (off.ok);
    REQUIRE (off.valid);

    int measuredOff = 0, sameOff = 0;
    float worstOff = 0.0f;
    for (int i = 0; i < FileAnalysis::kNumBands; ++i)
    {
        if (off.bandsDb[i] <= telescope::SpectrumFrame::kFloorDb) continue;
        ++measuredOff;
        if (off.bandsDb[i] == exo.liveBands[i]) ++sameOff;
        worstOff = juce::jmax (worstOff, std::abs (off.bandsDb[i] - exo.liveBands[i]));
    }
    std::printf ("REF[fija-file]  OFFLINE vs VIVO con la lente en orden 15/BH4/87.5 %%/M  ->  %d de %d "
                 "bandas iguales AL BIT (peor diferencia %.9f dB)  ·  I offline %+.6f, vivo %+.6f\n",
                 sameOff, measuredOff, worstOff, off.integratedLufs, exo.liveIntegrated);
    REQUIRE (measuredOff >= 25);
    REQUIRE (sameOff == measuredOff);
    REQUIRE (off.integratedLufs == exo.liveIntegrated);

    // ---- 3 · el criterio de REF[nivel] con la FFT de la lente movida: mismo material 6 dB más bajo ----
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (kSr, 512);
    proc.setSpectrumSettings (exoticSettings());
    proc.setEnabledModules (telescope::kSpectrum | telescope::kReference | telescope::kLoudness);

    proc.loadReference (wav);
    REQUIRE (telescope::test::waitUntil ([&] { return ! proc.referenceBusy(); }, 60000));

    // El ReferenceFrame se publica UNA VEZ POR HOP: sin audio corriendo no hay hop, así que `refValid`
    // recién se puede leer después de empujar. (No es un defecto: un frame que se refresca solo sin que
    // entre audio sería un frame que no mide nada.)
    juce::AudioBuffer<float> quieter (2, audio.getNumSamples());
    for (int c = 0; c < 2; ++c) quieter.copyFrom (c, 0, audio, c, 0, audio.getNumSamples());
    quieter.applyGain (std::pow (10.0f, -6.0f / 20.0f));
    pushBuffer (proc, quieter, 512);

    REQUIRE (telescope::test::waitUntil (
        [&] { return proc.analysis().read().timeSeconds >= kSeconds - 0.25; }, 30000));
    REQUIRE (telescope::test::waitUntil ([&] { const auto& f = proc.reference().read();
                                                 return f.liveValid && f.refValid; }, 8000));
    REQUIRE (telescope::test::waitStable ([&] { return (double) proc.reference().read().liveSeconds; },
                                          250, 30000));

    const auto cmp = proc.reference().read();
    int comparable = 0;
    float worstDelta = 0.0f;
    double worstHz = 0.0;
    for (int i = 0; i < ReferenceFrame::kNumBands; ++i)
    {
        if (! cmp.bandValid[i]) continue;
        ++comparable;
        if (std::abs (cmp.deltaDb[i]) > worstDelta)
        {
            worstDelta = std::abs (cmp.deltaDb[i]);
            worstHz    = telescope::kThirdOctaveHz[i];
        }
    }
    std::printf ("REF[fija-nivel] programa 6 dB mas bajo que la referencia (vivo I=%+.3f, ref I=%+.3f)  ->  "
                 "%d bandas comparables  ·  peor delta %.3f dB a %.0f Hz (criterio 0.7)\n",
                 cmp.liveIntegrated, cmp.refIntegrated, comparable, worstDelta, worstHz);
    REQUIRE (comparable >= 25);
    REQUIRE (worstDelta <= 0.7f);

    proc.releaseResources();
    wav.deleteFile();
}
