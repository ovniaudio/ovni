#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <deque>
#include <memory>
#include "analysis/AnalysisFrame.h"

// AudioAnalyzer — C++ (usa juce::dsp::FFT). Consume samples mono (mezcla de la entrada) del FIFO lock-free y
// produce AnalysisFrame (bandas log, graves/medios/agudos, RMS, onset por flujo espectral). Corre en el
// AnalysisThread, NO en el audio thread. Testeable con señales sintéticas (spec §6). Sin GPU, sin JUCE-graphics.
namespace supernova
{
class AudioAnalyzer
{
public:
    void prepare (double sampleRate, int fftOrder = 11);   // fftSize = 2^order (2048 por defecto)
    void reset();

    void push (const float* mono, int n) noexcept;         // acumula; computa frames en los bordes de hop
    bool popFrame (AnalysisFrame& out) noexcept;           // true si hay frame nuevo

    int  fftSize() const noexcept { return size; }
    int  hopSize() const noexcept { return hop; }

private:
    void computeFrame() noexcept;

    double sr   = 48000.0;
    int    order = 11, size = 2048, hop = 512;

    std::unique_ptr<juce::dsp::FFT>               fft;
    std::unique_ptr<juce::dsp::WindowingFunction<float>> window;

    std::vector<float> ring;      // últimos `size` samples (circular)
    int    ringWrite = 0;
    long   totalPushed = 0;
    int    sinceHop = 0;

    std::vector<float> timeBuf;   // ventana lineal para FFT/RMS (size)
    std::vector<float> fftBuf;    // 2*size (in/out de performFrequencyOnlyForwardTransform)
    std::vector<float> prevMag;   // size/2 (para flujo espectral)
    float  fluxAvg = 0.0f;
    double frameTimeSec = 0.0;

    // AGC (independencia de nivel): peaks lentos que normalizan bandas (divisor COMPARTIDO → ratios intactos)
    // y el RMS→energy. Sin esto la reactividad dependía del fader (reporte de campo: a −13dB no late).
    float  agcPeak = 0.05f;       // peak lento de bass+mid+treble
    float  rmsPeak = 0.05f;       // peak lento del RMS
    unsigned onsetCounter = 0;    // monotónico (ver AnalysisFrame::onsetCount)

    // Cola de frames producidos (el worker la drena en cada vuelta y publica el último al TripleBuffer;
    // en tests se pushea todo de una y se drena entero).
    std::deque<AnalysisFrame> frames;
};
}
