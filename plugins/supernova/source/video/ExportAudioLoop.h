#pragma once

// ExportAudioLoop — el audio que el export muxea al MP4 tiene que poder REPETIRSE sin tic.
//
// El clip dura lo que el usuario pida (hasta 600 s) pero el sonido sale de un anillo de unos pocos
// segundos de audio VIVO, así que el export lo repite. El empalme era un corte seco: la última muestra
// del tramo seguida de la primera, con la fase donde cayera. El verificador del prompt 39 lo midió en
// un clip real: correlación 0,99987 entre el segundo 0 y el segundo 12,000 exactos — el audio repetía,
// y en el empalme había un salto (tic) audible en cada vuelta.
//
// Este helper arma UNA vez el tramo que se va a repetir, con un crossfade corto en el empalme:
//
//   · CON COLCHÓN (`baseFrame >= crossfadeFrames`, el caso normal — el anillo guarda
//     kExportAudioHeadroomSeconds ADEMÁS del loop): la COLA de la vuelta se funde hacia el audio que
//     de verdad PRECEDE al arranque del loop. Al terminar la vuelta estamos oyendo lo que naturalmente
//     lleva a la primera muestra, así que el empalme queda continuo por construcción. El período NO se
//     acorta: video y audio siguen repitiendo con exactamente el mismo largo.
//   · SIN COLCHÓN (`baseFrame < crossfadeFrames`): no hay material anterior, así que el loop se acorta
//     en `crossfadeFrames` y la CABEZA entra fundida sobre la cola descartada (el crossfade de loop
//     clásico). El llamador usa `AudioLoop::frames` (no el período que pidió) para el módulo del AUDIO,
//     así que el audio siempre repite por donde el fundido lo dejó continuo. OJO — esto NO re-alinea el
//     video: el período del VIDEO es `loopV` y no se recalcula, así que en esta rama audio y video repiten
//     con largos distintos. Lo que la hace inofensiva es que es inalcanzable con reactividad: se llega
//     sólo con el editor recién reabierto (`recentFrames` vacío ⇒ `loopV = totalFrames`), y ahí el clip se
//     renderiza sin frames de análisis. Lo que de verdad garantiza el colchón en el camino normal es el
//     tope de `exportLoopWindow` + `kAnalysisRingSeconds == kExportAudioRingSeconds` + estos 50 ms.
//
// La curva es de POTENCIA CONSTANTE (u = sin(t·π/2), d = cos(t·π/2), u²+d² = 1): los dos tramos son
// momentos distintos de la misma música, sin relación de fase, y una rampa lineal les haría un pozo
// de nivel en el medio. Los pesos llegan a 0 y a 1 EXACTOS en los extremos (t = j/(x−1)), así que las
// dos costuras del fundido con el material intacto también son continuas.
//
// Puro (sin JUCE ni AVFoundation) → se prueba solo (tests [exportaudioloop]).
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <vector>

namespace supernova
{
// Cuántos SEGUNDOS de audio vivo se repiten en el clip exportado. D-42 (b), decidida por Joaquín el
// 8-sep: 12 s alcanzaban para un loop de Reels pero no para un video de prensa de 30-45 s, donde la
// vuelta se oía. Es CONTRATO con el editor: la ventana de análisis que se exporta tiene que cubrir el
// mismo tramo que este audio (SupernovaEditor::kAnalysisRingSeconds sale de acá).
inline constexpr int kExportAudioRingSeconds = 30;

// Lo que el anillo guarda ADEMÁS del loop: de acá sale el material del crossfade del empalme, y por eso
// el loop puede sonar continuo sin acortarse ni un sample. 50 ms = 4 800 muestras a 48 kHz (~38 KB).
inline constexpr double kExportAudioHeadroomSeconds = 0.050;

// Largo del fundido del empalme. 10 ms es el mínimo que borra un tic de corte sin que se oiga como un
// fundido; sobre un loop de 30 s es el 0,03 % del material.
inline constexpr double kExportAudioCrossfadeSeconds = 0.010;

struct AudioLoop
{
    std::vector<float> samples;        // interleaved L/R, `frames` cuadros — se lee en módulo `frames`
    std::size_t frames = 0;            // el período REAL del loop (puede ser < el pedido, ver arriba)
    std::size_t crossfadeFrames = 0;   // el fundido que se aplicó (0 = ninguno)
    bool        usedPreRoll = false;   // true = el material del fundido es el audio previo al loop
};

// `ring` es el anillo YA desenrollado (viejo→nuevo, interleaved L/R, `ringFrames` cuadros).
// Devuelve el tramo `[baseFrame, baseFrame + periodFrames)` listo para repetirse sin tic.
// Entradas inválidas (nulo, período 0, tramo fuera del anillo) devuelven un loop vacío.
inline AudioLoop makeSeamlessAudioLoop (const float* ring, std::size_t ringFrames, std::size_t baseFrame,
                                        std::size_t periodFrames, std::size_t crossfadeFrames)
{
    AudioLoop out;
    if (ring == nullptr || periodFrames == 0 || baseFrame > ringFrames
        || periodFrames > ringFrames - baseFrame)
        return out;

    // Un fundido nunca se come más de la mitad del loop (y con menos de 2 muestras no hay rampa).
    std::size_t x = std::min (crossfadeFrames, periodFrames / 2);
    if (x < 2) x = 0;

    const bool preRoll = (x > 0 && baseFrame >= x);
    const std::size_t len = (x == 0 || preRoll) ? periodFrames : (periodFrames - x);

    const float* base = ring + baseFrame * 2;
    out.frames = len;
    out.crossfadeFrames = x;
    out.usedPreRoll = preRoll;
    out.samples.assign (len * 2, 0.0f);
    std::copy (base, base + len * 2, out.samples.begin());
    if (x == 0) return out;

    // u sube de 0 a 1 y d baja de 1 a 0, con los extremos EXACTOS (j = 0 → t = 0, j = x−1 → t = 1).
    const auto weights = [x] (std::size_t j, float& u, float& d)
    {
        const float t = (float) j / (float) (x - 1);
        u = std::sin (t * 1.57079633f);
        d = std::cos (t * 1.57079633f);
    };

    if (preRoll)
    {
        // La cola se funde hacia el audio que precede al arranque: la vuelta termina oyendo lo que
        // naturalmente lleva a base[0].
        const float* pre = ring + (baseFrame - x) * 2;
        for (std::size_t j = 0; j < x; ++j)
        {
            float u = 0.0f, d = 0.0f; weights (j, u, d);
            const std::size_t o = (len - x + j) * 2;
            out.samples[o + 0] = base[o + 0] * d + pre[j * 2 + 0] * u;
            out.samples[o + 1] = base[o + 1] * d + pre[j * 2 + 1] * u;
        }
    }
    else
    {
        // Sin material previo: se descartan los últimos `x` cuadros y la cabeza entra fundida sobre ellos.
        for (std::size_t j = 0; j < x; ++j)
        {
            float u = 0.0f, d = 0.0f; weights (j, u, d);
            out.samples[j * 2 + 0] = base[j * 2 + 0] * u + base[(len + j) * 2 + 0] * d;
            out.samples[j * 2 + 1] = base[j * 2 + 1] * u + base[(len + j) * 2 + 1] * d;
        }
    }
    return out;
}
}
