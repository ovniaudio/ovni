#pragma once
#include <vector>
#include <cmath>

namespace ovni::engines {

// Cancelación de crosstalk RACE (Glasgal & Miller, "Recursive Ambiophonic Crosstalk
// Elimination") para modo PARLANTES. El binaural en parlantes sufre crosstalk (el parlante
// izquierdo llega también al oído derecho) y la imagen colapsa. RACE radia en cada canal el
// directo MENOS una copia invertida, retardada (~85 µs) y band-limitada de la salida opuesta:
//   L_out[n] = L_in[n] − A·R_out[n−D]
//   R_out[n] = R_in[n] − A·L_out[n−D]
// Recursivo (genera la serie de cancelación), A<1 (estable), band-limit 250 Hz–6 kHz, +3 dB
// al centro (recupera el +3 dB de suma acústica que el XTC saca). Sin calibración, latencia 0.
class Crosstalk
{
public:
    void prepare (double sr)
    {
        sampleRate = (sr > 0.0 ? sr : 48000.0);
        delaySamps = (float) (85.0e-6 * sampleRate);                 // ~85 µs
        const int sz = (int) std::ceil (delaySamps) + 4;
        histL.assign ((size_t) sz, 0.0f);
        histR.assign ((size_t) sz, 0.0f);
        hpCoef = (float) std::exp (-6.283185307179586 * 250.0  / sampleRate); // HP ~250 Hz
        lpCoef = 1.0f - (float) std::exp (-6.283185307179586 * 6000.0 / sampleRate); // LP ~6 kHz
        reset();
    }

    void reset()
    {
        std::fill (histL.begin(), histL.end(), 0.0f);
        std::fill (histR.begin(), histR.end(), 0.0f);
        writePos = 0;
        hpLx = hpLy = hpRx = hpRy = lpL = lpR = 0.0f;
    }

    // Procesa in-place. on=false -> bypass (pero avanza estado para activar sin click).
    void process (float* L, float* R, int n, bool on)
    {
        if (histL.empty()) return;
        const int   sz = (int) histL.size();
        const float A  = 0.66f;
        for (int i = 0; i < n; ++i)
        {
            // salida retardada (la recursión lee la SALIDA) con delay fraccional
            const float dL = readDelayed (histL.data(), sz, delaySamps);
            const float dR = readDelayed (histR.data(), sz, delaySamps);
            // band-pass del cross-stream (HP 250 + LP 6k): sólo se cancela esa banda
            const float hl = hpCoef * (hpLy + dL - hpLx); hpLx = dL; hpLy = hl; lpL += lpCoef * (hl - lpL);
            const float hr = hpCoef * (hpRy + dR - hpRx); hpRx = dR; hpRy = hr; lpR += lpCoef * (hr - lpR);

            float lo = L[i], ro = R[i];
            if (on)
            {
                lo = L[i] - A * lpR;          // cancelar crosstalk del canal opuesto (R) en L
                ro = R[i] - A * lpL;
                const float mid = 0.5f * (lo + ro);
                lo += 0.412f * mid; ro += 0.412f * mid; // +3 dB makeup al centro
            }
            histL[(size_t) writePos] = lo;     // alimentar la recursión con la salida
            histR[(size_t) writePos] = ro;
            writePos = (writePos + 1) % sz;
            L[i] = lo; R[i] = ro;
        }
    }

private:
    float readDelayed (const float* h, int sz, float delay) const
    {
        float rp = (float) writePos - delay;
        while (rp < 0.0f) rp += (float) sz;
        const int   i0 = (int) rp;
        const float f  = rp - (float) i0;
        const int   i1 = (i0 + 1) % sz;
        return h[i0] * (1.0f - f) + h[i1] * f; // interp lineal = delay fraccional
    }

    double sampleRate = 48000.0;
    float  delaySamps = 4.0f;
    std::vector<float> histL, histR;
    int    writePos = 0;
    float  hpCoef = 0.0f, lpCoef = 0.0f;
    float  hpLx = 0, hpLy = 0, hpRx = 0, hpRy = 0, lpL = 0, lpR = 0;
};

} // namespace ovni::engines
