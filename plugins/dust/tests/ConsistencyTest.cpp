// ConsistencyTest.cpp — [consistency][dust]: independencia de SAMPLE-RATE y de BLOCK-SIZE del
// DustProcessor REAL (qa-listening-checklist §2), con el MECANISMO ENCENDIDO y MIX realista.
//
//  · SAMPLE-RATE (44.1 / 48 / 96 kHz): los TIEMPOS no se corren — el eco k cae en k·RATE segundos
//    (±5 ms) en los tres SR, y su amplitud es consistente (±3 dB entre SRs: el anillo HRIR se
//    resamplea al SR en prepare). Caza coeficientes que olvidan recalcularse con Fs.
//  · BLOCK-SIZE (32 vs 512 vs 2048): la MISMA señal da la MISMA salida en RÉGIMEN (±2e-3 tras un
//    warmup de 2.5 s). El warmup se descarta A PROPÓSITO: el voice management de DUST escalona los
//    nacimientos 1-por-bloque (anti-click estructural, MultiTapEcho::updateVoices) → el ONSET de la
//    nube se puebla a ritmo del block-size del host; en régimen (todas las burbujas vivas, fades
//    completos, ganancias convergidas) el DSP es por-sample puro y NO depende del bloque. VIDA=0 en
//    este test: la fase del LFO de deriva queda fijada al nacer (cuantizado al bloque) → con deriva
//    la igualdad es perceptual, no sample-exacta (documentado en el QA).
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "template/tests/OvniTestHarness.h"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
    namespace pid = dust::params::id;

    // Setea el RATE FREE en ms exactos (vía el rango log del parámetro — una fuente de verdad).
    void setRateMsParam (dust::DustProcessor& proc, float ms)
    {
        if (auto* r = proc.apvts.getParameter (pid::RATE))
            r->setValueNotifyingHost (proc.apvts.getParameterRange (pid::RATE).convertTo0to1 (ms));
    }

    // Condiciones del test de SR: mecanismo ON (DENSIDAD default, banco HRIR activo), MIX realista,
    // grilla limpia (SPREAD 0 / VIDA 0: el timing puro, sin jitter legítimo encima).
    void setupForTiming (dust::DustProcessor& proc)
    {
        using ovni::test::setParam;
        setParam (proc, pid::MIX,     0.35f);
        setParam (proc, pid::DENSITY, 0.40f);
        setParam (proc, pid::SPREAD,  0.0f);
        setParam (proc, pid::VIDA,    0.0f);
        setRateMsParam (proc, 250.0f);
    }

    struct EchoPeak { double timeSec = 0.0; float amp = 0.0f; };

    // Impulso en t=0 -> envolvente |L|+|R| -> pico del eco k en la ventana k·rate ±40 %.
    std::vector<EchoPeak> measureEchoes (double sr, int block, int numEchoes, double rateSec)
    {
        dust::DustProcessor proc;
        setupForTiming (proc);
        proc.prepareToPlay (sr, block);

        const long total = (long) std::llround ((numEchoes + 0.7) * rateSec * sr);
        std::vector<float> env ((size_t) total, 0.0f);

        // Impulso de ÁREA constante: amplitud ∝ SR (un impulso de 1 sample a 96k tiene la mitad de
        // área que a 48k → sin esta normalización la amplitud del eco cae exactamente 44100/96000 y
        // el gate de ±3 dB mediría la física del estímulo, no el motor).
        const float impAmp = (float) (sr / 48000.0);
        juce::AudioBuffer<float> buf (2, block);
        bool first = true;
        for (long pos = 0; pos < total; pos += block)
        {
            buf.clear();
            if (first) { buf.setSample (0, 0, impAmp); buf.setSample (1, 0, impAmp); first = false; }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            const float* L = buf.getReadPointer (0);
            const float* R = buf.getReadPointer (1);
            for (int i = 0; i < block && pos + i < total; ++i)
                env[(size_t) (pos + i)] = std::abs (L[i]) + std::abs (R[i]);
        }

        std::vector<EchoPeak> peaks;
        for (int k = 1; k <= numEchoes; ++k)
        {
            const long c  = (long) std::llround ((double) k * rateSec * sr);
            const long w0 = c - (long) (0.4 * rateSec * sr);
            const long w1 = juce::jmin (total - 1, c + (long) (0.4 * rateSec * sr));
            EchoPeak p; long best = w0;
            for (long i = w0; i <= w1; ++i)
                if (env[(size_t) i] > p.amp) { p.amp = env[(size_t) i]; best = i; }
            p.timeSec = (double) best / sr;
            peaks.push_back (p);
        }
        return peaks;
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// SAMPLE-RATE: a 44.1/48/96 kHz el eco k cae en k·250 ms (±5 ms) y con amplitud consistente.
// ─────────────────────────────────────────────────────────────────────────────────────────────────
TEST_CASE ("DUST consistency: 44.1/48/96 kHz -> los ecos caen en el MISMO tiempo (+-5 ms)",
           "[consistency][dust]")
{
    constexpr double kRateSec = 0.250;
    constexpr int    kEchoes  = 4;
    const double srs[] = { 44100.0, 48000.0, 96000.0 };

    std::vector<std::vector<EchoPeak>> all;
    for (double sr : srs)
        all.push_back (measureEchoes (sr, 512, kEchoes, kRateSec));

    for (size_t s = 0; s < 3; ++s)
        for (int k = 0; k < kEchoes; ++k)
        {
            const auto& p = all[s][(size_t) k];
            std::printf ("SRCHECK[dust] sr=%.0f eco%u t=%.4f s amp=%.4f\n",
                         srs[s], (unsigned) k + 1, p.timeSec, p.amp);
            INFO ("sr=" << srs[s] << " eco k=" << (k + 1) << " t=" << p.timeSec << " amp=" << p.amp);

            // El eco existe (mecanismo ENCENDIDO, audible a MIX 35) y cae a ±5 ms del múltiplo.
            REQUIRE (p.amp > 0.02f);
            REQUIRE (std::abs (p.timeSec - (double) (k + 1) * kRateSec) <= 0.005);
        }

    // Amplitud consistente entre SRs (±3 dB por eco): el anillo HRIR resampleado no cambia el nivel.
    for (int k = 0; k < kEchoes; ++k)
        for (size_t s = 1; s < 3; ++s)
        {
            const float ratio = all[s][(size_t) k].amp / juce::jmax (1.0e-9f, all[0][(size_t) k].amp);
            INFO ("eco k=" << (k + 1) << " amp ratio sr" << srs[s] << "/44.1k = " << ratio);
            REQUIRE (ratio > 0.7071f);   // −3 dB
            REQUIRE (ratio < 1.4143f);   // +3 dB
        }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// BLOCK-SIZE: 32 vs 512 vs 2048 dan la MISMA salida en régimen (warmup de spawneo descartado).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace
{
    // Render con ruido rosa determinístico (instancia fresca); devuelve el stream L completo.
    std::vector<float> renderPinkL (int block, double seconds)
    {
        constexpr double SR = 48000.0;
        dust::DustProcessor proc;
        using ovni::test::setParam;
        setParam (proc, pid::MIX,     0.35f);
        setParam (proc, pid::DENSITY, 0.50f);
        setParam (proc, pid::SPREAD,  0.60f);
        setParam (proc, pid::VIDA,    0.0f);    // ver cabecera: la deriva fija su fase al nacer
        setRateMsParam (proc, 150.0f);
        proc.prepareToPlay (SR, block);

        ovni::test::Pink pink;
        const long total = (long) std::llround (seconds * SR);
        std::vector<float> out;
        out.reserve ((size_t) total + (size_t) block);

        juce::AudioBuffer<float> buf (2, block);
        for (long pos = 0; pos < total; pos += block)
        {
            const int n = (int) juce::jmin ((long) block, total - pos);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.5f * pink.next();
                buf.setSample (0, i, x);
                buf.setSample (1, i, x);
            }
            juce::MidiBuffer midi;
            proc.processBlock (buf, midi);
            const float* L = buf.getReadPointer (0);
            for (int i = 0; i < n; ++i) out.push_back (L[i]);
        }
        return out;
    }
}

TEST_CASE ("DUST consistency: blocks 32/512/2048 -> misma salida en regimen (+-2e-3)",
           "[consistency][dust]")
{
    constexpr double kSecs   = 5.0;
    constexpr double kWarmup = 2.5;     // spawneo completo + fades + ganancias convergidas
    constexpr float  kEps    = 2.0e-3f;

    const auto ref = renderPinkL (512, kSecs);
    const size_t i0 = (size_t) std::llround (kWarmup * 48000.0);

    for (int block : { 32, 2048 })
    {
        const auto other = renderPinkL (block, kSecs);
        const size_t cmp = juce::jmin (ref.size(), other.size());
        REQUIRE (cmp > i0);

        float maxDiff = 0.0f; double energy = 0.0;
        for (size_t i = i0; i < cmp; ++i)
        {
            maxDiff = juce::jmax (maxDiff, std::abs (ref[i] - other[i]));
            energy += (double) ref[i] * ref[i];
        }
        std::printf ("BLOCKCHECK[dust] block %d vs 512: maxDiff=%.3e (regimen %.1f..%.1f s, energia=%.3e)\n",
                     block, maxDiff, kWarmup, kSecs, energy);
        INFO ("block " << block << " vs 512: maxDiff=" << maxDiff);

        REQUIRE (energy > 1.0e-3);      // sonó de verdad (mecanismo ON, no silencio)
        REQUIRE (maxDiff <= kEps);      // misma salida en régimen: DSP block-agnóstico
    }
}
