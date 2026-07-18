#pragma once
// AnalysisFrame — POD que el AudioAnalyzer (worker thread) publica y el renderer consume (triple buffer, M1).
// M0: se rellena a cero (idle, sin audio). El CONTRATO queda congelado acá.
namespace supernova
{
struct AnalysisFrame
{
    static constexpr int kNumBands = 24;   // bandas log para el HUD/telemetría

    float  bands[kNumBands] = {};          // energía por banda [0..1], AGC compartido (independiente del fader)
    float  rms    = 0.0f;                   // nivel general CRUDO [0..1] (contrato absoluto: sigue la amplitud)
    float  energy = 0.0f;                   // RMS con AGC [0..1] — lo que consume el render (respiración)
    float  bass   = 0.0f;                   // graves  [0..1], AGC compartido (los ratios entre bandas se preservan)
    float  mid    = 0.0f;                   // medios  [0..1], AGC compartido
    float  treble = 0.0f;                   // agudos  [0..1], AGC compartido
    bool   onset  = false;                  // transitorio/kick este frame (bool de UN frame: puede pisarse)
    unsigned onsetCount = 0;                // contador MONOTÓNICO de onsets — sobrevive el pipeline latest-wins
                                            // (thread publica el último frame del batch / triple buffer pisa /
                                            // render lee a 60Hz frames de ~94Hz): el consumidor detecta el edge
                                            // por diferencia de contador, no por el bool.
    double timeSeconds = 0.0;               // tiempo determinista (para --render-frames)
};
}
