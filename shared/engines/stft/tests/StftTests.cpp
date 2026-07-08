// StftTests.cpp — [stft] StftEngine: la plomería STFT/OLA compartida (AURORA siembra, HORIZON reusa).
//
// ⚠ Los nombres de TEST_CASE van en "stft ..." MINÚSCULA a propósito: CTest registra el NOMBRE
// Catch2 y los filtros del pipeline (`ctest -R 'aurora|stft|...'`) son case-sensitive — con
// "STFT" mayúscula el orquestador los salteaba EN SILENCIO (hallazgo review 2026-06-10).
//
//   1. Identity null:  seno+ruido con FrameProcessor identity → alineado por latencySamples(),
//                      error RMS vs la entrada < −90 dB (reconstrucción perfecta, COLA exacta).
//   2. COLA:           entrada constante (DC) → salida PLANA (rizado < 0.01 dB) y al NIVEL exacto
//                      (la normalización 1/1.5 de Hann² @ 75% está bien horneada).
//   3. Latencia:       impulso → pico de cross-correlación EXACTO en latencySamples() (±0),
//                      con N default (2048) y con N=1024 (N configurable de verdad).
//   4. Robustez:       block sizes 32/64/333/512/2048 mezclados → MISMA salida que bloques fijos
//                      (el pipeline no depende del troceo del host), sin NaN.
//   5. Ley de potencia: un FrameProcessor que reparte la magnitud L/R por bin con ley de potencia
//                      conserva la energía total (±0.1 dB) — medido con el mecanismo ENCENDIDO
//                      (se verifica además que el reparto alteró la señal de verdad: no hay falso verde).
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engines/stft/StftEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

using ovni::engines::StftEngine;

namespace {

constexpr double kSr = 48000.0;

// Señal determinística seno 440 Hz + ruido blanco (semilla fija → corrida reproducible).
std::vector<float> makeSineNoise (int len)
{
    juce::Random rng ((juce::int64) 0x51F7AA);
    std::vector<float> v ((size_t) len);
    for (int n = 0; n < len; ++n)
        v[(size_t) n] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * n / kSr)
                      + 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
    return v;
}

struct StereoOut { std::vector<float> L, R; };

// Corre la señal (mono, duplicada L/R) por el engine en bloques tomados CÍCLICAMENTE de
// 'blockSizes' (un solo tamaño = bloques fijos). Devuelve la salida completa por canal.
StereoOut runStereo (StftEngine& eng, const std::vector<float>& in, const std::vector<int>& blockSizes)
{
    StereoOut out;
    out.L.reserve (in.size());
    out.R.reserve (in.size());
    int pos = 0;
    size_t bi = 0;
    const int total = (int) in.size();
    while (pos < total)
    {
        const int bs = juce::jmin (blockSizes[bi++ % blockSizes.size()], total - pos);
        juce::AudioBuffer<float> buf (2, bs);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (in.begin() + pos, in.begin() + pos + bs, buf.getWritePointer (ch));
        eng.process (buf);
        for (int j = 0; j < bs; ++j)
        {
            out.L.push_back (buf.getSample (0, j));
            out.R.push_back (buf.getSample (1, j));
        }
        pos += bs;
    }
    return out;
}

// Null en dB: energía del error (test[offset+n] − ref[n]) sobre la energía de ref, n = 0..count.
double nullDb (const std::vector<float>& ref, const std::vector<float>& test, int offset, int count)
{
    double err = 0.0, refE = 0.0;
    for (int n = 0; n < count; ++n)
    {
        const double d = (double) test[(size_t) (offset + n)] - (double) ref[(size_t) n];
        err  += d * d;
        refE += (double) ref[(size_t) n] * (double) ref[(size_t) n];
    }
    return 10.0 * std::log10 ((err + 1.0e-30) / (refE + 1.0e-30));
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v)
        if (! std::isfinite (x)) return false;
    return true;
}

} // namespace

// ------------------------------------------------------------------------------- 1. identity null
TEST_CASE ("stft identity: null < -90 dB alineando por latencySamples()", "[stft]")
{
    StftEngine eng;
    int framesSeen = 0;   // prueba que el camino del FrameProcessor corre de verdad
    eng.setFrameProcessor ([&framesSeen] (const StftEngine::FrameView& f)
    {
        ++framesSeen;
        REQUIRE (f.numChannels == 2);
        REQUIRE (f.numBins == f.fftSize / 2 + 1);
    });
    eng.prepare (kSr, 512, 2);

    REQUIRE (eng.latencySamples() == StftEngine::kDefaultFftSize);
    REQUIRE (eng.colaRippleDb() < 1.0e-4);   // COLA verificada en prepare (Hann periódica)

    const int lat = eng.latencySamples();
    const int len = (int) kSr * 2;           // 2 s de seno+ruido
    const auto in = makeSineNoise (len + lat + eng.hopSize());
    const auto out = runStereo (eng, in, { 512 });

    REQUIRE (framesSeen > 0);
    REQUIRE (allFinite (out.L));
    REQUIRE (allFinite (out.R));

    const double nullL = nullDb (in, out.L, lat, len);
    const double nullR = nullDb (in, out.R, lat, len);
    std::printf ("STFT_NULL_DB=%.1f\n", juce::jmax (nullL, nullR));
    REQUIRE (nullL < -90.0);
    REQUIRE (nullR < -90.0);
}

// ----------------------------------------------------------------------------------------- 2. COLA
TEST_CASE ("stft COLA: entrada constante -> salida plana al nivel exacto (rizado < 0.01 dB)", "[stft]")
{
    StftEngine eng;   // sin FrameProcessor: identity por default (MISMO pipeline FFT->IFFT)
    eng.prepare (kSr, 512, 2);

    const int N    = eng.fftSize();
    const int lat  = eng.latencySamples();
    const float dc = 0.5f;
    const std::vector<float> in ((size_t) (lat + 8 * N), dc);
    const auto out = runStereo (eng, in, { 512 });

    // Tras asentarse (latencia + 2N de margen), la salida debe ser PLANA: si la suma de
    // ventanas no fuera constante, acá se vería como modulación de amplitud periódica al hop.
    const int from = lat + 2 * N;
    const int count = 4 * N;
    float mn = 1.0e30f, mx = -1.0e30f;
    double sum = 0.0;
    for (int n = from; n < from + count; ++n)
    {
        const float y = out.L[(size_t) n];
        mn = juce::jmin (mn, y);
        mx = juce::jmax (mx, y);
        sum += (double) y;
    }
    const double rippleDb = 20.0 * std::log10 ((double) mx / juce::jmax ((double) mn, 1.0e-30));
    const double levelDb  = 20.0 * std::log10 ((sum / count) / (double) dc);
    std::printf ("STFT_COLA_RIPPLE_DB=%.6f LEVEL_ERR_DB=%.6f\n", rippleDb, levelDb);
    REQUIRE (rippleDb < 0.01);              // sin modulación de amplitud (COLA)
    REQUIRE (std::abs (levelDb) < 0.01);    // normalización exacta (no plana-pero-corrida)
}

// ------------------------------------------------------------------------------------- 3. latencia
TEST_CASE ("stft latencia: pico de cross-correlacion EXACTO en latencySamples()", "[stft]")
{
    // El mismo chequeo para el N default y para otro N: el accesor debe seguir al pipeline.
    for (const int fftSize : { StftEngine::kDefaultFftSize, 1024 })
    {
        StftEngine eng;
        eng.prepare (kSr, 512, 2, fftSize);
        REQUIRE (eng.latencySamples() == fftSize);

        const int P = 1000;   // posición del impulso
        std::vector<float> in ((size_t) (P + 4 * fftSize), 0.0f);
        in[(size_t) P] = 1.0f;
        const auto out = runStereo (eng, in, { 512 });

        // Cross-correlación entrada↔salida: c(lag) = Σ in[n]·out[n+lag]. El pico debe caer
        // EXACTAMENTE en latencySamples() (±0) y reconstruir el impulso (COLA → ganancia 1).
        const int maxLag = 3 * fftSize;
        int bestLag = -1;
        double bestC = -1.0e30;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double c = 0.0;
            const int count = (int) in.size() - lag;
            for (int n = 0; n < count; ++n)
                c += (double) in[(size_t) n] * (double) out.L[(size_t) (n + lag)];
            if (c > bestC) { bestC = c; bestLag = lag; }
        }
        std::printf ("STFT_LATENCY N=%d reportada=%d xcorr=%d pico=%.4f\n",
                     fftSize, eng.latencySamples(), bestLag, bestC);
        REQUIRE (bestLag == eng.latencySamples());
        REQUIRE (bestC > 0.99);   // el impulso vuelve entero (no un alias del pico)
    }
}

// ------------------------------------------------------------------------------------- 4. robustez
TEST_CASE ("stft robustez: blocks 32/64/333/512/2048 mezclados -> misma salida, sin NaN", "[stft]")
{
    const int len = (int) kSr + StftEngine::kDefaultFftSize;   // ~1 s + latencia
    const auto in = makeSineNoise (len);

    StftEngine ref, mix;
    ref.prepare (kSr, 2048, 2);
    mix.prepare (kSr, 2048, 2);

    const auto outRef = runStereo (ref, in, { 512 });
    const auto outMix = runStereo (mix, in, { 32, 64, 333, 512, 2048 });

    REQUIRE (allFinite (outMix.L));
    REQUIRE (allFinite (outMix.R));
    REQUIRE (outMix.L.size() == outRef.L.size());

    // El estado del pipeline depende del conteo GLOBAL de samples, no del troceo del host →
    // la salida debe coincidir sample a sample (tolerancia numérica de suma flotante).
    float maxDiff = 0.0f;
    for (size_t n = 0; n < outRef.L.size(); ++n)
        maxDiff = juce::jmax (maxDiff,
                              std::abs (outMix.L[n] - outRef.L[n]),
                              std::abs (outMix.R[n] - outRef.R[n]));
    std::printf ("STFT_BLOCKSIZE_MAXDIFF=%.3e\n", (double) maxDiff);
    REQUIRE (maxDiff < 1.0e-6f);
}

// ----------------------------------------------------------------- 5. ley de potencia por bin
TEST_CASE ("stft reparto L/R por bin con ley de potencia: conserva la energia total (+-0.1 dB)", "[stft]")
{
    // Mecanismo ENCENDIDO: cada bin k se reparte L/R con ley de potencia (gL²+gR²=1) según un
    // paneo que barre de izquierda (DC) a derecha (Nyquist). Entrada mono duplicada → la energía
    // del PAR por bin se conserva exacta: |L'|²+|R'|² = 2(gL²+gR²)|s|² = |L|²+|R|².
    StftEngine eng;
    eng.setFrameProcessor ([] (const StftEngine::FrameView& f)
    {
        const float root2 = juce::MathConstants<float>::sqrt2;
        float* L = f.spectra[0];
        float* R = f.spectra[1];
        for (int k = 0; k < f.numBins; ++k)
        {
            const float pan = (float) k / (float) (f.numBins - 1);            // 0=DC..1=Nyquist
            const float th  = pan * juce::MathConstants<float>::halfPi;
            const float gL  = root2 * std::cos (th);
            const float gR  = root2 * std::sin (th);
            const float re  = L[2 * k], im = L[2 * k + 1];   // L==R (entrada mono dual): s = bin L
            L[2 * k] = gL * re;  L[2 * k + 1] = gL * im;     // fase PRESERVADA (sólo magnitud)
            R[2 * k] = gR * re;  R[2 * k + 1] = gR * im;
        }
    });
    eng.prepare (kSr, 512, 2);

    const int lat = eng.latencySamples();
    const int len = (int) kSr * 2;   // 2 s de ruido (estacionario: bordes despreciables)
    juce::Random rng ((juce::int64) 0xB1A5E5);
    std::vector<float> in ((size_t) (len + lat + eng.hopSize()));
    for (auto& x : in) x = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);

    const auto out = runStereo (eng, in, { 512 });
    REQUIRE (allFinite (out.L));
    REQUIRE (allFinite (out.R));

    // Energía total del par, alineada por latencia: salida [lat, lat+len) vs entrada [0, len).
    double eIn = 0.0, eOut = 0.0;
    for (int n = 0; n < len; ++n)
    {
        const double x = (double) in[(size_t) n];
        const double l = (double) out.L[(size_t) (n + lat)];
        const double r = (double) out.R[(size_t) (n + lat)];
        eIn  += 2.0 * x * x;   // el par de entrada es (x, x)
        eOut += l * l + r * r;
    }
    const double deltaDb = 10.0 * std::log10 ((eOut + 1.0e-30) / (eIn + 1.0e-30));
    std::printf ("STFT_PAN_ENERGY_DELTA_DB=%.4f\n", deltaDb);
    REQUIRE (std::abs (deltaDb) < 0.1);

    // Anti-falso-verde: el reparto tiene que haber ALTERADO la señal de verdad (si el
    // FrameProcessor no corriera, este null daría -inf y la conservación sería trivial).
    const double alteredDb = nullDb (in, out.L, lat, len);
    const double lrDiffDb  = [&]
    {
        double d = 0.0, e = 0.0;
        for (int n = 0; n < len; ++n)
        {
            const double diff = (double) out.L[(size_t) (n + lat)] - (double) out.R[(size_t) (n + lat)];
            const double x    = (double) in[(size_t) n];
            d += diff * diff;
            e += x * x;
        }
        return 10.0 * std::log10 ((d + 1.0e-30) / (e + 1.0e-30));
    }();
    std::printf ("STFT_PAN_ALTERED_DB=%.1f LR_DIFF_DB=%.1f\n", alteredDb, lrDiffDb);
    REQUIRE (alteredDb > -30.0);   // L de salida YA NO es la entrada (el mecanismo actuó)
    REQUIRE (lrDiffDb  > -30.0);   // y L != R (reparto direccional real, no un passthrough)
}
