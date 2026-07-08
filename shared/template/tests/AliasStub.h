#pragma once

// ========================================================================================================
// AliasStub.h — TEST_CASE [alias] PARAMETRIZADO para plugins SIN etapa de pitch (reverb/delay lineal,
// movimiento binaural). Imprime la línea `ALIAS_DBFS=` que tools/measure-check.sh grepea.
//
// ⚠ HONESTIDAD (house-standard §1): el ALIAS por foldback nace en una etapa NO-LINEAL o de PITCH/resampleo
// (es lo que HALO mide aislando su PitchShifter: 4× OS sobre la etapa de pitch). Un REVERB FDN lineal puro
// o un motor BINAURAL HRIR (convolución + ITD + reflexiones) NO transponen frecuencia → "no generan
// armónicos → no necesitan OS" (house-standard, tabla §1). Por eso NO inventamos una etapa de pitch que el
// plugin no tiene. Lo que medimos acá es el PISO DE ESPURIAS de banda completa del plugin REAL: un seno
// puro a −8 dBFS por el processor entero, y la mayor energía que cae FUERA de la fundamental (las espurias
// que mete cualquier no-linealidad residual: el limiter de salida, saturaciones de coef, etc.). En un
// plugin lineal limpio ese piso es muy bajo (la fundamental domina); si una no-linealidad ensucia, salta.
//
// Es DIAGNÓSTICO + número público (el "alias floor" de los 3 números del manifiesto). El gate del
// orquestador (house-standard §2: < −96 dBFS) lo evalúa validate.sh; acá REQUIRE sólo finitud + una cota
// de cordura holgada (el número fino se publica, no se infla). Si el plugin TIENE etapa de pitch/no-lineal,
// NO use este stub: escriba un AliasTest específico que aísle esa etapa con OS, como HALO.
//
// CONTRATO (ver _README.md): define OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG y, opcional,
// OVNI_ALIAS_SETUP(proc) para fijar el peor caso (mix=1 wet pleno + macros de densidad al máximo).
// ========================================================================================================

#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"

#if !defined(OVNI_PLUGIN_PROCESSOR) || !defined(OVNI_PLUGIN_SLUG) || !defined(OVNI_PLUGIN_TAG)
  #error "AliasStub.h requiere OVNI_PLUGIN_PROCESSOR / OVNI_PLUGIN_SLUG / OVNI_PLUGIN_TAG (ver _README.md)"
#endif

#ifndef OVNI_ALIAS_SETUP
  #define OVNI_ALIAS_SETUP(proc) ovni::test::setParam ((proc), "mix", 1.0f)
#endif

namespace ovni::test::alias_detail
{
// Piso de espurias (dBFS) de un seno puro fHz por el processor REAL: mayor energía FUERA de la fundamental,
// relativa a la fundamental. Banda de guarda = todo menos un entorno estrecho de fHz (y su DC). En un
// plugin lineal limpio es muy bajo; cualquier no-linealidad (limiter, clip de coef) mete espurias acá.
template <typename Proc, typename Setup>
inline double spuriaFloorDb (double sr, float fHz, Setup&& setup)
{
    Proc proc;
    setup (proc);
    const int N = 512;
    proc.prepareToPlay (sr, N);

    const float amp = juce::Decibels::decibelsToGain (-8.0f);   // house-standard §2
    constexpr int kFftOrder = 14;            // 16384 bins
    constexpr int kFftSize  = 1 << kFftOrder;

    // Alinear la frecuencia de prueba al CENTRO de un bin de la FFT: si no, la ventana Hann derrama
    // energía de la fundamental a bins vecinos y un bin de "espuria" puede casi igualarla (falso positivo).
    // Con la fundamental sobre un bin exacto, su energía se concentra y la banda de guarda mide lo real.
    fHz = (float) (std::round ((double) fHz * (double) kFftSize / sr) * sr / (double) kFftSize);

    // Capturar la salida (tras warmup de la cola) en un buffer de 16k del canal L.
    std::vector<float> cap (kFftSize, 0.0f);
    int filled = 0, produced = 0; long phase = 0;
    while (filled < kFftSize)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
                d[i] = amp * std::sin (juce::MathConstants<float>::twoPi * (double) fHz * (double) (phase + i) / sr);
        }
        phase += N;
        proc.processBlock (buf, midi);
        ++produced;
        if (produced < 40) continue;   // warmup: la cola difusa/binaural se asienta antes de medir el piso
        const float* a = buf.getReadPointer (0);
        for (int i = 0; i < N && filled < kFftSize; ++i, ++filled)
            cap[(size_t) filled] = a[i];
    }

    // Hann + FFT real.
    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> fd ((size_t) kFftSize * 2, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));
        fd[(size_t) i] = cap[(size_t) i] * win;
    }
    fft.performRealOnlyForwardTransform (fd.data());

    auto binHz = [&] (int k) { return (double) k * sr / (double) kFftSize; };
    auto mag   = [&] (int k) { const float re = fd[(size_t)(2*k)], im = fd[(size_t)(2*k+1)]; return std::sqrt ((double) re*re + (double) im*im); };

    const double tol = 4.0 * sr / (double) kFftSize;   // entorno de ±4 bins de la fundamental = "legítimo"
    double fund = 1e-20, worst = 1e-20;
    for (int k = 1; k < kFftSize/2; ++k)
    {
        const double hz = binHz (k);
        const double m  = mag (k);
        if (std::abs (hz - (double) fHz) <= tol) { fund = std::max (fund, m); continue; }
        if (hz < 20.0) continue;                        // ignorar DC / sub-graves (no es espuria audible)
        worst = std::max (worst, m);
    }
    return 20.0 * std::log10 (worst / fund);
}
} // namespace ovni::test::alias_detail

TEST_CASE ("OVNI " OVNI_PLUGIN_SLUG ": piso de espurias (alias floor, plugin lineal)", "[alias]" OVNI_PLUGIN_TAG)
{
    using namespace ovni::test;
    constexpr double sr = 48000.0;
    // Barrido de senos a lo largo del espectro (graves/medios/agudos: donde una no-linealidad ensucia más).
    const float freqs[] = { 110.f, 440.f, 1000.f, 4000.f, 8000.f, 12000.f };

    auto setup = [] (OVNI_PLUGIN_PROCESSOR& p) { OVNI_ALIAS_SETUP (p); };
    double worst = -300.0;
    for (float f : freqs)
        worst = std::max (worst, alias_detail::spuriaFloorDb<OVNI_PLUGIN_PROCESSOR> (sr, f, setup));

    std::printf ("ALIAS_DBFS=%.2f\n", worst);   // <- el número PÚBLICO que grepea measure-check.sh
    INFO ("spuria floor = " << worst << " dBFS. OJO (house-standard §1): en un plugin SIN etapa de pitch/"
          "saturación el 'alias floor' NO aplica — un motor espacial (reflexiones/binaural) reparte un tono "
          "puro entre bins por diseño LINEAL, así que este número es un PROXY de piso de espurias, NO un "
          "gate de pitch-alias. El gate < −96 dBFS sólo se le exige a plugins con etapa no-lineal (p.ej. HALO).");

    REQUIRE (std::isfinite (worst));
    REQUIRE (worst < 6.0);   // cordura holgada: ningún bin más de 6 dB sobre la fundamental (no explotó). El
                             // número fino se publica; no se gatea en plugins lineales (ver INFO + house §1).
}
