#pragma once
#include <juce_core/juce_core.h>
#include "analysis/AnalysisFrame.h"
#include "analysis/SpectrumFrame.h"

// ========================================================================================================
// ReferenceFrame — lo que dibuja TONAL BALANCE (spec §5.6, prompt 54): el programa EN VIVO contra una
// referencia cargada de un archivo, las dos normalizadas a su propia loudness.
//
// POR QUÉ SE NORMALIZA, Y QUÉ SIGNIFICA EL DELTA. Comparar dos curvas de espectro sin normalizar sólo
// contesta "cuál está más fuerte", que ya lo dice el medidor de loudness. Lo que se quiere saber es el
// TILT: dónde tenés más y dónde menos que la referencia A IGUAL LOUDNESS. Por eso a cada curva se le resta
// SU PROPIO integrado:
//
//     norm[b]  = curva[b] − LUFS_integrado          (cada lado con el suyo)
//     delta[b] = live_norm[b] − ref_norm[b]
//
// El mismo ruido rosa a −14 y a −20 LUFS da delta 0 en todas las bandas. Eso es lo que se quiere.
//
// Y LA CONSECUENCIA QUE HAY QUE DECIR EN VOZ ALTA: normalizar por loudness hace que el delta sea de SUMA
// (ponderada) CERO. Si le subís 6 dB a todo lo que está arriba de 2 kHz, la loudness sube con eso, y el
// delta no muestra "+6 arriba y 0 abajo": muestra "+2.3 arriba y −3.7 abajo" (medido, ver REF[tilt]). Los
// dos números son la MISMA verdad —la diferencia entre las dos regiones sigue siendo exactamente 6 dB—
// contada como se escucha al comparar a igual volumen, que es como se compara una mezcla contra una
// referencia. La lente lo dice en pantalla y el README lo explica entero.
//
// VIAJA POR SU PROPIO TripleBuffer (son ~800 bytes). No va en el AnalysisFrame porque el resto de las
// lentes no lo dibuja, y el AnalysisFrame se copia diez veces por segundo para todas.
// ========================================================================================================
namespace telescope
{
struct ReferenceFrame
{
    static constexpr int kNumBands  = SpectrumFrame::kNumThird;   // 30 · ⅓ de octava ISO 266
    static constexpr int kNameChars = 128;

    // Segundos de audio promediado que hacen falta para que la curva viva signifique algo. Con menos, un
    // promedio infinito todavía está dominado por lo que haya pasado en el último segundo.
    static constexpr float kMinLiveSeconds = 3.0f;

    float liveBands[kNumBands];   // dBFS, promedio infinito desde el RESET (referencia de SpectrumFrame)
    float liveNorm [kNumBands];   // liveBands − liveIntegrated
    float refBands [kNumBands];   // lo mismo, del archivo
    float refNorm  [kNumBands];   // refBands − refIntegrated
    float deltaDb  [kNumBands];   // liveNorm − refNorm · 0 donde no se puede comparar (ver bandValid)

    // Una banda se puede COMPARAR sólo si los dos lados la midieron. Sin esto, una banda que la referencia
    // no tiene (archivo a 44.1 k, banda de 20 kHz) se dibujaría en delta 0, o sea "coincide perfecto".
    bool bandValid[kNumBands];

    float liveIntegrated = kSilenceDb;
    float refIntegrated  = kSilenceDb;
    float liveSeconds    = 0.0f;
    bool  liveValid      = false;   // ≥ kMinLiveSeconds Y con integrado válido
    bool  refValid       = false;

    char  refName[kNameChars] {};
    float refSeconds = 0.0f;

    ReferenceFrame()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            liveBands[b] = refBands[b] = SpectrumFrame::kFloorDb;
            liveNorm[b]  = refNorm[b]  = SpectrumFrame::kFloorDb;
            deltaDb[b]   = 0.0f;
            bandValid[b] = false;
        }
    }
};
}
