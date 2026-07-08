// MeasureTest.cpp — [measure][dust]: imagen estéreo del DustProcessor REAL (CORR/WIDTH/BAL/MONOSUM).
//
// Lo que se prueba (con el MECANISMO ENCENDIDO: banco HRIR + feedback reales, MIX 100 para medir el
// campo wet sin que el dry lo tape):
//   · SPREAD 0 -> 100: el campo se ABRE de verdad (la CORR BAJA — el HRIR filtra L/R distinto por
//     dirección; con todo apilado al frente quedan correlacionados).
//   · IN PHASE (monoSafe del chasis): el motor cae a paneo de potencia constante -> vuelve
//     mono-compatible (MONOSUM ≈ 0, CORR alta) aun con SPREAD 100.
// Imprime MEASURE[...] por caso + IACC= UNA vez (el caso canónico SPREAD 100) para measure-check.sh.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    // Ruido rosa mono por el processor real con SPREAD/IN PHASE dados; mide la imagen tras warmup.
    ovni::test::StereoImage measureImage (float spread01, bool inPhase)
    {
        using namespace ovni::test;
        namespace pid = dust::params::id;

        dust::DustProcessor proc;
        const double SR = 48000.0; const int N = 512;

        setParam (proc, pid::MIX, 1.0f);          // wet pleno: medimos el CAMPO de burbujas
        setParam (proc, pid::DENSITY, 0.5f);
        setParam (proc, pid::SPREAD, spread01);
        setParam (proc, pid::VIDA, 0.0f);         // sin deriva: la imagen es la de la distribución
        setParam (proc, "monoSafe", inPhase ? 1.0f : 0.0f);
        proc.prepareToPlay (SR, N);

        Pink pink;
        StereoImageMeter meter;
        const int warm = 150, meas = 500;         // ~1.6 s de warmup (nube asentada) + ~5.3 s de medición
        for (int blk = 0; blk < warm + meas; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N);
            juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i)
            {
                const float x = pink.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            proc.processBlock (buf, midi);
            if (blk >= warm)
                meter.addBlock (buf.getReadPointer (0), buf.getReadPointer (1), N);
        }
        return meter.finish();
    }
}

TEST_CASE ("DUST measure: SPREAD abre el campo (CORR baja) y IN PHASE lo vuelve mono-compatible",
           "[measure][dust]")
{
    const auto s0   = measureImage (0.0f, false);   // todo apilado en el ORIGIN (frente)
    const auto s100 = measureImage (1.0f, false);   // el campo entero (caso canónico)
    const auto sIP  = measureImage (1.0f, true);    // IN PHASE: paneo de potencia constante

    std::printf ("MEASURE[dust SPREAD=0  ] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 s0.corr, s0.width, s0.balDb, s0.monoSumDb);
    std::printf ("MEASURE[dust SPREAD=100] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 s100.corr, s100.width, s100.balDb, s100.monoSumDb);
    std::printf ("MEASURE[dust IN PHASE  ] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 sIP.corr, sIP.width, sIP.balDb, sIP.monoSumDb);
    std::printf ("IACC=%.3f\n", s100.corr);   // el caso canónico (una sola vez; measure-check.sh toma el último)

    // Cordura: el mecanismo sonó de verdad en los tres casos.
    REQUIRE (std::isfinite (s0.corr));
    REQUIRE (std::isfinite (s100.corr));
    REQUIRE (std::isfinite (sIP.corr));
    REQUIRE (s0.rms   > 1.0e-5);
    REQUIRE (s100.rms > 1.0e-5);
    REQUIRE (sIP.rms  > 1.0e-5);

    // SPREAD 0: punto al frente -> imagen correlacionada (HRIR frontal ~simétrica), centrada,
    // sin side y mono-compatible. Un SPREAD roto que esparce igual daría width > 0 acá.
    REQUIRE (s0.corr > 0.95);
    REQUIRE (s0.width < 0.05);
    REQUIRE (std::abs (s0.balDb) < 2.0);
    REQUIRE (s0.monoSumDb > -2.0);

    // SPREAD 100: el campo se ABRE de verdad — la CORR BAJA y aparece side real. MEDIDO con
    // min-phase 128 taps + ITD por bus (Woodworth, prescripción de curaduría — fix de review):
    // CORR 1.00 -> 0.56 y WIDTH 0.00 -> 0.54 (antes, sin ITD: 0.85 / 0.34 — sólo ILD espectral;
    // el ITD aporta la otra mitad del cue binaural). Un SPREAD muerto daría CORR≈1/WIDTH≈0 acá.
    REQUIRE (s100.corr < s0.corr - 0.30);
    REQUIRE (s100.corr < 0.70);
    REQUIRE (s100.width > 0.40);
    // BAL: el signo del offset angular ahora CONTRAPESA el momento lateral del campo (fix del
    // sesgo de semilla fija que clavaba −3.4 dB): medido +1.2 dB. Umbral apretado de 6 -> 3 dB.
    REQUIRE (std::abs (s100.balDb) < 3.0);

    // IN PHASE con SPREAD 100: paneo de potencia constante POR TAP (bypass del banco HRIR+ITD) ->
    // mono-SAFE de verdad: ganancias positivas sin delays interaurales = CERO cancelación de fase
    // al monoficar. OJO con la métrica: MONOSUM normaliza por 2·rmsL — un campo de ecos discretos
    // esparcido por TODO el círculo "pierde" hasta ~3 dB por LEY DE PAN (un tap hard-pan suma como
    // x, no como 2x), sin cancelar nada. Medido: −2.2 dB (dentro de la cota de pan), CORR +0.46
    // (positiva: nada anti-fase). El valor previo "≈0" estaba favorecido por el sesgo lateral del
    // campo (rmsL chico inflaba el ratio) — umbral re-calibrado honesto.
    REQUIRE (sIP.monoSumDb > -3.2);            // dentro de la ley de pan: sin cancelación de fase
    REQUIRE (sIP.monoSumDb < 1.0);
    REQUIRE (sIP.corr > 0.2);                  // correlación POSITIVA: nada fuera de fase
    REQUIRE (sIP.width > 0.1);                 // sigue habiendo campo (paneo real, no colapso a mono duro)
}
