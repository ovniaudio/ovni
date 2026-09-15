#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>
#include "analysis/SpectrogramRing.h"
#include "analysis/SpectrumFrame.h"

// ========================================================================================================
// Spectrum — el analizador de espectro de TELESCOPE (spec §5.2, prompt 50). Es el motor de la lente que
// más gente va a abrir, así que todo lo que se ve tiene una definición y un test.
//
// CÓMO CUENTA LOS FRAMES. El módulo tiene su propio ring de muestras por canal y un contador de muestras
// desde el reset. Un frame se computa en las posiciones FIJAS del stream
//
//     t = N,  N + H,  N + 2H,  …            con H = N·(1 - overlap)
//
// (N = tamaño de FFT). Que sean posiciones fijas del stream y no "cada vez que llega un bloque" es lo que
// hace que los números NO dependan del tamaño de bloque del host — lo mismo que hace el medidor de
// loudness con sus hops de 100 ms, y lo verifica el test del bloque 1/7/64/4096.
//
// El primer frame sale recién en t = N y no antes: hasta ahí el ring todavía tiene ceros del reset, y una
// ventana medio llena de ceros lee 6 dB abajo. Un número que arranca mintiendo por un cuarto de segundo
// no es un número.
//
// LA REFERENCIA DE dB está en analysis/SpectrumFrame.h (y entera en el README): un seno de amplitud A
// centrado en un bin lee exactamente 20·log10(A), con la corrección de ganancia coherente de la ventana.
//
// CADENCIA DE EMISIÓN. Con orden 10 y 87.5 % de solape salen 375 frames por segundo; publicar 260 KB
// trescientas setenta y cinco veces por segundo sería tirar 97 MB/s de memoria para dibujar 30. Los frames
// se COMPUTAN todos (el promediado y el peak hold los necesitan: solaparse es justo para eso) y se EMITEN
// uno de cada `emitEvery = ceil(frameRate/60)`, así la tasa de emisión nunca pasa de 60 Hz. La decimación
// es un entero, no un reloj: la emisión es tan determinista como el cómputo.
// ========================================================================================================
namespace telescope
{
class Spectrum
{
public:
    enum Window  { hann = 0, blackmanHarris4, kaiser9, kNumWindows };
    enum Channel { left = 0, right, mid, side, leftRight, kNumChannels };
    enum Average { avgNone = 0, avgExp, avgInfinite, kNumAverages };
    enum Bands   { bandsFft = 0, bandsThird, bandsBark, kNumBandModes };

    static constexpr int   kMinFftOrder = 10;      // 1 024
    static constexpr int   kMaxFftOrder = SpectrumFrame::kMaxFftOrder;   // 15 → 32 768
    static constexpr int   kNumOverlaps = 3;
    static constexpr float kOverlapOptions[kNumOverlaps] = { 0.5f, 0.75f, 0.875f };
    static constexpr int   kNumRanges = 3;
    static constexpr int   kRangeOptions[kNumRanges] = { 60, 90, 120 };

    static constexpr float kMinSlopeDbPerOct = 0.0f, kMaxSlopeDbPerOct = 4.5f, kSlopeStep = 0.5f;
    static constexpr float kMinAvgSeconds = 0.1f,    kMaxAvgSeconds = 10.0f;
    static constexpr float kMinHoldDecay  = 0.0f,    kMaxHoldDecay  = 60.0f;   // 0 = hold infinito

    // Tope de emisión: 60 Hz. Más rápido que eso no lo ve nadie y cuesta memoria de verdad.
    static constexpr double kMaxEmitHz = 60.0;

    // Los settings de la lente. NO son parámetros automatizables: viven en el ValueTree del APVTS (ver
    // TelescopeProcessor) y viajan al worker por atomics.
    struct Settings
    {
        int   fftOrder          = 12;                 // 10..15   (12 = 4 096)
        int   window            = hann;
        int   overlapIndex      = 1;                  // 75 %
        int   channel           = leftRight;
        float slopeDbPerOct     = 3.0f;               // el rosa se ve plano
        int   avgMode           = avgExp;
        float avgSeconds        = 0.5f;
        bool  peakHold          = true;
        float holdDecayDbPerSec = 12.0f;
        int   bandsMode         = bandsFft;
        int   rangeDbIndex      = 1;                  // 90 dB
        int   historySecIndex   = 1;                  // 30 s de espectrograma

        // Acota todo al rango legal (un estado guardado por una versión futura no puede romper el motor).
        void sanitise() noexcept;
        int   fftSize() const noexcept   { return 1 << fftOrder; }
        float overlap() const noexcept   { return kOverlapOptions[overlapIndex]; }
        int   rangeDb() const noexcept   { return kRangeOptions[rangeDbIndex]; }
        int   historySeconds() const noexcept { return SpectrogramRing::kHistoryOptions[historySecIndex]; }
        bool  operator== (const Settings& o) const noexcept;
        bool  operator!= (const Settings& o) const noexcept { return ! (*this == o); }
        // Cambiar cualquiera de estos obliga a rearmar el análisis (ring, promedios, hold, espectrograma).
        bool needsRestartVersus (const Settings& o) const noexcept;
    };

    // ====================================================================================================
    // FrameSink — el consumidor de TODOS los frames CALCULADOS, no sólo de los emitidos.
    //
    // Existe para el módulo StereoBands (prompt 51), que acumula por frame: si comiera sólo los emitidos
    // se perdería 6 de cada 7 con solape del 87.5 %, y su ventana de K frames diría una cosa y sumaría
    // otra. Recibe los complejos de L y R YA TRANSFORMADOS: el estéreo por banda no vuelve a hacer una
    // FFT que este módulo ya hizo.
    //
    // Con un sink enganchado, el módulo calcula SIEMPRE L y R aunque el modo de canal de la lente sea
    // otro (M, S, o un canal solo). Es el costo declarado de tener las dos lentes de fase abiertas: hasta
    // dos transformadas más por frame. Sin sink no se paga nada.
    // ====================================================================================================
    struct FrameInfo
    {
        const float* left  = nullptr;   // complejos intercalados (2·N floats), como los deja la FFT real
        const float* right = nullptr;
        int    numBins   = 0;
        double sr        = 0.0;
        double binHz     = 0.0;
        // (re² + im²) · powerNorm = potencia en la MISMA referencia de dB que SpectrumFrame (ver su
        // encabezado): un seno de amplitud A centrado en un bin da 20·log10(A).
        double powerNorm = 1.0;
        double frameRate = 0.0;         // frames CALCULADOS por segundo (sr/hop)
        double emitRate  = 0.0;         // frames EMITIDOS por segundo (frameRate / decimación)
        int    rangeDb   = 90;
        bool   emitted   = false;       // ¿este frame es de los que se materializan para la UI?
        // ===== 55 · la POSICIÓN de este frame en el stream, en muestras desde el reset =====
        //
        // Es la última muestra que entró en su ventana (o sea: la ventana cubre `(streamPos − N, streamPos]`).
        // La necesita la historia por SEGUNDO para meter cada frame en el segundo que le corresponde en vez
        // de "el segundo que estaba abierto cuando llegó". La diferencia no es cosmética: el espectro come
        // del CHUNK y el medidor come de HOPS, así que el espectro va SIEMPRE adelantado hasta un chunk —
        // y el chunk mide 4 800 muestras en vivo y 4 096 en el análisis de archivo. Repartir por posición
        // hace que el archivo y el vivo den las MISMAS filas al bit; repartir por "lo que llegó hasta
        // ahora" las haría depender del tamaño del bloque, que es justo lo que el motor evita en todo
        // el resto.
        long long streamPos = 0;
    };

    struct FrameSink
    {
        virtual ~FrameSink() = default;
        virtual void spectrumFrameComputed (const FrameInfo&) = 0;
    };

    void setFrameSink (FrameSink* s) noexcept { sink = s; }
    FrameSink* frameSink() const noexcept { return sink; }

    // El ring del espectrograma (opcional: los tests del espectro puro no lo necesitan). El módulo le
    // empuja una columna por frame EMITIDO, así `colsPerSec` del ring es exactamente la tasa de emisión.
    void setSpectrogramRing (SpectrogramRing* r) noexcept;

    void prepare (double sampleRate);
    void reset();

    // Se aplican en el próximo frame. Si cambia algo estructural (tamaño de FFT, ventana, solape, canal),
    // el análisis ARRANCA DE CERO: promediar un espectro de 4 096 con uno de 32 768 no querría decir nada.
    void applySettings (const Settings& s);
    const Settings& settings() const noexcept { return current; }

    // Acepta cualquier n: las posiciones de los frames las decide el contador interno (ver el encabezado).
    void process (const float* L, const float* R, int n);

    const SpectrumFrame& frame() const noexcept { return out; }
    // Frames EMITIDOS desde el reset. La UI/el thread comparan contra el suyo para saber si hay algo nuevo.
    juce::uint32 framesEmitted() const noexcept { return emitted; }

    int    fftSize()    const noexcept { return size; }
    int    numBins()    const noexcept { return bins; }
    int    hopSamples() const noexcept { return hop; }
    double frameRate()  const noexcept { return hop > 0 ? sr / (double) hop : 0.0; }
    double emitRate()   const noexcept { return frameRate() / (double) emitEvery; }
    int    emitDecimation() const noexcept { return emitEvery; }

    // La ventana, para quien tenga que rehacer la cuenta: CG = Σw/N (ganancia coherente, la que normaliza
    // la amplitud de un seno) y NPG = Σw²/N (ganancia de potencia, la que normaliza la energía del ruido).
    double coherentGain()   const noexcept { return cg; }
    double noisePowerGain() const noexcept { return npg; }

    // Llena `dst` (N muestras) con la ventana pedida y devuelve su CG. Estática para que el test de fuga
    // pueda medir la respuesta de las tres ventanas sin instanciar el módulo entero.
    static double fillWindow (float* dst, int n, int windowType);

    // La última columna de espectrograma calculada (SpectrogramRing::kRows bytes). La leen los tests.
    const juce::uint8* spectrogramColumn() const noexcept { return column.data(); }

private:
    void rebuild();          // (re)dimensiona ventana, FFT, rings y acumuladores, y resetea
    void computeFrame();
    void transform (const float* ring, int ringWritePos, float* dstComplex) const;
    void updateBandsAndHold (int slot, double dt, bool emit);
    void buildSpectrogramColumn();

    double sr = 48000.0;
    Settings current;

    int size = 4096, bins = 2049, hop = 1024, emitEvery = 1;

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float>  win;                 // la ventana (size muestras)
    std::vector<float>  ringL, ringR;        // el ring de muestras por canal (size muestras)
    int                 ringWrite = 0;
    long long           samplesSinceReset = 0;
    long long           nextFrameAt = 0;     // posición del próximo frame en el stream
    juce::uint32        computedFrames = 0;  // frames CALCULADOS (para la decimación de emisión)
    juce::uint32        emitted = 0;         // frames EMITIDOS

    double cg = 0.5, npg = 0.375;
    double normAmp = 1.0;                    // 2/(N·CG): de |X_k| a amplitud de seno equivalente

    // Trabajo por frame: los complejos de cada espectro (2·size floats) y la potencia promediada.
    std::vector<float>  work[SpectrumFrame::kMaxSpectra];
    // Los complejos de L y R cuando el modo de canal NO es L+R y hay un sink enganchado. Se dimensionan
    // recién cuando hacen falta: sin sink no cuestan un byte.
    std::vector<float>  lrWork[2];
    FrameSink*          sink = nullptr;
    std::vector<double> avgPow[SpectrumFrame::kMaxSpectra];
    std::vector<double> curPow[SpectrumFrame::kMaxSpectra];
    std::vector<float>  scratch;             // señal derivada (M o S) antes de ventanear
    double              avgCount = 0.0;      // para el promedio infinito
    std::vector<float>  hold[SpectrumFrame::kMaxSpectra];
    float               bandHold[SpectrumFrame::kMaxSpectra][SpectrumFrame::kNumThird] {};

    // La columna del espectrograma del frame emitido, y el ring donde va a parar.
    std::array<juce::uint8, (size_t) SpectrogramRing::kRows> column {};
    std::vector<double> midPow;        // potencia instantánea de M cuando el modo es L+R
    SpectrogramRing*    spectrogram = nullptr;

    SpectrumFrame out;
};
}
