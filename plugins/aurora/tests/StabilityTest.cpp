#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [stability][aurora] — tortura del AuroraProcessor REAL: automatización BRUTAL
// de TODOS los parámetros (saltos aleatorios full-range cada bloque, incluidos
// SYNC/división/rate) sobre ruido → silencio → impulsos → silencio, a 44.1/48/96k.
// Gates: ni un NaN/Inf, salida SIEMPRE bajo el techo, y la cola muere limpia
// (el STFT no tiene feedback: tras N samples de silencio el wet es CERO — si
// algo suena ahí es un bug de estado, no "carácter").
// =============================================================================
TEST_CASE ("AURORA stability: automatizacion brutal + silencio/ruido/impulso sin NaN ni residuos", "[stability][aurora]")
{
    namespace pid = aurora::params::id;
    const char* autoIds[] = { pid::SPREAD, pid::TILT, pid::MOTION, pid::MONOSAFEAMT,
                              pid::DUCK, pid::MIX, pid::MOTIONSYNC, pid::MOTIONDIV, pid::MOTIONRATE };

    for (const double SR : { 44100.0, 48000.0, 96000.0 })
    {
        DYNAMIC_SECTION ("SR=" << SR)
        {
            aurora::AuroraProcessor proc;
            const int N = 512;
            proc.prepareToPlay (SR, N);

            juce::Random rng (0x40720A);   // semilla fija → reproducible
            ovni::test::White noise;       // ruido determinístico del harness del sello

            float worstPeak = 0.0f;
            float tailPeak  = 0.0f;        // pico en el tramo final de silencio (debe ser ~0)
            bool  allFinite = true;

            const int kBlocks = 600;       // ~6.4 s @48k
            for (int blk = 0; blk < kBlocks; ++blk)
            {
                // Automatización brutal: TODOS los params saltan a valores aleatorios cada bloque.
                for (const char* id : autoIds)
                    if (auto* p = proc.apvts.getParameter (id))
                        p->setValueNotifyingHost (rng.nextFloat());

                juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
                buf.clear();
                if (blk < 200)                          // fase 1: ruido fuerte
                {
                    for (int n = 0; n < N; ++n)
                    {
                        const float x = 0.9f * noise.next();
                        buf.setSample (0, n, x); buf.setSample (1, n, x);
                    }
                }
                else if (blk >= 300 && blk < 304)       // fase 3: impulsos full-scale
                {
                    buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f);
                }
                // fases 2 y 4: silencio (el caso peligroso para estado sucio)

                proc.processBlock (buf, midi);

                float blockPeak = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* d = buf.getReadPointer (ch);
                    for (int n = 0; n < N; ++n)
                    {
                        if (! std::isfinite (d[n])) allFinite = false;
                        blockPeak = juce::jmax (blockPeak, std::abs (d[n]));
                    }
                }
                worstPeak = juce::jmax (worstPeak, blockPeak);
                if (blk >= 500) tailPeak = juce::jmax (tailPeak, blockPeak);   // > 4 s tras el último impulso
            }

            std::printf ("STABILITY[aurora SR=%.0f] worstPeak=%.4f tailPeak=%.6f finite=%d\n",
                         SR, worstPeak, tailPeak, (int) allFinite);

            REQUIRE (allFinite);
            REQUIRE (worstPeak <= 1.0f);     // el limiter contiene aun con saltos brutales
            REQUIRE (tailPeak  <  1.0e-3f);  // sin residuos: el silencio ES silencio (no hay feedback)
        }
    }
}
