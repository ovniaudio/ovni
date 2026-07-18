#pragma once
#include <cmath>
#include <cstring>
#include "analysis/AnalysisFrame.h"

// Escenarios de audio sintético DETERMINISTAS para los golden frames (§9.3/§9.4). Puro (sin JUCE): lo comparten
// el render tool (genera los goldens) y GoldenFrameTest (los compara) → ambos producen bytes idénticos.
namespace supernova
{
inline AnalysisFrame scenarioFrame (const char* s, int f, int nFrames) noexcept
{
    const int third = nFrames / 3 > 1 ? nFrames / 3 : 1;
    const float denom = (float) (nFrames > 1 ? nFrames - 1 : 1);
    AnalysisFrame af;

    // Nota: el render consume af.energy (RMS con AGC); acá energy = rms (escenario sintético, mismos bytes
    // que los goldens generados cuando el uniform leía rms crudo).
    if (std::strcmp (s, "idle") == 0) { /* todo cero */ }
    else if (std::strcmp (s, "kick") == 0)
    {
        af.onset  = (f == third);
        af.bass   = (f >= third && f < third + 4) ? 0.85f : 0.08f;
        af.rms    = 0.30f; af.energy = 0.30f;
        af.treble = 0.12f;
    }
    else if (std::strcmp (s, "sustained-bass") == 0)
    {
        af.bass = (float) f / denom * 0.9f;   // rampa 0→0.9
        af.rms  = 0.25f; af.energy = 0.25f;
    }
    else if (std::strcmp (s, "treble-shimmer") == 0)
    {
        af.treble = 0.8f; af.bass = 0.1f; af.rms = 0.20f; af.energy = 0.20f;
    }
    else if (std::strcmp (s, "rms-breathe") == 0)
    {
        af.rms  = 0.45f + 0.25f * std::sin ((float) f / (float) (nFrames > 1 ? nFrames : 1) * 6.2831853f);
        af.energy = af.rms;
        af.bass = 0.1f;
    }
    // "explode-param": audio en cero; el trigger va por ParticleParams (ver scenarioExplode)
    return af;
}

inline bool scenarioExplode (const char* s, int f, int nFrames) noexcept
{
    const int third = nFrames / 3 > 1 ? nFrames / 3 : 1;
    return std::strcmp (s, "explode-param") == 0 && f == third;
}
}
