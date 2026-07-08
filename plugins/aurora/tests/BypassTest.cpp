#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [null][aurora] — BYPASS COMPENSADO (fix del hallazgo HIGH, review 2026-06-10).
//
// AURORA declara latencia N=2048 → el pass-through instantáneo del chasis NO
// vale acá (saltaba ~43 ms en el tiempo al conmutar y des-alineaba el PDC del
// host mientras estaba en bypass). El contrato del bypass de AURORA es:
//
//   (a) en bypass ESTABLE la salida es la entrada RETRASADA getLatencySamples(),
//       BIT-EXACT (el "off" sigue siendo off: no colorea, no clampea — y el PDC
//       del host sigue correcto: "latencia declarada == real" también en bypass);
//   (b) conmutar el power EN VIVO no salta en el tiempo ni clickea: cruce
//       wet↔dry-retrasado con micro-fade (~12 ms) sobre señales ALINEADAS.
//
// El NullStub genérico del chasis ("bypass == entrada SIN retardo") queda para
// los plugins de latencia 0 — acá sería exigir el bug.
// =============================================================================

namespace
{
namespace pid = aurora::params::id;
constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
}

TEST_CASE ("AURORA: bypass = entrada RETRASADA N bit-exact (PDC constante)", "[null][aurora]")
{
    using namespace ovni::test;
    aurora::AuroraProcessor proc;

    // Bypass ON ANTES del prepare: una sesión que carga bypasseada arranca ya asentada
    // (sin fade de entrada) → desde el primer bloque la salida es el dry retrasado.
    setParam (proc, pid::BYPASS, 1.0f);
    proc.prepareToPlay (kSR, kBlk);

    const int lat = proc.getLatencySamples();
    REQUIRE (lat > 0);   // AURORA SIEMPRE tiene latencia OLA real (2048 @48k)

    // Historia de la entrada (mono duplicado) para comparar contra la salida retrasada.
    Pink pink;
    std::vector<float> hist;   // hist[g] = entrada en el sample global g
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
                const int g = base + i - lat;                       // sample de entrada esperado
                const float expected = g >= 0 ? hist[(size_t) g] : 0.0f;   // pre-stream = silencio
                const double d = std::abs ((double) buf.getSample (ch, i) - (double) expected);
                maxDiff = juce::jmax (maxDiff, d);
                ++compared;
            }
    }

    std::printf ("NULL[aurora] bypass-vs-dryRetrasado(N=%d) maxDiff=%.3e over %ld samples\n",
                 lat, maxDiff, compared);
    REQUIRE (inEnergy > 1e-6);    // la señal de prueba no era silencio
    REQUIRE (maxDiff == 0.0);     // GATE: bypass == entrada retrasada N, BIT-EXACT
}

TEST_CASE ("AURORA: conmutar el power en vivo no salta en el tiempo ni clickea", "[null][aurora]")
{
    using namespace ovni::test;
    aurora::AuroraProcessor proc;

    // Carácter bien distinto del dry para que el cruce sea un cruce REAL (no trivial):
    // SPREAD 100 + MIX 100 → por canal el wet difiere fuerte del dry.
    setParam (proc, pid::SPREAD, 1.0f);
    setParam (proc, pid::MIX,    1.0f);
    proc.prepareToPlay (kSR, kBlk);
    const int lat = proc.getLatencySamples();

    // Seno 331 Hz (período NO conmensurado con N=2048): el pass-through instantáneo del
    // chasis viejo daba acá un salto temporal con discontinuidad ~0.45 — este test lo caza.
    constexpr float kAmp = 0.6f, kHz = 331.0f;
    auto sine = [] (long g) { return kAmp * std::sin (juce::MathConstants<float>::twoPi
                                                      * kHz * (float) g / (float) kSR); };

    const int kSettle = 100, kByp = 60, kBack = 100;   // bloques: activo → bypass → activo
    const int kTotal  = kSettle + kByp + kBack;

    float maxJump   = 0.0f;          // peor salto sample-a-sample (continuidad del cruce)
    float prevL     = 0.0f;
    bool  first     = true;
    double maxDiffSettled = -1.0;    // bypass ASENTADO (≥ 50 ms tras conmutar) vs dry retrasado

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

        // Bypass asentado (≥ 6 bloques = 64 ms ≫ fade 12 ms): salida == dry retrasado N.
        if (blk >= kSettle + 6 && blk < kSettle + kByp)
            for (int i = 0; i < kBlk; ++i)
            {
                const long g = (long) blk * kBlk + i - lat;
                maxDiffSettled = juce::jmax (maxDiffSettled,
                    (double) std::abs (L[i] - sine (g)));
            }
    }

    // Cota de continuidad: el seno natural salta ≤ 2π·f/sr·0.85 ≈ 0.037 por sample (techo
    // del sello); el micro-fade agrega ≤ |wet−dry|·paso ≈ 0.003. Margen amplio: 0.08.
    // (El conmutado VIEJO sin compensar medía un salto ~0.45 acá: 5.6× la cota.)
    std::printf ("BYPASS_TOGGLE[aurora] maxJump=%.4f settledMaxDiff=%.3e (fade 12 ms, lat %d)\n",
                 maxJump, maxDiffSettled, lat);
    REQUIRE (maxJump < 0.08f);
    REQUIRE (maxDiffSettled >= 0.0);      // la ventana de bypass asentado se midió de verdad
    REQUIRE (maxDiffSettled == 0.0);      // bypass asentado == dry retrasado, bit-exact
}
