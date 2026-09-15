#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>
#include "analysis/CqtFrame.h"

// ========================================================================================================
// Cqt — el análisis MUSICAL de TELESCOPE (spec §5.4, prompt 52): un espectro cuya resolución sigue la
// escala, el cromagrama que sale de él y la tonalidad estimada.
//
// POR QUÉ NO ALCANZA UN FFT. Un FFT reparte sus bins a distancias IGUALES en Hz: con 4 096 puntos a
// 48 kHz cada bin mide 11.7 Hz, así que en el registro grave —donde un semitono entre C1 y C#1 son 2 Hz—
// dos notas distintas caen en el mismo bin, y en los agudos sobran cincuenta bins para un semitono. El
// espectro de un FFT es un instrumento de física; la música se mide en octavas.
//
// LA TRANSFORMADA CONSTANT-Q (Brown 1991; Brown & Puckette 1992, kernels dispersos) reparte los bins a
// distancias iguales en OCTAVAS y le da a cada uno su propia ventana, tan larga como haga falta para que
// su ancho de banda relativo sea siempre el mismo:
//
//     B = 24 bins por octava (dos por semitono)     f_k = f_min · 2^(k/B)     f_min = 27.5 Hz (A0)
//     f_max = min (20 kHz, 0.45·fs)                 numBins = ⌈B · log2(f_max/f_min)⌉   (229 a 48 kHz)
//     Q = 1 / (2^(1/B) − 1) = 34.127                N_k = round (Q · fs / f_k)          (Hann)
//
// El precio es la LATENCIA, y es inherente: el bin de A0 necesita N_0 = 59 568 muestras, o sea 1.241 s a
// 48 kHz; el de 20 kHz, 82 muestras (1.7 ms). No hay forma de tener resolución de un cuarto de tono en
// 27.5 Hz sin escuchar un segundo largo. TELESCOPE lo declara —`lowestBinLatencySec`, el rótulo de la
// lente y el README— en vez de esconderlo: un CQT honesto no puede prometer que el grave es instantáneo.
//
// LOS KERNELS SON DISPERSOS, y es lo que hace viable el método. Se transforma UNA vez el bloque entero
// (una FFT de 2^16 a 48 kHz) y cada bin sale de un producto interno con su kernel espectral:
//
//     kernel temporal   k_k[n] = w_k[n] · e^{−2πj f_k (n − centro)/fs} / N_k
//     kernel espectral  K_k    = FFT (k_k)          (con el soporte pegado al FINAL del bloque)
//     X_cq[k] = Σ_j X[j] · conj (K_k[j])            sólo sobre los j guardados
//
// De K_k se guardan SÓLO los coeficientes con |K| > 0.0054 · max|K| (el umbral de Brown & Puckette): son
// del orden del 1-3 % de 2^16 por bin. Sin esa poda habría que multiplicar 229 × 65 536 complejos por
// frame; con ella, unos 250 000.
//
// EL SOPORTE VA AL FINAL DEL BLOQUE, no al centro. Es la decisión que hace que la latencia sea la que
// dice el párrafo de arriba: con los kernels centrados, TODOS los bins —incluso los de 20 kHz— mirarían
// el centro del bloque, o sea 0.68 s en el pasado. Pegados al final, cada bin ve las últimas N_k muestras
// y nada más: los agudos son casi instantáneos y sólo el grave paga su ventana.
//
// LA REFERENCIA DE dB es la de SpectrumFrame: un seno de amplitud A en f_k lee 20·log10(A). La ganancia
// coherente se calcula POR BIN a partir de su propia ventana (Σw/N_k), no se copia de una tabla: cada bin
// tiene una ventana distinta y es la única forma de que las tres octavas de un A lean el mismo número.
//
// CÓMO CUENTA LOS FRAMES. Ring propio de 2^16 muestras del canal elegido y un contador de muestras desde
// el reset; un frame en cada posición t = H, 2H, 3H… con H = round(fs/10) (el hop de 100 ms del
// AnalysisThread). Posiciones FIJAS del stream, igual que el módulo Spectrum: por eso los números no
// dependen del tamaño de bloque del host, y el test del bloque 1/7/64/4096 puede comparar al bit.
//
// LOS KERNELS SE CONSTRUYEN LA PRIMERA VEZ QUE SE PIDE UN FRAME, no en prepare(). Son ~229 FFTs de 2^16
// (unos 100 ms) y es la única parte cara del módulo. Ponerlas en prepare() haría que CADA instancia del
// plugin las pague en cada prepareToPlay aunque nadie abra nunca las lentes 6 y 7 — justo el costo que la
// "lente a demanda" existe para no pagar. Construirlas al primer frame las deja en el WORKER (nunca en el
// audio thread, que es lo que importa) y sólo cuando la lente está a la vista. De ahí en más, cero
// allocations por frame.
// ========================================================================================================
namespace telescope
{
class Cqt
{
public:
    enum Channel { left = 0, right, mid, kNumChannels };

    static constexpr int    kBinsPerOctave = 24;
    static constexpr double kFMinHz        = 27.5;      // A0
    static constexpr double kFMaxHz        = 20000.0;
    static constexpr double kNyquistFrac   = 0.45;      // f_max nunca pasa de 0.45·fs (ver el header)
    static constexpr int    kMaxBins       = CqtFrame::kMaxBins;
    // El umbral de poda de Brown & Puckette 1992 (§III): se descartan los coeficientes por debajo de
    // 0.0054 del máximo del kernel. No es un número de la casa; se cita.
    static constexpr double kSparseThreshold = 0.0054;
    // Potencia por debajo de la cual no hay nada que medir (el piso de −120 dB del frame).
    static constexpr double kTinyPower = 1.0e-12;

    static constexpr int   kNumChromaSecOptions = 3;
    static constexpr float kChromaSecOptions[kNumChromaSecOptions] = { 0.5f, 2.0f, 5.0f };
    static constexpr int   kDefaultChromaSecIndex = 1;   // 2 s

    struct Settings
    {
        int   channel           = mid;                        // L / R / M
        int   chromaSecIndex    = kDefaultChromaSecIndex;     // suavizado del cromagrama
        float holdDecayDbPerSec = 12.0f;                      // COMPARTIDO con SPECTRUM

        void  sanitise() noexcept;
        float chromaSeconds() const noexcept { return kChromaSecOptions[chromaSecIndex]; }
        bool  operator== (const Settings&) const noexcept;
        bool  operator!= (const Settings& o) const noexcept { return ! (*this == o); }
        // Sólo el canal obliga a arrancar de cero: el suavizado y el decaimiento del hold son
        // presentación sobre el mismo análisis.
        bool  needsRestartVersus (const Settings&) const noexcept;
    };

    void prepare (double sampleRate);
    void reset();
    void applySettings (const Settings& s);
    const Settings& settings() const noexcept { return current; }

    // Acepta cualquier n: las posiciones de los frames las decide el contador interno.
    void process (const float* L, const float* R, int n);

    const CqtFrame& frame() const noexcept { return out; }
    juce::uint32    framesEmitted() const noexcept { return emitted; }

    // ---- geometría (la leen la lente, el README y los tests) ----
    int    numBins()  const noexcept { return bins; }
    int    fftOrder() const noexcept { return order; }
    int    fftSize()  const noexcept { return size; }
    int    hopSamples() const noexcept { return hop; }
    double sampleRate() const noexcept { return sr; }
    double binFrequency (int k) const noexcept { return kFMinHz * std::pow (2.0, (double) k / (double) kBinsPerOctave); }
    int    kernelLength (int k) const noexcept;             // N_k
    double lowestBinLatencySec() const noexcept;            // N_0 / fs
    // Coeficientes guardados tras la poda (y su fracción del denso): el número que sostiene "1-3 %".
    long long kernelCoefficients() const noexcept { return coeffCount; }
    double    kernelDensity() const noexcept;
    double    kernelBuildMs() const noexcept { return buildMs; }
    bool      kernelsBuilt() const noexcept { return ! binStart.empty(); }
    // Construye los kernels ahora (normalmente lo hace el primer frame). Público para poder cronometrarlo.
    void buildKernels();

    // La constante Q del diseño: 1/(2^(1/B) − 1).
    static double qFactor() noexcept;

    // ---- cromagrama y tonalidad: definiciones que se testean solas ----
    // El bin k cae en el semitono ⌊k·12/B⌋ y su clase es (semitono + 9) mod 12: el bin 0 es A0, y +9 lleva
    // A a la posición 9 de la numeración C=0 … B=11.
    static int pitchClassOf (int bin) noexcept;

    static constexpr int kNumKeys = 24;   // 12 tónicas × 2 modos
    enum Mode { major = 0, minor };
    // Los perfiles de Krumhansl & Kessler (1982), con C en la posición 0.
    static const double* keyProfile (int mode) noexcept;

    struct Key { int tonic = -1, mode = -1; float confidence = 0.0f; };
    // Correlación de Pearson del cromagrama contra los 24 perfiles rotados; gana la más alta.
    static Key estimateKey (const float* chroma12) noexcept;
    // La correlación de Pearson de una tónica y un modo concretos (la usan los tests para no depender del
    // ganador: un test que sólo mira al ganador no puede decir por cuánto ganó).
    static double keyCorrelation (const float* chroma12, int tonic, int mode) noexcept;

private:
    void rebuild();
    void computeFrame();
    void updateChromaAndKey (double dt);

    double sr = 48000.0;
    Settings current;

    int order = 16, size = 1 << 16, bins = 0, hop = 4800;

    std::unique_ptr<juce::dsp::FFT> fft;         // real, para el bloque de entrada
    std::unique_ptr<juce::dsp::FFT> kernelFft;   // complejo, sólo para construir los kernels

    // Los kernels dispersos, en SoA: para el bin k, los coeficientes [binStart[k], binStart[k]+binCount[k])
    // de idx/kre/kim, donde (kre, kim) ya es el CONJUGADO de K_k[idx] (conjugar por frame sería gratis
    // pero innecesario).
    std::vector<int>   binStart, binCount, idx;
    std::vector<float> kre, kim;
    std::vector<float> binNorm;                  // 2/(N·CG_k): de |X_cq| a amplitud de seno equivalente
    std::vector<int>   binLen;                   // N_k
    long long          coeffCount = 0;
    double             buildMs = 0.0;

    std::vector<float> ring;                     // 2^order muestras del canal elegido
    int                ringWrite = 0;
    long long          samplesSinceReset = 0;
    long long          nextFrameAt = 0;
    juce::uint32       emitted = 0;

    std::vector<float>  work;                    // 2·size floats para la FFT real
    std::vector<double> binPow;                  // potencia por bin (amplitud²)
    std::vector<float>  hold;

    std::array<double, CqtFrame::kNumClasses> chromaPow {};    // instantáneo
    std::array<double, CqtFrame::kNumClasses> smoothPow {};    // suavizado sobre chromaSeconds()

    // El % del tiempo: un contador por tonalidad y el total de frames CON cromagrama medible.
    std::array<juce::uint32, kNumKeys> keyCount {};
    juce::uint32 keyFrames = 0;

    CqtFrame out;
};
}
