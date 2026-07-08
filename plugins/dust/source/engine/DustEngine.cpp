// DustEngine.cpp — fachada del motor de ecos binaurales de DUST (MOV·03).
#include "DustEngine.h"

namespace dust::engine
{

void DustEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    maxBlock   = (int) spec.maximumBlockSize;

    echo.prepare (spec.sampleRate, maxBlock);
    field.prepare (spec.sampleRate, maxBlock);
    limiter.setTuning ({ 0.85f, 60.0f });   // techo 0.85 (~-1.4 dB): margen true-peak del sello
    limiter.prepare (spec.sampleRate);

    monoBuf.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void DustEngine::reset()
{
    echo.reset();
    field.reset();
    limiter.reset();
    numBirths = 0;
    originX = originXTgt;
    originY = originYTgt;
    originAzCont = originAzimuth (originX, originY);
}

// Azimut del ORIGIN: x + = derecha de pantalla, y + = frente; az 0 = frente, + = izquierda (CCW).
// Pantalla-derecha tiene que sonar canal-derecho -> az = atan2(-x, y) (la negación del §4 de
// physics-measurement-and-pitfalls.md). Cerca del centro (x,y ~ 0) se mantiene el último azimut.
float DustEngine::originAzimuth (float x, float y) const noexcept
{
    if (x * x + y * y < 1.0e-6f)
        return originAzCont;
    return std::atan2 (-x, y);
}

void DustEngine::process (juce::AudioBuffer<float>& buffer, int numInputChannels)
{
    juce::ScopedNoDenormals noDenormals;   // colas de feedback + FIRs largos: flush-to-zero

    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    jassert (n <= maxBlock);

    // Mono-sum de la fuente (cada burbuja se coloca binauralmente desde una fuente mono; spec §3).
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numInputChannels > 1 ? buffer.getReadPointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
        monoBuf[(size_t) i] = inR != nullptr ? 0.5f * (inL[i] + inR[i]) : inL[i];

    // ORIGIN con rampa por-sample: el (x,y) se desliza con one-pole (τ=25 ms) hacia el target;
    // el camino dentro del bloque es la rampa lineal entre extremos y el BubbleField rampea las
    // ganancias de bus POR-SAMPLE entre esos extremos -> arrastrar el origen no chasquea nunca.
    const float azStartRaw = originAzimuth (originX, originY);
    const float slewCoef   = 1.0f - std::exp (-(float) n / (0.025f * (float) sampleRate));
    originX += (originXTgt - originX) * slewCoef;
    originY += (originYTgt - originY) * slewCoef;
    const float azEndRaw = originAzimuth (originX, originY);

    // Des-wrap: el azimut continuo no salta al cruzar ±π (la rampa de ganancias sigue el camino corto).
    constexpr float kPi = juce::MathConstants<float>::pi;
    float dStart = azStartRaw - originAzCont;
    while (dStart >  kPi) dStart -= 2.0f * kPi;
    while (dStart < -kPi) dStart += 2.0f * kPi;
    const float azStart = originAzCont + dStart;
    float dEnd = azEndRaw - azStartRaw;
    while (dEnd >  kPi) dEnd -= 2.0f * kPi;
    while (dEnd < -kPi) dEnd += 2.0f * kPi;
    const float azEnd = azStart + dEnd;
    originAzCont = azEnd;

    // Burbujas -> banco binaural -> limiter (estéreo-linked, techo 0.85).
    const int numTaps = echo.process (monoBuf.data(), n, azStart, azEnd, renders);

    // Telemetría: nacimientos de este bloque (azimut al nacer + energía del slot). El processor los
    // drena después de process() — mismo hilo de audio, sin carrera — y los publica al FIFO de la UI.
    numBirths = 0;
    for (int k = 0; k < numTaps; ++k)
        if (renders[(size_t) k].born)
            births[(size_t) numBirths++] = { renders[(size_t) k].azStart, renders[(size_t) k].gain };

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    if (outR == nullptr)
    {
        // Salida mono (no debería pasar con el chasis: salida SIEMPRE estéreo) — degradar con cordura.
        field.process (renders, numTaps, n, outL, monoBuf.data());
        return;
    }

    field.process (renders, numTaps, n, outL, outR);
    limiter.process (outL, outR, n);
}

} // namespace dust::engine
