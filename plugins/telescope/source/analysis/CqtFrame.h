#pragma once
#include <juce_core/juce_core.h>

// ========================================================================================================
// CqtFrame — el espectro CONSTANT-Q que dibujan CQT (lente 6) y SPIRAL (lente 7), con su cromagrama y su
// tonalidad estimada (spec §5.4, prompt 52).
//
// Viaja por su PROPIO TripleBuffer, como el SpectrumFrame y por el mismo motivo: son ~4 KB por slot, y
// meterlos en el AnalysisFrame haría que LOUDNESS copie el CQT diez veces por segundo para dibujar tres
// números. (Los CUATRO números de la tonalidad sí van también en el AnalysisFrame: VERDICT los va a leer
// y son 16 bytes, no 4 KB.)
//
// POD de arrays FIJOS dimensionados al peor caso. `numBins` depende del sample rate —el eje llega hasta
// min(20 kHz, 0.45·fs) desde A0— y a 48 kHz son 229; 512 deja lugar de sobra para cualquier fs.
//
// REFERENCIA DE dB: la MISMA de SpectrumFrame. Un seno de amplitud A exactamente en la frecuencia del bin
// k lee 20·log10(A), con la corrección de ganancia coherente de la ventana de ESE bin (cada bin tiene su
// propia ventana: es lo que quiere decir "constant-Q"). El piso es −120 dB y no −200: por debajo de eso,
// en un espectro cuya resolución baja de 1.24 s a 1.7 ms según la nota, no hay nada que mirar.
// ========================================================================================================
namespace telescope
{
struct CqtFrame
{
    static constexpr int kMaxBins = 512;
    static constexpr int kNumClasses = 12;      // las doce clases de nota del cromagrama
    static constexpr float kFloorDb = -120.0f;

    int    numBins       = 0;
    float  fMin          = 0.0f;   // Hz del bin 0 (A0 = 27.5)
    int    binsPerOctave = 0;      // 24 → dos bins por semitono

    float  magDb  [kMaxBins] {};
    float  holdDb [kMaxBins] {};

    // Cromagrama: potencia por clase de nota (C=0 … B=11), NORMALIZADA al máximo (0…1). El suavizado va
    // sobre `cqtChromaSec` y es el que alimenta la estimación de tonalidad.
    float  chroma      [kNumClasses] {};
    float  chromaSmooth[kNumClasses] {};

    // Tonalidad estimada (Krumhansl-Schmuckler). tonic = clase de nota (C=0 … B=11), mode 0 = mayor,
    // 1 = menor. Los DOS en −1 cuando no hay cromagrama que correlacionar (silencio): "no sé" es un
    // resultado, y decir "Do mayor con confianza 0" sería peor que no decir nada.
    int    keyTonic = -1, keyMode = -1;
    // La correlación de Pearson del cromagrama suavizado con el perfil ganador (−1 … 1) y la fracción del
    // tiempo DESDE EL RESET en que esa tonalidad fue la mejor (0 … 1). Los dos se muestran SIEMPRE junto
    // a la tonalidad: una tonalidad estimada sin su confianza es una afirmación que nadie autorizó.
    float  keyConfidence = 0.0f, keyTimeFraction = 0.0f;

    juce::uint32 frameIndex = 0;   // frames publicados desde el reset

    // Lo que "ve" el bin más grave: N_0/fs segundos (1.241 s a 48 kHz). Es la latencia INHERENTE al
    // método —un análisis con resolución de un cuarto de tono en A0 necesita ese tiempo, no hay vuelta—
    // y por eso se publica y se rotula en pantalla en vez de esconderse.
    float  lowestBinLatencySec = 0.0f;

    // Frecuencia del bin k: fMin · 2^(k/binsPerOctave).
    double binHz (int k) const noexcept
    {
        return binsPerOctave > 0 ? (double) fMin * std::pow (2.0, (double) k / (double) binsPerOctave) : 0.0;
    }
};
}
