// Medidor OBJETIVO de imagen estéreo / fase de PULSAR (sin depender del Insight ni de capturas).
// Corre RUIDO ROSA mono por el PulsarProcessor REAL a varios WIDTH (+ IN PHASE) e imprime los mismos
// números que un vectorscope/correlímetro pro:
//   CORR       = correlación de fase L/R: +1 = mono/correlacionado, ~0 = ancho/decorrelacionado, <0 = fuera de fase
//   WIDTH      = RMS(side)/RMS(mid): 0 = mono, ↑ = más ancho (>1 = el side domina)
//   BAL_dB     = balance de energía L vs R (≈0 = centrado/equilibrado)
//   MONOSUM_dB = nivel de la suma L+R vs el directo: 0 ≈ sin pérdida al monoficar, muy negativo = cancela
// Es DIAGNÓSTICO (no pass/fail): imprime líneas MEASURE[...] que el harness/Claude lee. REQUIRE sólo finitud.
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include "PluginProcessor.h"

namespace
{
// Ruido rosa determinístico (Paul Kellet "economy") sobre un LCG con semilla fija (reproducible).
struct Pink
{
    std::uint32_t s = 0x13572468u;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    float white() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
    float next()
    {
        const float w = white();
        b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
        b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
        b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
        const float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
        return p * 0.11f;   // ~[-1,1]
    }
};

void measure (pulsar::PulsarProcessor& proc, float width01, bool inPhase, const char* tag,
              bool emitIacc = false)
{
    const double SR = 48000.0; const int N = 512;
    auto set = [&] (const char* id, float v) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v); };
    // RATE muy lento + MOTION bajo: la fuente casi quieta -> medimos el ANCHO del motor, no el barrido.
    set ("rate", 0.0f); set ("motion", 0.25f); set ("shape", 0.5f); set ("smear", 0.0f);
    set ("sync", 0.0f); set ("width", width01); set ("monoSafe", inPhase ? 1.0f : 0.0f);
    proc.prepareToPlay (SR, N);

    Pink pink;
    double sLL=0,sRR=0,sLR=0,sMid=0,sSide=0,sMono=0; long cnt=0;
    for (int blk = 0; blk < 500; ++blk)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int n = 0; n < N; ++n) { const float x = pink.next(); buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        if (blk < 100) continue;   // warmup (delays HRIR/reflexiones se asientan)
        const float* L = buf.getReadPointer (0); const float* R = buf.getReadPointer (1);
        for (int n = 0; n < N; ++n)
        {
            const double l = L[n], r = R[n];
            sLL += l*l; sRR += r*r; sLR += l*r;
            const double m = 0.5*(l+r), sd = 0.5*(l-r);
            sMid += m*m; sSide += sd*sd; sMono += (l+r)*(l+r); ++cnt;
        }
    }
    const double corr   = sLR / (std::sqrt (sLL * sRR) + 1e-12);
    const double width  = std::sqrt (sSide / (sMid + 1e-12));
    const double balDB  = 10.0 * std::log10 ((sLL + 1e-12) / (sRR + 1e-12));
    const double rmsL   = std::sqrt (sLL / (double) cnt);
    const double rmsMono= std::sqrt (sMono / (double) cnt);
    const double monoDB = 20.0 * std::log10 ((rmsMono + 1e-12) / (2.0 * rmsL + 1e-12));
    std::printf ("MEASURE[%-18s] CORR=%+.3f  WIDTH=%.3f  BAL_dB=%+.2f  MONOSUM_dB=%+.2f\n",
                 tag, corr, width, balDB, monoDB);
    // IACC = correlación interaural (= CORR de fase L/R acá): el cue de envolvimiento que el sello publica.
    // Sólo se emite en el caso canónico (WIDTH=100, el ancho máximo del binaural) → measure-check.sh lo grepea.
    if (emitIacc) std::printf ("IACC=%.3f\n", corr);
    REQUIRE (std::isfinite (corr));
}
} // namespace

TEST_CASE ("PULSAR imagen estéreo / fase con ruido rosa (diagnóstico)", "[measure][pulsar]")
{
    pulsar::PulsarProcessor proc;
    measure (proc, 0.0f, false, "WIDTH=0");
    measure (proc, 0.5f, false, "WIDTH=50");
    measure (proc, 1.0f, false, "WIDTH=100", /*emitIacc*/ true);
    measure (proc, 1.0f, true,  "WIDTH=100 INPHASE");
}
