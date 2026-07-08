#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [alias][horizon] — piso de espurias del eje espectral, A MEDIDA (HORIZON es STFT;
// hay que separar DOS números honestos):
//
//   1) ALIAS_DBFS=  (freeze OFF, whisper 0) — el piso de alias/espurias REAL del
//      proceso. Con freeze OFF el WET es IDENTITY (el pipeline FFT→IFFT corre igual,
//      nadie toca los bins) → lo único que puede ensuciar es la reconstrucción
//      OLA/COLA. ESTE es el número del gate H7 (< −96 dBFS house-standard) y el que
//      se publica como "alias floor" (el peaje "STFT a 0× OS" del house-standard §3).
//
//   2) WHISPER_TOLL_DBFS=  (freeze ON, whisper 100) — el ENSANCHAMIENTO de banda de
//      la whisperization. NO es alias: es el efecto haciendo lo que dice (randomiza
//      la fase por frame → la energía se difunde alrededor de la fundamental). Se
//      publica como el peaje honesto del WHISPER (clave distinta para que
//      measure-check NUNCA lo confunda con el alias).
//
// Método (protocolo del sello): seno a −8 dBFS alineado al grid del FFT de medición
// (16384, Hann), warmup amplio (captura + cross-fade del freeze), espuria = mayor bin
// fuera de ±4 bins de la fundamental (ignorando < 20 Hz), peor caso sobre el barrido
// y sobre AMBOS canales.
// =============================================================================

namespace
{
namespace pid = horizon::params::id;

constexpr int    kFftN = 16384;
constexpr double kSR   = 48000.0;
constexpr int    kBlk  = 512;
constexpr float  kAmp  = 0.398107f;   // −8 dBFS

// Peor espuria (dBFS) del processor con la config dada, barriendo las frecuencias del sello.
float worstSpurDbfs (bool freeze, float whisper01, const char* label)
{
    const double freqs[] = { 110.0, 440.0, 1000.0, 4000.0, 8000.0, 12000.0 };
    float worst = -300.0f;

    for (const double fWanted : freqs)
    {
        horizon::HorizonProcessor proc;
        auto set = [&] (const char* id, float v01) {
            if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (v01);
        };
        set (pid::FREEZE,  freeze ? 1.0f : 0.0f);
        set (pid::WHISPER, whisper01);
        set (pid::SPREAD,  0.0f);    // mono (la espuria no depende del paneo de fase)
        set (pid::MIX,     1.0f);    // wet puro (aislar el camino espectral)

        proc.prepareToPlay (kSR, kBlk);

        const int    bin  = juce::jmax (1, juce::roundToInt (fWanted * kFftN / kSR));
        const double freq = (double) bin * kSR / kFftN;

        std::vector<float> capL ((size_t) kFftN, 0.0f), capR ((size_t) kFftN, 0.0f);
        long g = 0; int captured = 0;
        const int warmBlocks = 80;   // captura del freeze + cross-fade + latencia OLA
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

TEST_CASE ("HORIZON alias: piso IDENTITY del STFT (gate) + peaje del WHISPER (declarado)", "[alias][horizon]")
{
    // 1) Freeze OFF + whisper 0: el WET es identity → el piso de reconstrucción OLA (gate H7).
    const float aliasFloor = worstSpurDbfs (false, 0.0f, "identity");

    // 2) Freeze ON + whisper 100: el ensanchamiento de banda de la whisperization (peaje honesto).
    const float whisperToll = worstSpurDbfs (true, 1.0f, "whisper");

    std::printf ("WHISPER_TOLL_DBFS=%.2f\n", whisperToll);   // peaje del whisper (clave propia)
    std::printf ("ALIAS_DBFS=%.2f\n", aliasFloor);           // <- el gate (measure-check toma el ÚLTIMO match)

    REQUIRE (std::isfinite (aliasFloor));
    REQUIRE (std::isfinite (whisperToll));

    // GATE de cordura local (el fino lo evalúa validate.sh con ALIAS_FLOOR): el camino
    // identity del STFT debe estar holgadamente bajo el house-standard.
    REQUIRE (aliasFloor < -96.0f);

    // El whisper a 100 ensancha la banda de verdad (NO es un control muerto) — su "espuria"
    // (energía difundida) está MUY por encima del piso identity (documenta que es el peaje).
    REQUIRE (whisperToll > aliasFloor);
}
