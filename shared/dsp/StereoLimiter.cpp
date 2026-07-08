#include "dsp/StereoLimiter.h"
#include <cmath>
#include <algorithm>

namespace ovni::dsp {

void StereoLimiter::prepare (double sr)
{
    sampleRate = (sr > 0.0 ? sr : 48000.0);
    updateRelease();
    reset();
}

void StereoLimiter::reset() noexcept
{
    gain = 1.0f;
}

void StereoLimiter::setTuning (LimiterTuning t) noexcept
{
    tuning = t;
    updateRelease();
}

void StereoLimiter::updateRelease() noexcept
{
    // Release 1-polo: coef tal que la ganancia recupera con cte de tiempo = releaseMs.
    const double ms = std::max (1.0e-3, (double) tuning.releaseMs);
    relCoef = 1.0f - (float) std::exp (-1.0 / (0.001 * ms * sampleRate));
}

void StereoLimiter::process (float* left, float* right, int n) noexcept
{
    const float ceiling = tuning.ceiling;
    for (int i = 0; i < n; ++i)
    {
        const float pk  = std::max (std::abs (left[i]), std::abs (right[i]));
        const float tgt = (pk > ceiling) ? ceiling / pk : 1.0f;
        if (tgt < gain) gain  = tgt;                       // attack instantáneo (nunca pasa el techo)
        else            gain += (tgt - gain) * relCoef;    // release suave
        left[i]  *= gain;
        right[i] *= gain;
    }
}

} // namespace ovni::dsp
