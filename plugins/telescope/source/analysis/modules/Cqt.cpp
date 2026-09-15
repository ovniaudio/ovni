#include "analysis/modules/Cqt.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Los perfiles de Krumhansl & Kessler (1982), tal como los publica el paper: la "jerarquía tonal" media
// que reportaron los oyentes para cada grado de la escala, con la TÓNICA en la posición 0. Rotarlos por la
// tónica candidata es todo lo que hace el método de Krumhansl-Schmuckler.
constexpr double kMajorProfile[CqtFrame::kNumClasses] =
    { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
constexpr double kMinorProfile[CqtFrame::kNumClasses] =
    { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

float powerToDb (double p) noexcept
{
    return p <= Cqt::kTinyPower ? CqtFrame::kFloorDb
                                : (float) std::max ((double) CqtFrame::kFloorDb, 10.0 * std::log10 (p));
}
}

//======================================================================================== definiciones
double Cqt::qFactor() noexcept
{
    return 1.0 / (std::pow (2.0, 1.0 / (double) kBinsPerOctave) - 1.0);
}

// Dos bins por semitono (B = 24), así que el semitono del bin k es ⌊k·12/B⌋. El bin 0 es A0, y sumar 9
// lleva el La a la posición 9 de la numeración de la casa (C=0, C#=1 … B=11).
int Cqt::pitchClassOf (int bin) noexcept
{
    const int semitone = (std::max (0, bin) * 12) / kBinsPerOctave;
    return (semitone + 9) % 12;
}

const double* Cqt::keyProfile (int mode) noexcept
{
    return mode == minor ? kMinorProfile : kMajorProfile;
}

// Correlación de Pearson entre el cromagrama y el perfil ROTADO a esa tónica. Es una correlación, no una
// distancia: no le importa la escala del cromagrama (que ya viene normalizado) ni su nivel medio.
double Cqt::keyCorrelation (const float* chroma12, int tonic, int mode) noexcept
{
    if (chroma12 == nullptr) return 0.0;
    const double* prof = keyProfile (mode);

    double mx = 0.0, my = 0.0;
    for (int c = 0; c < CqtFrame::kNumClasses; ++c) { mx += (double) chroma12[c]; my += prof[c]; }
    mx /= (double) CqtFrame::kNumClasses;
    my /= (double) CqtFrame::kNumClasses;   // rotar es permutar: la media del perfil no cambia

    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const double dx = (double) chroma12[c] - mx;
        const double dy = prof[((c - tonic) % 12 + 12) % 12] - my;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    const double d = std::sqrt (sxx * syy);
    return d > 1.0e-12 ? std::clamp (sxy / d, -1.0, 1.0) : 0.0;
}

Cqt::Key Cqt::estimateKey (const float* chroma12) noexcept
{
    Key best;
    double bestR = -2.0;
    for (int mode = 0; mode < 2; ++mode)
        for (int tonic = 0; tonic < CqtFrame::kNumClasses; ++tonic)
        {
            const double r = keyCorrelation (chroma12, tonic, mode);
            if (! std::isfinite (r) || r <= bestR) continue;
            bestR = r;
            best.tonic = tonic;
            best.mode = mode;
            best.confidence = (float) r;
        }
    return best;
}

//======================================================================================== settings
void Cqt::Settings::sanitise() noexcept
{
    channel           = std::clamp (channel, 0, (int) kNumChannels - 1);
    chromaSecIndex    = std::clamp (chromaSecIndex, 0, kNumChromaSecOptions - 1);
    holdDecayDbPerSec = std::clamp (holdDecayDbPerSec, 0.0f, 60.0f);
}

bool Cqt::Settings::operator== (const Settings& o) const noexcept
{
    return channel == o.channel && chromaSecIndex == o.chromaSecIndex
        && holdDecayDbPerSec == o.holdDecayDbPerSec;
}

// Sólo el CANAL obliga a arrancar de cero: el ring tiene muestras de la otra señal, y mezclarlas dentro
// de la misma ventana daría un espectro de algo que nunca sonó. El suavizado del cromagrama y el
// decaimiento del hold son presentación sobre el mismo análisis.
bool Cqt::Settings::needsRestartVersus (const Settings& o) const noexcept
{
    return channel != o.channel;
}

//======================================================================================== ciclo de vida
void Cqt::prepare (double sampleRate)
{
    sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    rebuild();
}

void Cqt::rebuild()
{
    const double q = qFactor();
    // El eje llega hasta 20 kHz, pero nunca más allá de 0.45·fs: el kernel del bin más agudo tiene la
    // ventana más CORTA y por lo tanto el lóbulo más ANCHO, y ese lóbulo tiene que caber por debajo de
    // Nyquist. Con 0.45 entra con margen a 48 kHz y raspa a 44.1 (donde se le recortan las faldas lejanas
    // del último bin: menos de 0.1 dB).
    const double fMax = std::min (kFMaxHz, kNyquistFrac * sr);
    bins = std::clamp ((int) std::ceil ((double) kBinsPerOctave * std::log2 (fMax / kFMinHz)), 1, kMaxBins);

    // El bloque tiene que contener la ventana MÁS LARGA, que es la del bin 0. A 48 kHz son 59 568
    // muestras y el orden mínimo que las contiene es 16; a 96 kHz, 17; a 192 kHz, 18. La latencia del bin
    // grave en SEGUNDOS no cambia con el sample rate — es Q/f_min, 1.241 s siempre.
    const int n0 = (int) std::lround (q * sr / kFMinHz);
    order = 12;
    while ((1 << order) < n0 && order < 20) ++order;
    size = 1 << order;

    hop = std::max (1, (int) std::llround (sr / 10.0));   // 100 ms, el hop del AnalysisThread

    fft = std::make_unique<juce::dsp::FFT> (order);
    kernelFft.reset();

    ring.assign ((size_t) size, 0.0f);
    work.assign ((size_t) (2 * size), 0.0f);
    binPow.assign ((size_t) bins, 0.0);
    hold.assign ((size_t) bins, CqtFrame::kFloorDb);

    binNorm.assign ((size_t) bins, 0.0f);
    binLen.assign ((size_t) bins, 0);
    for (int k = 0; k < bins; ++k)
        binLen[(size_t) k] = std::clamp ((int) std::lround (q * sr / binFrequency (k)), 4, size);

    // Los kernels se tiran: la geometría cambió. Se rearman en el primer frame (ver el header).
    binStart.clear();
    binCount.clear();
    idx.clear();
    kre.clear();
    kim.clear();
    coeffCount = 0;
    buildMs = 0.0;

    reset();
}

void Cqt::reset()
{
    std::fill (ring.begin(), ring.end(), 0.0f);
    ringWrite = 0;
    samplesSinceReset = 0;
    nextFrameAt = hop;          // el primer frame a los 100 ms; el ring arranca en cero y eso se VE subir
    emitted = 0;

    std::fill (hold.begin(), hold.end(), CqtFrame::kFloorDb);
    std::fill (binPow.begin(), binPow.end(), 0.0);
    chromaPow.fill (0.0);
    smoothPow.fill (0.0);
    keyCount.fill (0);
    keyFrames = 0;

    out = CqtFrame{};
    out.numBins       = bins;
    out.fMin          = (float) kFMinHz;
    out.binsPerOctave = kBinsPerOctave;
    out.lowestBinLatencySec = (float) lowestBinLatencySec();
    std::fill (out.magDb,  out.magDb  + CqtFrame::kMaxBins, CqtFrame::kFloorDb);
    std::fill (out.holdDb, out.holdDb + CqtFrame::kMaxBins, CqtFrame::kFloorDb);
}

void Cqt::applySettings (const Settings& s)
{
    Settings wanted = s;
    wanted.sanitise();
    const bool restart = wanted.needsRestartVersus (current);
    current = wanted;
    if (restart && ! ring.empty()) reset();
}

int Cqt::kernelLength (int k) const noexcept
{
    return (k >= 0 && k < (int) binLen.size()) ? binLen[(size_t) k] : 0;
}

double Cqt::lowestBinLatencySec() const noexcept
{
    return binLen.empty() || sr <= 0.0 ? 0.0 : (double) binLen[0] / sr;
}

double Cqt::kernelDensity() const noexcept
{
    const double dense = (double) bins * (double) (size / 2 + 1);
    return dense > 0.0 ? (double) coeffCount / dense : 0.0;
}

//======================================================================================== kernels
// LA ÚNICA PARTE CARA, y se paga UNA vez: ~229 FFTs complejas de 2^16. Corre en el worker (nunca en el
// audio thread) y sólo cuando alguien abre una de las dos lentes que la piden. Ver el header.
void Cqt::buildKernels()
{
    if (! binStart.empty() || bins <= 0 || ring.empty()) return;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (kernelFft == nullptr) kernelFft = std::make_unique<juce::dsp::FFT> (order);

    binStart.assign ((size_t) bins, 0);
    binCount.assign ((size_t) bins, 0);
    idx.clear();
    kre.clear();
    kim.clear();

    std::vector<juce::dsp::Complex<float>> tmp ((size_t) size), spec ((size_t) size);
    const int half = size / 2;

    for (int k = 0; k < bins; ++k)
    {
        const int    nk     = binLen[(size_t) k];
        const double fk     = binFrequency (k);
        const int    start  = size - nk;                    // el soporte PEGADO AL FINAL (ver el header)
        const double centre = 0.5 * (double) (nk - 1);      // la fase se referencia al centro del kernel

        std::fill (tmp.begin(), tmp.end(), juce::dsp::Complex<float> {});
        double sumW = 0.0;
        for (int n = 0; n < nk; ++n)
        {
            // Hann PERIÓDICA (denominador N, no N−1): la misma definición del módulo Spectrum, y la
            // correcta para análisis espectral.
            const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) n / (double) nk);
            sumW += w;
            // EL SIGNO ES +, y no es un detalle. Lo que hay que correlacionar con la señal es
            // e^{−2πj f_k n/fs}; por Parseval eso se consigue transformando su CONJUGADO —o sea el
            // exponente POSITIVO— y multiplicando después por conj(K). Con el signo negativo acá, el
            // kernel queda centrado en la frecuencia −f_k: su lóbulo cae en la mitad del espectro que una
            // FFT real ni siquiera devuelve, el producto da ~0 y la poda, sin un máximo real contra el
            // cual comparar, se queda con el 29 % de los coeficientes en vez del 3 %. (Los dos síntomas
            // se midieron: −120 dB en todos los bins y 2.19 millones de coeficientes.)
            const double ph = kTwoPi * fk * ((double) n - centre) / sr;
            tmp[(size_t) (start + n)] = { (float) (w * std::cos (ph) / (double) nk),
                                          (float) (w * std::sin (ph) / (double) nk) };
        }

        // LA CALIBRACIÓN, POR BIN. CG = Σw/N_k es la ganancia coherente de ESA ventana; con ella,
        // amplitud = 2·|X_cq| / (N·CG) y un seno de amplitud A en f_k lee 20·log10(A) — la misma
        // referencia de SpectrumFrame. Se calcula, no se copia de una tabla: cada bin tiene su ventana.
        const double cg = sumW / (double) nk;
        binNorm[(size_t) k] = (float) (2.0 / ((double) size * std::max (1.0e-12, cg)));

        kernelFft->perform (tmp.data(), spec.data(), false);

        // Sólo las frecuencias NO NEGATIVAS: el bloque de entrada es real y su FFT sólo devuelve ésas, y
        // el kernel —una exponencial compleja a +f_k— no tiene del otro lado nada por encima del umbral.
        double maxNorm = 0.0;
        for (int j = 0; j <= half; ++j) maxNorm = std::max (maxNorm, (double) std::norm (spec[(size_t) j]));

        const double thrNorm = kSparseThreshold * kSparseThreshold * maxNorm;   // se compara en potencia
        binStart[(size_t) k] = (int) idx.size();
        for (int j = 0; j <= half; ++j)
        {
            const auto v = spec[(size_t) j];
            if ((double) std::norm (v) <= thrNorm) continue;
            idx.push_back (j);
            kre.push_back (v.real());
            kim.push_back (-v.imag());        // el CONJUGADO, ya guardado: conjugar por frame sería tonto
        }
        binCount[(size_t) k] = (int) idx.size() - binStart[(size_t) k];
    }

    coeffCount = (long long) idx.size();
    buildMs = juce::Time::getMillisecondCounterHiRes() - t0;
}

//======================================================================================== entrada
void Cqt::process (const float* L, const float* R, int n)
{
    if (ring.empty() || fft == nullptr) return;
    if (binStart.empty()) buildKernels();     // la primera vez, en el worker (ver el header)

    for (int i = 0; i < n; ++i)
    {
        const float l = L != nullptr ? L[i] : 0.0f;
        const float r = R != nullptr ? R[i] : 0.0f;
        ring[(size_t) ringWrite] = current.channel == left  ? l
                                 : current.channel == right ? r
                                                            : 0.5f * (l + r);
        if (++ringWrite == size) ringWrite = 0;

        if (++samplesSinceReset >= nextFrameAt)
        {
            computeFrame();
            nextFrameAt += hop;
        }
    }
}

void Cqt::computeFrame()
{
    // El bloque en orden CRONOLÓGICO: la muestra más nueva queda en size−1, que es donde termina el
    // soporte de todas las ventanas. El ring se recorre en dos tramos, no con un módulo por muestra.
    std::fill (work.begin(), work.end(), 0.0f);
    const int first = size - ringWrite;                  // ringWrite apunta a la muestra MÁS VIEJA
    for (int i = 0; i < first; ++i)      work[(size_t) i] = ring[(size_t) (ringWrite + i)];
    for (int i = 0; i < ringWrite; ++i)  work[(size_t) (first + i)] = ring[(size_t) i];

    fft->performRealOnlyForwardTransform (work.data(), true);

    // X_cq[k] = Σ_j X[j]·conj(K_k[j]) sobre los coeficientes que sobrevivieron la poda.
    for (int k = 0; k < bins; ++k)
    {
        const int s = binStart[(size_t) k], cnt = binCount[(size_t) k];
        double re = 0.0, im = 0.0;
        for (int i = 0; i < cnt; ++i)
        {
            const int    j  = idx[(size_t) (s + i)];
            const double xr = (double) work[(size_t) (2 * j)], xi = (double) work[(size_t) (2 * j + 1)];
            const double cr = (double) kre[(size_t) (s + i)],  ci = (double) kim[(size_t) (s + i)];
            re += xr * cr - xi * ci;
            im += xr * ci + xi * cr;
        }
        const double amp = std::sqrt (re * re + im * im) * (double) binNorm[(size_t) k];
        binPow[(size_t) k] = amp * amp;
    }

    const double dt    = (double) hop / sr;
    const float  decay = current.holdDecayDbPerSec * (float) dt;
    for (int k = 0; k < bins; ++k)
    {
        const float db = powerToDb (binPow[(size_t) k]);
        // El hold BAJA `decay` dB por frame y sube de una: es un medidor de picos (igual que SPECTRUM, y
        // con el mismo setting). Con decaimiento 0 no baja nunca.
        hold[(size_t) k] = std::max (hold[(size_t) k] - decay, db);
        out.magDb[k]  = db;
        out.holdDb[k] = hold[(size_t) k];
    }

    updateChromaAndKey (dt);
    out.frameIndex = emitted++;
}

//======================================================================================== cromagrama y tonalidad
void Cqt::updateChromaAndKey (double dt)
{
    chromaPow.fill (0.0);
    for (int k = 0; k < bins; ++k)
        chromaPow[(size_t) pitchClassOf (k)] += binPow[(size_t) k];

    const double instMax = *std::max_element (chromaPow.begin(), chromaPow.end());
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
        out.chroma[c] = instMax > kTinyPower ? (float) (chromaPow[(size_t) c] / instMax) : 0.0f;

    // El suavizado va sobre la POTENCIA, nunca sobre dB ni sobre el cromagrama ya normalizado: promediar
    // decibeles no es promediar nada físico, y promediar normalizados le daría el mismo peso a un
    // compás fuerte y a un silencio.
    const double tau   = std::max (1.0e-3, (double) current.chromaSeconds());
    const double alpha = 1.0 - std::exp (-dt / tau);
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
        smoothPow[(size_t) c] += alpha * (chromaPow[(size_t) c] - smoothPow[(size_t) c]);

    const double smoothMax = *std::max_element (smoothPow.begin(), smoothPow.end());
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
        out.chromaSmooth[c] = smoothMax > kTinyPower ? (float) (smoothPow[(size_t) c] / smoothMax) : 0.0f;

    if (smoothMax > kTinyPower)
    {
        const auto key = estimateKey (out.chromaSmooth);
        if (key.tonic >= 0)
        {
            ++keyFrames;
            ++keyCount[(size_t) (key.mode * CqtFrame::kNumClasses + key.tonic)];

            out.keyTonic        = key.tonic;
            out.keyMode         = key.mode;
            out.keyConfidence   = key.confidence;
            // El % del tiempo DESDE EL RESET en que ESTA tonalidad fue la mejor. Es el número que separa
            // "la pieza está en La menor" de "en este instante La menor es la que mejor correlaciona".
            out.keyTimeFraction = (float) ((double) keyCount[(size_t) (key.mode * CqtFrame::kNumClasses + key.tonic)]
                                           / (double) keyFrames);
            return;
        }
    }

    // Sin cromagrama medible no hay tonalidad: −1 y cero. "No sé" es un resultado.
    out.keyTonic        = -1;
    out.keyMode         = -1;
    out.keyConfidence   = 0.0f;
    out.keyTimeFraction = 0.0f;
}
}
