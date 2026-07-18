#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"
#include "engine/HaloEngine.h"
#include "template/tests/OvniTestHarness.h"

// =============================================================================
// [honestidad][halo] — BARRIDO FINO de cada macro en 5 posiciones (0/25/50/75/
// 100) sobre el HaloProcessor REAL: cada knob produce el cambio MEDIBLE y en la
// dirección que su nombre promete (regla del sello: "mové el control a ciegas →
// un productor nota el cambio"). HALO no tenía este test (QA catálogo 2026-07-16;
// patrón de aurora/horizon).
//
// Métrica por macro (la firma física de CADA promesa):
//   · DECAY   → la COLA dura más: RMS de la cola en [1..2 s] post-señal sube.
//               (Además imprime el T60 estimado por pendiente → calibración del
//               readout RT60 de la UI.)
//   · SIZE    → el pre-delay crece: el onset del wet llega más tarde.
//   · SHIMMER → nace la capa pitched: energía en la octava+quinta (880/1320 Hz
//               con seno 440) sube. A 0, NO hay octava (reverb a secas).
//   · TONE    → la cola se oscurece: banda alta del wet baja.
//   · ORBIT   → el halo te rodea: energía del vaivén L/R a la frecuencia orbital
//               (SYNC 1 bar @ 120 BPM = 0.5 Hz) sube con la profundidad.
//   · MIX     → más wet: el residuo (out − dry) sube.
// =============================================================================

namespace
{
using namespace ovni::test;
namespace pid = halo::params::id;

constexpr double kSR  = 48000.0;
constexpr int    kBlk = 512;
constexpr float  kPos[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

struct Cfg
{
    float mix = 1.0f, size = 0.5f, decay = 0.5f, shimmer = 0.55f,
          tone = 0.45f, orbit = 0.0f;
    bool  sync = false;   // ORBIT SYNC (1 bar @ 120 BPM → 0.5 Hz)
};

void applyCfg (halo::HaloProcessor& proc, const Cfg& c)
{
    setParam (proc, pid::MIX,     c.mix);
    setParam (proc, pid::SIZE,    c.size);
    setParam (proc, pid::DECAY,   c.decay);
    setParam (proc, pid::SHIMMER, c.shimmer);
    setParam (proc, pid::TONE,    c.tone);
    setParam (proc, pid::ORBIT,   c.orbit);
    setParam (proc, pid::ORBITSYNC, c.sync ? 1.0f : 0.0f);
    setParam (proc, pid::ORBITDIV,  0.0f);    // "1 bar" (índice 0) → 0.5 Hz @ 120 BPM
    setParam (proc, pid::FREEZE,  0.0f);
}

struct PH : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (120.0); p.setIsPlaying (true); p.setPpqPosition (ppq); return p;
    }
};

// Corre `feedBlocks` de pink estéreo-mono y después `tailBlocks` de silencio.
// Devuelve el RMS estéreo por bloque de TODO el recorrido.
std::vector<double> runPinkThenTail (const Cfg& c, int feedBlocks, int tailBlocks)
{
    halo::HaloProcessor proc;
    applyCfg (proc, c);
    proc.prepareToPlay (kSR, kBlk);
    Pink pink;
    std::vector<double> rms;
    rms.reserve ((size_t) (feedBlocks + tailBlocks));
    for (int blk = 0; blk < feedBlocks + tailBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
        buf.clear();
        if (blk < feedBlocks)
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.35f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        double acc = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        { auto* d = buf.getReadPointer (ch); for (int n = 0; n < kBlk; ++n) acc += (double) d[n] * d[n]; }
        rms.push_back (std::sqrt (acc / (2.0 * kBlk)));
    }
    return rms;
}

double windowRms (const std::vector<double>& rms, double fromSec, double toSec)
{
    const int from = (int) (fromSec * kSR / kBlk), to = (int) (toSec * kSR / kBlk);
    double acc = 0.0; int cnt = 0;
    for (int i = from; i < to && i < (int) rms.size(); ++i) { acc += rms[(size_t) i] * rms[(size_t) i]; ++cnt; }
    return std::sqrt (acc / juce::jmax (1, cnt));
}

// Goertzel: energía en una frecuencia puntual de una serie muestreada a fs.
double goertzel (const std::vector<double>& x, double freq, double fs)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / fs;
    const double coef = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (const double v : x) { s0 = v + coef * s1 - s2; s2 = s1; s1 = s0; }
    return s1 * s1 + s2 * s2 - coef * s1 * s2;
}
} // namespace

TEST_CASE ("HALO honestidad: DECAY alarga la cola (RMS tardio monotonico) + T60 impreso", "[honestidad][halo]")
{
    // La cola TEMPRANA la domina el difusor glacial FIJO (kFdnDecay01=0.95 → t60≈9.4 s de diseño):
    // ahí DECAY casi no se ve. La promesa del knob vive en la cola TARDÍA (los repasos del lazo):
    // medimos RMS en [3..4 s] post-señal + el T60 de pendiente [1.5 .. 5.5 s] (calibra el readout RT60).
    const int feed = (int) (1.0 * kSR / kBlk), tail = (int) (7.0 * kSR / kBlk);
    std::vector<double> late, t60s;
    for (const float pos : kPos)
    {
        Cfg c; c.decay = pos; c.shimmer = 0.55f; c.orbit = 0.0f;
        const auto rms = runPinkThenTail (c, feed, tail);
        const double t0   = 1.0;                              // fin de la señal (s)
        const double e1   = windowRms (rms, t0 + 3.0, t0 + 4.0);
        const double eA   = windowRms (rms, t0 + 1.0, t0 + 2.0);
        const double eB   = windowRms (rms, t0 + 5.0, t0 + 6.0);
        const double dDb  = 20.0 * std::log10 (juce::jmax (1e-12, eA) / juce::jmax (1e-12, eB));
        const double t60  = dDb > 0.1 ? 60.0 * 4.0 / dDb : 999.0;   // pendiente entre centros (Δt=4 s)
        std::printf ("HONESTIDAD[HALO DECAY %3.0f] lateRms[3..4s]=%.6f  T60~%.1f s\n", pos * 100, e1, t60);
        late.push_back (e1); t60s.push_back (t60);
    }
    for (size_t i = 1; i < late.size(); ++i)
        REQUIRE (late[i] > late[i - 1]);                      // monótono estricto en la cola tardía
    REQUIRE (late.back() > late.front() * 1.5);               // magnitud útil 0→100
    REQUIRE (t60s.back() > t60s.front());                     // y la cola de verdad DURA más

    // El readout RT60 de la UI (HaloEngine::estimatedRt60Seconds) tiene que seguir la MEDICIÓN
    // (calibración 2026-07-16). Si el motor cambia y la estimación queda vieja, esto se pone rojo.
    for (size_t i = 0; i < t60s.size(); ++i)
    {
        const double est = (double) halo::HaloEngine::estimatedRt60Seconds (kPos[i], 0.55f);
        INFO ("decay=" << kPos[i] << "  medido=" << t60s[i] << " s  readout=" << est << " s");
        REQUIRE (est > t60s[i] * 0.75);
        REQUIRE (est < t60s[i] * 1.25);
    }
}

TEST_CASE ("HALO honestidad: SIZE atrasa el onset del wet (pre-delay real)", "[honestidad][halo]")
{
    int prev = -1;
    for (const float pos : kPos)
    {
        halo::HaloProcessor proc;
        Cfg c; c.size = pos; c.shimmer = 0.0f;
        applyCfg (proc, c);
        proc.prepareToPlay (kSR, kBlk);
        const int total = (int) (2.5 * kSR / kBlk);
        int onset = -1;
        for (int blk = 0; blk < total && onset < 0; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            buf.clear();
            if (blk == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); }
            proc.processBlock (buf, midi);
            auto* d = buf.getReadPointer (0);
            for (int n = 0; n < kBlk; ++n)
                if (! (blk == 0 && n == 0) && std::abs (d[n]) > 1.0e-4f) { onset = blk * kBlk + n; break; }
        }
        std::printf ("HONESTIDAD[HALO SIZE %3.0f] onset=%d samples (%.1f ms)\n", pos * 100, onset, onset * 1000.0 / kSR);
        REQUIRE (onset > 0);
        if (prev >= 0) REQUIRE (onset > prev);                // el pre-delay CRECE con SIZE
        prev = onset;
    }
}

TEST_CASE ("HALO honestidad: SHIMMER sostiene la capa pitched (bloom en la cola tardia)", "[honestidad][halo]")
{
    // Topología post-fix 2026-07-16 (OK Joaquín): el wet de salida hace blend pre/post-pitch por
    // w = min(1, shimmer/0.55) → 0 = reverb A SECAS de verdad; al default y por encima, idéntico
    // a la salida histórica. Medimos la capa pitched en la cola [1..3 s] post-señal (seno 440).
    std::vector<double> energies, hiRms;
    for (const float pos : kPos)
    {
        halo::HaloProcessor proc;
        Cfg c; c.shimmer = pos; c.decay = 0.6f; c.tone = 0.3f;
        applyCfg (proc, c);
        proc.prepareToPlay (kSR, kBlk);
        const int feed = (int) (2.0 * kSR / kBlk), total = (int) (5.0 * kSR / kBlk);
        const int from = (int) (3.0 * kSR / kBlk);            // cola [1..3 s] después del corte (t=2 s)
        std::vector<double> mono; mono.reserve ((size_t) ((total - from) * kBlk));
        long g = 0;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            buf.clear();
            if (blk < feed)
                for (int n = 0; n < kBlk; ++n, ++g)
                {
                    const float x = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) g / kSR);
                    buf.setSample (0, n, x); buf.setSample (1, n, x);
                }
            proc.processBlock (buf, midi);
            if (blk < from) continue;
            auto* L = buf.getReadPointer (0); auto* R = buf.getReadPointer (1);
            for (int n = 0; n < kBlk; ++n) mono.push_back (0.5 * ((double) L[n] + (double) R[n]));
        }
        // DOS métricas (cada una sola miente):
        //  · PARCIALES pitched (Goertzel 880·1320·1760·1980·2640·3520): a 0 tiene que ser ~nada
        //    (reverb a secas); crece 0→75. De 75→100 la cascada granular se DESPARRAMA
        //    espectralmente (blur) → los bins exactos pueden bajar sin que el knob muera…
        //  · …por eso el tramo alto se gatea con la BANDA ANCHA >600 Hz (HP 1 polo), que ahí
        //    SÍ crece (más energía pitched total, más difusa).
        double eOct = 0.0;
        for (const double f : { 880.0, 1320.0, 1760.0, 1980.0, 2640.0, 3520.0 })
            eOct += goertzel (mono, f, kSR);
        const double hiCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 600.0 / kSR);
        double lp = 0.0, acc = 0.0;
        for (const double v : mono) { lp += hiCoef * (v - lp); const double h = v - lp; acc += h * h; }
        const double eHi = std::sqrt (acc / (double) juce::jmax ((size_t) 1, mono.size()));
        std::printf ("HONESTIDAD[HALO SHIMMER %3.0f] E_tail(pitched)=%.4g  hiRms(>600)=%.6f\n",
                     pos * 100, eOct, eHi);
        energies.push_back (eOct);
        hiRms.push_back (eHi);
    }
    for (size_t i = 1; i + 1 < energies.size(); ++i)
        REQUIRE (energies[i] > energies[i - 1]);              // parciales monótonos 0→75
    REQUIRE (energies[3] > energies[0] * 100.0);              // 0 ≈ a secas → magnitud ENORME
    REQUIRE (energies[4] > energies[0] * 100.0);
    REQUIRE (hiRms[4] > hiRms[3]);                            // 75→100: el pitched total sigue subiendo (blur)
}

TEST_CASE ("HALO honestidad: TONE oscurece la cola (banda alta del wet baja)", "[honestidad][halo]")
{
    double prev = -1.0;
    for (const float pos : kPos)
    {
        halo::HaloProcessor proc;
        Cfg c; c.tone = pos; c.shimmer = 0.4f; c.decay = 0.5f;
        applyCfg (proc, c);
        proc.prepareToPlay (kSR, kBlk);
        Pink pink;
        const int total = (int) (6.0 * kSR / kBlk), from = (int) (3.0 * kSR / kBlk);
        const double hiCoef = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 3000.0 / kSR);
        double lp = 0.0, hi = 0.0, tot = 0.0; long cnt = 0;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.3f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            if (blk < from) continue;
            auto* L = buf.getReadPointer (0);
            for (int n = 0; n < kBlk; ++n)
            {
                const double v = (double) L[n];
                lp += hiCoef * (v - lp);
                const double h = v - lp;
                hi += h * h; tot += v * v; ++cnt;
            }
        }
        const double hiShare = std::sqrt (hi / (double) cnt) / juce::jmax (1e-12, std::sqrt (tot / (double) cnt));
        std::printf ("HONESTIDAD[HALO TONE %3.0f] hiShare(>3k)=%.4f\n", pos * 100, hiShare);
        if (prev >= 0.0) REQUIRE (hiShare < prev);            // monótono: más TONE = más oscuro
        prev = hiShare;
    }
}

TEST_CASE ("HALO honestidad: ORBIT profundiza el vaiven L/R a la frecuencia orbital", "[honestidad][halo]")
{
    // SYNC "1 bar" @ 120 BPM → órbita a 0.5 Hz exactos. El balance por-bloque (fs = SR/kBlk)
    // se filtra con Goertzel EN 0.5 Hz: la energía del vaivén tiene que subir con la profundidad.
    std::vector<double> energies;
    for (const float pos : kPos)
    {
        halo::HaloProcessor proc;
        Cfg c; c.orbit = pos; c.shimmer = 0.5f; c.decay = 0.6f; c.sync = true;
        applyCfg (proc, c);
        PH ph;
        proc.setPlayHead (&ph);
        proc.prepareToPlay (kSR, kBlk);
        Pink pink;
        const int total = (int) (24.0 * kSR / kBlk), from = (int) (4.0 * kSR / kBlk);
        std::vector<double> bal; bal.reserve ((size_t) (total - from));
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.3f * pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            ph.ppq += (double) kBlk / kSR * 2.0;              // 120 BPM = 2 beats/s
            if (blk < from) continue;
            double eL = 0, eR = 0;
            auto* L = buf.getReadPointer (0); auto* R = buf.getReadPointer (1);
            for (int n = 0; n < kBlk; ++n) { eL += (double) L[n] * L[n]; eR += (double) R[n] * R[n]; }
            bal.push_back (eL - eR);
        }
        const double e = goertzel (bal, 0.5, kSR / (double) kBlk);
        std::printf ("HONESTIDAD[HALO ORBIT %3.0f] balE@0.5Hz=%.4g\n", pos * 100, e);
        energies.push_back (e);
    }
    // monótono con tolerancia al ruido estocástico del wash + magnitud útil 0→100.
    for (size_t i = 1; i < energies.size(); ++i)
        REQUIRE (energies[i] > energies[i - 1] * 0.9);
    REQUIRE (energies.back() > energies.front() * 10.0);
}

TEST_CASE ("HALO honestidad: MIX sube el wet (residuo out-dry monotonico)", "[honestidad][halo]")
{
    double prev = -1.0;
    for (const float pos : kPos)
    {
        halo::HaloProcessor proc;
        Cfg c; c.mix = pos; c.shimmer = 0.5f; c.decay = 0.5f;
        applyCfg (proc, c);
        proc.prepareToPlay (kSR, kBlk);
        Pink pink;
        const int total = (int) (5.0 * kSR / kBlk), from = (int) (2.0 * kSR / kBlk);
        double acc = 0.0; long cnt = 0;
        for (int blk = 0; blk < total; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            std::vector<float> dry ((size_t) kBlk);
            for (int n = 0; n < kBlk; ++n)
            { const float x = 0.3f * pink.next(); dry[(size_t) n] = x; buf.setSample (0, n, x); buf.setSample (1, n, x); }
            proc.processBlock (buf, midi);
            if (blk < from) continue;
            auto* L = buf.getReadPointer (0);
            for (int n = 0; n < kBlk; ++n)
            { const double r = (double) L[n] - (double) dry[(size_t) n]; acc += r * r; ++cnt; }
        }
        const double resid = std::sqrt (acc / (double) juce::jmax (1L, cnt));
        std::printf ("HONESTIDAD[HALO MIX %3.0f] residuo(out-dry)=%.6f\n", pos * 100, resid);
        if (prev >= 0.0) REQUIRE (resid > prev);              // monótono: más MIX = más wet
        prev = resid;
    }
}
