#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [alias][aurora] — piso de espurias del despliegue espectral, A MEDIDA (no el
// AliasStub: AURORA es STFT y hay que separar DOS números honestos):
//
//   1) ALIAS_DBFS=  (motion 0, despliegue QUIETO) — el piso de alias/espurias
//      REAL del proceso: el paneo por bin es LINEAL (ganancias reales constantes
//      por frame, fase intacta), así que lo único que puede ensuciar es el
//      leakage entre bins re-pesado por la curva + el OLA. ESTE es el número
//      del gate H7 (< −96 dBFS house-standard) y el que se publica como "alias
//      floor". El house-standard §3 ya declara el peaje "STFT a 0× OS" si el
//      material brillante lo excede: acá lo MEDIMOS.
//
//   2) MOTION_SIDEBAND_DBFS=  (motion 100 @ 8 Hz) — las BANDAS LATERALES de la
//      modulación del abanico. NO son alias: son el efecto haciendo lo que dice
//      (el ángulo varía a 8 Hz → sidebands a ±n·8 Hz alrededor del carrier, bajo
//      la cota anti-AM de 20 Hz). Se publican como el peaje honesto del MOTION
//      (clave distinta para que measure-check NUNCA las confunda con el alias).
//
// Método (mismo protocolo del sello): seno a −8 dBFS alineado al grid del FFT de
// análisis (16384, Hann), 40 bloques de warmup, espuria = mayor bin fuera de ±4
// bins de la fundamental (ignorando < 20 Hz), peor caso sobre el barrido de
// frecuencias y sobre AMBOS canales (el despliegue reparte el carrier L/R).
// =============================================================================

namespace
{
namespace pid = aurora::params::id;

constexpr int    kFftN   = 16384;
constexpr double kSR     = 48000.0;
constexpr int    kBlk    = 512;
constexpr float  kAmp    = 0.398107f;   // −8 dBFS

// Peor espuria (dBFS) del processor con la config dada, barriendo las frecuencias del sello.
// Imprime el perfil POR FRECUENCIA (documenta DÓNDE vive el peaje del STFT 0× OS).
float worstSpurDbfs (float motion01, float motionRateNorm, const char* label)
{
    const double freqs[] = { 110.0, 440.0, 1000.0, 4000.0, 8000.0, 12000.0 };
    float worst = -300.0f;

    for (const double fWanted : freqs)
    {
        aurora::AuroraProcessor proc;
        auto set = [&] (const char* id, float v01) {
            if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
        };
        set (pid::SPREAD,      1.0f);
        set (pid::TILT,        1.0f);    // +100: el mapeo más agresivo (curva más convexa)
        set (pid::MONOSAFEAMT, 0.0f);
        set (pid::MIX,         1.0f);
        set (pid::MOTION,      motion01);
        set (pid::MOTIONRATE,  motionRateNorm);

        proc.prepareToPlay (kSR, kBlk);

        // Frecuencia alineada al grid del FFT de medición (bin entero → sin leakage propio).
        const int    bin  = juce::jmax (1, juce::roundToInt (fWanted * kFftN / kSR));
        const double freq = (double) bin * kSR / kFftN;

        std::vector<float> capL ((size_t) kFftN, 0.0f), capR ((size_t) kFftN, 0.0f);
        long g = 0; int captured = 0;
        const int warmBlocks = 40;
        for (int blk = 0; captured < kFftN; ++blk)
        {
            juce::AudioBuffer<float> buf (2, kBlk); juce::MidiBuffer midi;
            for (int n = 0; n < kBlk; ++n)
            {
                const float x = kAmp * (float) std::sin (juce::MathConstants<double>::twoPi * freq * (double) (g + n) / kSR);
                buf.setSample (0, n, x); buf.setSample (1, n, x);
            }
            g += kBlk;
            proc.processBlock (buf, midi);
            if (blk < warmBlocks) continue;
            const int take = juce::jmin (kBlk, kFftN - captured);
            for (int n = 0; n < take; ++n)
            {
                capL[(size_t) (captured + n)] = buf.getSample (0, n);
                capR[(size_t) (captured + n)] = buf.getSample (1, n);
            }
            captured += take;
        }

        // FFT 16384 con Hann sobre cada canal; espuria = mayor bin fuera de ±4 de la
        // fundamental, ignorando < 20 Hz. Normalización coherente Hann (ganancia 0.5).
        juce::dsp::FFT fft (14);
        std::vector<float> win ((size_t) kFftN);
        for (int n = 0; n < kFftN; ++n)
            win[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) (kFftN - 1)));

        float worstHere = -300.0f;
        for (const auto* cap : { &capL, &capR })
        {
            std::vector<float> fd ((size_t) (2 * kFftN), 0.0f);
            for (int n = 0; n < kFftN; ++n) fd[(size_t) n] = (*cap)[(size_t) n] * win[(size_t) n];
            fft.performRealOnlyForwardTransform (fd.data(), true);

            const int minBin = (int) std::ceil (20.0 * kFftN / kSR);   // ignorar < 20 Hz
            for (int k = minBin; k <= kFftN / 2; ++k)
            {
                if (std::abs (k - bin) <= 4) continue;                 // ±4 bins de la fundamental
                const float re = fd[(size_t) (2 * k)], im = fd[(size_t) (2 * k + 1)];
                const float amp = 2.0f * std::sqrt (re * re + im * im) / ((float) kFftN * 0.5f);
                const float db  = 20.0f * std::log10 (juce::jmax (amp, 1.0e-12f));
                worstHere = juce::jmax (worstHere, db);
            }
        }
        std::printf ("ALIAS_PROFILE[%s] f=%.0fHz spur=%.2f dBFS\n", label, freq, worstHere);
        worst = juce::jmax (worst, worstHere);
    }
    return worst;
}
} // namespace

TEST_CASE ("AURORA alias: piso LINEAL quieto (gate) + sidebands del MOTION (peaje declarado)", "[alias][aurora]")
{
    // 1) Despliegue QUIETO (motion 0): el número del gate H7 y el que se publica.
    const float aliasStill = worstSpurDbfs (0.0f, 0.0f, "still");

    // 2) MOTION máximo (100 @ 8 Hz FREE): sidebands de la modulación — el peaje honesto.
    const float motionSb = worstSpurDbfs (1.0f, 1.0f, "motion");

    std::printf ("MOTION_SIDEBAND_DBFS=%.2f\n", motionSb);   // peaje del MOTION (clave propia)
    std::printf ("ALIAS_DBFS=%.2f\n", aliasStill);           // <- el gate (measure-check toma el ÚLTIMO match)

    REQUIRE (std::isfinite (aliasStill));
    REQUIRE (std::isfinite (motionSb));

    // GATE de cordura local (el fino lo evalúa validate.sh con ALIAS_FLOOR): el paneo por
    // bin es lineal → el piso quieto debe estar holgadamente bajo el house-standard.
    REQUIRE (aliasStill < -96.0f);

    // Las sidebands del MOTION existen (el abanico late: NO es un control muerto) pero no
    // ensucian más que un tremolo musical (cota anti-AM cableada a < 20 Hz).
    REQUIRE (motionSb < -6.0f);
    REQUIRE (motionSb > aliasStill);   // documenta la diferencia: el peaje ES del motion
}
