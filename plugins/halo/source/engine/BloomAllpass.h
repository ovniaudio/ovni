#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>

namespace halo {

// =====================================================================================
// BloomAllpass — difusor de FADE-IN (el "bloom" glacial del shimmer, base técnica §4).
//
// Cascada de 4 allpass de Schroeder con delays PRIMOS (20/30/41/55 ms aprox) y un coef común c≈0.55.
// Un allpass es plano en magnitud (no colorea) pero dispersa la energía en el TIEMPO: con c en 0.50–0.62
// el impulso se reparte como un fade-in tipo gaussiano (CLT de Costello), NO un ataque instantáneo. Esto
// es lo que hace que el halo "florezca" gradualmente en vez de aparecer de golpe. Va a la ENTRADA del wet
// (tras el pre-delay, antes del FDN), mono — el FDN y el spatializer hacen la imagen estéreo después.
//
// NO toca FdnReverb (Riesgo R6): es un helper LOCAL de HALO. Delays primos → sin ringing metálico. El
// coef se de-zippea por bloque (rampa lineal) → SIZE puede moverlo sin clicks. RT-safe: process() no asigna.
// =====================================================================================
class BloomAllpass
{
public:
    static constexpr int kStages = 4;

    void prepare (double sr) noexcept
    {
        sampleRate = (sr > 0.0 ? sr : 48000.0);
        // Delays primos (ms) — mutuamente primos para evitar coincidencias de eco (ringing metálico).
        static constexpr double kMs[kStages] = { 20.0, 30.0, 41.0, 55.0 };
        for (int s = 0; s < kStages; ++s)
        {
            int len = std::max (1, (int) std::round (kMs[s] * 0.001 * sampleRate));
            delayLen[(size_t) s] = len;
            buf[(size_t) s].assign ((size_t) len, 0.0f);
            widx[(size_t) s] = 0;
        }
        coefSm.reset (sampleRate, 0.03);   // de-zipper del coef (30 ms)
        coefSm.setCurrentAndTargetValue (kDefaultCoef);
        reset();
    }

    void reset() noexcept
    {
        for (int s = 0; s < kStages; ++s)
        {
            std::fill (buf[(size_t) s].begin(), buf[(size_t) s].end(), 0.0f);
            widx[(size_t) s] = 0;
        }
    }

    // c en [0,1] — más alto = más cola/fade-in (rango útil 0.50–0.62). Se de-zippea por bloque.
    void setCoefficient (float c) noexcept { coefTarget = juce::jlimit (0.0f, 0.97f, c); }

    // Procesa un bloque mono in-place (un canal). RT-safe.
    void processMono (float* x, int n) noexcept
    {
        coefSm.setTargetValue (coefTarget);
        for (int i = 0; i < n; ++i)
        {
            const float c = coefSm.getNextValue();
            float v = x[i];
            for (int s = 0; s < kStages; ++s)
            {
                auto& b   = buf[(size_t) s];
                const int wi = widx[(size_t) s];
                const float delayed = b[(size_t) wi];          // salida del delay (la muestra más vieja)
                const float in      = v + (-c) * delayed;      // allpass Schroeder: in = x − c·z^-d
                b[(size_t) wi] = in;                            // escribir el nuevo estado
                v = delayed + c * in;                          // salida del allpass = z^-d + c·in
                int nwi = wi + 1; if (nwi >= delayLen[(size_t) s]) nwi = 0;
                widx[(size_t) s] = nwi;
            }
            x[i] = v;
        }
    }

private:
    static constexpr float kDefaultCoef = 0.55f;
    double sampleRate = 48000.0;
    std::array<std::vector<float>, kStages> buf;
    std::array<int, kStages> delayLen { 1, 1, 1, 1 };
    std::array<int, kStages> widx { 0, 0, 0, 0 };
    float coefTarget = kDefaultCoef;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> coefSm;
};

} // namespace halo
