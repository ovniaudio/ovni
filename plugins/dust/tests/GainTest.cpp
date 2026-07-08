// GainTest.cpp — [gain][dust]: anti-clip del DustProcessor REAL en el PEOR caso conocido.
//
// El peor caso de PEAK de DUST (cazado en la etapa motor): un tono SOSTENIDO cuyo período divide
// EXACTO al RATE apila los 24 taps EN FASE (+13 dB de coherencia) y clava el limiter (~17 s a
// DENSIDAD 100). Acá lo reproducimos adrede: 200 Hz (período 240 samples @48k) con RATE 100 ms
// (4800 samples = 20 períodos justos) + todas las macros al máximo (DENSIDAD 100 = feedback en el
// piso de estabilidad, SPREAD 100, VIDA 100, MIX 100 = wet pleno). La salida DEBE quedar bajo el
// techo del limiter estéreo-linked (0.85, margen true-peak del sello).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

TEST_CASE ("DUST gain staging: tono coherente con la grilla + macros al maximo -> no clip",
           "[gain][dust]")
{
    dust::DustProcessor proc;
    const double SR = 48000.0; const int N = 512;

    namespace pid = dust::params::id;
    // Peor caso de clip: wet pleno, nube infinita, campo entero, deriva máxima.
    for (const char* id : { pid::MIX, pid::DENSITY, pid::SPREAD, pid::VIDA })
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (1.0f);

    // RATE = 100 ms: 4800 samples = 20 períodos EXACTOS del tono de 200 Hz -> apilamiento coherente.
    if (auto* r = proc.apvts.getParameter (pid::RATE))
        r->setValueNotifyingHost (proc.apvts.getParameterRange (pid::RATE).convertTo0to1 (100.0f));

    proc.prepareToPlay (SR, N);

    // ~26 s: cubre con margen el clavado del limiter (~17 s medidos en la etapa motor).
    float peak = 0.0f;
    bool  finite = true;
    const int blocks = (int) std::lround (26.0 * SR / N);
    for (int blk = 0; blk < blocks && finite; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int n = 0; n < N; ++n)
                d[n] = std::sin (juce::MathConstants<float>::twoPi * 200.0f * (float) (blk * N + n) / (float) SR);
        }
        proc.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, N), buf.getMagnitude (1, N));
        for (int ch = 0; ch < 2 && finite; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int n = 0; n < N; ++n)
                if (! std::isfinite (d[n])) { finite = false; break; }
        }
    }

    std::printf ("PEAK=%.6f\n", peak);   // <- la línea que grepea gain-staging-check.sh (techo 0.85)
    REQUIRE (finite);
    REQUIRE (peak <= 0.851f);            // limiter 0.85 estéreo-linked (attack instantáneo) + redondeo float
    REQUIRE (peak > 0.1f);               // el mecanismo está ENCENDIDO (no un falso verde en silencio)
}
