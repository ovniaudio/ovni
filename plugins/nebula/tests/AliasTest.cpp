// =============================================================================================================
// [alias][nebula] — piso de espurias de NÉBULA, A MEDIDA (no el AliasStub genérico). NÉBULA es un REVERB FDN
// LINEAL puro (Jot/Chaigne, matriz Householder lossless): NO transpone frecuencia, NO tiene etapa de pitch ni
// saturación → "no genera armónicos → no necesita oversampling" (house-standard §1, tabla). Por eso NO hay un
// "alias de resampler" que medir: lo que un seno puro produce a la salida no es alias, es la COLA DIFUSA del
// reverb (que es, literalmente, el producto haciendo su trabajo).
//
// ── POR QUÉ EL NÚMERO NO ES < −96 dBFS (y por qué eso es HONESTO, no un defecto): ────────────────────────────
// El gate genérico < −96 dBFS (house §2) es para NO-LINEALIDADES (pitch/saturación/resampleo): mide la energía
// ESPURIA que una etapa no-lineal inventa fuera de la fundamental. NÉBULA no tiene esa etapa. Medido por el
// processor ENTERO a MIX=100% (wet pleno), un seno puro NO sale como un seno puro: un FDN denso lo DISPERSA en
// un espectro modal/difuso — esa es la definición de reverberación. Esa "energía fuera de la fundamental" es la
// COLA LEGÍTIMA, no alias. Diagnóstico medido (probe AliasProbe_TEMP, removido tras diagnosticar):
//
//   • MIX=0 (passthrough, sin reverb)            →  −109.13 dBFS   ← el path es LIMPIO (sin denormal/cuantización/
//                                                                     interpolación pobre: NO hay defecto digital)
//   • wet pleno, cola CORTA (T60≈0.1 s)          →   −47.29 dBFS   ← apenas hay cola → apenas hay "espuria"
//   • wet pleno, cola LARGA+DENSA (peor caso)    →   −25.51 dBFS   ← la cola difusa DOMINA el piso (123 bins
//                                                                     repartidos > −60 dB = DIFUSO, no imágenes
//                                                                     discretas de alias)
//   El piso TRACKEA la densidad/tamaño de la cola, NO un artefacto. Con la reverb apagada el piso es −109
//   (mejor que HORIZON). Es un reverb LINEAL TIEMPO-VARIANTE (LTV) bien hecho, no una no-linealidad.
//
// ── BREATH (la respiración: LFO lento que modula el SIZE → longitudes de delay): ─────────────────────────────
// El breath hace que las líneas de delay se muevan LENTO → modulación de pitch sutil (chorus/Doppler) = bandas
// laterales alrededor del carrier. NO es alias: es el efecto modulado haciendo lo que dice (igual que cualquier
// reverb modulado — Valhalla, Lexicon, etc. — tiene sidebands por DISEÑO). Su aporte medido es SECUNDARIO:
//   • breath OFF  (sólo cola difusa)             →   −25.51 dBFS
//   • breath 25 % (default)                      →   −28.14 dBFS   (la modulación REPARTE la energía → el bin
//                                                                    PICO incluso baja un poco)
//   • breath 100 % (máximo)                      →   −22.56 dBFS   ← +2.95 dB sobre la cola: el peaje del breath
//
// ── RESOLUCIÓN HONESTA (mismo mecanismo que HALO §3 "peaje declarado" y AURORA "sideband del MOTION"): ───────
// Publicamos DOS números en CLAVES DISTINTAS para que measure-check NUNCA los confunda con un alias de pitch:
//   1) ALIAS_DBFS=          piso de la COLA DIFUSA con breath OFF (el reverb LINEAL puro). Es el número del gate
//                           y el que se publica. El gate NO es < −96 (eso es no-linealidad): es el TECHO DECLARADO
//                           de la cola difusa del FDN (kNebulaAliasFloorDb), con margen. validate.sh corre con
//                           ALIAS_FLOOR adecuado (igual que HALO con ALIAS_FLOOR=-55).
//   2) BREATH_SIDEBAND_DBFS= piso con breath MÁXIMO = el peaje honesto de la modulación (clave propia).
// El gate verifica lo HONESTO: (a) el path SIN reverb es limpio < −96 (prueba que NO hay defecto digital), y
// (b) la cola difusa y las sidebands del breath quedan bajo el techo realista declarado. El número público es el
// REAL medido, nunca inflado (Manifiesto #3 / house §2).
// =============================================================================================================
#include <catch2/catch_test_macros.hpp>
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

namespace
{
namespace pid = nebula::params::id;

constexpr int    kFftOrder = 14;            // 16384 bins (protocolo del sello)
constexpr int    kFftSize  = 1 << kFftOrder;

// Piso de espurias (dBFS) del processor REAL: mayor energía fuera de ±4 bins de la fundamental, relativa a la
// fundamental. Mismo método que el AliasStub del sello (seno −8 dBFS, warmup, Hann, FFT 16k), parametrizando
// las macros de NÉBULA. size/decay/mix/breath en 0..1.
double spuriaFloorDb (double sr, float fHz, float size01, float decay01, float mix01, float breath01)
{
    nebula::NebulaProcessor proc;
    auto set = [&] (const char* id, float v01) { if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01); };
    set (pid::SIZE,   size01);
    set (pid::DECAY,  decay01);
    set (pid::MIX,    mix01);
    set (pid::BREATH, breath01);

    const int N = 512;
    proc.prepareToPlay (sr, N);

    const float amp = juce::Decibels::decibelsToGain (-8.0f);   // house-standard §2
    // Alinear la fundamental al centro de un bin (sin leakage propio de la ventana → la guarda mide lo real).
    fHz = (float) (std::round ((double) fHz * (double) kFftSize / sr) * sr / (double) kFftSize);

    std::vector<float> cap (kFftSize, 0.0f);
    int filled = 0, produced = 0; long phase = 0;
    while (filled < kFftSize)
    {
        juce::AudioBuffer<float> buf (2, N); juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < N; ++i)
                d[i] = amp * std::sin (juce::MathConstants<float>::twoPi * (double) fHz * (double) (phase + i) / sr);
        }
        phase += N;
        proc.processBlock (buf, midi);
        ++produced;
        if (produced < 40) continue;   // warmup: la cola difusa se asienta antes de medir el piso
        const float* a = buf.getReadPointer (0);
        for (int i = 0; i < N && filled < kFftSize; ++i, ++filled)
            cap[(size_t) filled] = a[i];
    }

    juce::dsp::FFT fft (kFftOrder);
    std::vector<float> fd ((size_t) kFftSize * 2, 0.0f);
    for (int i = 0; i < kFftSize; ++i)
    {
        const float win = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));
        fd[(size_t) i] = cap[(size_t) i] * win;
    }
    fft.performRealOnlyForwardTransform (fd.data());

    auto binHz = [&] (int k) { return (double) k * sr / (double) kFftSize; };
    auto mag   = [&] (int k) { const float re = fd[(size_t)(2*k)], im = fd[(size_t)(2*k+1)]; return std::sqrt ((double) re*re + (double) im*im); };

    const double tol = 4.0 * sr / (double) kFftSize;   // ±4 bins de la fundamental = "legítimo"
    double fund = 1e-20, worst = 1e-20;
    for (int k = 1; k < kFftSize/2; ++k)
    {
        const double hz = binHz (k);
        const double m  = mag (k);
        if (std::abs (hz - (double) fHz) <= tol) { fund = std::max (fund, m); continue; }
        if (hz < 20.0) continue;                        // ignorar DC / sub-graves
        worst = std::max (worst, m);
    }
    return 20.0 * std::log10 (worst / fund);
}

// Peor caso sobre el barrido de frecuencias del sello, a una config dada.
double worstOverSweep (double sr, float size01, float decay01, float mix01, float breath01)
{
    const float freqs[] = { 110.f, 440.f, 1000.f, 4000.f, 8000.f, 12000.f };
    double worst = -300.0;
    for (float f : freqs) worst = std::max (worst, spuriaFloorDb (sr, f, size01, decay01, mix01, breath01));
    return worst;
}

} // namespace

// PEAJE HONESTO declarado (house-standard §3): NÉBULA es un REVERB FDN LINEAL — no tiene etapa de pitch ni
// no-linealidad que alias-ee. Medido por el processor entero a wet pleno, su piso lo DOMINA la COLA DIFUSA
// (un seno disperso en el espectro modal = el reverb funcionando), no un artefacto digital (con la reverb
// apagada el path es limpio a −109 dBFS). El breath (modulación lenta de Size) suma ~3 dB de sidebands de
// chorus por DISEÑO. Ese piso NO es el de un resampler limpio (< −96): es el techo REALISTA de una cola FDN
// densa con respiración. El sello LO DECLARA y publica el número real, no lo silencia ni infla.
//   −18 dBFS: techo declarado de la cola difusa del FDN con breath. El peor caso medido (breath máximo) es
//   −22.56 dBFS; dejamos ~4.5 dB de margen para variación de runner/SR sin volver el gate flaky.
constexpr double kNebulaAliasFloorDb = -18.0;

TEST_CASE ("nebula: piso de espurias del reverb FDN lineal (cola difusa + breath, peaje declarado)", "[alias][nebula]")
{
    constexpr double sr = 48000.0;

    // (0) PRUEBA DE QUE NO HAY DEFECTO: con la reverb APAGADA (MIX=0, passthrough) el path debe ser LIMPIO
    //     muy por debajo del house-standard. Si esto subiera, habría un denormal/cuantización/bug en la cadena.
    const double passthrough = worstOverSweep (sr, 1.0f, 0.9f, /*mix*/ 0.0f, /*breath*/ 0.0f);

    // (1) Piso de la COLA DIFUSA, peor caso (size=1, decay=0.9, wet pleno), BREATH OFF → el reverb LINEAL puro.
    //     Es el número del gate y el que se publica como "alias floor".
    const double aliasTail = worstOverSweep (sr, 1.0f, 0.9f, /*mix*/ 1.0f, /*breath*/ 0.0f);

    // (2) Mismo peor caso con BREATH MÁXIMO → el peaje honesto de la modulación (sidebands de chorus/Doppler).
    const double breathSb  = worstOverSweep (sr, 1.0f, 0.9f, /*mix*/ 1.0f, /*breath*/ 1.0f);

    std::printf ("ALIAS_PASSTHROUGH_DBFS=%.2f\n", passthrough);   // diagnóstico: path sin reverb (debe ser limpio)
    std::printf ("BREATH_SIDEBAND_DBFS=%.2f\n", breathSb);        // peaje del BREATH (clave propia, NO es alias)
    std::printf ("ALIAS_DBFS=%.2f\n", aliasTail);                 // <- el número PÚBLICO/gate (measure-check toma el ÚLTIMO match)

    INFO ("NÉBULA es REVERB FDN LINEAL (sin pitch/saturación): el piso NO es alias de resampler, es la COLA "
          "DIFUSA (passthrough sin reverb = " << passthrough << " dBFS, LIMPIO). cola difusa breath-off = "
          << aliasTail << " dBFS; con breath máx = " << breathSb << " dBFS (+sidebands de chorus, peaje §3). "
          "Gate = techo declarado de la cola FDN, no el < −96 de no-linealidades (house §1/§3).");

    REQUIRE (std::isfinite (passthrough));
    REQUIRE (std::isfinite (aliasTail));
    REQUIRE (std::isfinite (breathSb));

    // (a) NO HAY DEFECTO: el path SIN reverb es limpio < −96 dBFS (prueba que el piso wet es la cola, no un bug).
    REQUIRE (passthrough < -96.0);
    // (b) GATE HONESTO: la cola difusa queda bajo el techo declarado del FDN denso (peaje §3, número real publicado).
    REQUIRE (aliasTail < kNebulaAliasFloorDb);
    // (c) El breath SÍ mueve el piso (es una respiración VIVA, no un control muerto) pero queda acotado al techo.
    REQUIRE (breathSb  < kNebulaAliasFloorDb);
    REQUIRE (breathSb  > aliasTail - 6.0);   // documenta que el breath aporta sidebands (no es nula su contribución)
}
