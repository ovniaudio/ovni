#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include "analysis/SpectrumFrame.h"

// ========================================================================================================
// StereoBandsFrame — la COHERENCIA POR BIN que dibuja STEREO SPECTROGRAM (spec §5.3/§5.4, prompt 51).
//
// Viaja por su propio TripleBuffer, por el mismo motivo que el SpectrumFrame: son 16 385 bins × 2 floats
// (131 KB). Meterlos en el AnalysisFrame haría que LOUDNESS copie 131 KB diez veces por segundo para
// dibujar tres números.
//
// LOS DOS NÚMEROS POR BIN, y por qué uno va suavizado y el otro no:
//
//   coh_k       = Σ Re(L_k·R_k*) / √(Σ|L_k|² · Σ|R_k|²)   sobre los K frames de la ventana.
//                 +1 = mono/en fase · 0 = decorrelacionado o un solo canal · −1 = fuera de fase.
//                 VA SUAVIZADA porque la coherencia de un frame solo es puro ruido: sobre dos fuentes
//                 independientes un frame suelto da valores repartidos por todo [−1, +1], y el dibujo
//                 sería confeti. Sobre la ventana se queda alrededor de 0 con dispersión, que es lo que
//                 un estimador de coherencia hace de verdad.
//                 Es 0 POR DEFINICIÓN cuando un canal no tiene energía en ese bin (Σ|L|²·Σ|R|² ≈ 0): no
//                 hay dos fases que comparar. No es "descorrelacionado": es "no definida".
//
//   energyDb_k  = 10·log10(|L_k|² + |R_k|²) del frame ACTUAL, en la referencia de dB de SpectrumFrame.
//                 NO va suavizada: es el brillo de un espectrograma, y su eje X es el tiempo — promediarla
//                 borraría justo lo que muestra (la misma decisión que la columna del sonograma del 50).
//                 Ojo con la comparación directa contra SPECTRUM: acá se suman LOS DOS canales, así que
//                 una señal mono lee 3.01 dB por encima de lo que lee el canal L solo.
// ========================================================================================================
namespace telescope
{
struct StereoBandsFrame
{
    static constexpr int kMaxBins = SpectrumFrame::kMaxBins;      // 16 385 (orden 15)
    static constexpr float kFloorDb = SpectrumFrame::kFloorDb;    // −200 dB, nunca −inf

    int          numBins    = 0;
    double       sr         = 0.0;
    double       binHz      = 0.0;
    juce::uint32 frameIndex = 0;      // frames PUBLICADOS desde el reset
    float        windowSeconds = 0.0f;

    float coh      [kMaxBins] {};
    float energyDb [kMaxBins] {};

    // ===== 53: PANEO POR ENERGÍA por bin (lo come el módulo Field, spec §5.5) =====
    //
    //   pan_k = (ΣRR_k − ΣLL_k) / (ΣRR_k + ΣLL_k) ∈ [−1, +1]
    //           −1 = sólo L · 0 = centro (o sin energía) · +1 = sólo R
    //
    // Sale de LAS MISMAS SUMAS de la ventana con las que se calcula `coh` — un cociente más, ni una
    // multiplicación de más — así que hereda su determinismo y su ventana efectiva.
    //
    // ES PANEO POR ENERGÍA, NO POSICIÓN. Con la ley de potencia constante (L = cos θ·x, R = sin θ·x)
    // vale exactamente sin²θ − cos²θ = −cos 2θ: 0° → −1, 22.5° → −0.707, 45° → 0, 90° → +1. Lo que NO
    // dice es dónde estaba la fuente en la sala: de una mezcla estéreo terminada ese problema no tiene
    // solución única (la trampa del ITD de ORBIT, informe 21). Por eso la lente FIELD rotula su eje
    // "L … C … R" y nunca "−90° … +90°".
    //
    // ES 0 POR DEFINICIÓN sin energía en el bin: no es "centrado", es que no hay nada que ubicar. Igual
    // que `coh`, que es 0 cuando no hay dos fases que comparar.
    //
    // OJO con la diferencia contra el §5.5 del spec, que lo escribe en AMPLITUD, (|R|−|L|)/(|R|+|L|), y
    // en la misma línea afirma que da sin θ. Las dos cosas no pueden ser ciertas: en amplitud la ley de
    // potencia constante da (sin θ − cos θ)/(sin θ + cos θ), que a 22.5° vale −0.414, no −0.383 ni
    // sin(22.5°). La versión en ENERGÍA es la que sale de las sumas que el módulo ya tiene (Parseval), la
    // que da un número redondo y verificable (−cos 2θ) y la que se implementa acá. Está en el README.
    float pan [kMaxBins] {};

    // Copia SÓLO el prefijo vivo: con orden 12 lo que importa son 16 KB de los 131.
    void copyTo (StereoBandsFrame& dst) const noexcept
    {
        dst.numBins       = numBins;
        dst.sr            = sr;
        dst.binHz         = binHz;
        dst.frameIndex    = frameIndex;
        dst.windowSeconds = windowSeconds;

        const auto n = (size_t) std::max (0, std::min (numBins, kMaxBins));
        std::copy (coh,      coh + n,      dst.coh);
        std::copy (energyDb, energyDb + n, dst.energyDb);
        std::copy (pan,      pan + n,      dst.pan);   // ===== 53 =====
    }
};
}
