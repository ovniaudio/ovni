#include "engines/stft/StftEngine.h"
#include <cmath>
#include <cstring>

namespace ovni::engines {

// =====================================================================================
// Mecánica del pipeline (esquema fifo clásico de STFT streaming):
//
//   por cada sample:  emitir outFifo[pos] → host · limpiar el slot · inFifo[pos] = entrada
//                     · pos++ (mod N) · cada R samples se procesa UN frame.
//
//   processFrame():   desenrollar inFifo en orden temporal (el más viejo en fifoPos)
//                     → ×Hann → FFT → FrameProcessor (identity si vacío) → IFFT
//                     → ×Hann/COLA → overlap-add en outFifo a partir de fifoPos.
//
// Latencia: el frame procesado tras el sample n se emite en los tiempos n+1 … n+N; su
// sample j corresponde a la entrada n−N+1+j → TODA contribución de x[m] cae en el tiempo
// de salida m+N. Con COLA exacta (Hann² @ R=N/4 suma 1.5, horneada en winSynth) la
// reconstrucción identity es perfecta y la latencia es EXACTAMENTE N (test [stft] 3).
// =====================================================================================

void StftEngine::prepare (double sampleRate, int maxBlockSize, int numChannels, int newFftSize)
{
    jassert (sampleRate > 0.0 && maxBlockSize > 0 && numChannels > 0);
    juce::ignoreUnused (maxBlockSize);   // los rings absorben cualquier block size del host

    // N: potencia de 2 obligatoria (juce::dsp::FFT trabaja por orden). Si llega otra cosa,
    // se redondea al orden más cercano y el jassert avisa en debug (fail-fast).
    const int order = juce::jlimit (5, 15, juce::roundToInt (std::log2 ((double) newFftSize)));
    fftLen = 1 << order;
    jassert (fftLen == newFftSize);
    hop   = fftLen / kOverlapFactor;
    sr    = sampleRate;
    numCh = numChannels;

    fft = std::make_unique<juce::dsp::FFT> (order);

    // Ventana Hann PERIÓDICA (denominador N, no N−1): es la que cumple COLA exacta al
    // solaparse cada N/4. La simétrica de juce::dsp::WindowingFunction (N−1) deja un
    // rizado ~1/N que acá no aceptamos — la curaduría pide COLA verificada.
    winAnalysis.resize ((size_t) fftLen);
    for (int n = 0; n < fftLen; ++n)
        winAnalysis[(size_t) n] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                           * (float) n / (float) fftLen));

    // Verificación COLA numérica: la suma de w²(n − kR) sobre los kOverlapFactor solapes
    // debe ser CONSTANTE (= 1.5 para Hann²). Se mide el rizado real y se normaliza por la
    // media — si algún día cambia la ventana/solape, el jassert y el test [stft] lo cazan.
    double colaMin = 1.0e30, colaMax = 0.0;
    for (int n = 0; n < hop; ++n)
    {
        double s = 0.0;
        for (int k = 0; k < kOverlapFactor; ++k)
        {
            const double w = (double) winAnalysis[(size_t) (n + k * hop)];
            s += w * w;
        }
        colaMin = juce::jmin (colaMin, s);
        colaMax = juce::jmax (colaMax, s);
    }
    const double colaGain = 0.5 * (colaMin + colaMax);
    colaRipple = 20.0 * std::log10 (colaMax / juce::jmax (colaMin, 1.0e-30));
    jassert (colaRipple < 1.0e-4);   // Hann periódica: rizado ~precisión de máquina

    // Ventana de síntesis = Hann / COLA → la normalización queda horneada y la emisión
    // de salida es una copia pura (ni una multiplicación por sample en el hot path).
    winSynth.resize ((size_t) fftLen);
    for (int n = 0; n < fftLen; ++n)
        winSynth[(size_t) n] = winAnalysis[(size_t) n] / (float) colaGain;

    // Rings + workspace, por canal. Todo acá: process() NO aloca (RT-safe).
    inFifo.assign  ((size_t) numCh, std::vector<float> ((size_t) fftLen, 0.0f));
    outFifo.assign ((size_t) numCh, std::vector<float> ((size_t) fftLen, 0.0f));
    fftData.assign ((size_t) numCh, std::vector<float> ((size_t) (2 * fftLen), 0.0f));
    spectraPtrs.resize ((size_t) numCh);
    for (int ch = 0; ch < numCh; ++ch)
        spectraPtrs[(size_t) ch] = fftData[(size_t) ch].data();

    fifoPos  = 0;
    hopCount = 0;
}

void StftEngine::reset()
{
    for (auto& v : inFifo)  std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : outFifo) std::fill (v.begin(), v.end(), 0.0f);
    for (auto& v : fftData) std::fill (v.begin(), v.end(), 0.0f);
    fifoPos  = 0;
    hopCount = 0;
}

void StftEngine::process (juce::AudioBuffer<float>& buffer)
{
    // El chasis ya lo pone en processBlock; acá también para el uso standalone (tests del
    // motor solo) — bins de baja energía generan denormals en el workspace (gotcha 22).
    juce::ScopedNoDenormals noDenormals;

    if (fft == nullptr)   // prepare() no llamado: no-op (jassert avisa en debug)
    {
        jassertfalse;
        return;
    }

    jassert (buffer.getNumChannels() >= numCh);   // los rings comparten posición entre canales
    const int nCh   = juce::jmin (buffer.getNumChannels(), numCh);
    const int total = buffer.getNumSamples();

    int done = 0;
    while (done < total)
    {
        // Avanza por tramos contiguos: hasta el próximo frame y sin cruzar el wrap del ring.
        const int chunk = juce::jmin (total - done, hop - hopCount, fftLen - fifoPos);

        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* src = buffer.getReadPointer (ch) + done;
            float* dst  = buffer.getWritePointer (ch) + done;
            float* inF  = inFifo[(size_t) ch].data()  + fifoPos;
            float* outF = outFifo[(size_t) ch].data() + fifoPos;
            for (int j = 0; j < chunk; ++j)
            {
                const float x = src[j];   // leer ANTES de escribir (proceso in-place)
                dst[j]  = outF[j];        // normalización ya horneada en winSynth
                outF[j] = 0.0f;           // el slot queda libre para el próximo OLA
                inF[j]  = x;
            }
        }

        fifoPos += chunk;
        if (fifoPos == fftLen) fifoPos = 0;
        hopCount += chunk;
        done     += chunk;

        if (hopCount == hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

void StftEngine::processFrame()
{
    const int tail = fftLen - fifoPos;   // muestras desde fifoPos hasta el final del ring

    // Análisis: desenrollar + ventana + FFT, canal por canal.
    for (int ch = 0; ch < numCh; ++ch)
    {
        float*       fd   = fftData[(size_t) ch].data();
        const float* fifo = inFifo[(size_t) ch].data();

        // El ring en orden temporal: el sample más viejo del frame está en fifoPos.
        std::memcpy (fd,        fifo + fifoPos, (size_t) tail    * sizeof (float));
        std::memcpy (fd + tail, fifo,           (size_t) fifoPos * sizeof (float));

        juce::FloatVectorOperations::multiply (fd, winAnalysis.data(), fftLen);
        juce::FloatVectorOperations::clear (fd + fftLen, fftLen);   // mitad alta del workspace
        fft->performRealOnlyForwardTransform (fd, true);            // → re/im packed (N/2+1 bins)
    }

    // DSP espectral del plugin (identity si no hay procesador — MISMO camino, sin modos ocultos).
    if (frameProcessor)
    {
        const FrameView view { spectraPtrs.data(), numCh, fftLen / 2 + 1, fftLen, sr };
        frameProcessor (view);
    }

    // Síntesis: IFFT + ventana (con COLA horneada) + overlap-add al ring de salida.
    for (int ch = 0; ch < numCh; ++ch)
    {
        float* fd = fftData[(size_t) ch].data();
        fft->performRealOnlyInverseTransform (fd);                  // (JUCE escala 1/N)
        juce::FloatVectorOperations::multiply (fd, winSynth.data(), fftLen);

        // frame[j] → outFifo[(fifoPos + j) mod N]: dos tramos contiguos (mismo wrap del análisis).
        float* outF = outFifo[(size_t) ch].data();
        juce::FloatVectorOperations::add (outF + fifoPos, fd, tail);
        juce::FloatVectorOperations::add (outF, fd + tail, fifoPos);
    }
}

} // namespace ovni::engines
