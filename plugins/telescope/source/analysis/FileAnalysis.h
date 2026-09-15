#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstring>
#include <vector>
#include "analysis/AnalysisFrame.h"
#include "analysis/SecondHistory.h"
#include "analysis/SpectrumFrame.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// FileAnalysis — lo que deja el análisis OFFLINE de un archivo (spec §5.7, prompt 54).
//
// Es el resultado de correr EL MISMO MOTOR (Loudness + Spectrum) sobre un archivo entero, más rápido que
// tiempo real. Dos consumidores:
//
//   · TONAL BALANCE (lente 12) lo usa como REFERENCIA: compara la curva del programa en vivo contra la
//     del archivo, las dos normalizadas a su propia loudness integrada. Se compara el TILT, no el nivel.
//   · VERDICT (lente 13, prompt 55) va a usar `shortTermHistory` y `truePeakPerSecond` para decir "qué
//     falta y dónde" con tiempos exactos: los dos son series temporales con cadencia FIJA y declarada.
//
// LA PROMESA QUE SOSTIENE TODO ESTO: el análisis offline da EXACTAMENTE los mismos números que el análisis
// en vivo del mismo audio — no "parecidos", idénticos al bit. Se puede prometer porque el motor es
// independiente del tamaño de bloque por construcción (contratos casa-4 y SPEC[bloque]), y porque acá se
// usan las MISMAS clases, no una segunda implementación. Lo verifica FileAnalyzerTest FILE[identidad].
//
// `ok` vs `valid`: `ok` dice que el archivo se pudo abrir y leer (si es false, `error` explica por qué);
// `valid` dice que el análisis LLEGÓ AL FINAL (false si se canceló a mitad). Un análisis cancelado no es
// un error, pero sus números no son de todo el archivo y no se pueden usar como referencia.
// ========================================================================================================
namespace telescope
{
struct FileAnalysis
{
    static constexpr int kNumBands = SpectrumFrame::kNumThird;   // 30 · ⅓ de octava ISO 266

    bool         ok    = false;
    juce::String error;     // por qué NO se pudo (vacío si ok)
    juce::String warning;   // se pudo, pero con una salvedad (p. ej. más de dos canales)
    juce::String name;      // nombre del archivo, tal como se muestra
    juce::String path;      // ruta completa (es lo que persiste en el estado)

    double sr       = 0.0;   // la del ARCHIVO, no la de la sesión (ver el encabezado de FileAnalyzer)
    int    channels = 0;
    double seconds  = 0.0;

    float integratedLufs = kSilenceDb;
    float lra            = 0.0f;
    float truePeakDbtp   = kSilenceDb;
    float momentaryMax   = kSilenceDb;
    float shortTermMax   = kSilenceDb;

    // ⅓ de octava, promedio INFINITO de potencia sobre todos los frames del archivo, en dBFS con la misma
    // referencia que SPECTRUM (ver SpectrumFrame.h). Suma LOS DOS canales: una señal mono lee 3.01 dB por
    // encima de lo que lee el canal L solo — la misma nota que StereoBandsFrame.
    float bandsDb[kNumBands];

    std::vector<float> shortTermHistory;    // LUFS short-term, uno por hop de 100 ms → 10 Hz EXACTOS
    std::vector<float> truePeakPerSecond;   // dBTP máximo de cada segundo → 1 Hz EXACTO

    // ===== 55: LAS MISMAS FILAS POR SEGUNDO que arma el análisis en vivo (analysis/SecondHistory.h).
    // Es lo que le permite a VERDICT decir "entre 0:20 y 0:35" sobre un archivo con tiempos EXACTOS, y
    // producir el MISMO informe que produciría escuchándolo en vivo. Una fila por segundo COMPLETO: el
    // resto final de menos de un segundo no genera fila (igual que en vivo).
    //
    // Se llama `secondRows` y no `seconds` porque `seconds` YA es la duración del archivo en este mismo
    // POD: dos campos con el mismo nombre no pueden convivir, y renombrar el viejo rompería el contrato
    // append-only de la estructura.
    std::vector<SecondRow> secondRows;

    // ===== 55: los AGREGADOS que VERDICT lee del frame en vivo y que el archivo tiene que traer para
    // producir el MISMO informe (ver Verdict::aggregatesFrom). Salen del mismo medidor y del mismo CQT
    // que las filas, al final del archivo — no de un resumen de las filas.
    juce::uint32 clipEvents = 0;
    float        dcL = 0.0f, dcR = 0.0f;
    int          keyTonic = -1, keyMode = -1;
    float        keyConfidence = 0.0f, keyTimeFraction = 0.0f;

    bool valid = false;

    FileAnalysis() { for (auto& b : bandsDb) b = SpectrumFrame::kFloorDb; }

    // La huella de UNA fila por segundo. Aparte para que se pueda comparar fila a fila (el test de la
    // historia dice CUÁL difiere, no sólo que difieren).
    template <typename Hex>
    static juce::String secondFingerprint (const SecondRow& r, const Hex& hex)
    {
        juce::String s;
        s << "m" << (int) r.measuredModules;
        for (int b = 0; b < SecondRow::kNumBands; ++b)
            s << "," << hex (r.bandsDb[b]) << "/" << hex (r.bandCorr[b]) << "/" << hex (r.bandMonoLossDb[b]);
        s << "|" << hex (r.shortTermMin) << "," << hex (r.shortTermMax) << "," << hex (r.momentaryMax)
          << "," << hex (r.tpMaxDbtp) << "," << (int) r.clipEvents
          << "," << hex (r.corr) << "," << hex (r.width) << "," << hex (r.balanceDb) << "," << hex (r.monoLossDb)
          << "," << hex (r.dcL) << "," << hex (r.dcR)
          << "," << r.keyTonic << "," << r.keyMode << "," << hex (r.keyConfidence);
        return s;
    }

    // Serialización de texto, para comparar DOS análisis byte a byte (test de determinismo). No es un
    // formato de archivo: es una huella. Los float van en hexadecimal para que la comparación sea del
    // BIT y no de cuántos decimales imprime printf.
    juce::String fingerprint() const
    {
        const auto hex = [] (float v)
        {
            juce::uint32 bits;
            static_assert (sizeof (bits) == sizeof (v), "float de 32 bits");
            std::memcpy (&bits, &v, sizeof (bits));
            return juce::String::toHexString ((int) bits);
        };

        juce::String s;
        s << "ok=" << (int) ok << " valid=" << (int) valid << " err=" << error << " warn=" << warning
          << " name=" << name << " sr=" << juce::String (sr, 6) << " ch=" << channels
          << " sec=" << juce::String (seconds, 9)
          << " I=" << hex (integratedLufs) << " LRA=" << hex (lra) << " TP=" << hex (truePeakDbtp)
          << " Mmax=" << hex (momentaryMax) << " Smax=" << hex (shortTermMax);
        for (int b = 0; b < kNumBands; ++b) s << " b" << b << "=" << hex (bandsDb[b]);
        s << " st=" << (int) shortTermHistory.size();
        for (const auto v : shortTermHistory) s << ":" << hex (v);
        s << " tp=" << (int) truePeakPerSecond.size();
        for (const auto v : truePeakPerSecond) s << ":" << hex (v);
        // ===== 55: las filas por segundo entran a la huella. Si no entraran, dos análisis que difieren
        // SÓLO en las filas darían la misma huella y el test de determinismo no vería nada.
        s << " clips=" << (int) clipEvents << " dc=" << hex (dcL) << "," << hex (dcR)
          << " key=" << keyTonic << "," << keyMode << "," << hex (keyConfidence) << "," << hex (keyTimeFraction);
        s << " rows=" << (int) secondRows.size();
        for (const auto& r : secondRows) s << ":" << secondFingerprint (r, hex);
        return s;
    }
};

// ========================================================================================================
// ThirdOctaveAverage — el acumulador INFINITO de potencia por ⅓ de octava, alimentado por el FrameSink del
// módulo Spectrum (o sea: NO vuelve a transformar nada; come los complejos que la STFT ya calculó).
//
// VIVE ACÁ, EN UN SOLO LUGAR, A PROPÓSITO. Lo usan los dos lados de TONAL BALANCE: el archivo
// (FileAnalyzer) y el programa en vivo (modules/Reference). Si fueran dos implementaciones, la promesa
// "el offline da los mismos números que el vivo" pasaría a depender de que nadie las separe nunca. Con una
// sola, la promesa es estructural: es literalmente el mismo código sumando las mismas potencias.
//
// LOS BORDES DE BANDA son los MISMOS que usa SPECTRUM para sus barras de ⅓ de octava (fc·2^∓1/6, con el
// intervalo [lo, hi) y el mismo `ceil` en los dos extremos): dos rejillas distintas para la misma banda
// harían que la curva de TONAL BALANCE y la de SPECTRUM no se pudieran comparar mirándolas.
//
// UNA BANDA SIN NINGÚN BIN (⅓ de octava en los graves con FFT chica) NO se promedia a cero: queda en el
// piso y se marca como sin medición. Cero sería decir "acá no hay energía", que es otra cosa.
//
// CORRECCIÓN DE ANCHO CUBIERTO — y por qué acá sí y en SPECTRUM no. Una banda de ⅓ de octava en los graves
// se lleva UNO o DOS bins, y cuáles depende de dónde caiga la rejilla: la banda de 63 Hz ([56.1, 70.7) Hz)
// agarra dos bins a 48 kHz y uno solo a 44.1 kHz con FFT de 4 096. Sumarlos crudo hace que el MISMO ruido
// rosa lea 3.4 dB distinto según el sample rate del archivo — medido, no estimado (ver FILE[sr]). Como
// TONAL BALANCE existe justamente para comparar un archivo contra el programa en vivo, y esos dos pueden
// estar a sample rates distintos, ese sesgo se comería el delta que la lente dibuja.
//
// Por eso la potencia de la banda se estima como DENSIDAD × ANCHO NOMINAL: la potencia media por unidad de
// frecuencia de los bins que caen adentro, extendida al ancho real de la banda.
//
// ========================================================================================================
// ===== 57c · POR SOLAPAMIENTO FRACCIONARIO, Y POR QUÉ HACÍA FALTA =====
//
// «Tonal balance: se corta la línea» (Joaquín, 12-sep, con la foto). La curva se cortaba en 40 Hz — en las
// DOS curvas, la del programa y la de la referencia — y no era la música: era la REJILLA.
//
// Hasta el 57b los bins se tomaban ENTEROS: k0 = ceil(lo/binHz), k1 = ceil(hi/binHz), y si k1 ≤ k0 la
// banda quedaba VACÍA. Con FFT de 4 096 a 48 kHz el bin mide 11.719 Hz y la banda de 40 Hz mide 9.26
// (35.64 a 44.90): 0.79 bins. Los dos ceil caían en el mismo entero (k0 = k1 = 4) y la banda no recibía
// NADA, teniendo energía de sobra. Lo mismo la de 20 Hz (0.39 bins). A 44.1 kHz el agujero se muda a
// 25 Hz, que es peor: cambia de lugar según el archivo.
//
// Ahora cada bin aporta EN PROPORCIÓN A LO QUE SE SOLAPA con la banda. El bin k representa el intervalo
// [(k−½)·binHz, (k+½)·binHz) —que es lo que un bin de una FFT es— y
//
//     overlap_k = max (0, min (hi, (k+½)·binHz) − max (lo, (k−½)·binHz))
//     P_b       = ( Σ_k P_k · overlap_k / binHz ) · (hi − lo) / Σ_k overlap_k
//
// El primer factor es densidad × ancho: cada bin pone su densidad (P_k / binHz) por los hercios que de
// verdad comparte con la banda. El segundo vale EXACTAMENTE 1 mientras los bins cubran la banda entera —o
// sea siempre, por debajo de Nyquist— y sólo hace algo en la banda de 20 kHz a 44.1 kHz, donde media banda
// queda por encima de Nyquist: ahí extiende la densidad medida al ancho nominal, que es lo que la
// corrección de ancho cubierto hacía desde el 54 y lo que mantiene comparables dos archivos a sample rates
// distintos.
//
// NINGUNA BANDA QUEDA VACÍA: una más angosta que un bin recibe la densidad del bin donde cae. Lo que sí
// cambia es qué significa `binsInBand`: pasa a ser FRACCIONARIO (20 Hz ≈ 0.39, 40 Hz ≈ 0.79, 50 Hz ≈ 0.99
// a 4 096/48 k) y la lente lo dice en la lectura — es una DENSIDAD, no una medición independiente.
//
// DETERMINISMO: la suma es en `double`, bin por bin, frame por frame, en orden fijo. Mismo audio → misma
// suma AL BIT, venga en bloques de 1 o de 4 096 (las posiciones de frame las decide el módulo Spectrum
// contando muestras desde el reset, no el tamaño del bloque). Y como el análisis de archivo y el módulo
// vivo pasan los dos por ESTA clase, la identidad offline = vivo no se puede romper desde acá.
// ========================================================================================================
class ThirdOctaveAverage : public Spectrum::FrameSink
{
public:
    static constexpr int    kNumBands  = SpectrumFrame::kNumThird;
    static constexpr double kSixthDown = 0.8908987181403393;   // 2^(-1/6)
    static constexpr double kSixthUp   = 1.1224620483093730;   // 2^(+1/6)

    void reset() noexcept
    {
        for (auto& v : sum)    v = 0.0;
        for (auto& v : binsIn) v = 0.0;
        frameCount = 0;
        srSeen = 0.0;
        fftSize = 0;
        hopSize = 0;
    }

    void spectrumFrameComputed (const Spectrum::FrameInfo& info) override
    {
        if (info.left == nullptr || info.right == nullptr || info.numBins <= 0 || info.binHz <= 0.0) return;

        // Geometría nueva (cambió el tamaño de FFT o el sample rate): el promedio arranca de cero. Sumar
        // potencias medidas con dos resoluciones distintas no daría ninguna de las dos.
        const int n = 2 * (info.numBins - 1);
        if (n != fftSize || info.sr != srSeen) { reset(); fftSize = n; srSeen = info.sr; }
        hopSize = info.frameRate > 0.0 ? (int) std::lround (info.sr / info.frameRate) : 0;

        for (int b = 0; b < kNumBands; ++b)
        {
            const double lo = kThirdOctaveHz[b] * kSixthDown, hi = kThirdOctaveHz[b] * kSixthUp;

            // Los bins que PUEDEN solaparse: el que contiene `lo` y el que contiene `hi`, con un bin de
            // guarda a cada lado. Los que no se solapan dan overlap 0 y no aportan — el rango holgado
            // ahorra pensar en los bordes sin cambiar el resultado.
            const int k0 = std::max (0, (int) std::floor (lo / info.binHz) - 1);
            const int k1 = std::min (info.numBins - 1, (int) std::ceil (hi / info.binHz) + 1);
            if (k1 < k0) { binsIn[b] = 0.0; continue; }

            double p = 0.0, covered = 0.0;
            for (int k = k0; k <= k1; ++k)
            {
                const double binLo = ((double) k - 0.5) * info.binHz;
                const double binHi = ((double) k + 0.5) * info.binHz;
                const double ov = std::min (hi, binHi) - std::max (lo, binLo);
                if (! (ov > 0.0)) continue;

                const double lr = info.left[2 * k],  li = info.left[2 * k + 1];
                const double rr = info.right[2 * k], ri = info.right[2 * k + 1];
                p       += ((lr * lr + li * li) + (rr * rr + ri * ri)) * (ov / info.binHz);
                covered += ov;
            }

            binsIn[b] = covered / info.binHz;      // FRACCIONARIO: es una densidad, no un conteo
            if (! (covered > 0.0)) continue;

            // Densidad × ancho nominal (ver el encabezado): el factor vale 1 mientras los bins cubran la
            // banda entera y sólo corrige la de 20 kHz a 44.1 k, donde media banda pasa Nyquist. Es
            // CONSTANTE por banda mientras la geometría no cambie, así que no toca el determinismo.
            sum[b] += p * info.powerNorm * ((hi - lo) / covered);
        }

        ++frameCount;
    }

    // dB por banda (referencia de SpectrumFrame). Banda sin bins o sin frames → piso.
    void bandsDb (float* dst) const noexcept
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            if (frameCount <= 0 || binsIn[b] <= 0.0 || sum[b] <= 0.0) { dst[b] = SpectrumFrame::kFloorDb; continue; }
            const double mean = sum[b] / (double) frameCount;
            dst[b] = (float) std::max ((double) SpectrumFrame::kFloorDb, 10.0 * std::log10 (mean));
        }
    }

    bool measured (int band) const noexcept
    {
        return band >= 0 && band < kNumBands && frameCount > 0 && binsIn[band] > 0.0 && sum[band] > 0.0;
    }

    // Cuánto de la banda cubre la rejilla de la FFT vigente, EN BINS y fraccionario (57c). Menos de 1 =
    // la banda es más angosta que un bin y su valor es la densidad del bin donde cae, no una medición
    // independiente; la lente lo dice en la lectura. 0 = la banda está entera fuera del espectro.
    double binsCoveredInBand (int band) const noexcept
    {
        return band >= 0 && band < kNumBands ? binsIn[band] : 0.0;
    }

    // Cuántos bins TOCAN la banda, redondeado hacia arriba. Es la forma entera de lo de arriba, que es lo
    // que pide la firma de Reference::binsInBand (motor congelado, prompt 57c).
    int binsInBand (int band) const noexcept
    {
        const double v = binsCoveredInBand (band);
        return v > 0.0 ? std::max (1, (int) std::ceil (v)) : 0;
    }

    long long frames() const noexcept { return frameCount; }

    // Audio EFECTIVAMENTE promediado: el primer frame sale recién en t = N (ver Spectrum.h), y de ahí en
    // más uno cada `hop`. Es el número que la lente usa para decir "todavía no hay suficiente".
    double seconds() const noexcept
    {
        if (frameCount <= 0 || srSeen <= 0.0) return 0.0;
        return ((double) fftSize + (double) (frameCount - 1) * (double) hopSize) / srSeen;
    }

private:
    double    sum[kNumBands]    {};
    double    binsIn[kNumBands] {};   // 57c — bins CUBIERTOS, fraccionario (ver el encabezado)
    long long frameCount = 0;
    double    srSeen  = 0.0;
    int       fftSize = 0;
    int       hopSize = 0;
};
}
