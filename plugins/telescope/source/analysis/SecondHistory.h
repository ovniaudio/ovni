#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>
#include "analysis/AnalysisFrame.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// SecondHistory — la historia POR SEGUNDO (spec §5.7, prompt 55). Es lo que hace posible el "dónde" de
// VERDICT: sin una serie por segundo, la lente 13 podría decir "hay un hueco en 630 Hz" pero nunca
// "entre 0:20 y 0:35".
//
// TRES PIEZAS EN ESTE ARCHIVO:
//   · `SecondRow`     el POD de un segundo (append-only, como AnalysisFrame: se AGREGAN campos, nunca se
//                     renombran ni se reordenan).
//   · `SecondBuilder` el que arma la fila. Es un `Spectrum::FrameSink` (come los complejos del espectro
//                     FIJO) más un contador de hops. Lo usan los DOS lados: el AnalysisThread en vivo y
//                     el FileAnalyzer sobre el archivo. UNA sola implementación, no dos.
//   · `SecondHistory` el ring de 10 minutos a 1 Hz que la lente lee.
//
// ========================================================================================================
// POR QUÉ EL ESPECTRO SE REPARTE POR **POSICIÓN DE STREAM** Y NO POR "LO QUE LLEGÓ HASTA AHORA"
//
// El medidor de loudness come HOPS de 100 ms exactos; el espectro come el CHUNK que se acaba de drenar y
// decide sus propias posiciones de frame. O sea que cuando se cierra el décimo hop de un segundo, el
// espectro ya consumió TODO el chunk que contenía esa muestra — y va adelantado una cantidad que depende
// del tamaño del chunk: 4 800 muestras en vivo (0.1 s) y 4 096 en el análisis de archivo.
//
// Si la fila se armara con "los frames que llegaron hasta que cerró el segundo", el archivo y el vivo
// sumarían conjuntos DISTINTOS de frames y las filas no coincidirían — justo lo contrario de la promesa
// que sostiene todo el módulo. Por eso cada frame se mete en el bucket del segundo al que pertenece por su
// `streamPos`, y el bucket se cierra cuando el hop número 10 de ese segundo termina.
//
// Y ESO ES SUFICIENTE, no "casi": un frame en la posición `p` se computa cuando el espectro consumió `p`
// muestras; cuando cierra el décimo hop del segundo `s`, el hop va por la muestra `(s+1)·sr` y el espectro
// consumió AL MENOS esa cantidad (fue el mismo chunk). Entonces todos los frames con `p ≤ (s+1)·sr` ya
// llegaron, o sea todos los del segundo `s`. Los que se adelantaron caen en el bucket `s+1`, que todavía
// no se cerró. El ring de buckets tiene 4 entradas: el adelanto máximo es un chunk, o sea 0.1 s.
// ========================================================================================================
namespace telescope
{
// Los módulos de los que depende una fila. `kReference` es el bit que enciende la FFT FIJA (ver
// AnalysisThread::feedReference): sin él no hay parte espectral, y la fila lo dice en vez de publicar
// treinta ceros que parecerían una medición.
inline constexpr juce::uint32 kSecondRowModules = kLoudness | kReference | kCqt;

// ========================================================================================================
// SecondRow — un segundo de programa. APPEND-ONLY.
//
// Los campos espectrales usan la MISMA definición de banda que `ThirdOctaveAverage` (bordes fc·2^∓1/6 y
// corrección de ancho cubierto), porque VERDICT compara estas filas contra la curva de TONAL BALANCE y
// dos rejillas distintas para la misma banda harían que los números no se puedan cruzar.
// ========================================================================================================
struct SecondRow
{
    static constexpr int kNumBands = SpectrumFrame::kNumThird;   // 30 · ⅓ de octava ISO 266

    // ---- espectro y estéreo por banda (promedio de POTENCIA sobre los frames del segundo) ----
    float bandsDb       [kNumBands];   // dBFS, referencia de SpectrumFrame; piso si la banda no se midió
    float bandCorr      [kNumBands];   // ΣLR / √(ΣLL·ΣRR)      +1 mono · 0 sin correlación · −1 fuera de fase
    float bandMonoLossDb[kNumBands];   // 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2)   0 si L=R · −3.01 indep

    // ---- loudness del segundo (de los diez hops) ----
    float shortTermMin  = kSilenceDb;
    float shortTermMax  = kSilenceDb;
    float momentaryMax  = kSilenceDb;
    float tpMaxDbtp     = kSilenceDb;
    juce::uint32 clipEvents = 0;       // los que EMPEZARON en este segundo

    // ---- estéreo de banda ancha (las mismas sumas, sobre TODOS los bins) ----
    float corr       = 0.0f;
    float width      = 0.0f;
    float balanceDb  = 0.0f;
    float monoLossDb = 0.0f;

    // ---- continua: media de las muestras del segundo, por canal ----
    float dcL = 0.0f, dcR = 0.0f;

    // ---- tonalidad estimada al cerrar el segundo (−1 = sin medición) ----
    int   keyTonic = -1, keyMode = -1;
    float keyConfidence = 0.0f;

    // Qué módulos midieron ESTE segundo (los diez hops). Un módulo que se encendió a mitad no cuenta: una
    // fila a medias que no se declara a medias es peor que una fila que falta.
    juce::uint32 measuredModules = 0;

    SecondRow()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            bandsDb[b]        = SpectrumFrame::kFloorDb;
            bandCorr[b]       = 0.0f;
            bandMonoLossDb[b] = 0.0f;
        }
    }

    bool hasSpectrum() const noexcept { return (measuredModules & kReference)  != 0; }
    bool hasLoudness() const noexcept { return (measuredModules & kLoudness)   != 0; }
    bool hasKey()      const noexcept { return (measuredModules & kCqt)        != 0 && keyTonic >= 0; }

    bool bandMeasured (int b) const noexcept
    {
        return hasSpectrum() && b >= 0 && b < kNumBands && bandsDb[b] > SpectrumFrame::kFloorDb;
    }

    // El ancho estéreo de una banda NO se guarda: sale EXACTO de la pérdida al monoficar, porque
    // ΣMM + ΣSS = (ΣLL + ΣRR)/2 por construcción. Con m = monoLoss en dB:
    //     ΣMM / ((ΣLL+ΣRR)/2) = 10^(m/10)   →   width² = ΣSS/ΣMM = 10^(−m/10) − 1
    // Guardar un número que se deduce de otro es guardar dos oportunidades de que se contradigan.
    static float widthFromMonoLossDb (float monoLossDb) noexcept
    {
        const double w2 = std::pow (10.0, -(double) monoLossDb / 10.0) - 1.0;
        return (float) juce::jlimit (0.0, 10.0, w2 > 0.0 ? std::sqrt (w2) : 0.0);
    }

    float bandWidth (int b) const noexcept
    {
        return bandMeasured (b) ? widthFromMonoLossDb (bandMonoLossDb[b]) : 0.0f;
    }
};

// ========================================================================================================
// SecondBuilder — arma las filas. Es el MISMO objeto en vivo y en el archivo.
// ========================================================================================================
class SecondBuilder : public Spectrum::FrameSink
{
public:
    static constexpr int kNumBands   = SecondRow::kNumBands;
    static constexpr int kHopsPerSec = 10;      // hops de 100 ms
    static constexpr int kBuckets    = 4;       // el adelanto del espectro nunca pasa de un chunk (0.1 s)

    // Los mismos clamps que StereoBands: si dos módulos publican el mismo número con topes distintos, se
    // contradicen en pantalla.
    static constexpr double kTinyEnergy = 1.0e-20;
    static constexpr float  kDbFloor    = -60.0f;
    static constexpr float  kDbCeil     =  60.0f;

    void prepare (double sampleRate) noexcept
    {
        srSamples = juce::jmax (1LL, (long long) std::llround (sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (auto& bk : buckets) bk = Bucket{};
        hop = Hop{};
        hopsInSecond = 0;
        secondsClosed = 0;
        currentSecond = 0;
        spectrumOrigin = 0;
        geometry = Geometry{};
    }

    long long secondsClosedCount() const noexcept { return secondsClosed; }

    // ========================================================================================================
    // EL ORIGEN DEL ESPECTRO. `FrameInfo::streamPos` cuenta desde el reset del módulo Spectrum que emite,
    // y ese módulo se resetea cuando la lente lo ENCIENDE a mitad de una sesión (flanco de subida de
    // kReference: sin resetear, el primer promedio mezclaría muestras de la última vez que estuvo activo).
    // El reloj de los hops, en cambio, no se reinicia. Así que el que engancha el builder le dice en qué
    // muestra ABSOLUTA —contada desde el RESET del análisis— quedó el `streamPos = 0` del espectro.
    //
    // Con los dos relojes atados, un frame va SIEMPRE al segundo que le corresponde en el tema, no al que
    // le tocaría por casualidad según cuándo se abrió la lente.
    // ========================================================================================================
    void setSpectrumOrigin (long long absoluteSample) noexcept
    {
        spectrumOrigin = absoluteSample;
        for (auto& bk : buckets) bk = Bucket{};
    }

    //=================================================================== lado ESPECTRO (la instancia FIJA)
    void spectrumFrameComputed (const Spectrum::FrameInfo& info) override
    {
        if (info.left == nullptr || info.right == nullptr || info.numBins <= 0 || info.binHz <= 0.0) return;

        const int n = 2 * (info.numBins - 1);
        if (n != geometry.fftSize || info.sr != geometry.sr) rebuildGeometry (info, n);

        const long long absPos = spectrumOrigin + info.streamPos;
        const long long sec = srSamples > 0 ? absPos / srSamples : 0;
        auto& bk = bucketFor (sec);

        for (int b = 0; b < kNumBands; ++b)
        {
            const int k0 = geometry.k0[b], k1 = geometry.k1[b];
            if (k1 <= k0) continue;

            double ll = 0.0, rr = 0.0, lr = 0.0;
            for (int k = k0; k < k1; ++k)
            {
                const double lre = info.left[2 * k],  lim = info.left[2 * k + 1];
                const double rre = info.right[2 * k], rim = info.right[2 * k + 1];
                ll += lre * lre + lim * lim;
                rr += rre * rre + rim * rim;
                lr += lre * rre + lim * rim;      // Re(L·R*)
            }

            bk.ll[b] += ll;
            bk.rr[b] += rr;
            bk.lr[b] += lr;
            // La POTENCIA de la banda con la misma corrección de ancho cubierto que ThirdOctaveAverage
            // (ver FileAnalysis.h): sin ella, la misma señal lee distinto según dónde caiga la rejilla.
            bk.power[b] += (ll + rr) * info.powerNorm * geometry.widthFactor[b];
        }

        // Y las mismas tres sumas sobre TODOS los bins: el estéreo de banda ancha del segundo.
        double wll = 0.0, wrr = 0.0, wlr = 0.0;
        for (int k = 0; k < info.numBins; ++k)
        {
            const double lre = info.left[2 * k],  lim = info.left[2 * k + 1];
            const double rre = info.right[2 * k], rim = info.right[2 * k + 1];
            wll += lre * lre + lim * lim;
            wrr += rre * rre + rim * rim;
            wlr += lre * rre + lim * rim;
        }
        bk.wideLL += wll;
        bk.wideRR += wrr;
        bk.wideLR += wlr;
        ++bk.frames;
    }

    //=================================================================== lado HOP (100 ms exactos)
    struct HopInput
    {
        juce::uint32 modules = 0;          // la máscara con la que corrió ESTE hop
        float shortTerm   = kSilenceDb;
        float momentary   = kSilenceDb;
        float truePeakHop = kSilenceDb;
        juce::uint32 clipEventsHop = 0;
        double dcSumL = 0.0, dcSumR = 0.0;   // SUMA de las muestras del hop (no la media: se dividen al final)
        int    hopSamples = 0;
        int    keyTonic = -1, keyMode = -1;
        float  keyConfidence = 0.0f;
    };

    // Devuelve true (y llena `out`) cuando este hop cerró un segundo.
    bool pushHop (const HopInput& in, SecondRow& out)
    {
        if (hopsInSecond == 0)
        {
            hop = Hop{};
            hop.modules = in.modules;
        }
        else
        {
            hop.modules &= in.modules;   // un módulo que se encendió a mitad NO cuenta para esta fila
        }

        hop.shortTermMin = juce::jmin (hop.shortTermMin, in.shortTerm);
        hop.shortTermMax = juce::jmax (hop.shortTermMax, in.shortTerm);
        hop.momentaryMax = juce::jmax (hop.momentaryMax, in.momentary);
        hop.tpMax        = juce::jmax (hop.tpMax, in.truePeakHop);
        hop.clips       += in.clipEventsHop;
        hop.dcSumL      += in.dcSumL;
        hop.dcSumR      += in.dcSumR;
        hop.samples     += (long long) juce::jmax (0, in.hopSamples);
        hop.keyTonic      = in.keyTonic;
        hop.keyMode       = in.keyMode;
        hop.keyConfidence = in.keyConfidence;

        if (++hopsInSecond < kHopsPerSec) return false;

        out = finish();
        hopsInSecond = 0;
        ++currentSecond;
        ++secondsClosed;
        return true;
    }

private:
    struct Bucket
    {
        long long second = -1;
        long long frames = 0;
        double ll[kNumBands] {}, rr[kNumBands] {}, lr[kNumBands] {}, power[kNumBands] {};
        double wideLL = 0.0, wideRR = 0.0, wideLR = 0.0;
    };

    struct Hop
    {
        juce::uint32 modules = 0;
        float shortTermMin =  1.0e9f;
        float shortTermMax = -1.0e9f;
        float momentaryMax = kSilenceDb;
        float tpMax        = kSilenceDb;
        juce::uint32 clips = 0;
        double dcSumL = 0.0, dcSumR = 0.0;
        long long samples = 0;
        int   keyTonic = -1, keyMode = -1;
        float keyConfidence = 0.0f;
    };

    struct Geometry
    {
        int    fftSize = 0;
        double sr = 0.0;
        int    k0[kNumBands] {}, k1[kNumBands] {};
        double widthFactor[kNumBands] {};
    };

    void rebuildGeometry (const Spectrum::FrameInfo& info, int fftSize) noexcept
    {
        // Geometría nueva: los buckets a medio llenar no se pueden mezclar con la resolución nueva.
        for (auto& bk : buckets) bk = Bucket{};
        geometry.fftSize = fftSize;
        geometry.sr = info.sr;

        constexpr double kSixthDown = 0.8908987181403393;   // 2^(-1/6)
        constexpr double kSixthUp   = 1.1224620483093730;   // 2^(+1/6)

        for (int b = 0; b < kNumBands; ++b)
        {
            const double lo = kThirdOctaveHz[b] * kSixthDown, hi = kThirdOctaveHz[b] * kSixthUp;
            const int k0 = juce::jmax (0, (int) std::ceil (lo / info.binHz));
            const int k1 = juce::jmin (info.numBins, (int) std::ceil (hi / info.binHz));
            geometry.k0[b] = k0;
            geometry.k1[b] = k1;
            geometry.widthFactor[b] = k1 > k0 ? (hi - lo) / ((double) (k1 - k0) * info.binHz) : 0.0;
        }
    }

    Bucket& bucketFor (long long sec) noexcept
    {
        auto& bk = buckets[(size_t) (sec % kBuckets)];
        if (bk.second != sec) { bk = Bucket{}; bk.second = sec; }
        return bk;
    }

    // Las cinco cuentas de StereoBands (spec §5.3) a partir de ΣLL, ΣRR y ΣLR. ΣMM y ΣSS no se guardan:
    // salen de las otras tres porque |(L±R)/2|² = (|L|² + |R|² ± 2·Re(L·R*))/4.
    struct Stereo { float corr, balanceDb, monoLossDb; };

    static Stereo stereoFrom (double ll, double rr, double lr) noexcept
    {
        Stereo s { 0.0f, 0.0f, 0.0f };
        if (ll <= kTinyEnergy && rr <= kTinyEnergy) return s;

        const double denom = std::sqrt (ll * rr);
        s.corr = denom > kTinyEnergy ? (float) juce::jlimit (-1.0, 1.0, lr / denom) : 0.0f;
        s.balanceDb = (ll > kTinyEnergy && rr > kTinyEnergy)
                        ? (float) juce::jlimit ((double) kDbFloor, (double) kDbCeil, 10.0 * std::log10 (rr / ll))
                        : (rr > ll ? kDbCeil : kDbFloor);

        const double mm = (ll + rr + 2.0 * lr) * 0.25;
        const double ref = (ll + rr) * 0.5;
        // 56b: el mismo TECHO de 0 dB que aplica StereoBands::finish. Sumar a mono no puede DAR ganancia:
        // el máximo teórico es 0 dB (L = R). Sin el techo, el redondeo de punto flotante en material casi
        // mono deja +0.0001 dB y las dos rutas —la del vivo y la de la fila por segundo— dan números
        // distintos para el mismo audio, que es justo lo que este POD existe para evitar.
        s.monoLossDb = (mm > kTinyEnergy && ref > kTinyEnergy)
                         ? (float) juce::jlimit ((double) kDbFloor, 0.0, 10.0 * std::log10 (mm / ref))
                         : kDbFloor;
        return s;
    }

    SecondRow finish() noexcept
    {
        SecondRow r;
        r.measuredModules = hop.modules & kSecondRowModules;

        if ((r.measuredModules & kLoudness) != 0)
        {
            r.shortTermMin = hop.shortTermMin <=  1.0e8f ? hop.shortTermMin : kSilenceDb;
            r.shortTermMax = hop.shortTermMax >= -1.0e8f ? hop.shortTermMax : kSilenceDb;
            r.momentaryMax = hop.momentaryMax;
            r.tpMaxDbtp    = hop.tpMax;
            r.clipEvents   = hop.clips;
            if (hop.samples > 0)
            {
                r.dcL = (float) (hop.dcSumL / (double) hop.samples);
                r.dcR = (float) (hop.dcSumR / (double) hop.samples);
            }
        }

        if ((r.measuredModules & kCqt) != 0)
        {
            r.keyTonic      = hop.keyTonic;
            r.keyMode       = hop.keyMode;
            r.keyConfidence = hop.keyConfidence;
        }

        const auto& bk = buckets[(size_t) (currentSecond % kBuckets)];
        if ((r.measuredModules & kReference) != 0 && bk.second == currentSecond && bk.frames > 0)
        {
            const double inv = 1.0 / (double) bk.frames;
            for (int b = 0; b < kNumBands; ++b)
            {
                if (geometry.k1[b] <= geometry.k0[b] || bk.power[b] <= 0.0) continue;
                r.bandsDb[b] = (float) juce::jmax ((double) SpectrumFrame::kFloorDb,
                                                   10.0 * std::log10 (bk.power[b] * inv));
                const auto s = stereoFrom (bk.ll[b], bk.rr[b], bk.lr[b]);
                r.bandCorr[b]       = s.corr;
                r.bandMonoLossDb[b] = s.monoLossDb;
            }

            const auto w = stereoFrom (bk.wideLL, bk.wideRR, bk.wideLR);
            r.corr       = w.corr;
            r.balanceDb  = w.balanceDb;
            r.monoLossDb = w.monoLossDb;
            r.width      = SecondRow::widthFromMonoLossDb (w.monoLossDb);
        }
        else
        {
            // Sin parte espectral la fila NO declara kReference, así que nadie va a leer estos campos;
            // se dejan en cero explícitamente para que la huella de determinismo sea estable.
            r.measuredModules &= ~kReference;
        }

        return r;
    }

    std::array<Bucket, (size_t) kBuckets> buckets {};
    Hop       hop {};
    Geometry  geometry {};
    long long srSamples = 48000;
    long long spectrumOrigin = 0;
    long long currentSecond = 0;
    long long secondsClosed = 0;
    int       hopsInSecond = 0;
};

// ========================================================================================================
// SecondHistory — el ring de 10 minutos a 1 Hz, desde el último RESET. Escribe el worker (o el analizador
// de archivo), lee el message thread. Misma disciplina que LoudnessHistory: un SpinLock de sección corta,
// con el escritor entrando UNA vez por segundo.
//
// 600 filas × ~380 bytes = ~228 KB. Es el precio de poder decir "entre 0:20 y 0:35".
// ========================================================================================================
class SecondHistory
{
public:
    static constexpr int kHz       = 1;
    static constexpr int kCapacity = 10 * 60 * kHz;   // 10 minutos

    void push (const SecondRow& row)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        rows[(size_t) (writePos % kCapacity)] = row;
        ++writePos;
    }

    void clear() noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        writePos = 0;
    }

    int size() const noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return juce::jmin (writePos, kCapacity);
    }

    // Cuántos segundos se analizaron DESDE EL RESET (puede pasar la capacidad: el ring se come los viejos).
    int analysed() const noexcept
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return writePos;
    }

    // Copia las últimas `count` filas en orden cronológico. Devuelve cuántas copió y, en `firstSecond`,
    // el índice absoluto de la primera (para que los `mm:ss` sean los del tema y no los del buffer).
    int copyLatest (std::vector<SecondRow>& dst, int count, int* firstSecond = nullptr) const
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        const int have = juce::jmin (writePos, kCapacity);
        const int n    = juce::jmin (count, have);
        dst.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            dst[(size_t) i] = rows[(size_t) ((writePos - n + i) % kCapacity)];
        if (firstSecond != nullptr) *firstSecond = writePos - n;
        return n;
    }

private:
    mutable juce::SpinLock lock;
    std::array<SecondRow, kCapacity> rows {};
    int writePos = 0;   // monotónico; el índice real es writePos % kCapacity
};
}
