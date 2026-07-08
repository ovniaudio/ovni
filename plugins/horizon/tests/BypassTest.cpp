#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [bypass][horizon] — BYPASS COMPENSADO (HORIZON es latente como AURORA, N=2048).
// El pass-through instantáneo del chasis NO vale acá (saltaría ~43 ms y des-alinearía
// el PDC del host). El contrato del bypass de HORIZON es:
//
//   (a) en bypass ESTABLE la salida es la entrada RETRASADA getLatencySamples(),
//       BIT-EXACT (PDC del host correcto: "latencia declarada == real" en bypass);
//   (b) conmutar el power EN VIVO no salta en el tiempo ni clickea: cruce
//       wet↔dry-retrasado con micro-fade (~12 ms) sobre señales ALINEADAS.
// =============================================================================

namespace
{
namespace pid = horizon::params::id;
constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
}

TEST_CASE ("HORIZON: bypass = entrada RETRASADA N bit-exact (PDC constante)", "[bypass][horizon]")
{
    using namespace ovni::test;
    horizon::HorizonProcessor proc;

    // Bypass ON ANTES del prepare: una sesión que carga bypasseada arranca ya asentada.
    setParam (proc, pid::BYPASS, 1.0f);
    proc.prepareToPlay (kSR, kBlk);

    const int lat = proc.getLatencySamples();
    REQUIRE (lat > 0);   // HORIZON SIEMPRE tiene latencia OLA real (2048 @48k)

    Pink pink;
    std::vector<float> hist;
    hist.reserve (50 * kBlk);

    double maxDiff = 0.0; long compared = 0; double inEnergy = 0.0;
    for (int blk = 0; blk < 50; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            const float x = pink.next();
            buf.setSample (0, i, x); buf.setSample (1, i, x);
            hist.push_back (x);
            inEnergy += (double) x * x;
        }
        proc.processBlock (buf, midi);

        const int base = blk * kBlk;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlk; ++i)
            {
                const int g = base + i - lat;
                const float expected = g >= 0 ? hist[(size_t) g] : 0.0f;
                const double d = std::abs ((double) buf.getSample (ch, i) - (double) expected);
                maxDiff = juce::jmax (maxDiff, d);
                ++compared;
            }
    }

    std::printf ("BYPASS[horizon] bypass-vs-dryRetrasado(N=%d) maxDiff=%.3e over %ld samples\n",
                 lat, maxDiff, compared);
    REQUIRE (inEnergy > 1e-6);    // la señal de prueba no era silencio
    REQUIRE (maxDiff == 0.0);     // GATE: bypass == entrada retrasada N, BIT-EXACT
}

TEST_CASE ("HORIZON: conmutar el power en vivo no salta en el tiempo ni clickea", "[bypass][horizon]")
{
    using namespace ovni::test;
    horizon::HorizonProcessor proc;

    // Carácter distinto del dry: FREEZE on + SPREAD 100 + MIX 100 → el wet difiere fuerte.
    setParam (proc, pid::FREEZE, 1.0f);
    setParam (proc, pid::SPREAD, 1.0f);
    setParam (proc, pid::MIX,    1.0f);
    proc.prepareToPlay (kSR, kBlk);
    const int lat = proc.getLatencySamples();

    constexpr float kAmp = 0.6f, kHz = 331.0f;
    auto sine = [] (long g) { return kAmp * std::sin (juce::MathConstants<float>::twoPi
                                                      * kHz * (float) g / (float) kSR); };

    const int kSettle = 100, kByp = 60, kBack = 100;
    const int kTotal  = kSettle + kByp + kBack;

    float maxJump   = 0.0f;
    float prevL     = 0.0f;
    bool  first     = true;
    double maxDiffSettled = -1.0;

    for (int blk = 0; blk < kTotal; ++blk)
    {
        if (blk == kSettle)          setParam (proc, pid::BYPASS, 1.0f);   // power OFF en vivo
        if (blk == kSettle + kByp)   setParam (proc, pid::BYPASS, 0.0f);   // power ON de nuevo

        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        for (int i = 0; i < kBlk; ++i)
        {
            const float x = sine ((long) blk * kBlk + i);
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);

        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < kBlk; ++i)
        {
            if (! first) maxJump = juce::jmax (maxJump, std::abs (L[i] - prevL));
            prevL = L[i]; first = false;
        }

        // Bypass asentado (≥ 6 bloques ≫ fade 12 ms): salida == dry retrasado N.
        if (blk >= kSettle + 6 && blk < kSettle + kByp)
            for (int i = 0; i < kBlk; ++i)
            {
                const long g = (long) blk * kBlk + i - lat;
                maxDiffSettled = juce::jmax (maxDiffSettled,
                    (double) std::abs (L[i] - sine (g)));
            }
    }

    std::printf ("BYPASS_TOGGLE[horizon] maxJump=%.4f settledMaxDiff=%.3e (fade 12 ms, lat %d)\n",
                 maxJump, maxDiffSettled, lat);
    REQUIRE (maxJump < 0.08f);            // el seno natural salta ≤ 0.037/sample; margen amplio
    REQUIRE (maxDiffSettled >= 0.0);      // la ventana de bypass asentado se midió de verdad
    REQUIRE (maxDiffSettled == 0.0);      // bypass asentado == dry retrasado, bit-exact
}
