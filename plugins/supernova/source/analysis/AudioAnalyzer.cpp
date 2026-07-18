#include "analysis/AudioAnalyzer.h"
#include <cmath>
#include <algorithm>

namespace supernova
{
void AudioAnalyzer::prepare (double sampleRate, int fftOrder)
{
    sr    = sampleRate > 0.0 ? sampleRate : 48000.0;
    order = juce::jlimit (8, 13, fftOrder);
    size  = 1 << order;
    hop   = size / 4;                                  // 75% overlap

    fft    = std::make_unique<juce::dsp::FFT> (order);
    window = std::make_unique<juce::dsp::WindowingFunction<float>> (
                 (size_t) size, juce::dsp::WindowingFunction<float>::hann, false);

    ring.assign ((size_t) size, 0.0f);
    timeBuf.assign ((size_t) size, 0.0f);
    fftBuf.assign ((size_t) size * 2, 0.0f);
    prevMag.assign ((size_t) size / 2, 0.0f);
    reset();
}

void AudioAnalyzer::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (prevMag.begin(), prevMag.end(), 0.0f);
    ringWrite = 0; totalPushed = 0; sinceHop = 0;
    fluxAvg = 0.0f; frameTimeSec = 0.0;
    agcPeak = 0.05f; rmsPeak = 0.05f; onsetCounter = 0;
    frames.clear();
}

void AudioAnalyzer::push (const float* mono, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) ringWrite] = mono[i];
        ringWrite = (ringWrite + 1) % size;
        ++totalPushed;
        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            if (totalPushed >= size)     // recién cuando el ring está lleno
                computeFrame();
        }
    }
}

void AudioAnalyzer::computeFrame() noexcept
{
    // Desenrollá el ring (el más viejo primero) a timeBuf.
    for (int i = 0; i < size; ++i)
        timeBuf[(size_t) i] = ring[(size_t) ((ringWrite + i) % size)];

    // RMS en dominio temporal (sin ventana).
    double sumSq = 0.0;
    for (int i = 0; i < size; ++i) sumSq += (double) timeBuf[(size_t) i] * timeBuf[(size_t) i];
    const float rms = (float) std::sqrt (sumSq / (double) size);

    // Ventana Hann + FFT de magnitudes.
    std::copy (timeBuf.begin(), timeBuf.end(), fftBuf.begin());
    std::fill (fftBuf.begin() + size, fftBuf.end(), 0.0f);
    window->multiplyWithWindowingTable (fftBuf.data(), (size_t) size);
    fft->performFrequencyOnlyForwardTransform (fftBuf.data());   // magnitudes en [0, size/2]

    const int nBins = size / 2;
    const double binHz = sr / (double) size;
    const float norm = 2.0f / (float) size;   // escala aproximada → sine 0dBFS ≈ 1 en su bin

    AnalysisFrame f {};
    f.rms = juce::jlimit (0.0f, 1.0f, rms * 1.41421356f);   // ~1 para sine 0dBFS
    f.timeSeconds = frameTimeSec;

    // Bandas log (para HUD) + graves/medios/agudos por rango de frecuencia.
    // SUMA de magnitudes por banda (no promedio): así un seno angosto "enciende" su banda igual sea el rango
    // ancho (agudos) o angosto (graves); el promedio diluía los senos en las bandas de muchos bins.
    const double fLow = 40.0, fHigh = 16000.0;
    const double logSpan = std::log (fHigh / fLow);
    float  bandAcc[AnalysisFrame::kNumBands] = {};
    double bassAcc = 0, midAcc = 0, trebAcc = 0;

    for (int k = 1; k < nBins; ++k)                 // saltá DC (k=0)
    {
        const double freq = k * binHz;
        const float  mag  = fftBuf[(size_t) k] * norm;

        if (freq >= fLow && freq <= fHigh)
        {
            const double t = std::log (freq / fLow) / logSpan;   // 0..1
            int b = (int) (t * AnalysisFrame::kNumBands);
            b = juce::jlimit (0, AnalysisFrame::kNumBands - 1, b);
            bandAcc[b] += mag;
        }
        if (freq >= 20.0   && freq < 250.0)   bassAcc += mag;
        if (freq >= 250.0  && freq < 2500.0)  midAcc  += mag;
        if (freq >= 2500.0 && freq < 16000.0) trebAcc += mag;
    }

    // AGC compartido (independencia de nivel): un peak LENTO de la energía total divide a las tres bandas y a
    // las bands[] por igual → los RATIOS entre bandas quedan intactos (contrato de los tests) pero la escala se
    // vuelve independiente del fader (a −13dB el kick sigue llevando bass→~1). Ataque instantáneo, release
    // lento (~0.999^94fps ≈ −0.8dB/s); piso 0.05 para no amplificar silencio/ruido a full-scale.
    const float total = (float) (bassAcc + midAcc + trebAcc);
    agcPeak = std::max ({ total, agcPeak * 0.999f, 0.05f });
    const float agcInv = 1.0f / agcPeak;

    for (int b = 0; b < AnalysisFrame::kNumBands; ++b)
        f.bands[b] = juce::jlimit (0.0f, 1.0f, bandAcc[b] * agcInv);
    f.bass   = juce::jlimit (0.0f, 1.0f, (float) bassAcc * agcInv);
    f.mid    = juce::jlimit (0.0f, 1.0f, (float) midAcc  * agcInv);
    f.treble = juce::jlimit (0.0f, 1.0f, (float) trebAcc * agcInv);

    // energy = RMS con su propio AGC (misma idea) — lo consume el render (respiración). f.rms queda CRUDO
    // (contrato absoluto: "el RMS sigue la amplitud").
    rmsPeak  = std::max ({ f.rms, rmsPeak * 0.999f, 0.05f });
    f.energy = juce::jlimit (0.0f, 1.0f, f.rms / rmsPeak);

    // Onset por flujo espectral: Σ max(0, mag[k]-prevMag[k]). Gate RELATIVO (dispara sobre el promedio lento;
    // sin piso absoluto que dependa del nivel — solo un epsilon anti-ruido-numérico en silencio).
    float flux = 0.0f;
    for (int k = 1; k < nBins; ++k)
    {
        const float m = fftBuf[(size_t) k] * norm;
        const float d = m - prevMag[(size_t) k];
        if (d > 0.0f) flux += d;
        prevMag[(size_t) k] = m;
    }
    const bool warmed = frameTimeSec > (double) size / sr * 2.0;   // dejá calentar el promedio
    f.onset = warmed && flux > 1.8f * fluxAvg + 0.008f;
    if (f.onset) ++onsetCounter;
    f.onsetCount = onsetCounter;   // monotónico: sobrevive el pipeline latest-wins (el bool puede pisarse)
    fluxAvg = 0.97f * fluxAvg + 0.03f * flux;

    frameTimeSec += (double) hop / sr;
    frames.push_back (f);
}

bool AudioAnalyzer::popFrame (AnalysisFrame& out) noexcept
{
    if (frames.empty()) return false;
    out = frames.front();
    frames.pop_front();
    return true;
}
}
