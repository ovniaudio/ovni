// [supernova][exportaudioloop] — el audio del clip exportado se REPITE, y el empalme no puede tiquear.
//
// El verificador del prompt 39 midió el bug en un MP4 real: correlación 0,99987 entre el segundo 0 y el
// segundo 12,000 exactos (el audio repetía) y en el empalme un corte seco. D-42 (b) lo cierra por dos
// lados: el anillo pasa de 12 a 30 s y el empalme lleva un crossfade corto.
//
// Lo que este archivo fija es el contrato del crossfade, medido: el peor SALTO muestra a muestra en la
// vuelta tiene que caer al orden del salto que el propio material ya tiene (su pendiente), en vez de ser
// el salto arbitrario entre dos momentos cualesquiera. Puro, sin GPU ni JUCE.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include "video/ExportAudioLoop.h"

namespace
{
constexpr double kSr = 48000.0;
constexpr float  kAmp = 0.4f;

// Un anillo continuo: coseno a `hz`. Elegido para que el tramo del loop abarque N + 0,5 ciclos → sin
// crossfade el empalme salta de +A a −A, el peor caso posible (y el que de verdad se oye como un tic).
std::vector<float> cosRing (std::size_t frames, double hz)
{
    std::vector<float> r (frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const float v = kAmp * (float) std::cos (2.0 * 3.14159265358979 * hz * (double) i / kSr);
        r[i * 2 + 0] = v; r[i * 2 + 1] = v;
    }
    return r;
}

// Ruido determinista (LCG) — dos tramos SIN relación de fase, que es la situación real de un loop de 30 s.
std::vector<float> noiseRing (std::size_t frames, uint32_t seed)
{
    std::vector<float> r (frames * 2);
    uint32_t s = seed;
    for (std::size_t i = 0; i < frames * 2; ++i)
    {
        s = s * 1664525u + 1013904223u;
        r[i] = kAmp * ((float) (s >> 8) / 8388608.0f - 1.0f);
    }
    return r;
}

// El peor salto muestra a muestra DENTRO del tramo (la pendiente propia del material).
float worstInteriorStep (const supernova::AudioLoop& l)
{
    float w = 0.0f;
    for (std::size_t i = 0; i + 1 < l.frames; ++i)
        for (int c = 0; c < 2; ++c)
            w = std::max (w, std::fabs (l.samples[(i + 1) * 2 + c] - l.samples[i * 2 + c]));
    return w;
}

// El salto EN EL EMPALME: la última muestra de una vuelta y la primera de la siguiente.
float seamStep (const supernova::AudioLoop& l)
{
    float w = 0.0f;
    for (int c = 0; c < 2; ++c)
        w = std::max (w, std::fabs (l.samples[c] - l.samples[(l.frames - 1) * 2 + c]));
    return w;
}

float rmsRange (const supernova::AudioLoop& l, std::size_t from, std::size_t to)
{
    double sum = 0.0; std::size_t n = 0;
    for (std::size_t i = from; i < to && i < l.frames; ++i)
        for (int c = 0; c < 2; ++c) { const double v = l.samples[i * 2 + c]; sum += v * v; ++n; }
    return n ? (float) std::sqrt (sum / (double) n) : 0.0f;
}
}

TEST_CASE ("exportaudioloop: el crossfade baja el salto del empalme al orden de la pendiente del material",
           "[supernova][exportaudioloop]")
{
    // 1 s de loop, 50 ms de colchón delante. 100,5 Hz → el tramo abarca 100,5 ciclos: el empalme crudo
    // salta de +A a −A (2·0,4 = 0,8), el peor caso.
    const std::size_t head = (std::size_t) (0.050 * kSr);
    const std::size_t per  = (std::size_t) (1.000 * kSr);
    const auto ring = cosRing (head + per, 100.5);
    const std::size_t x = (std::size_t) std::llround (supernova::kExportAudioCrossfadeSeconds * kSr);

    const auto crudo = supernova::makeSeamlessAudioLoop (ring.data(), head + per, head, per, 0);
    const auto suave = supernova::makeSeamlessAudioLoop (ring.data(), head + per, head, per, x);

    REQUIRE (crudo.frames == per);
    REQUIRE (suave.frames == per);          // con colchón el período NO se acorta
    REQUIRE (suave.usedPreRoll);
    REQUIRE (suave.crossfadeFrames == x);

    const float pendiente = worstInteriorStep (crudo);
    const float antes     = seamStep (crudo);
    const float despues   = seamStep (suave);
    INFO ("pendiente propia del material: " << pendiente);
    INFO ("salto en el empalme  ANTES  : " << antes   << "  (" << antes   / pendiente << "x la pendiente)");
    INFO ("salto en el empalme  DESPUES: " << despues << "  (" << despues / pendiente << "x la pendiente)");

    CHECK (antes   > 0.7f);                 // el bug: de +A a −A de una muestra a la otra
    CHECK (despues < pendiente * 2.0f);     // el arreglo: el empalme es una muestra más, como cualquier otra
    CHECK (despues < antes * 0.05f);        // y al menos 20 veces más chico que antes
}

TEST_CASE ("exportaudioloop: la vuelta entera queda sin saltos, no sólo la costura",
           "[supernova][exportaudioloop]")
{
    // El fundido tiene DOS costuras más (donde empieza y donde termina): ninguna puede introducir un salto
    // mayor que la pendiente del propio material.
    const std::size_t head = (std::size_t) (0.050 * kSr);
    const std::size_t per  = (std::size_t) (1.000 * kSr);
    const auto ring = cosRing (head + per, 100.5);
    const std::size_t x = (std::size_t) std::llround (supernova::kExportAudioCrossfadeSeconds * kSr);

    const auto crudo = supernova::makeSeamlessAudioLoop (ring.data(), head + per, head, per, 0);
    const auto suave = supernova::makeSeamlessAudioLoop (ring.data(), head + per, head, per, x);
    const float pendiente = worstInteriorStep (crudo);
    const float peorSuave = std::max (worstInteriorStep (suave), seamStep (suave));
    INFO ("peor salto de TODA la vuelta, con fundido: " << peorSuave
          << "  vs pendiente del material: " << pendiente);
    CHECK (peorSuave < pendiente * 2.0f);

    // Y el interior (fuera del fundido) no se tocó ni un bit.
    for (std::size_t i = 0; i + x < suave.frames; ++i)
        REQUIRE (suave.samples[i * 2] == crudo.samples[i * 2]);
}

TEST_CASE ("exportaudioloop: potencia constante — el fundido no hace un pozo de nivel",
           "[supernova][exportaudioloop]")
{
    // Dos tramos sin relación de fase (el caso real: dos momentos distintos de la música). Con una rampa
    // LINEAL el nivel caería ~3 dB en el medio del fundido; con potencia constante se mantiene.
    const std::size_t head = (std::size_t) (0.050 * kSr);
    const std::size_t per  = (std::size_t) (1.000 * kSr);
    const auto ring = noiseRing (head + per, 12345u);
    const std::size_t x = (std::size_t) std::llround (supernova::kExportAudioCrossfadeSeconds * kSr);
    const auto l = supernova::makeSeamlessAudioLoop (ring.data(), head + per, head, per, x);
    REQUIRE (l.usedPreRoll);

    const float dentro = rmsRange (l, l.frames - x, l.frames);
    const float fuera  = rmsRange (l, l.frames - 4 * x, l.frames - x);
    const float dB = 20.0f * std::log10 (dentro / fuera);
    INFO ("RMS dentro del fundido " << dentro << " · fuera " << fuera << " → " << dB << " dB");
    CHECK (std::fabs (dB) < 1.5f);
}

TEST_CASE ("exportaudioloop: sin colchón el loop se acorta, y el llamador lee el largo REAL",
           "[supernova][exportaudioloop]")
{
    // Si el anillo no guarda nada antes del tramo no hay material previo: el fundido se hace con la cabeza
    // y el loop mide `x` cuadros menos. Lo que importa es que `frames` diga la verdad — el export loopea
    // con ESE número, así que audio y video no pueden desincronizarse en silencio.
    const std::size_t per = (std::size_t) (1.000 * kSr);
    const auto ring = cosRing (per, 100.5);
    const std::size_t x = (std::size_t) std::llround (supernova::kExportAudioCrossfadeSeconds * kSr);

    const auto l = supernova::makeSeamlessAudioLoop (ring.data(), per, 0, per, x);
    REQUIRE_FALSE (l.usedPreRoll);
    REQUIRE (l.frames == per - x);
    REQUIRE (l.samples.size() == (per - x) * 2);

    const auto crudo = supernova::makeSeamlessAudioLoop (ring.data(), per, 0, per, 0);
    const float pendiente = worstInteriorStep (crudo);
    INFO ("sin colchón · empalme " << seamStep (l) << " vs pendiente " << pendiente);
    CHECK (seamStep (l) < pendiente * 2.0f);
}

TEST_CASE ("exportaudioloop: las constantes del export caen en el camino con colchón",
           "[supernova][exportaudioloop]")
{
    // Coherencia de los tres números que usa el editor: con el anillo real (loop + colchón) y el tramo
    // real (los últimos kExportAudioRingSeconds), el fundido SIEMPRE tiene material previo → el período
    // que ve el muxer es exactamente el del video.
    using namespace supernova;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        const std::size_t ringF = (std::size_t) (sr * ((double) kExportAudioRingSeconds + kExportAudioHeadroomSeconds));
        const std::size_t per   = (std::size_t) std::llround ((double) kExportAudioRingSeconds * sr);
        const std::size_t x     = (std::size_t) std::llround (kExportAudioCrossfadeSeconds * sr);
        std::vector<float> ring (ringF * 2, 0.25f);
        const auto l = makeSeamlessAudioLoop (ring.data(), ringF, ringF - per, per, x);
        INFO ("sr=" << sr << " ringF=" << ringF << " per=" << per << " x=" << x);
        CHECK (l.usedPreRoll);
        CHECK (l.frames == per);
        CHECK (l.crossfadeFrames == x);
    }
}

TEST_CASE ("exportaudioloop: bordes — nulo, período 0, tramo fuera del anillo, fundido gigante",
           "[supernova][exportaudioloop]")
{
    const std::size_t per = 1000;
    const auto ring = cosRing (per, 100.5);

    CHECK (supernova::makeSeamlessAudioLoop (nullptr, per, 0, per, 8).frames == 0);
    CHECK (supernova::makeSeamlessAudioLoop (ring.data(), per, 0, 0, 8).frames == 0);
    CHECK (supernova::makeSeamlessAudioLoop (ring.data(), per, 900, 200, 8).frames == 0);   // se pasa del anillo
    CHECK (supernova::makeSeamlessAudioLoop (ring.data(), per, per + 1, 1, 8).frames == 0);

    // Un fundido más largo que el propio loop se recorta a la mitad del tramo, nunca lo deja en nada.
    const auto huge = supernova::makeSeamlessAudioLoop (ring.data(), per, 0, per, per * 4);
    CHECK (huge.crossfadeFrames == per / 2);
    CHECK (huge.frames == per - per / 2);

    // Un fundido de 1 muestra no es una rampa: se ignora y el tramo sale entero e intacto.
    const auto tiny = supernova::makeSeamlessAudioLoop (ring.data(), per, 0, per, 1);
    CHECK (tiny.crossfadeFrames == 0);
    CHECK (tiny.frames == per);
}
