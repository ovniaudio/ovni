#include "analysis/modules/Spectrum.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
// Potencia por debajo de la cual no hay nada que medir: 1e-20 en potencia = -200 dB, el piso del frame.
constexpr double kTinyPower = 1.0e-20;

// Media celda de una fila del espectrograma, en factor de frecuencia: las 512 filas cubren 3 décadas, así
// que cada una mide 10^(3/511) ≈ 1.0136 y su celda va de f/√1.0136 a f·√1.0136.
const double kRowCellHalf = std::pow (10.0, 1.5 / (double) (SpectrogramRing::kRows - 1));

float powerToDb (double p) noexcept
{
    return p <= kTinyPower ? SpectrumFrame::kFloorDb
                           : (float) std::max ((double) SpectrumFrame::kFloorDb, 10.0 * std::log10 (p));
}

// Bessel modificada de orden 0, por su serie. Converge rapidísimo para β ≤ 9 (20 términos sobran).
double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x * 0.5) / (double) k;
        const double t2 = term * term;
        sum += t2;
        if (t2 < 1.0e-18 * sum) break;
    }
    return sum;
}
}

//======================================================================================== settings
void Spectrum::Settings::sanitise() noexcept
{
    fftOrder     = std::clamp (fftOrder, kMinFftOrder, kMaxFftOrder);
    window       = std::clamp (window, 0, (int) kNumWindows - 1);
    overlapIndex = std::clamp (overlapIndex, 0, kNumOverlaps - 1);
    channel      = std::clamp (channel, 0, (int) kNumChannels - 1);
    avgMode      = std::clamp (avgMode, 0, (int) kNumAverages - 1);
    bandsMode    = std::clamp (bandsMode, 0, (int) kNumBandModes - 1);
    rangeDbIndex = std::clamp (rangeDbIndex, 0, kNumRanges - 1);
    historySecIndex = std::clamp (historySecIndex, 0, SpectrogramRing::kNumHistoryOptions - 1);

    // El slope va de a 0.5 dB/oct: son 10 posiciones, no un continuo. Se redondea al paso para que el
    // número que muestra la UI sea EL número (y no 2.9999998).
    slopeDbPerOct = std::clamp (std::round (slopeDbPerOct / kSlopeStep) * kSlopeStep,
                                kMinSlopeDbPerOct, kMaxSlopeDbPerOct);
    avgSeconds        = std::clamp (avgSeconds, kMinAvgSeconds, kMaxAvgSeconds);
    holdDecayDbPerSec = std::clamp (holdDecayDbPerSec, kMinHoldDecay, kMaxHoldDecay);
}

bool Spectrum::Settings::operator== (const Settings& o) const noexcept
{
    return fftOrder == o.fftOrder && window == o.window && overlapIndex == o.overlapIndex
        && channel == o.channel && slopeDbPerOct == o.slopeDbPerOct && avgMode == o.avgMode
        && avgSeconds == o.avgSeconds && peakHold == o.peakHold
        && holdDecayDbPerSec == o.holdDecayDbPerSec && bandsMode == o.bandsMode
        && rangeDbIndex == o.rangeDbIndex && historySecIndex == o.historySecIndex;
}

// Lo que obliga a arrancar de cero. El slope y el modo de bandas NO están: son transformaciones de lo
// mismo, no otro análisis. El promediado SÍ: cambiar de exponencial a infinito con el acumulador viejo
// adentro daría un número que no es ninguno de los dos.
//
// La HISTORIA del espectrograma tampoco está (LOW del revisor del 50): estirar la ventana de 10 a 60
// segundos no cambia una sola cuenta de la FFT — cambia cuántas columnas se guardan. Lo único que hay que
// rehacer es el anillo (y limpiarlo, porque su capacidad cambia), y eso lo hace applySettings aparte.
// Reiniciar el análisis ahí tiraba el promedio acumulado y el peak hold: el usuario estira la historia
// para mirar más atrás y pierde justo el número que estaba mirando.
bool Spectrum::Settings::needsRestartVersus (const Settings& o) const noexcept
{
    return fftOrder != o.fftOrder || window != o.window || overlapIndex != o.overlapIndex
        || channel != o.channel || avgMode != o.avgMode || avgSeconds != o.avgSeconds
        || rangeDbIndex != o.rangeDbIndex;
}

//======================================================================================== ventanas
double Spectrum::fillWindow (float* dst, int n, int windowType)
{
    if (n <= 0) return 0.0;

    // Definición PERIÓDICA (DFT-even): el denominador es N, no N-1. Es la correcta para análisis
    // espectral — con ella un seno centrado en un bin y ventana Hann ocupa EXACTAMENTE tres bins, que es
    // de lo que vive el test de fuga. La simétrica (N-1) es para diseñar filtros FIR, no para medir.
    const double twoPi = juce::MathConstants<double>::twoPi;
    double sum = 0.0;

    if (windowType == blackmanHarris4)
    {
        // Blackman-Harris de 4 términos (Harris 1978): lóbulo lateral -92 dB, lóbulo principal 8 bins.
        constexpr double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / (double) n;
            const double w = a0 - a1 * std::cos (twoPi * t) + a2 * std::cos (2.0 * twoPi * t)
                                - a3 * std::cos (3.0 * twoPi * t);
            dst[i] = (float) w;
            sum += w;
        }
    }
    else if (windowType == kaiser9)
    {
        // Kaiser β = 9: el compromiso ajustable — lóbulo principal más ancho que Hann, laterales mucho
        // más abajo, sin llegar al ancho de Blackman-Harris.
        constexpr double beta = 9.0;
        const double denom = besselI0 (beta);
        for (int i = 0; i < n; ++i)
        {
            const double r = 2.0 * (double) i / (double) n - 1.0;      // [-1, 1)
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / denom;
            dst[i] = (float) w;
            sum += w;
        }
    }
    else
    {
        for (int i = 0; i < n; ++i)                                     // Hann
        {
            const double w = 0.5 - 0.5 * std::cos (twoPi * (double) i / (double) n);
            dst[i] = (float) w;
            sum += w;
        }
    }
    return sum / (double) n;
}

//======================================================================================== ciclo de vida
void Spectrum::setSpectrogramRing (SpectrogramRing* r) noexcept
{
    spectrogram = r;
    if (spectrogram != nullptr) spectrogram->configure (current.historySeconds(), emitRate());
}

void Spectrum::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    rebuild();   // incondicional: el sample rate cambia la tasa de frames y la decimación de emisión
}

void Spectrum::applySettings (const Settings& s)
{
    Settings wanted = s;
    wanted.sanitise();

    const bool restart       = wanted.needsRestartVersus (current) || win.empty() || fft == nullptr;
    const bool historyMoved  = wanted.historySecIndex != current.historySecIndex;
    current = wanted;

    if (! restart)
    {
        // Sólo cambió algo de presentación (slope, modo de bandas, hold): no se toca el análisis en curso.
        // La historia del espectrograma rehace y LIMPIA el anillo —columnas de 10 s y de 60 s tienen otra
        // capacidad— pero el análisis sigue exactamente donde estaba.
        if (historyMoved && spectrogram != nullptr)
            spectrogram->configure (current.historySeconds(), emitRate());
        out.channelMode = current.channel;
        return;
    }
    rebuild();
}

void Spectrum::rebuild()
{
    size = current.fftSize();
    bins = size / 2 + 1;
    hop  = std::max (1, (int) std::llround ((double) size * (1.0 - (double) current.overlap())));
    // La decimación de emisión es un ENTERO: la tasa emitida queda en frameRate/emitEvery ≤ 60 Hz, y es
    // tan reproducible como el cómputo (nada de relojes de pared decidiendo qué frame sale).
    emitEvery = std::max (1, (int) std::ceil ((sr / (double) hop) / kMaxEmitHz - 1.0e-9));

    fft = std::make_unique<juce::dsp::FFT> (current.fftOrder);

    win.assign ((size_t) size, 0.0f);
    cg = fillWindow (win.data(), size, current.window);
    double sumSq = 0.0;
    for (int i = 0; i < size; ++i) sumSq += (double) win[(size_t) i] * (double) win[(size_t) i];
    npg     = sumSq / (double) size;
    normAmp = 2.0 / ((double) size * std::max (1.0e-12, cg));

    ringL.assign ((size_t) size, 0.0f);
    ringR.assign ((size_t) size, 0.0f);
    scratch.assign ((size_t) size, 0.0f);
    midPow.assign ((size_t) bins, 0.0);

    // El ring del espectrograma se rehace con la tasa de emisión nueva (y se limpia): columnas medidas
    // con dos tamaños de FFT o dos rangos distintos no forman un dibujo, forman dos.
    if (spectrogram != nullptr) spectrogram->configure (current.historySeconds(), emitRate());

    lrWork[0].clear();
    lrWork[1].clear();   // se redimensionan solos en el próximo frame si hay sink

    for (int s2 = 0; s2 < SpectrumFrame::kMaxSpectra; ++s2)
    {
        work[s2].assign ((size_t) (2 * size), 0.0f);
        avgPow[s2].assign ((size_t) bins, 0.0);
        curPow[s2].assign ((size_t) bins, 0.0);
        hold[s2].assign ((size_t) bins, SpectrumFrame::kFloorDb);
    }

    reset();
}

void Spectrum::reset()
{
    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
    ringWrite = 0;
    samplesSinceReset = 0;
    nextFrameAt = size;          // el primer frame recién cuando la ventana está llena de audio real
    computedFrames = 0;
    emitted = 0;
    avgCount = 0.0;

    for (int s = 0; s < SpectrumFrame::kMaxSpectra; ++s)
    {
        std::fill (avgPow[s].begin(), avgPow[s].end(), 0.0);
        std::fill (curPow[s].begin(), curPow[s].end(), 0.0);
        std::fill (hold[s].begin(), hold[s].end(), SpectrumFrame::kFloorDb);
        std::fill (std::begin (bandHold[s]), std::end (bandHold[s]), SpectrumFrame::kFloorDb);
    }

    column.fill (0);
    if (spectrogram != nullptr) spectrogram->clear();

    out = SpectrumFrame{};
    out.fftSize     = size;
    out.numBins     = bins;
    out.sr          = sr;
    out.channelMode = current.channel;
    for (int s = 0; s < SpectrumFrame::kMaxSpectra; ++s)
    {
        std::fill (out.magDb[s],     out.magDb[s] + bins, SpectrumFrame::kFloorDb);
        std::fill (out.holdDb[s],    out.holdDb[s] + bins, SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bands[s]),     std::end (out.bands[s]),     SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bandsHold[s]), std::end (out.bandsHold[s]), SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bark[s]),      std::end (out.bark[s]),      SpectrumFrame::kFloorDb);
    }
}

//======================================================================================== entrada
void Spectrum::process (const float* L, const float* R, int n)
{
    if (win.empty() || fft == nullptr) return;

    for (int i = 0; i < n; ++i)
    {
        ringL[(size_t) ringWrite] = L[i];
        ringR[(size_t) ringWrite] = R[i];
        if (++ringWrite == size) ringWrite = 0;

        if (++samplesSinceReset >= nextFrameAt)
        {
            computeFrame();
            nextFrameAt += hop;
        }
    }
}

// Copia las `size` muestras del ring en orden cronológico, las ventanea y transforma. La FFT real de JUCE
// pide un buffer de 2·size floats con la señal en la primera mitad y devuelve los complejos intercalados.
//
// El ring se recorre en DOS tramos (de writePos al final, y del principio a writePos) en vez de con un
// módulo por muestra: son hasta 32 768 muestras por frame y hasta 375 frames por segundo.
void Spectrum::transform (const float* ring, int writePos, float* dstComplex) const
{
    std::fill (dstComplex, dstComplex + 2 * size, 0.0f);

    const int first = size - writePos;                 // writePos apunta a la muestra MÁS VIEJA del ring
    for (int i = 0; i < first; ++i)
        dstComplex[i] = ring[(size_t) (writePos + i)] * win[(size_t) i];
    for (int i = 0; i < writePos; ++i)
        dstComplex[first + i] = ring[(size_t) i] * win[(size_t) (first + i)];

    fft->performRealOnlyForwardTransform (dstComplex, true);
}

void Spectrum::computeFrame()
{
    const int numSpectra = spectrumNumSpectra (current.channel);

    // ¿Este frame se EMITE? Se computan todos (el promediado y el peak hold viven de eso) pero sólo uno
    // de cada `emitEvery` se materializa en `out`. Que el frame emitido sea exactamente el que se computó
    // en esa posición —y no "el último calculado, con el índice del anterior"— es lo que hace que el test
    // del bloque 1/7/64/4096 pueda comparar el frame 20 al bit.
    const bool emit = (computedFrames % (juce::uint32) emitEvery) == 0;
    ++computedFrames;

    // ---- qué señal transforma cada slot ----
    if (current.channel == leftRight)
    {
        transform (ringL.data(), ringWrite, work[0].data());
        transform (ringR.data(), ringWrite, work[1].data());
    }
    else if (current.channel == right)
    {
        transform (ringR.data(), ringWrite, work[0].data());
    }
    else if (current.channel == mid || current.channel == side)
    {
        const float sign = current.channel == mid ? 1.0f : -1.0f;
        const int first = size - ringWrite;
        for (int i = 0; i < first; ++i)
        {
            const size_t src = (size_t) (ringWrite + i);
            scratch[(size_t) i] = 0.5f * (ringL[src] + sign * ringR[src]);
        }
        for (int i = 0; i < ringWrite; ++i)
            scratch[(size_t) (first + i)] = 0.5f * (ringL[(size_t) i] + sign * ringR[(size_t) i]);

        // scratch ya está en orden cronológico: se transforma como un ring cuyo "más viejo" es el 0.
        transform (scratch.data(), 0, work[0].data());
    }
    else
    {
        transform (ringL.data(), ringWrite, work[0].data());
    }

    // ---- el sink de TODOS los frames (StereoBands): los complejos de L y R, sean o no lo que dibuja
    //      la lente de espectro. Ver Spectrum::FrameSink en el header.
    if (sink != nullptr)
    {
        const float* lc = nullptr;
        const float* rc = nullptr;

        if (current.channel == leftRight)
        {
            lc = work[0].data();
            rc = work[1].data();
        }
        else
        {
            const auto ensure = [this] (int i)
            {
                if ((int) lrWork[i].size() != 2 * size) lrWork[i].assign ((size_t) (2 * size), 0.0f);
            };
            if (current.channel == left) { lc = work[0].data(); }
            else { ensure (0); transform (ringL.data(), ringWrite, lrWork[0].data()); lc = lrWork[0].data(); }

            if (current.channel == right) { rc = work[0].data(); }
            else { ensure (1); transform (ringR.data(), ringWrite, lrWork[1].data()); rc = lrWork[1].data(); }
        }

        FrameInfo info;
        info.left      = lc;
        info.right     = rc;
        info.numBins   = bins;
        info.sr        = sr;
        info.binHz     = sr / (double) size;
        info.powerNorm = normAmp * normAmp;
        info.frameRate = frameRate();
        info.emitRate  = emitRate();
        info.rangeDb   = current.rangeDb();
        info.emitted   = emit;
        info.streamPos = samplesSinceReset;   // ver FrameInfo::streamPos
        sink->spectrumFrameComputed (info);
    }

    // ---- potencia instantánea por bin, en la referencia de dB del frame ----
    const double normSq = normAmp * normAmp;
    for (int s = 0; s < numSpectra; ++s)
    {
        const float* c = work[s].data();
        for (int k = 0; k < bins; ++k)
        {
            const double re = c[2 * k], im = c[2 * k + 1];
            curPow[s][(size_t) k] = (re * re + im * im) * normSq;
        }
    }

    // ---- promediado (en POTENCIA, nunca en dB: promediar decibeles no es promediar nada físico) ----
    const double dt = (double) hop / sr;
    avgCount += 1.0;
    const double alphaExp = 1.0 - std::exp (-dt / (double) std::max (1.0e-6f, current.avgSeconds));

    for (int s = 0; s < numSpectra; ++s)
    {
        auto&       a = avgPow[s];
        const auto& x = curPow[s];
        if (current.avgMode == avgNone)
        {
            std::copy (x.begin(), x.end(), a.begin());
        }
        else if (current.avgMode == avgExp)
        {
            for (int k = 0; k < bins; ++k) a[(size_t) k] += alphaExp * (x[(size_t) k] - a[(size_t) k]);
        }
        else
        {
            const double w = 1.0 / avgCount;      // media acumulada exacta desde el reset
            for (int k = 0; k < bins; ++k) a[(size_t) k] += w * (x[(size_t) k] - a[(size_t) k]);
        }
    }

    // ---- dB, peak hold y bandas ----
    // El HOLD se actualiza en TODOS los frames (si no, con 87.5 % de solape se perdería el 85 % de los
    // picos); lo que se decide por `emit` es qué frame se materializa para la UI.
    for (int s = 0; s < numSpectra; ++s)
    {
        const auto& a = avgPow[s];
        auto&       h = hold[s];
        const float decay = current.holdDecayDbPerSec * (float) dt;

        for (int k = 0; k < bins; ++k)
        {
            const float db = powerToDb (a[(size_t) k]);
            // El hold BAJA `decay` dB por frame y sube de una: es un medidor de picos, no un promedio.
            // Con holdDecayDbPerSec = 0 no baja nunca (hold infinito), que es lo que pide un barrido.
            h[(size_t) k] = std::max (h[(size_t) k] - decay, db);
            if (emit)
            {
                out.magDb[s][k]  = db;
                out.holdDb[s][k] = current.peakHold ? h[(size_t) k] : db;
            }
        }
        updateBandsAndHold (s, dt, emit);
    }

    if (! emit) return;

    // Los slots que no se usan quedan en el piso: dibujar una copia del canal 0 ahí sería inventar.
    for (int s = numSpectra; s < SpectrumFrame::kMaxSpectra; ++s)
    {
        std::fill (out.magDb[s],  out.magDb[s] + bins,  SpectrumFrame::kFloorDb);
        std::fill (out.holdDb[s], out.holdDb[s] + bins, SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bands[s]),     std::end (out.bands[s]),     SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bandsHold[s]), std::end (out.bandsHold[s]), SpectrumFrame::kFloorDb);
        std::fill (std::begin (out.bark[s]),      std::end (out.bark[s]),      SpectrumFrame::kFloorDb);
    }

    out.fftSize     = size;
    out.numBins     = bins;
    out.sr          = sr;
    out.channelMode = current.channel;
    out.frameIndex  = emitted++;

    buildSpectrogramColumn();
    if (spectrogram != nullptr) spectrogram->push (column.data());
}

// ========================================================================================================
// LA COLUMNA DEL ESPECTROGRAMA. 512 filas log-espaciadas de 20 Hz a 20 kHz, dB mapeado a 0-255 sobre
// [-rangeDb, 0]. Fila 0 = 20 Hz.
//
// Dos decisiones que están acá y no en la lente, porque son del DATO:
//
// 1) Se usa la potencia INSTANTÁNEA, nunca la promediada. Un espectrograma es la evolución en el tiempo;
//    promediarlo borraría exactamente lo que muestra. (El promedio sigue vivo para la curva de SPECTRUM.)
//    Con el canal en L+R la columna es M = (L+R)/2, calculada de los complejos: X_M = (X_L + X_R)/2.
//
// 2) Cada fila toma el MÁXIMO de los bins que caen en su celda; sólo cuando la celda no contiene ningún
//    bin (los graves con FFT chica, donde las filas son más densas que los bins) se interpola linealmente
//    en dB entre los dos vecinos. Interpolar SIEMPRE atenuaría los picos angostos —justo lo que un
//    espectrograma existe para mostrar— hasta 4 dB en el peor caso.
// ========================================================================================================
void Spectrum::buildSpectrogramColumn()
{
    const int    numSpectra = spectrumNumSpectra (current.channel);
    const double binHz = sr / (double) size;
    const double range = (double) current.rangeDb();

    // La potencia instantánea que mira el espectrograma.
    const std::vector<double>* src = &curPow[0];
    if (numSpectra > 1)
    {
        const float* a = work[0].data();
        const float* b = work[1].data();
        const double normSq = normAmp * normAmp;
        for (int k = 0; k < bins; ++k)
        {
            const double re = 0.5 * ((double) a[2 * k]     + (double) b[2 * k]);
            const double im = 0.5 * ((double) a[2 * k + 1] + (double) b[2 * k + 1]);
            midPow[(size_t) k] = (re * re + im * im) * normSq;
        }
        src = &midPow;
    }

    const auto& p = *src;
    for (int row = 0; row < SpectrogramRing::kRows; ++row)
    {
        const double f  = SpectrogramRing::rowFrequency (row);
        const double lo = f / kRowCellHalf, hi = f * kRowCellHalf;   // la celda de la fila

        const int k0 = (int) std::ceil (lo / binHz);
        const int k1 = std::min (bins, (int) std::ceil (hi / binHz));

        double pw;
        if (k1 > std::max (0, k0))
        {
            pw = 0.0;
            for (int k = std::max (0, k0); k < k1; ++k) pw = std::max (pw, p[(size_t) k]);
            column[(size_t) row] = (juce::uint8) std::clamp (
                (int) std::lround (255.0 * (powerToDb (pw) + range) / range), 0, 255);
        }
        else
        {
            const double pos = f / binHz;
            const int    ka  = std::clamp ((int) std::floor (pos), 0, bins - 1);
            const int    kb  = std::clamp (ka + 1, 0, bins - 1);
            const double t   = std::clamp (pos - (double) ka, 0.0, 1.0);
            const double db  = (double) powerToDb (p[(size_t) ka])
                             + t * ((double) powerToDb (p[(size_t) kb]) - (double) powerToDb (p[(size_t) ka]));
            column[(size_t) row] = (juce::uint8) std::clamp (
                (int) std::lround (255.0 * (db + range) / range), 0, 255);
        }
    }
}

// ⅓ de octava (ISO 266) y Bark: la potencia se suma ANTES de pasar a dB. Sumar decibeles sería sumar
// logaritmos, que no es la energía de nada.
void Spectrum::updateBandsAndHold (int slot, double dt, bool emit)
{
    const auto&  a     = avgPow[slot];
    const double binHz = sr / (double) size;
    const float  decay = current.holdDecayDbPerSec * (float) dt;

    const auto sumPower = [&] (double lo, double hi)
    {
        const int k0 = std::max (0, (int) std::ceil (lo / binHz));
        const int k1 = std::min (bins, (int) std::ceil (hi / binHz));   // [lo, hi)
        double p = 0.0;
        for (int k = k0; k < k1; ++k) p += a[(size_t) k];
        return k1 > k0 ? p : -1.0;      // banda sin ningún bin (FFT chica en graves): no hay medición
    };

    constexpr double kSixthOctDown = 0.8908987181403393;   // 2^(-1/6)
    constexpr double kSixthOctUp   = 1.1224620483093730;   // 2^(+1/6)

    for (int b = 0; b < SpectrumFrame::kNumThird; ++b)
    {
        const double p = sumPower (kThirdOctaveHz[b] * kSixthOctDown, kThirdOctaveHz[b] * kSixthOctUp);
        const float db = p < 0.0 ? SpectrumFrame::kFloorDb : powerToDb (p);
        bandHold[slot][b] = std::max (bandHold[slot][b] - decay, db);
        if (emit)
        {
            out.bands[slot][b]     = db;
            out.bandsHold[slot][b] = current.peakHold ? bandHold[slot][b] : db;
        }
    }

    for (int b = 0; b < SpectrumFrame::kNumBark; ++b)
    {
        if (! emit) continue;
        const double p = sumPower (kBarkEdgesHz[b], kBarkEdgesHz[b + 1]);
        out.bark[slot][b] = p < 0.0 ? SpectrumFrame::kFloorDb : powerToDb (p);
    }
}
}
