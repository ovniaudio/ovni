#pragma once
#include <array>
#include <vector>
#include "analysis/SpectrogramRing.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/StereoBandsFrame.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// StereoBands — el estéreo POR BANDA DE FRECUENCIA (spec §5.3, prompt 51). El diferencial del producto:
// no "la mezcla está fuera de fase", sino EN QUÉ FRECUENCIAS lo está y cuánto se pierde al monoficarlas.
//
// LA MATEMÁTICA es exactamente la del módulo Stereo (banda ancha, dominio del tiempo) llevada a los bins
// de la STFT, que es lo que Parseval permite. Para la banda `b` sobre los `K` frames de la ventana y los
// bins `k` que caen dentro de ella:
//
//     ΣLL = Σ|L_k|²   ΣRR = Σ|R_k|²   ΣLR = Σ Re(L_k·R_k*)   ΣMM = Σ|(L+R)/2|²   ΣSS = Σ|(L−R)/2|²
//
//     corr_b     = ΣLR / √(ΣLL·ΣRR)                        +1 mono · 0 sin correlación · −1 fuera de fase
//     width_b    = √(ΣSS / ΣMM)                            0 mono · 1 independientes · tope 10
//     balance_b  = 10·log10(ΣRR / ΣLL)                     + = R más fuerte
//     monoLoss_b = 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2)   0 si L=R · −3.01 indep · piso −60
//
// ΣMM y ΣSS no hacen falta guardarlas: salen de las otras tres, porque |(L±R)/2|² = (|L|² + |R|² ± 2·Re(L·R*))/4.
// Los mismos casos borde y los mismos clamps que Stereo, en `double`, y NUNCA un NaN: la UI dibuja esto.
//
// EL CRUCE OBLIGATORIO: la banda ancha calculada acá desde los bins tiene que coincidir con lo que mide el
// módulo Stereo en el dominio del tiempo con la misma ventana (±0.02 en corr, ±0.1 dB en monoLoss, medido
// sobre ruido rosa independiente). Si no coincide, uno de los dos está mal. Por eso se publica `wide*`.
//
// LA VENTANA. `K = round(sec · frames por segundo)` frames del módulo Spectrum — cuyas posiciones son
// fijas en el stream (conteo de muestras desde el reset), así que el determinismo se hereda: los mismos
// números al bit venga el audio en bloques de 1 o de 4 096. Se publica la ventana EFECTIVA, no la pedida.
//
// EL ANILLO POR BIN, y su tope. Una ventana deslizante exacta necesita poder RESTAR el frame que sale, y
// eso es guardar las tres sumas de cada bin de cada frame: hasta 3·sr/(2·(1−solape)) entradas, o sea
// ~576 000 a 48 kHz (7 MB en float) pero 2.3 millones a 192 kHz (28 MB), que ya es medio presupuesto de
// memoria del plugin para una lente. Por encima de `kMaxBinEntries` los frames se AGRUPAN de a `G` y la
// ventana se cuantiza a G frames: a 48 kHz G vale 1 siempre —o sea ventana rectangular exacta de K
// frames— y a 192 kHz vale 4 sobre ~4 500, que es medio milisegundo sobre tres segundos. Lo que se
// publica es, otra vez, la ventana que REALMENTE se sumó.
//
// El acumulador `total` es `double` y se le suma/resta EXACTAMENTE el mismo valor `float` que guarda el
// anillo, así que la cancelación es exacta y no hay deriva por más que la sesión dure horas.
// ========================================================================================================
namespace telescope
{
class StereoBands : public Spectrum::FrameSink
{
public:
    static constexpr int kNumBands = SpectrumFrame::kNumThird;   // 30 · ⅓ de octava ISO 266
    static constexpr int kRows     = StereoSpectrogramRing::kRows;

    static constexpr int   kNumWindowOptions = 3;
    static constexpr float kWindowSecOptions[kNumWindowOptions] = { 0.3f, 1.0f, 3.0f };
    static constexpr int   kDefaultWindowIndex = 1;              // 1 s

    static constexpr double kTinyEnergy = 1.0e-20;   // por debajo de esto no hay energía que medir
    static constexpr float  kWidthMax   = 10.0f;     // tope de width (L = −R daría infinito)
    static constexpr float  kDbFloor    = -60.0f;    // piso de balance y monoLoss
    static constexpr float  kDbCeil     =  60.0f;    // techo de balance

    // Tope del anillo por bin: entradas (frames × bins) que se guardan. 3 floats cada una → 6 MB.
    static constexpr int kMaxBinEntries = 1 << 19;

    struct Result
    {
        float corr      [kNumBands] {};
        float width     [kNumBands] {};
        float balanceDb [kNumBands] {};
        float monoLossDb[kNumBands] {};
        float energyDb  [kNumBands] {};   // (ΣLL+ΣRR)/frames de la banda, en la referencia de SpectrumFrame
        int   binsInBand[kNumBands] {};   // 0 = a esta resolución la banda no se puede medir

        // La misma cuenta sobre TODOS los bins: es lo que se cruza contra el módulo Stereo del tiempo.
        float wideCorr = 0.0f, wideWidth = 0.0f, wideBalanceDb = 0.0f, wideMonoLossDb = 0.0f;

        float windowSeconds = 0.0f;   // la ventana EFECTIVA
        int   windowFrames  = 0;      // los frames que realmente se sumaron
        int   valid         = 0;      // bandas con medición (bins y energía)
    };

    // 0 = 0.3 s · 1 = 1 s · 2 = 3 s. Cambiarla rearma el anillo (y por lo tanto lo limpia): sumar frames
    // medidos con dos ventanas distintas no daría ninguna de las dos.
    void setWindowIndex (int i) noexcept;
    int  windowIndex() const noexcept { return windowIdx; }

    // El anillo del espectrograma estéreo (opcional). Se le empuja una columna por frame EMITIDO.
    void setSpectrogramRing (StereoSpectrogramRing* r) noexcept;
    void setHistorySeconds (int seconds) noexcept;
    int  historySeconds() const noexcept { return historySec; }

    void reset();

    void spectrumFrameComputed (const Spectrum::FrameInfo&) override;

    const Result&           result() const noexcept { return current; }
    const StereoBandsFrame& frame()  const noexcept { return out; }
    juce::uint32            framesEmitted() const noexcept { return emitted; }

    // La última columna calculada (kRows × 2 bytes: [coherencia, energía] por fila). La leen los tests.
    const juce::uint8* column() const noexcept { return col.data(); }

    int numBins()    const noexcept { return bins; }
    int frameGroup() const noexcept { return group; }   // frames por grupo del anillo (1 en todo caso real)

    // Los dos mapeos byte ↔ número del espectrograma estéreo, en un solo lugar (los usan el módulo, la
    // lente y los tests: tres copias de la misma fórmula son tres oportunidades de que se separen).
    static juce::uint8 coherenceToByte (double c) noexcept;
    static float       byteToCoherence (int b) noexcept { return (float) ((double) b / 127.5 - 1.0); }
    static juce::uint8 dbToByte (double db, double rangeDb) noexcept;
    static float       byteToDb (int b, double rangeDb) noexcept
    {
        return (float) ((double) b / 255.0 * rangeDb - rangeDb);
    }

    // ====================================================================================================
    // ===== 53: FieldSink — el consumidor de los frames EMITIDOS, para el módulo Field (lente 11) =====
    //
    // Existe por la misma razón que el `Spectrum::FrameSink` del que este módulo cuelga: FIELD necesita el
    // paneo por bin y la energía por bin de CADA frame publicado, y volver a calcularlos sería repetir el
    // trabajo que acá ya está hecho. Enganchado al frame EMITIDO (no al calculado) por dos motivos:
    //
    //   · determinismo — los frames emitidos están en posiciones FIJAS del stream (uno de cada
    //     `emitEvery` calculados, y los calculados salen en t = N, N+H, N+2H…), así que la grilla del
    //     campo sale idéntica al bit venga el audio en bloques de 1 o de 4 096;
    //   · costo — con orden 12 y 87.5 % de solape se CALCULAN 375 frames por segundo y se EMITEN 60.
    //     Acumular una grilla de 96×64 seis veces de más por frame sería tirar CPU para dibujar lo mismo.
    //
    // `pan` apunta al `pan[]` del frame publicado (ventana deslizante) y `energy` a la energía
    // INSTANTÁNEA por bin del frame actual — el mismo par que dibuja el espectrograma estéreo: el paneo
    // suavizado (el de un frame solo es ruido) y el nivel sin suavizar (su eje es el tiempo).
    // ====================================================================================================
    struct FieldFrameInfo
    {
        const float*  pan    = nullptr;   // paneo por bin de la VENTANA, [−1, +1]
        const double* energy = nullptr;   // |L_k|² + |R_k|² INSTANTÁNEA, en la referencia de SpectrumFrame
        int    numBins   = 0;
        double sr        = 0.0;
        double binHz     = 0.0;
        double emitRate  = 0.0;           // frames EMITIDOS por segundo (el dt del decaimiento)
        juce::uint32 frameIndex = 0;      // índice del frame emitido
    };

    struct FieldSink
    {
        virtual ~FieldSink() = default;
        virtual void stereoBandsFrameEmitted (const FieldFrameInfo&) = 0;
        // Se llama cuando ESTE módulo se limpia o se rearma (cambió la ventana, los bins o la tasa): la
        // grilla del campo tampoco puede mezclar los dos lados. Que lo avise el módulo y no el
        // AnalysisThread evita tener que acordarse en cada uno de los cinco sitios que llaman a reset().
        virtual void stereoBandsReset() = 0;
    };

    void setFieldSink (FieldSink* s) noexcept { fieldSink = s; }
    FieldSink* fieldSink_() const noexcept { return fieldSink; }

private:
    void rebuild (const Spectrum::FrameInfo&);
    void updateResult();
    void buildFrame (const Spectrum::FrameInfo&);
    void buildColumn (const Spectrum::FrameInfo&);

    // Las tres sumas de la ventana para el bin k (grupos completos + el grupo en curso).
    struct Sums { double ll, rr, lr; };
    Sums sumsAt (int k) const noexcept
    {
        const auto i = (size_t) (3 * k);
        return { total[i] + cur[i], total[i + 1] + cur[i + 1], total[i + 2] + cur[i + 2] };
    }
    static void finish (const Sums& s, int frames, float& corr, float& width, float& balanceDb,
                        float& monoLossDb, float& energyDb) noexcept;

    int    bins = 0;
    double rate = 0.0;          // frames por segundo del módulo Spectrum
    double emitRate = 0.0;
    double binHz = 0.0;
    int    windowIdx = kDefaultWindowIndex;
    int    historySec = SpectrogramRing::kHistoryOptions[1];
    bool   dirty = true;        // hay que rearmar (cambió la ventana, los bins o la tasa de frames)

    int       windowTarget = 1;     // K frames pedidos
    int       group = 1;            // frames por grupo del anillo (G)
    int       ringGroups = 1;       // grupos que guarda el anillo
    long long writeGroup = 0;
    int       groupsFilled = 0;
    int       framesInGroup = 0;

    std::vector<double> total;      // 3 · bins · sumas de los grupos COMPLETOS
    std::vector<double> cur;        // 3 · bins · el grupo en curso
    std::vector<float>  ring;       // ringGroups · 3 · bins
    std::vector<double> inst;       // energía INSTANTÁNEA por bin (|L|² + |R|²) del frame actual

    int bandK0[kNumBands] {}, bandK1[kNumBands] {};

    Result           current;
    StereoBandsFrame out;
    juce::uint32     emitted = 0;

    // La columna del espectrograma estéreo: TAMAÑO FIJO (512 filas × 2 bytes). Array y no vector a
    // propósito — `reset()` puede llegar antes del primer frame (el AnalysisThread lo llama al arrancar)
    // y un vector todavía vacío ahí es un desbordamiento silencioso.
    std::array<juce::uint8, (size_t) StereoSpectrogramRing::kCellBytes> col {};
    StereoSpectrogramRing* spectrogram = nullptr;

    FieldSink* fieldSink = nullptr;   // ===== 53 =====
};
}
