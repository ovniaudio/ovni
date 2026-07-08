#pragma once

// ========================================================================================================
// shared/template/tests/OvniTestHarness.h — PRIMITIVAS COMPARTIDAS de la batería de medición del sello.
//
// No define TEST_CASEs: junta los generadores de señal (ruido rosa, ruido blanco, transientes), los
// medidores de imagen estéreo (CORR/WIDTH/BAL/MONOSUM/IACC) y los helpers de corrida por el processor
// REAL que TODOS los tests parametrizados de plugins/<slug>/tests reusan. Así la medición es UNIFORME
// (un solo lugar donde vive cada cuenta) y un plugin nuevo no copia-pega nada.
//
// CONTRATO para los archivos que lo incluyen (ver _README.md de esta carpeta):
//   antes de incluir cualquier *Stub.h de esta carpeta, el .cpp del plugin define:
//     #define OVNI_PLUGIN_PROCESSOR  <ns>::<Plugin>Processor   // tipo concreto (deriva de PluginProcessorBase)
//     #define OVNI_PLUGIN_SLUG       "pulsar"                    // slug en minúsculas (para los tags)
//     #define OVNI_PLUGIN_TAG        "[pulsar]"                  // tag Catch2 del plugin
//   e incluye su PluginProcessor.h. Las primitivas de acá NO dependen del tipo concreto: reciben el
//   processor por referencia a ovni::PluginProcessorBase (el chasis común) o por template, así que este
//   header puede incluirse sin que OVNI_PLUGIN_PROCESSOR esté definido (lo usan también los *Stub.h).
// ========================================================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

#include "template/PluginProcessorBase.h"

namespace ovni::test
{

// ── Material de prueba determinístico (semilla fija → reproducible en cualquier runner) ─────────────────

// Ruido blanco (LCG) en [−1,1]. Semilla fija.
struct White
{
    std::uint32_t s = 0xCAFEF00Du;
    explicit White (std::uint32_t seed = 0xCAFEF00Du) : s (seed) {}
    float next() { s = s * 1664525u + 1013904223u; return ((float) (s >> 9) * (1.0f / 4194304.0f)) - 1.0f; }
};

// Ruido rosa (Paul Kellet "economy" sobre un LCG). MISMA implementación que StereoMeasure/MeasureTest →
// los números de imagen son comparables entre plugins.
struct Pink
{
    std::uint32_t s = 0x13572468u;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    explicit Pink (std::uint32_t seed = 0x13572468u) : s (seed) {}
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

// Transientes AGRESIVAS (el peor caso para crackle/true-peak): impulsos full-scale ±1 aislados (banda ancha)
// + click HF (ruido con decaimiento rápido), espaciados con silencio. Determinístico por muestra global.
// Idéntico al material de plugins/nebula/tests/TransientTest.cpp → la medición de transientes es uniforme.
struct Transients
{
    White w;
    int   periodSamp;
    int   hitLen;
    explicit Transients (double sr) : periodSamp ((int) std::lround (0.18 * sr)),
                                       hitLen     ((int) std::lround (0.004 * sr)) {}
    float sample (long g)
    {
        const int phase = (int) (g % periodSamp);
        if (phase >= hitLen) return 0.0f;
        const long hitIdx = g / periodSamp;
        White hw; hw.s = 0x9E3779B9u ^ (std::uint32_t) (hitIdx * 2654435761u);
        for (int k = 0; k < phase; ++k) hw.next();
        float x = 0.0f;
        if (phase < 2) x = (phase == 0) ? 1.0f : -1.0f;
        const float env = std::exp (-6.0f * (float) phase / (float) hitLen);
        x += 0.9f * env * hw.next();
        return juce::jlimit (-1.0f, 1.0f, x);
    }
};

// ── Helpers de parámetros / corrida por el processor REAL ────────────────────────────────────────────────

// Setea un parámetro normalizado (0..1) por id si existe (no rompe si el plugin no lo tiene).
inline void setParam (ovni::PluginProcessorBase& proc, const char* id, float norm01)
{
    if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (norm01);
}

// Llena un AudioBuffer estéreo con la misma señal mono en ambos canales (generador funcional g(longGlobal)).
template <typename Gen>
inline void fillStereoMono (juce::AudioBuffer<float>& buf, long startSample, Gen&& g)
{
    const int n  = buf.getNumSamples();
    const int ch = buf.getNumChannels();
    for (int i = 0; i < n; ++i)
    {
        const float x = (float) g (startSample + i);
        for (int c = 0; c < ch; ++c) buf.setSample (c, i, x);
    }
}

// ── Medición de imagen estéreo (vectorscope/correlímetro pro) ──────────────────────────────────────────
// CORR/IACC = correlación de fase L/R (misma definición que el correlímetro de PULSAR y el IACC de HALO).
struct StereoImage
{
    double corr    = 0.0;   // = IACC: +1 mono/correlacionado · ~0 ancho · <0 fuera de fase
    double width   = 0.0;   // RMS(side)/RMS(mid): 0 mono · ↑ más ancho
    double balDb   = 0.0;   // balance de energía L vs R (≈0 centrado)
    double monoSumDb = 0.0; // nivel de L+R vs directo (0 ≈ sin pérdida al monoficar · muy neg = cancela)
    double rms     = 0.0;   // RMS del canal L (chequeo de energía: que la salida no sea silencio)
};

// Acumulador incremental de imagen estéreo. Alimentá con addBlock() y leé con finish().
class StereoImageMeter
{
public:
    void addSample (double l, double r)
    {
        sLL += l*l; sRR += r*r; sLR += l*r;
        const double mid = 0.5*(l+r), sd = 0.5*(l-r);
        sMid += mid*mid; sSide += sd*sd; sMono += (l+r)*(l+r); ++cnt;
    }
    void addBlock (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i) addSample ((double) L[i], (double) R[i]);
    }
    StereoImage finish() const
    {
        StereoImage m;
        m.corr      = sLR / (std::sqrt (sLL * sRR) + 1e-12);
        m.width     = std::sqrt (sSide / (sMid + 1e-12));
        m.balDb     = 10.0 * std::log10 ((sLL + 1e-12) / (sRR + 1e-12));
        const double rmsL    = std::sqrt (sLL / (double) juce::jmax (1L, cnt));
        const double rmsMono = std::sqrt (sMono / (double) juce::jmax (1L, cnt));
        m.monoSumDb = 20.0 * std::log10 ((rmsMono + 1e-12) / (2.0 * rmsL + 1e-12));
        m.rms       = rmsL;
        return m;
    }
private:
    double sLL=0,sRR=0,sLR=0,sMid=0,sSide=0,sMono=0; long cnt=0;
};

} // namespace ovni::test
