#pragma once

// ========================================================================================================
// Los 13 IDs de lente de TELESCOPE, en el ORDEN del spec §2 (docs/superpowers/specs/2026-09-07-telescope-design.md).
//
// EL ÍNDICE ES CONTRATO: el param `lens` es un AudioParameterChoice y su índice se guarda en el estado y
// en los presets. Se AGREGA al final, NUNCA se reordena ni se renombra — un preset guardado hoy con
// LOUDNESS tiene que seguir abriendo LOUDNESS cuando estén las 13 lentes construidas.
// ========================================================================================================
namespace telescope
{
enum class LensId
{
    loudness = 0,        // 1  · LUFS M/S/I, LRA, dBTP, historia, objetivo por plataforma
    dynamics,            // 2  · PLR/PSR, histograma, clips
    spectrum,            // 3  · FFT configurable, RTA ⅓ oct, Bark
    spectrogram,         // 4  · sonograma 2D
    waterfall,           // 5  · espectrograma 2.5D
    cqt,                 // 6  · constant-Q + cromagrama
    spiral,              // 7  · el CQT enrollado por octava
    scope,               // 8  · Lissajous, polar, correlímetro, osciloscopio
    bandCorrelation,     // 9  · correlación / ancho / pérdida al monoficar POR BANDA
    stereoSpectrogram,   // 10 · espectrograma coloreado por ancho/fase
    field,               // 11 · energía por dirección de paneo × frecuencia
    tonalBalance,        // 12 · promedio espectral vs referencia
    verdict              // 13 · conclusiones con el número y la regla que las sostiene
};

inline constexpr int kNumLenses = 13;

// Los nombres tal cual se muestran en la tira de lentes y en el Choice del host.
inline constexpr const char* kLensNames[kNumLenses] = {
    "LOUDNESS", "DYNAMICS", "SPECTRUM", "SPECTROGRAM", "WATERFALL", "CQT", "SPIRAL",
    "SCOPE", "BAND CORRELATION", "STEREO SPECTROGRAM", "FIELD", "TONAL BALANCE", "VERDICT"
};

inline constexpr const char* lensName (LensId id) noexcept { return kLensNames[(int) id]; }
}
