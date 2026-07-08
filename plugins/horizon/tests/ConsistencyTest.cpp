#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [consistency][horizon] — QA checklist §2, las dos independencias medidas con el
// MECANISMO ENCENDIDO (FREEZE on + gate latiendo + DUCK trabajando) y MIX realista
// (lección "condiciones reales"):
//
//  1) BLOCK-SIZE 32->2048: la MISMA señal (ráfagas de pink que mueven el DUCK) por
//     instancias frescas a block 32 / 128 / 2048 debe dar la MISMA salida que la
//     referencia a 512 (±eps de float). FREEZE se activa ANTES de prepare → la
//     captura cae en el MISMO hop global a cualquier block; el detector del DUCK
//     está anclado a las fronteras de hop y el avance del gate es por-frame
//     (gatePhaseInc = rateHz·hop/sr) → nada depende del bloque del host.
//
//  2) SAMPLE-RATE 44.1/48/96 kHz: el PERÍODO DEL GATE en SEGUNDOS no se corre con el
//     SR (la queja clásica de un freeze rítmico: que el latido se acelere al cambiar
//     de DAW/SR). A RATE 4 Hz FREE el gate debe pulsar a 4 Hz REALES en los tres SRs
//     (se cuentan los ciclos de la envolvente del wet). Y la LATENCIA declarada por
//     SR: N = 2048 @44.1/48k, 4096 @96k (mismo tiempo de frame ~43 ms), siempre ==
//     engine.latencySamples().
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = horizon::params::id;

// ── 1) BLOCK-SIZE ────────────────────────────────────────────────────────────
// Señal: ráfagas de pink (0.25 s ON / 0.25 s OFF) → el DUCK trabaja de verdad y el
// freeze se oye en los huecos. FREEZE on + RATE 4 Hz FREE (gate latiendo) + MIX 40.
std::vector<float> renderBlocks (const std::vector<float>& src, int block)
{
    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,  1.0f);    // MECANISMO ENCENDIDO desde el primer bloque
    setParam (proc, pid::WHISPER, 0.0f);    // fase coherente (determinística, sin el rng del whisper interfiriendo el diff)
    setParam (proc, pid::SPREAD,  0.60f);
    setParam (proc, pid::DUCK,    0.60f);
    setParam (proc, pid::RATE,    0.50f);   // 4 Hz FREE (Range 0..8 → norm 0.5) — el gate late
    setParam (proc, pid::MIX,     0.40f);   // MIX realista

    proc.prepareToPlay (48000.0, block);

    std::vector<float> out;
    out.reserve (src.size());
    size_t g = 0;
    while (g < src.size())
    {
        const int n = (int) juce::jmin ((size_t) block, src.size() - g);
        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        for (int i = 0; i < n; ++i)
        {
            const float x = src[g + (size_t) i];
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        const float* L = buf.getReadPointer (0);
        for (int i = 0; i < n; ++i) out.push_back (L[i]);
        g += (size_t) n;
    }
    return out;
}

// ── 2) SAMPLE-RATE ───────────────────────────────────────────────────────────
struct SrProfile
{
    double gateHzMeasured = 0.0;   // frecuencia REAL del latido del gate
    int    latencyReported = 0;
    int    latencyEngine   = 0;
};

// Mide el período del gate leyendo la telemetría del MOTOR (uiGateAmp = la envolvente
// raised-cosine REAL del latido, 0..1 por bloque) — NO el |output|, que arrastra la
// fluctuación intrínseca del frozen-pink y enmascararía el período. FREEZE on + RATE
// 4 Hz FREE + MIX 100 (wet pleno). Se cuentan los ciclos del gate y se dividen por los
// SEGUNDOS de wall-clock → Hz reales, independientes del SR por construcción.
SrProfile profileAtSr (double sr)
{
    SrProfile out;
    const int block = 512;

    horizon::HorizonProcessor proc;
    setParam (proc, pid::FREEZE,   1.0f);
    setParam (proc, pid::RATESYNC, 0.0f);   // FREE (rate en Hz absolutos → SR-independiente por diseño)
    setParam (proc, pid::WHISPER,  0.0f);
    setParam (proc, pid::SPREAD,   0.0f);
    setParam (proc, pid::RATE,     0.50f);  // 4 Hz FREE
    setParam (proc, pid::MIX,      1.0f);
    proc.prepareToPlay (sr, block);
    out.latencyReported = proc.getLatencySamples();
    out.latencyEngine   = proc.engineForTest().latencySamples();

    // 6 s; warmup 2 s (captura + asentar el gate) y medición sobre 4 s del gate.
    const double warmSecs = 2.0, measSecs = 4.0;
    const int warmBlks = (int) std::lround (warmSecs * sr / block);
    const int measBlks = (int) std::lround (measSecs * sr / block);

    Pink pink;
    std::vector<float> gate;   // uiGateAmp por bloque (la envolvente del latido)
    gate.reserve ((size_t) measBlks);
    for (int blk = 0; blk < warmBlks + measBlks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
        for (int i = 0; i < block; ++i)
        {
            const float x = 0.5f * pink.next();
            buf.setSample (0, i, x); buf.setSample (1, i, x);
        }
        proc.processBlock (buf, midi);
        if (blk >= warmBlks) gate.push_back (proc.uiGateAmp.load());
    }

    // Cruces hacia abajo por la media del gate = ciclos. La envolvente raised-cosine es
    // limpia (0..1) → un cruce por ciclo, robusto. Hz = ciclos / segundos medidos.
    double mean = 0.0; for (const float v : gate) mean += v; mean /= (double) gate.size();
    int crossings = 0;
    for (size_t i = 1; i < gate.size(); ++i)
        if (gate[i - 1] >= (float) mean && gate[i] < (float) mean) ++crossings;
    const double measuredSecs = (double) measBlks * (double) block / sr;
    out.gateHzMeasured = (double) crossings / measuredSecs;
    return out;
}
} // namespace

TEST_CASE ("HORIZON block-size 32->2048: misma salida (FREEZE on, gate latiendo, DUCK, MIX 40)", "[consistency][horizon]")
{
    // Señal compartida: 2 s de ráfagas de pink 0.25 s ON / 0.25 s OFF.
    constexpr double SR = 48000.0;
    const int total = (int) (2.0 * SR);
    std::vector<float> src ((size_t) total);
    Pink pink;
    for (int i = 0; i < total; ++i)
    {
        const bool on = ((i / 12000) % 2) == 0;   // 0.25 s @48k
        src[(size_t) i] = on ? 0.7f * pink.next() : 0.0f;
    }

    const std::vector<float> ref = renderBlocks (src, 512);
    double energy = 0.0; for (const float v : ref) energy += (double) v * v;
    REQUIRE (energy > 1e-3);   // hubo salida real (el freeze sonó, no silencio)

    for (const int block : { 32, 128, 2048 })
    {
        const std::vector<float> alt = renderBlocks (src, block);
        const size_t cmp = std::min (ref.size(), alt.size());
        float maxDiff = 0.0f;
        for (size_t i = 0; i < cmp; ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (ref[i] - alt[i]));
        std::printf ("CONSISTENCY[horizon block=%4d vs 512] maxDiff=%.3e (n=%zu)\n", block, maxDiff, cmp);
        REQUIRE (cmp > 0);
        REQUIRE (maxDiff <= 2.0e-3f);   // mismo eps que el stub del sello (float acumulado)
    }
}

TEST_CASE ("HORIZON sample-rate 44.1/48/96: el periodo del gate en SEGUNDOS no se corre", "[consistency][horizon]")
{
    const SrProfile p44 = profileAtSr (44100.0);
    const SrProfile p48 = profileAtSr (48000.0);
    const SrProfile p96 = profileAtSr (96000.0);

    for (const auto* p : { &p44, &p48, &p96 })
        std::printf ("CONSISTENCY[horizon SR] lat=%d gateHz=%.3f\n", p->latencyReported, p->gateHzMeasured);

    // Latencia por SR: N exacto (mismo tiempo de frame) y declarada == la del motor.
    REQUIRE (p44.latencyReported == 2048);
    REQUIRE (p48.latencyReported == 2048);
    REQUIRE (p96.latencyReported == 4096);
    for (const auto* p : { &p44, &p48, &p96 })
        REQUIRE (p->latencyReported == p->latencyEngine);

    // EL LATIDO CORRE EN SEGUNDOS: 4 Hz pedidos = 4 Hz medidos (±0.2) en los tres SRs.
    // Si el período dependiera del SR (bug clásico de un coef cableado en samples), el
    // gate se aceleraría a 96k respecto de 44.1k → este gate lo caza.
    for (const auto* p : { &p44, &p48, &p96 })
        REQUIRE (std::abs (p->gateHzMeasured - 4.0) < 0.2);
    // Y entre SRs el latido coincide entre sí (no sólo cerca del nominal).
    REQUIRE (std::abs (p44.gateHzMeasured - p96.gateHzMeasured) < 0.15);
}
