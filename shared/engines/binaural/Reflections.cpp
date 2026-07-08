#include "engines/binaural/Reflections.h"
#include <cmath>
#include <algorithm>

namespace ovni::engines {

static constexpr double kTwoPi     = 6.283185307179586;
static constexpr float  kWindowSec = 0.080f;  // ventana temprana (toda la externalización vive acá)
static constexpr float  kDensity   = 800.0f;  // pulsos/seg (densidad velvet)
static constexpr float  kTauSec    = 0.028f;  // constante de decaimiento del envelope
static constexpr float  kStartGain = 0.45f;   // ganancia del primer tramo

void Reflections::buildTaps (std::vector<Tap>& taps, unsigned int seed)
{
    taps.clear();
    const int   M  = (int) std::floor (kWindowSec * kDensity);
    const float Td = (float) sampleRate / kDensity; // grilla velvet en samples
    unsigned int s = seed;
    auto rnd = [&] { s = s * 1664525u + 1013904223u; return (float) ((double) s / 4294967295.0); };

    for (int m = 0; m < M; ++m)
    {
        const float jitter = rnd();
        int k = (int) std::round ((float) m * Td + jitter * (Td - 1.0f)); // posición pseudo-aleatoria
        if (k < 1) k = 1;
        const float sign = (rnd() < 0.5f) ? -1.0f : 1.0f;
        const float tSec = (float) k / (float) sampleRate;
        const float env  = kStartGain * std::exp (-tSec / kTauSec);       // decaimiento exponencial
        taps.push_back ({ k, sign * env });
    }
}

void Reflections::prepare (double sr, int maxBlock)
{
    sampleRate = (sr > 0.0 ? sr : 48000.0);

    int need = (int) std::ceil (0.095 * sampleRate) + std::max (maxBlock, 64) + 4;
    int n = 1; while (n < need) n <<= 1;        // potencia de 2 -> máscara circular
    ring.assign ((size_t) n, 0.0f);
    mask = n - 1;
    writePos = 0;

    buildTaps (tapsL, 0x1234567u);
    buildTaps (tapsR, 0x89ABCDEFu);             // semillas distintas -> L y R decorrelados

    hpCoef = (float) std::exp (-kTwoPi * 300.0  / sampleRate);          // HP 1-polo ~300 Hz
    lpCoef = 1.0f - (float) std::exp (-kTwoPi * 7000.0 / sampleRate);   // LP 1-polo ~7 kHz

    reset();
}

void Reflections::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    writePos = 0;
    hpLx = hpLy = hpRx = hpRy = 0.0f;
    lpL = lpR = 0.0f;
}

void Reflections::process (const float* monoIn, float* outL, float* outR, int n, float busGain)
{
    if (ring.empty()) return;

    if (busGain <= 1.0e-6f)
    {
        // Sin reflexiones: igual avanzamos el buffer para mantener continuidad temporal.
        for (int i = 0; i < n; ++i) { ring[(size_t) writePos] = monoIn[i]; writePos = (writePos + 1) & mask; }
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) writePos] = monoIn[i];

        float accL = 0.0f, accR = 0.0f;
        for (const auto& t : tapsL) accL += t.gain * ring[(size_t) ((writePos - t.delay) & mask)];
        for (const auto& t : tapsR) accR += t.gain * ring[(size_t) ((writePos - t.delay) & mask)];

        // HP 1-polo (DC-blocker): y = a*(yPrev + x - xPrev)
        const float hl = hpCoef * (hpLy + accL - hpLx); hpLx = accL; hpLy = hl;
        const float hr = hpCoef * (hpRy + accR - hpRx); hpRx = accR; hpRy = hr;
        // LP 1-polo (aire)
        lpL += lpCoef * (hl - lpL);
        lpR += lpCoef * (hr - lpR);

        outL[i] += busGain * lpL;
        outR[i] += busGain * lpR;

        writePos = (writePos + 1) & mask;
    }
}

} // namespace ovni::engines
