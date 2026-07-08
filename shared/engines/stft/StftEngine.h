#pragma once
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <vector>

namespace ovni::engines {

// =====================================================================================
// StftEngine — plomería STFT/OLA estéreo GENÉRICA del eje espectral (verde).
// La siembra AURORA (SPL·01) y la reusa HORIZON (SPL·02) tal cual: este motor NO sabe
// nada de paneo/freeze — sólo análisis y síntesis. El plugin enchufa su DSP espectral
// como un FrameProcessor que modifica los bins de cada frame listo.
//
// Diseño (curaduría 2026-06-10 + spec 04-aurora §3):
//   · Ventana Hann PERIÓDICA al 75% de solape (hop R = N/4) en análisis Y síntesis
//     (Hann² suma EXACTAMENTE 1.5 por COLA → la normalización se hornea en la ventana
//     de síntesis y se VERIFICA numéricamente en prepare()).
//   · N configurable (potencia de 2; default 2048 @44.1/48k). float32: no hay feedback
//     ni acumuladores largos (house-standard §1, regla de precisión).
//   · Ring buffers de entrada/salida → process() acepta bloques de host ARBITRARIOS
//     (32, 333, 2048…) con salida idéntica sample a sample.
//   · Latencia del pipeline = N samples EXACTOS (el frame entero: el sample más viejo
//     de un frame sale N muestras después de entrar). El plugin la declara al host con
//     setLatencySamples (latencySamples()) — gate [latency]: reportada == real.
//   · RT-safe: sin locks ni allocations en process() (todo dimensionado en prepare()).
// =====================================================================================
class StftEngine {
public:
    static constexpr int kDefaultFftSize = 2048;   // compromiso resolución/latencia (curaduría)
    static constexpr int kOverlapFactor  = 4;      // 75% de solape → hop = N/4 (COLA Hann²)

    // Vista de UN frame en frecuencia, todos los canales a la vez (un reparto L/R por bin
    // necesita ver el par). Layout por canal (formato juce::dsp::FFT real-only packed):
    //   spectra[ch] = [re0, im0, re1, im1, …, reNyq, imNyq]  → 2·numBins floats
    // (im0 = imNyq = 0: DC y Nyquist son reales). El procesador modifica IN-PLACE.
    struct FrameView {
        float* const* spectra;     // spectra[ch] → bins re/im intercalados del canal ch
        int    numChannels;        // canales preparados
        int    numBins;            // N/2 + 1 (DC..Nyquist)
        int    fftSize;            // N vigente
        double sampleRate;         // para que el procesador calcule Hz por bin: k·sr/N
    };
    using FrameProcessor = std::function<void (const FrameView&)>;

    // DSP espectral del plugin. Default (vacío) = identity: el pipeline FFT→IFFT corre
    // IGUAL (mismo camino, misma latencia — sin modos ocultos), sólo que nadie toca los
    // bins. ⚠ Setearlo en prepare/ctor (asignar un std::function puede alocar): NO es
    // para el audio thread con el stream corriendo.
    void setFrameProcessor (FrameProcessor fp) { frameProcessor = std::move (fp); }

    // Dimensiona todo (ventanas, FFT, rings, workspace) y verifica COLA. fftSize debe ser
    // potencia de 2 (si no, se redondea al orden más cercano + jassert en debug).
    // maxBlockSize no condiciona el pipeline (los rings absorben cualquier bloque): forma
    // parte del contrato prepare() del sello y se valida por cordura.
    void prepare (double sampleRate, int maxBlockSize, int numChannels,
                  int fftSize = kDefaultFftSize);

    // Limpia el estado (rings + fase del hop) sin tocar la configuración.
    void reset();

    // Procesa IN-PLACE un bloque de host de tamaño arbitrario. Llamar SIEMPRE con al menos
    // los canales preparados (los rings comparten posición de escritura entre canales).
    void process (juce::AudioBuffer<float>& buffer);

    // Latencia EXACTA del pipeline OLA (= N). Es el valor que el plugin declara al host.
    int latencySamples() const noexcept { return fftLen; }

    int    fftSize() const noexcept       { return fftLen; }
    int    hopSize() const noexcept       { return hop; }
    int    numBins() const noexcept       { return fftLen / 2 + 1; }
    double preparedSampleRate() const noexcept { return sr; }

    // Verificación COLA medida en prepare(): rizado pico-a-pico de la suma de ventanas
    // (dB; ~0 para Hann periódica). Accesor de diagnóstico (lo chequea el test [stft]).
    double colaRippleDb() const noexcept { return colaRipple; }

private:
    void processFrame();   // desenrolla rings → ventana → FFT → FrameProcessor → IFFT → OLA

    FrameProcessor frameProcessor;             // identity si está vacío
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> winAnalysis;            // Hann periódica (N)
    std::vector<float> winSynth;               // Hann periódica / colaGain (normalización horneada)

    std::vector<std::vector<float>> inFifo;    // [ch][N]  ring de entrada
    std::vector<std::vector<float>> outFifo;   // [ch][N]  ring de salida (acumulador OLA)
    std::vector<std::vector<float>> fftData;   // [ch][2N] workspace real-only de juce::dsp::FFT
    std::vector<float*> spectraPtrs;           // punteros a fftData para el FrameView (pre-armados)

    double sr        = 0.0;
    double colaRipple = 0.0;
    int    fftLen    = 0;
    int    hop       = 0;
    int    numCh     = 0;
    int    fifoPos   = 0;   // posición compartida de lectura/escritura de los rings
    int    hopCount  = 0;   // samples acumulados desde el último frame
};

} // namespace ovni::engines
