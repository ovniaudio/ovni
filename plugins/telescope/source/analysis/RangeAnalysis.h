#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <optional>
#include "analysis/FileAnalysis.h"
#include "analysis/SecondHistory.h"
#include "analysis/modules/Cqt.h"
#include "analysis/modules/Loudness.h"
// FrameSinkFanout vive en Reference.h (es el fan-out del FrameSink de Spectrum; ver el prompt 55/1e).
#include "analysis/modules/Reference.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// RangeAnalyzer — UNA función mide un TRAMO de un archivo (prompt 101, F3 de TELESCOPE 0.2; contrato D-100 §3).
//
// El tramo es [from_s, to_s) en SEGUNDOS DEL ARCHIVO, no del proyecto. La misma función sirve para tres cosas:
//   · el archivo entero de TONAL BALANCE (FileAnalyzer la llama sin rango: es el mismo camino, no una copia);
//   · la herramienta `telescope-measure`, que es como EYEPIECE mide los stems («medí el estribillo»);
//   · la tira de forma de onda de T6 (F4): el tramo que el usuario elige con el mouse para la referencia.
//
// QUÉ ES MEDIR UN TRAMO. Es exactamente medir un archivo que empieza en `from` y termina en `to`: el motor se
// resetea al principio del tramo y no ve nada de afuera. La prueba es RANGE[corte] (tests/MeasureTest.cpp):
// medir [a, b) de un archivo da los mismos números, al bit, que medir un archivo cortado en esas muestras.
//
// DE SEGUNDOS A MUESTRAS: `sampleAt (s, sr) = llround (s · sr)`, redondeo al más cercano con los medios lejos
// del cero. El tramo es [sampleAt (from), sampleAt (to)): la muestra de `to` no entra. Está escrito también en
// measure/SCHEMA.md, que es lo que lee EYEPIECE.
//
// LO QUE SE PASA DEL ARCHIVO SE RECORTA Y SE DICE (`clipped`): un `from` negativo va a 0 y un `to` más allá del
// final va al final. Un tramo que queda vacío (to ≤ from, antes o después de recortar) NO se mide: es una
// negativa (`Status::rangeEmpty`), nunca una medición de cero segundos.
//
// ESTÉREO DE BANDA ANCHA SOBRE EL TRAMO. Además del FileAnalysis, el tramo trae las tres sumas del dominio del
// tiempo (ΣLL, ΣRR, ΣLR, en double, muestra por muestra y en orden) con las que se arma la correlación
// ΣLR / √(ΣLL·ΣRR): la MISMA definición que el módulo Stereo (modules/Stereo.h), sobre el tramo entero en vez
// de una ventana de 100/300/1000 ms. Se suman siempre (también para el FileAnalyzer, que no las usa): son tres
// multiplicaciones por muestra al lado de una FFT, un CQT y un sobremuestreo de true peak.
// ========================================================================================================
namespace telescope
{
struct SampleRange
{
    juce::int64 start = 0;   // primera muestra que entra
    juce::int64 end   = 0;   // primera que NO entra
    juce::int64 length() const noexcept { return end > start ? end - start : 0; }
};

// Segundos del archivo → índice de muestra (ver el encabezado).
inline juce::int64 sampleAt (double seconds, double sampleRate) noexcept
{
    return (juce::int64) std::llround (seconds * sampleRate);
}

struct RangeMeasurement
{
    enum class Status
    {
        measured,        // se midió el tramo entero
        fileNotFound,    // el archivo no existe
        notAudio,        // existe pero ningún lector lo reconoce
        noAudio,         // se abre pero no tiene muestras (o SR/canales en 0)
        readError,       // se cortó la lectura a mitad
        rangeEmpty,      // el tramo, recortado al archivo, no tiene muestras
        cancelled        // lo cortó quien lo pidió (shouldExit)
    };

    Status status = Status::fileNotFound;

    // El análisis del TRAMO. `name`, `sr`, `channels`, `seconds` y `warning` son del ARCHIVO (seconds = su
    // duración entera); los números y las series son del tramo. Con el archivo entero es, byte por byte, el
    // FileAnalysis de siempre (lo sostiene [golden]).
    FileAnalysis analysis;

    juce::int64 fileSamples = 0;

    // Lo PEDIDO (sin recortar) y lo MEDIDO (recortado al archivo).
    std::optional<double> requestedFromS, requestedToS;
    SampleRange range;
    bool clipped = false;

    // Estéreo de banda ancha en el dominio del tiempo, sobre el tramo (ver el encabezado).
    double sumLL = 0.0, sumRR = 0.0, sumLR = 0.0;

    double secondsMeasured() const noexcept
    {
        return analysis.sr > 0.0 ? (double) range.length() / analysis.sr : 0.0;
    }
};

class RangeAnalyzer
{
public:
    RangeAnalyzer();

    // Mide [fromS, toS) de `file`. Sin `fromS`, desde el principio; sin `toS`, hasta el final.
    // `shouldExit` se consulta una vez por bloque de lectura (4 096 muestras); `progress` recibe 0…1.
    RangeMeasurement measure (const juce::File& file,
                              std::optional<double> fromS = std::nullopt,
                              std::optional<double> toS = std::nullopt,
                              const std::function<bool()>& shouldExit = {},
                              const std::function<void (float)>& progress = {});

    // Bloque de lectura. 4 096 muestras: suficientemente grande para que el I/O no domine y chico para que un
    // pedido de salida conteste rápido. Es el de FileAnalyzer desde el 54.
    static constexpr int kReadBlock = 4096;

private:
    juce::AudioFormatManager formats;

    // El motor: instancias PROPIAS, nunca las del AnalysisThread (ver FileAnalyzer.h). Se resetean enteras al
    // empezar cada medición: el resultado no depende de lo que se midió antes (MEASURE[orden]).
    Loudness           loudness;
    Spectrum           spectrum;
    ThirdOctaveAverage bands;
    SecondBuilder      secondBuilder;
    Cqt                cqt;
    FrameSinkFanout    fanout;
    double             cqtPreparedSr = 0.0;   // la SR con que se armaron los kernels del CQT (ver measure())

    JUCE_DECLARE_NON_COPYABLE (RangeAnalyzer)
};
}
