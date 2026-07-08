// Medidor OBJETIVO de la física del reverb de NÉBULA (sin depender de capturas ni del oído). Corre un
// impulso / un burst de ruido por el NebulaProcessor REAL (processor + motor FDN + limiter) e imprime los
// números que un acústico mediría de una cola de reverb:
//   T60       = tiempo de decaimiento real de la cola a −60 dB (con decay01 medio).
//   CORR      = correlación de fase L/R de la cola: +1 = mono colapsada, <1 = difusa/ancha. DEBE ser < 1.
//   FREEZE    = energía RMS de la cola congelada (decay=100) al principio y al final → se sostiene, finita.
//   MONOSUM   = dB al sumar L+R vs el directo: 0 ≈ sin pérdida al monoficar; muy negativo = cancela (fase).
// Es DIAGNÓSTICO: imprime líneas con std::printf que el harness/Claude lee de stdout. REQUIRE sólo lo
// CRÍTICO: finito, no-NaN, y la cola NO colapsada a mono (CORR < 0.98). El tuning fino de carácter
// (metálico/respiración) lo cierra el oído de Joaquín; acá garantizamos que NO colapsa ni explota.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>   // Catch::Approx (tabla de divisiones del SYNC)
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"     // nebula::params::sync (tabla compartida con el motor)

namespace
{
constexpr double SR = 48000.0;
constexpr int    N  = 512;

// Ruido blanco determinístico (LCG con semilla fija → reproducible) en [−1,1].
struct White
{
    std::uint32_t s = 0x9E3779B9u;
    float next() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

// Setea un parámetro del APVTS por su valor 0..1 normalizado (los % son rango 0..100 → 1.0 = 100%).
void setNorm (nebula::NebulaProcessor& proc, const char* id, float v01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
}

// ── T60 de la cola (decay01 medio) ──────────────────────────────────────────────────────────────────
// Excita con un impulso, deja decaer en silencio con MIX=100% (sólo wet) y mide el tiempo a −60 dB de la
// envolvente RMS por bloque. Imprime T60=<s>. (decay01=0.5 → la fórmula del motor da ~1.1 s.)
double measureT60 (nebula::NebulaProcessor& proc)
{
    setNorm (proc, "size", 0.5f); setNorm (proc, "decay", 0.5f); setNorm (proc, "tone", 0.2f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);
    proc.prepareToPlay (SR, N);

    // Impulso inicial.
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi; buf.clear();
        buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f);
        proc.processBlock (buf, midi);
    }

    std::vector<float> env;            // RMS por bloque del wet en silencio
    const int numBlocks = (int) std::ceil (6.0 * SR / N);   // hasta ~6 s
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
        proc.processBlock (z, midi);
        double acc = 0.0;
        for (int ch = 0; ch < 2; ++ch) { auto* d = z.getReadPointer (ch); for (int i = 0; i < N; ++i) acc += (double) d[i] * d[i]; }
        env.push_back ((float) std::sqrt (acc / (2.0 * N)));
    }

    float peak = 0.0f; for (float e : env) peak = juce::jmax (peak, e);
    if (peak <= 1.0e-6f) return -1.0;
    const float thresh = peak * 0.001f;   // −60 dB
    for (int b = 0; b < (int) env.size(); ++b)
        if (env[(size_t) b] <= thresh) return (double) b * N / SR;
    return -1.0;   // no cayó dentro de la ventana
}

// ── Correlación L/R + MONOSUM de la cola difusa ──────────────────────────────────────────────────────
// Inyecta un burst de ruido MONO (idéntico en L y R) por el processor, deja correr y mide SÓLO la cola
// (tras el burst). Si el motor decorrelaciona, la cola tiene CORR < 1 aunque la entrada era mono.
struct ImageStats { double corr; double monoDB; double rmsL; double rmsR; };

ImageStats measureImage (nebula::NebulaProcessor& proc)
{
    setNorm (proc, "size", 0.6f); setNorm (proc, "decay", 0.7f); setNorm (proc, "tone", 0.25f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);
    proc.prepareToPlay (SR, N);

    White w;
    // 1) Burst de ruido mono ~0.2 s para cargar la red.
    const int burstBlocks = (int) std::ceil (0.2 * SR / N);
    for (int blk = 0; blk < burstBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int i = 0; i < N; ++i) { const float x = 0.5f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        proc.processBlock (buf, midi);
    }

    // 2) Medir SÓLO la cola en silencio (la entrada ya no es mono: es lo que el motor difunde).
    double sLL = 0, sRR = 0, sLR = 0, sMono = 0; long cnt = 0;
    const int tailBlocks = (int) std::ceil (1.5 * SR / N);
    for (int blk = 0; blk < tailBlocks; ++blk)
    {
        juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
        proc.processBlock (z, midi);
        const float* L = z.getReadPointer (0); const float* R = z.getReadPointer (1);
        for (int i = 0; i < N; ++i)
        {
            const double l = L[i], r = R[i];
            sLL += l * l; sRR += r * r; sLR += l * r; sMono += (l + r) * (l + r); ++cnt;
        }
    }
    const double corr   = sLR / (std::sqrt (sLL * sRR) + 1e-12);
    const double rmsL   = std::sqrt (sLL / juce::jmax (1L, cnt));
    const double rmsR   = std::sqrt (sRR / juce::jmax (1L, cnt));
    const double rmsMono= std::sqrt (sMono / juce::jmax (1L, cnt));
    const double monoDB = 20.0 * std::log10 ((rmsMono + 1e-12) / (rmsL + rmsR + 1e-12));
    return { corr, monoDB, rmsL, rmsR };
}

// ── Estabilidad del freeze (decay=100) ───────────────────────────────────────────────────────────────
// Carga la red con un burst y la congela: mide la energía RMS de la cola al principio (~0.5 s) y al final
// (~5 s). En freeze debe SOSTENERSE (finita, ni 0 ni explota). Imprime FREEZE_first / FREEZE_last / peak.
struct FreezeStats { float first; float last; float peak; bool finite; };

FreezeStats measureFreeze (nebula::NebulaProcessor& proc)
{
    setNorm (proc, "size", 0.6f); setNorm (proc, "decay", 1.0f);   // 100% → freeze
    setNorm (proc, "tone", 0.2f); setNorm (proc, "breath", 0.3f); setNorm (proc, "mix", 1.0f);
    proc.prepareToPlay (SR, N);

    White w;
    const int burstBlocks = (int) std::ceil (0.3 * SR / N);
    for (int blk = 0; blk < burstBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int i = 0; i < N; ++i) { const float x = 0.4f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        proc.processBlock (buf, midi);
    }

    std::vector<float> env; float peak = 0.0f; bool finite = true;
    const int numBlocks = (int) std::ceil (5.0 * SR / N);
    for (int blk = 0; blk < numBlocks; ++blk)
    {
        juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
        proc.processBlock (z, midi);
        double acc = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = z.getReadPointer (ch);
            for (int i = 0; i < N; ++i) { if (! std::isfinite (d[i])) finite = false; acc += (double) d[i] * d[i]; peak = juce::jmax (peak, std::abs (d[i])); }
        }
        env.push_back ((float) std::sqrt (acc / (2.0 * N)));
    }
    const float first = env[(size_t) (numBlocks / 10)];   // ~0.5 s
    const float last  = env[(size_t) (numBlocks - 2)];    // ~5 s
    return { first, last, peak, finite };
}

// ── IN PHASE (monoSafe): correlación L/R de la cola con el toggle OFF vs ON ───────────────────────────
// Igual setup que measureImage (burst mono → mide SÓLO la cola) pero seteando el param "monoSafe" del
// chasis. Con IN PHASE ON el motor colapsa la cola WET a mono ANTES del mix → la cola queda EN FASE
// (CORR→~1, mono-compatible). Con OFF la cola es difusa/decorrelada (CORR≈0.14). Devuelve la CORR medida.
double measureTailCorr (nebula::NebulaProcessor& proc, bool inPhase)
{
    setNorm (proc, "size", 0.6f); setNorm (proc, "decay", 0.7f); setNorm (proc, "tone", 0.25f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);
    setNorm (proc, "monoSafe", inPhase ? 1.0f : 0.0f);   // IN PHASE del chasis (inyectado)
    proc.prepareToPlay (SR, N);

    White w;
    // 1) Burst de ruido mono ~0.2 s para cargar la red.
    const int burstBlocks = (int) std::ceil (0.2 * SR / N);
    for (int blk = 0; blk < burstBlocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int i = 0; i < N; ++i) { const float x = 0.5f * w.next(); buf.setSample (0, i, x); buf.setSample (1, i, x); }
        proc.processBlock (buf, midi);
    }

    // 2) Medir SÓLO la cola en silencio.
    double sLL = 0, sRR = 0, sLR = 0;
    const int tailBlocks = (int) std::ceil (1.5 * SR / N);
    for (int blk = 0; blk < tailBlocks; ++blk)
    {
        juce::AudioBuffer<float> z (2, N); juce::MidiBuffer midi; z.clear();
        proc.processBlock (z, midi);
        const float* L = z.getReadPointer (0); const float* R = z.getReadPointer (1);
        for (int i = 0; i < N; ++i) { const double l = L[i], r = R[i]; sLL += l * l; sRR += r * r; sLR += l * r; }
    }
    return sLR / (std::sqrt (sLL * sRR) + 1e-12);
}
} // namespace

TEST_CASE ("NEBULA física del reverb: T60 / CORR / FREEZE / MONOSUM (diagnóstico)", "[measure][nebula]")
{
    // ── T60 (cola con decay medio). Objetivo = la fórmula pública del motor para decay01=0.5. ──────
    double t60 = -1.0;
    {
        nebula::NebulaProcessor proc;
        t60 = measureT60 (proc);
        const float t60Target = ovni::engines::FdnReverb::t60ForDecay (0.5f);
        std::printf ("MEASURE[nebula T60]     T60=%.3f s  (decay01=0.5, objetivo motor=%.3f s)\n", t60, t60Target);
    }

    // ── CORR L/R + MONOSUM de la cola difusa (entrada mono → cola decorrelada). ────────────────────
    ImageStats img {};
    {
        nebula::NebulaProcessor proc;
        img = measureImage (proc);
        std::printf ("MEASURE[nebula image]   CORR=%+.3f  MONOSUM=%+.2f dB  (rmsL=%.5f rmsR=%.5f)\n",
                     img.corr, img.monoDB, img.rmsL, img.rmsR);
        // IACC = correlación interaural de la cola difusa (mismo número que CORR acá): el cue de
        // envolvimiento que el sello publica. <1 = cola decorrelada (envolvente). Lo grepea measure-check.sh.
        std::printf ("IACC=%.3f\n", img.corr);
    }

    // ── Estabilidad del freeze (decay=100): energía sostenida, finita. ─────────────────────────────
    FreezeStats fz {};
    {
        nebula::NebulaProcessor proc;
        fz = measureFreeze (proc);
        std::printf ("MEASURE[nebula freeze]  FREEZE_first=%.5f  FREEZE_last=%.5f  ratio=%.3f  peak=%.4f\n",
                     fz.first, fz.last, (fz.first > 0.0f ? fz.last / fz.first : 0.0f), fz.peak);
    }

    // ── REQUIRE sólo lo CRÍTICO (diagnóstico, no pass/fail de carácter): ───────────────────────────
    REQUIRE (std::isfinite (img.corr));
    REQUIRE (std::isfinite (img.monoDB));
    REQUIRE (img.corr < 0.98);                 // la cola NO colapsa a mono (es difusa/ancha)
    REQUIRE (fz.finite);                        // el freeze no produce NaN/Inf
    REQUIRE (fz.peak <= 1.0f);                  // ni clippea
    REQUIRE (fz.first > 1.0e-5f);               // hay cola congelada (no murió)
    REQUIRE (fz.last  > 0.10f * fz.first);      // se sostiene (no decae a silencio)
    REQUIRE (fz.last  < 10.0f * fz.first);      // no explota
    REQUIRE (t60 > 0.0);                        // midió un T60 finito dentro de la ventana
}

// ── Feature 1 — IN PHASE colapsa la cola a mono-compatible ───────────────────────────────────────────
// Con IN PHASE OFF la cola del FDN está MUY decorrelada (CORR≈0.14). Con IN PHASE ON el motor suma la
// cola WET a mono ANTES del mix → la cola queda EN FASE: CORR debe SUBIR hacia ~1.0 (mono-safe real).
// Esto prueba que el toggle YA afecta el sonido (antes era no-op para la cola full-band).
TEST_CASE ("NEBULA IN PHASE: la cola colapsa a mono (CORR sube hacia 1)", "[measure][nebula]")
{
    double corrOff = 0.0, corrOn = 0.0;
    { nebula::NebulaProcessor proc; corrOff = measureTailCorr (proc, /*inPhase*/ false); }
    { nebula::NebulaProcessor proc; corrOn  = measureTailCorr (proc, /*inPhase*/ true ); }

    std::printf ("MEASURE[nebula inphase] CORR monoSafe=OFF %+.3f  ->  monoSafe=ON %+.3f\n", corrOff, corrOn);

    REQUIRE (std::isfinite (corrOff));
    REQUIRE (std::isfinite (corrOn));
    REQUIRE (corrOff < 0.5);          // OFF: cola difusa/decorrelada (la firma actual ≈0.14)
    REQUIRE (corrOn  > 0.95);         // ON: cola EN FASE (mono-compatible: CORR→~1)
    REQUIRE (corrOn  > corrOff + 0.5); // el toggle SÍ mueve la fase (no es no-op)
}

// ── Feature 2 — SYNC: la tabla de divisiones deriva la frecuencia de respiración del BPM ──────────────
// Verifica la fuente de verdad compartida (params::sync): beatsForDiv por índice + breathRateHz(bpm,div).
// breathRateHz = (bpm/60) / beatsPerCycle. A 120 BPM (=2 beats/seg): 1 bar (4 beats) → 0.5 Hz, etc.
TEST_CASE ("NEBULA SYNC: tabla de divisiones y breathRateHz", "[measure][nebula]")
{
    namespace sd = nebula::params::sync;

    // La tabla tiene las 4 divisiones esperadas, en orden, con sus beats por ciclo.
    REQUIRE (sd::kCount == 4);
    REQUIRE (sd::beatsForDiv (0) == Catch::Approx (2.0));    // 1/2
    REQUIRE (sd::beatsForDiv (1) == Catch::Approx (4.0));    // 1 bar (default)
    REQUIRE (sd::beatsForDiv (2) == Catch::Approx (8.0));    // 2 bars
    REQUIRE (sd::beatsForDiv (3) == Catch::Approx (16.0));   // 4 bars
    REQUIRE (sd::kDefaultIndex == 1);                        // "1 bar"

    // A 120 BPM = 2 beats/seg. breathRateHz = beats_por_seg / beats_por_ciclo.
    REQUIRE (sd::breathRateHz (120.0, 0) == Catch::Approx (1.0));    // 1/2   → 2/2  = 1.0 Hz
    REQUIRE (sd::breathRateHz (120.0, 1) == Catch::Approx (0.5));    // 1 bar → 2/4  = 0.5 Hz
    REQUIRE (sd::breathRateHz (120.0, 2) == Catch::Approx (0.25));   // 2 bars→ 2/8  = 0.25 Hz
    REQUIRE (sd::breathRateHz (120.0, 3) == Catch::Approx (0.125));  // 4 bars→ 2/16 = 0.125 Hz

    // Otro tempo: 90 BPM = 1.5 beats/seg → 1 bar = 1.5/4 = 0.375 Hz.
    REQUIRE (sd::breathRateHz (90.0, 1)  == Catch::Approx (0.375));

    std::printf ("MEASURE[nebula sync]    120BPM: 1/2=%.3f 1bar=%.3f 2bars=%.3f 4bars=%.3f Hz\n",
                 sd::breathRateHz (120.0, 0), sd::breathRateHz (120.0, 1),
                 sd::breathRateHz (120.0, 2), sd::breathRateHz (120.0, 3));
}

// ── Feature 3 — LOW CUT: high-pass del WET que entra a la cola (el DRY pasa entero) ───────────────────
// Mide la energía del WET (mix=100% → la salida ES la cola) con un SENO sostenido a una frecuencia dada,
// con el low-cut APAGADO (0%) vs al MÁXIMO (~500 Hz). Un seno grave (~60 Hz) debe salir CLARAMENTE atenuado
// (el HP le quita la excitación a la cola); uno medio (~2 kHz) debe pasar ~transparente. Sin NaN, estable.
namespace
{
// RMS de estado estable del WET para un seno a 'hz' con lowCut a 'lowCutNorm' (0..1). Setea mix=100% (la
// salida es la cola, sin dry), corre el seno hasta que la cola se asienta, y promedia la RMS de las colas.
double measureWetRms (double hz, float lowCutNorm)
{
    nebula::NebulaProcessor proc;
    setNorm (proc, "size", 0.5f); setNorm (proc, "decay", 0.5f); setNorm (proc, "tone", 0.0f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);   // 100% wet → la salida ES la cola
    setNorm (proc, "lowCut", lowCutNorm);
    proc.prepareToPlay (SR, N);

    long g = 0;
    auto pushSine = [&] (int blocks, bool measure, double& accOut, long& cntOut)
    {
        for (int blk = 0; blk < blocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<double>::twoPi * hz * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x); ++g;
            }
            proc.processBlock (buf, midi);
            if (measure)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { accOut += (double) d[i] * d[i]; ++cntOut; } }
        }
    };

    double dummyAcc = 0.0; long dummyCnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), false, dummyAcc, dummyCnt);   // ~1 s para que la cola se asiente
    double acc = 0.0; long cnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), true, acc, cnt);              // ~1 s de medición
    return std::sqrt (acc / juce::jmax (1L, cnt));
}

// RMS de estado estable del WET para un seno a 'hz' con HI CUT a 'hiCutNorm' (0..1). Igual setup que el del
// low-cut pero seteando el param "hiCut" (low-pass del wet). Reutilizar el patrón mantiene la medición coherente.
double measureWetRmsHiCut (double hz, float hiCutNorm)
{
    nebula::NebulaProcessor proc;
    setNorm (proc, "size", 0.5f); setNorm (proc, "decay", 0.5f); setNorm (proc, "tone", 0.0f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 1.0f);   // 100% wet → la salida ES la cola
    setNorm (proc, "hiCut", hiCutNorm);
    proc.prepareToPlay (SR, N);

    long g = 0;
    auto pushSine = [&] (int blocks, bool measure, double& accOut, long& cntOut)
    {
        for (int blk = 0; blk < blocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<double>::twoPi * hz * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x); ++g;
            }
            proc.processBlock (buf, midi);
            if (measure)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { accOut += (double) d[i] * d[i]; ++cntOut; } }
        }
    };

    double dummyAcc = 0.0; long dummyCnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), false, dummyAcc, dummyCnt);   // ~1 s para que la cola se asiente
    double acc = 0.0; long cnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), true, acc, cnt);              // ~1 s de medición
    return std::sqrt (acc / juce::jmax (1L, cnt));
}
} // namespace

TEST_CASE ("NEBULA LOW CUT: el HP atenua los graves del wet y deja pasar los medios", "[lowcut][nebula]")
{
    // Energía del wet con low-cut OFF (0%) y al MÁXIMO (100% ≈ 500 Hz), a 60 Hz (grave) y 2 kHz (medio).
    const double rms60Off  = measureWetRms (60.0,   0.0f);
    const double rms60On   = measureWetRms (60.0,   1.0f);
    const double rms2kOff  = measureWetRms (2000.0, 0.0f);
    const double rms2kOn   = measureWetRms (2000.0, 1.0f);

    const double ratio60 = rms60On / juce::jmax (1.0e-9, rms60Off);   // cuánto sobrevive el grave con HP máx
    const double ratio2k = rms2kOn / juce::jmax (1.0e-9, rms2kOff);   // cuánto sobrevive el medio con HP máx

    std::printf ("MEASURE[nebula lowcut]  60Hz: off=%.5f on=%.5f (ratio=%.3f, %.1f dB)  |  2kHz: off=%.5f on=%.5f (ratio=%.3f, %.1f dB)\n",
                 rms60Off, rms60On, ratio60, 20.0 * std::log10 (juce::jmax (1.0e-9, ratio60)),
                 rms2kOff, rms2kOn, ratio2k, 20.0 * std::log10 (juce::jmax (1.0e-9, ratio2k)));

    // Todo finito (sin NaN/Inf en ninguna corrida).
    for (double v : { rms60Off, rms60On, rms2kOff, rms2kOn }) REQUIRE (std::isfinite (v));
    // Hay cola que medir en los casos sin filtrar (el motor produce wet).
    REQUIRE (rms60Off > 1.0e-4);
    REQUIRE (rms2kOff > 1.0e-4);

    // (1) El grave (60 Hz) sale CLARAMENTE atenuado con el HP al máximo (≈500 Hz): sobrevive < 50% (> 6 dB abajo).
    REQUIRE (ratio60 < 0.5);
    // (2) El medio (2 kHz) pasa ~transparente con el HP al máximo: el 500 Hz no lo toca (> 85% sobrevive).
    REQUIRE (ratio2k > 0.85);
    // (3) El HP discrimina por frecuencia: el grave se atenúa MUCHO más que el medio (al menos 6 dB de diferencia).
    REQUIRE (ratio60 < ratio2k * 0.5);
}

// ── LOW CUT en OFF (default 0%) = transparente: no cambia el wet respecto a no tener el control ───────────
// Verifica que con lowCut=0 (el default de fábrica) el wet a 60 Hz y a 2 kHz sea PRÁCTICAMENTE idéntico a
// cuando el feature no actuaba: el de-zipper del cutoff arranca en 20 Hz (off) y el path de restitución del
// dry no toca el wet (a mix=100% dryG=0). El 20 Hz HP sólo saca DC/subsónico → el seno audible pasa entero.
TEST_CASE ("NEBULA LOW CUT: en OFF (0%) el wet es transparente", "[lowcut][nebula]")
{
    const double rms60   = measureWetRms (60.0,   0.0f);
    const double rms2k   = measureWetRms (2000.0, 0.0f);
    // Un toque por encima de 0 (1%) sigue siendo ~20.6 Hz → debe seguir dejando pasar el 60 Hz casi entero.
    const double rms60lo = measureWetRms (60.0,   0.01f);

    std::printf ("MEASURE[nebula lowcut-off] 60Hz off=%.5f  60Hz@1%%=%.5f  2kHz off=%.5f\n", rms60, rms60lo, rms2k);

    for (double v : { rms60, rms2k, rms60lo }) REQUIRE (std::isfinite (v));
    // A 0% el 60 Hz pasa entero (el HP está en 20 Hz): hay cola grave de verdad.
    REQUIRE (rms60 > 1.0e-4);
    // A 1% (≈20.6 Hz) el 60 Hz sigue pasando casi igual que en off (> 90%): el default no adelgaza el grave.
    REQUIRE (rms60lo > 0.90 * rms60);
}

// ── Feature — HI CUT: low-pass del WET que entra a la cola (el par del LOW CUT; el DRY pasa entero) ────
// Mide la energía del WET (mix=100% → la salida ES la cola) con un SENO sostenido, con el hi-cut APAGADO (0%)
// vs al MÁXIMO (~1.5 kHz). Un seno agudo (~8 kHz) debe salir CLARAMENTE atenuado (el LP le quita la excitación
// a la cola); uno bajo-medio (~300 Hz) debe pasar ~transparente (queda muy por debajo del corte). Sin NaN, estable.
TEST_CASE ("NEBULA HI CUT: el LP atenua los agudos del wet y deja pasar los bajos", "[hicut][nebula]")
{
    // Energía del wet con hi-cut OFF (0%) y al MÁXIMO (100% ≈ 1.5 kHz), a 8 kHz (agudo) y 300 Hz (bajo-medio).
    const double rms8kOff  = measureWetRmsHiCut (8000.0, 0.0f);
    const double rms8kOn   = measureWetRmsHiCut (8000.0, 1.0f);
    const double rms300Off = measureWetRmsHiCut (300.0,  0.0f);
    const double rms300On  = measureWetRmsHiCut (300.0,  1.0f);

    const double ratio8k  = rms8kOn  / juce::jmax (1.0e-9, rms8kOff);    // cuánto sobrevive el agudo con LP máx
    const double ratio300 = rms300On / juce::jmax (1.0e-9, rms300Off);   // cuánto sobrevive el bajo con LP máx

    std::printf ("MEASURE[nebula hicut]  8kHz: off=%.5f on=%.5f (ratio=%.3f, %.1f dB)  |  300Hz: off=%.5f on=%.5f (ratio=%.3f, %.1f dB)\n",
                 rms8kOff, rms8kOn, ratio8k, 20.0 * std::log10 (juce::jmax (1.0e-9, ratio8k)),
                 rms300Off, rms300On, ratio300, 20.0 * std::log10 (juce::jmax (1.0e-9, ratio300)));

    // Todo finito (sin NaN/Inf en ninguna corrida).
    for (double v : { rms8kOff, rms8kOn, rms300Off, rms300On }) REQUIRE (std::isfinite (v));
    // Hay cola que medir en los casos sin filtrar (el motor produce wet).
    REQUIRE (rms8kOff  > 1.0e-4);
    REQUIRE (rms300Off > 1.0e-4);

    // (1) El agudo (8 kHz) sale CLARAMENTE atenuado con el LP al máximo (≈1.5 kHz): sobrevive < 50% (> 6 dB abajo).
    REQUIRE (ratio8k < 0.5);
    // (2) El bajo-medio (300 Hz) pasa ~transparente con el LP al máximo: el corte en 1.5 kHz no lo toca (> 85%).
    REQUIRE (ratio300 > 0.85);
    // (3) El LP discrimina por frecuencia: el agudo se atenúa MUCHO más que el bajo (al menos 6 dB de diferencia).
    REQUIRE (ratio8k < ratio300 * 0.5);
}

// ── HI CUT en OFF (default 0%) = transparente: no cambia el wet respecto a no tener el control ────────────
// Con hiCut=0 (el default de fábrica) el wet a 8 kHz y a 300 Hz debe ser PRÁCTICAMENTE idéntico a cuando el
// feature no actuaba: el cutoff arranca en 20 kHz (off) y el path de restitución del dry no toca el wet (a
// mix=100% dryG=0). El LP en 20 kHz deja pasar todo el rango audible → el seno de 8 kHz pasa casi entero.
TEST_CASE ("NEBULA HI CUT: en OFF (0%) el wet es transparente", "[hicut][nebula]")
{
    const double rms8k   = measureWetRmsHiCut (8000.0, 0.0f);
    const double rms300  = measureWetRmsHiCut (300.0,  0.0f);
    // Un toque por encima de 0 (1%) sigue siendo ~19.5 kHz → debe seguir dejando pasar el 8 kHz casi entero.
    const double rms8klo = measureWetRmsHiCut (8000.0, 0.01f);

    std::printf ("MEASURE[nebula hicut-off] 8kHz off=%.5f  8kHz@1%%=%.5f  300Hz off=%.5f\n", rms8k, rms8klo, rms300);

    for (double v : { rms8k, rms300, rms8klo }) REQUIRE (std::isfinite (v));
    // A 0% el 8 kHz pasa entero (el LP está en 20 kHz): hay cola aguda de verdad.
    REQUIRE (rms8k > 1.0e-4);
    // A 1% (≈19.5 kHz) el 8 kHz sigue pasando casi igual que en off (> 90%): el default no oscurece la cola.
    REQUIRE (rms8klo > 0.90 * rms8k);
}

// ── FIX del bug — LOW CUT / HI CUT filtran la SALIDA COMPLETA (dry+wet), no sólo el wet ────────────────────
// EL BUG (mismo que se arregló en HALO): antes los filtros tocaban SÓLO el wet de entrada a la cola y se le
// RESTITUÍA el grave/agudo al dry → a MIX bajo (40%) el dry full-range tapaba el corte y "no hacía nada" en la
// salida total (medido en HALO: −0.5 dB = imperceptible). EL FIX: filtran la SALIDA dry+wet → el corte se OYE a
// CUALQUIER MIX. Este DIAG mide la SALIDA TOTAL del processor a MIX=40% con un seno sostenido, off vs 100%, y
// exige corte FUERTE ahora (no el ≈−0.5 dB de antes).
namespace
{
// RMS de estado estable de la SALIDA TOTAL (dry+wet) a MIX=40% para un seno a 'hz', con lowCut/hiCut a 'norm'
// (0..1). A diferencia de measureWetRms (que pone mix=100% → mide sólo el wet), acá MIX=40% deja el dry
// full-range en la salida: es el escenario del bug. paramId = "lowCut" o "hiCut".
double measureOutRmsAtMix40 (const char* paramId, double hz, float norm)
{
    nebula::NebulaProcessor proc;
    setNorm (proc, "size", 0.5f); setNorm (proc, "decay", 0.5f); setNorm (proc, "tone", 0.0f);
    setNorm (proc, "breath", 0.0f); setNorm (proc, "mix", 0.40f);   // 40% wet → el dry full-range domina la salida
    setNorm (proc, paramId, norm);
    proc.prepareToPlay (SR, N);

    long g = 0;
    auto pushSine = [&] (int blocks, bool measure, double& accOut, long& cntOut)
    {
        for (int blk = 0; blk < blocks; ++blk)
        {
            juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
            for (int i = 0; i < N; ++i)
            {
                const float x = 0.5f * std::sin (juce::MathConstants<double>::twoPi * hz * (double) g / SR);
                buf.setSample (0, i, x); buf.setSample (1, i, x); ++g;
            }
            proc.processBlock (buf, midi);   // in-place: buf queda con la SALIDA TOTAL del plugin (dry+wet)
            if (measure)
                for (int ch = 0; ch < 2; ++ch)
                { auto* d = buf.getReadPointer (ch); for (int i = 0; i < N; ++i) { accOut += (double) d[i] * d[i]; ++cntOut; } }
        }
    };

    double dummyAcc = 0.0; long dummyCnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), false, dummyAcc, dummyCnt);   // ~1 s para que la cola se asiente
    double acc = 0.0; long cnt = 0;
    pushSine ((int) std::ceil (1.0 * SR / N), true, acc, cnt);              // ~1 s de medición
    return std::sqrt (acc / juce::jmax (1L, cnt));
}
} // namespace

TEST_CASE ("NEBULA LOW/HI CUT: cortan la SALIDA dry+wet a MIX bajo (el FIX del bug)", "[lowcutreal][hicutreal][nebula]")
{
    // ── HI CUT 100% sobre una banda ALTA (8 kHz) a MIX=40%: la SALIDA TOTAL debe bajar FUERTE (≥10 dB). ──────
    const double hi8kOff = measureOutRmsAtMix40 ("hiCut", 8000.0, 0.0f);
    const double hi8kOn  = measureOutRmsAtMix40 ("hiCut", 8000.0, 1.0f);
    const double hiRatio = hi8kOn / juce::jmax (1.0e-9, hi8kOff);
    const double hiDb    = 20.0 * std::log10 (juce::jmax (1.0e-9, hiRatio));

    // ── LOW CUT 100% sobre una banda BAJA (60 Hz) a MIX=40%: la SALIDA TOTAL debe bajar FUERTE. ─────────────
    const double lo60Off = measureOutRmsAtMix40 ("lowCut", 60.0, 0.0f);
    const double lo60On  = measureOutRmsAtMix40 ("lowCut", 60.0, 1.0f);
    const double loRatio = lo60On / juce::jmax (1.0e-9, lo60Off);
    const double loDb    = 20.0 * std::log10 (juce::jmax (1.0e-9, loRatio));

    std::printf ("MEASURE[nebula cutreal @MIX40] HI CUT 8kHz: off=%.5f on=%.5f (%.1f dB)  |  LOW CUT 60Hz: off=%.5f on=%.5f (%.1f dB)\n",
                 hi8kOff, hi8kOn, hiDb, lo60Off, lo60On, loDb);

    for (double v : { hi8kOff, hi8kOn, lo60Off, lo60On }) REQUIRE (std::isfinite (v));
    // Hay señal que medir en los casos sin filtrar (el dry a 40% + algo de wet).
    REQUIRE (hi8kOff > 1.0e-4);
    REQUIRE (lo60Off > 1.0e-4);

    // (1) HI CUT 100% baja la banda ALTA ≥ 10 dB en la SALIDA TOTAL a MIX 40% (antes el dry la tapaba → ≈−0.5 dB).
    REQUIRE (hiDb <= -10.0);
    // (2) LOW CUT 100% baja la banda BAJA fuerte en la SALIDA TOTAL a MIX 40% (≥ 6 dB, holgado para el HP de 500 Hz).
    REQUIRE (loDb <= -6.0);
}
