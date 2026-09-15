#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

// ========================================================================================================
// SpectrumFrame — el espectro que dibujan SPECTRUM y SPECTROGRAM (spec §5.2, prompt 50).
//
// Viaja por su PROPIO TripleBuffer, como el ScopeFrame y por el mismo motivo llevado al extremo: son
// ~260 KB de bins por slot. Meterlos en el AnalysisFrame haría que LOUDNESS copie 260 KB diez veces por
// segundo para dibujar tres números.
//
// POD de arrays FIJOS dimensionados al peor caso (orden 15 → 16 385 bins): cero allocations en el worker
// y cero sorpresas de tamaño al cambiar el orden de FFT en vivo. Lo que vale es el prefijo [0, numBins).
//
// REFERENCIA DE dB (§5.2, y el README lo explica entero):
//
//     dB_k = 20·log10( 2·|X_k| / (N · CG) )        con CG = Σw/N (ganancia coherente de la ventana)
//
// Un seno de amplitud A (pico) centrado en el bin k lee EXACTAMENTE 20·log10(A): 0 dBFS = seno de escala
// completa. Consecuencia: el ruido blanco a escala completa NO lee 0 dBFS por bin — su energía está
// repartida entre los N/2+1 bins, así que cuanto más grande la FFT, más abajo lee cada bin. Ningún
// analizador serio dice otra cosa; nosotros además lo escribimos.
// ========================================================================================================
namespace telescope
{
struct SpectrumFrame
{
    static constexpr int kMaxFftOrder = 15;
    static constexpr int kMaxFftSize  = 1 << kMaxFftOrder;      // 32 768
    static constexpr int kMaxBins     = kMaxFftSize / 2 + 1;    // 16 385
    static constexpr int kNumThird    = 30;                     // ISO 266, 25 Hz … 20 kHz
    static constexpr int kNumBark     = 24;                     // Zwicker
    static constexpr int kMaxSpectra  = 2;                      // L+R son dos; los demás modos, uno

    // Piso de los campos en dB. No es -inf: la UI dibuja estos números y un -inf ensucia todo lo que toca.
    static constexpr float kFloorDb = -200.0f;

    int          fftSize     = 0;
    int          numBins     = 0;          // fftSize/2 + 1
    double       sr          = 0.0;
    juce::uint32 frameIndex  = 0;          // frames PUBLICADOS desde el reset (la UI detecta "hay uno nuevo")
    // Spectrum::Channel. Es la ÚNICA fuente de verdad sobre cuántos espectros trae el frame: el slot 1
    // sólo tiene algo en modo L+R (ver spectrumNumSpectra más abajo). Dos campos diciendo lo mismo son
    // dos campos que algún día se contradicen.
    int          channelMode = 0;

    float magDb     [kMaxSpectra][kMaxBins]  {};
    float holdDb    [kMaxSpectra][kMaxBins]  {};
    float bands     [kMaxSpectra][kNumThird] {};   // ⅓ de octava (ISO 266), suma de potencia por banda
    float bandsHold [kMaxSpectra][kNumThird] {};
    float bark      [kMaxSpectra][kNumBark]  {};

    // Copia SÓLO el prefijo vivo. El struct entero son ~260 KB; con orden 12 lo que importa son 32 KB.
    // Copiar lo que no se lee es el 90 % del ancho de banda de memoria de esta lente, tirado.
    void copyTo (SpectrumFrame& dst) const noexcept
    {
        dst.fftSize     = fftSize;
        dst.numBins     = numBins;
        dst.sr          = sr;
        dst.frameIndex  = frameIndex;
        dst.channelMode = channelMode;

        const auto n = (size_t) std::max (0, std::min (numBins, kMaxBins));
        for (int s = 0; s < kMaxSpectra; ++s)
        {
            std::copy (magDb[s],  magDb[s] + n,  dst.magDb[s]);
            std::copy (holdDb[s], holdDb[s] + n, dst.holdDb[s]);
            std::copy (std::begin (bands[s]),     std::end (bands[s]),     std::begin (dst.bands[s]));
            std::copy (std::begin (bandsHold[s]), std::end (bandsHold[s]), std::begin (dst.bandsHold[s]));
            std::copy (std::begin (bark[s]),      std::end (bark[s]),      std::begin (dst.bark[s]));
        }
    }

    double binHz() const noexcept { return fftSize > 0 ? sr / (double) fftSize : 0.0; }
};

// Cuántos espectros trae el frame: dos sólo en L+R (Spectrum::Channel::leftRight == 4), uno en el resto.
inline constexpr int kChannelLeftRight = 4;
inline constexpr int spectrumNumSpectra (int channelMode) noexcept
{
    return channelMode == kChannelLeftRight ? 2 : 1;
}

// Centros ISO 266 de ⅓ de octava. Cada banda suma la potencia de los bins en [fc·2^(-1/6), fc·2^(1/6)).
inline constexpr double kThirdOctaveHz[SpectrumFrame::kNumThird] = {
    25.0,   31.5,   40.0,   50.0,   63.0,   80.0,   100.0,  125.0,  160.0,  200.0,
    250.0,  315.0,  400.0,  500.0,  630.0,  800.0,  1000.0, 1250.0, 1600.0, 2000.0,
    2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0
};

// Bordes de las 24 bandas críticas de Bark (Zwicker): 25 bordes.
inline constexpr double kBarkEdgesHz[SpectrumFrame::kNumBark + 1] = {
    20.0,   100.0,  200.0,  300.0,  400.0,  510.0,  630.0,  770.0,  920.0,  1080.0, 1270.0, 1480.0,
    1720.0, 2000.0, 2320.0, 2700.0, 3150.0, 3700.0, 4400.0, 5300.0, 6400.0, 7700.0, 9500.0, 12000.0,
    15500.0
};

// El SLOPE se aplica en DISPLAY, nunca a los datos: pivote en 1 kHz, +slope dB por octava hacia arriba.
// Con slope = 3 el ruido rosa (-3 dB/oct) se ve plano, que es el default y el motivo del default.
inline float spectrumDisplayDb (float rawDb, double freqHz, float slopeDbPerOct) noexcept
{
    if (slopeDbPerOct == 0.0f || freqHz <= 0.0) return rawDb;
    return rawDb + slopeDbPerOct * (float) (std::log2 (freqHz / 1000.0));
}
}
