// [supernova][dissolve][.gpu] — el FUNDIDO en el motor. Dos promesas separadas:
//   1. IDENTIDAD EN REPOSO: con el fundido armado pero sin cambio de imagen, cada byte que sale del
//      renderer es el de hoy. El camino nuevo es un branch por uniform; los goldens no se tocan.
//   2. (siguiente commit) el fundido en sí: el cambio de foto deja de ser un salto.
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <utility>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "render/RenderScenarios.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 512;
constexpr int kPx   = 160;

bool gpuOk (supernova::MetalRenderer& r)
{
    if (r.isAvailable()) return true;
    SUCCEED ("sin GPU Metal — test [dissolve][.gpu] saltado (CI)");
    return false;
}

double meanAbsDiff (const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += std::abs ((int) a[i] - (int) b[i]);
    return n ? acc / (double) n : 0.0;
}

void prepareWithFactory (supernova::MetalRenderer& r)
{
    r.prepare (kGrid, kGrid);
    auto img = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ img.data(), kGrid, kGrid });
}
}

TEST_CASE ("dissolve: en REPOSO el motor es byte-idéntico al de siempre (60 cuadros, sin cambio de imagen)",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer withFade, plain;
    if (! gpuOk (withFade)) return;
    prepareWithFactory (withFade);
    prepareWithFactory (plain);

    // El renderer NACE en 0 (= corte, el comportamiento de hoy); el editor y el export le suben el valor.
    // Armar el fundido sin cambiar de imagen NO puede mover un solo byte: el camino nuevo es un branch.
    withFade.setDissolveSeconds (0.7);

    supernova::ParticleParams pp;
    std::vector<uint8_t> a ((size_t) kPx * kPx * 4), b (a.size());
    for (int f = 0; f < 60; ++f)
    {
        const auto af = supernova::scenarioFrame ("kick", f, 60);
        REQUIRE (withFade.renderOffscreen (af, pp, kPx, kPx, a.data()));
        REQUIRE (plain.renderOffscreen    (af, pp, kPx, kPx, b.data()));
    }
    const bool identical = (a == b);   // bool: si falla, Catch no vuelca los 100 KB del frame
    REQUIRE (identical);
}

TEST_CASE ("dissolve: en reposo tampoco cambia nada con el mundo cargado (trails, 3D, cutout, figura)",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer withFade, plain;
    if (! gpuOk (withFade)) return;
    prepareWithFactory (withFade);
    prepareWithFactory (plain);
    withFade.setDissolveSeconds (0.35);

    // Los caminos que TOCA el fundido, encendidos a la vez: el vertex (colores, máscara del CUTOUT, depth
    // de la cáscara 3D, figura) y la sim (pesos, flow, content2, extra). Sin PLEXUS: ver el test siguiente.
    supernova::ParticleParams pp;
    pp.trailAmt = 0.6f; pp.depthAmt = 0.8f; pp.cutoutAmt = 0.5f;
    pp.formAmt = 0.4f;  pp.formMode = 2;    pp.rotYRad = 0.3f;

    std::vector<uint8_t> a ((size_t) kPx * kPx * 4), b (a.size());
    for (int f = 0; f < 40; ++f)
    {
        const auto af = supernova::scenarioFrame ("rms-breathe", f, 40);
        REQUIRE (withFade.renderOffscreen (af, pp, kPx, kPx, a.data()));
        REQUIRE (plain.renderOffscreen    (af, pp, kPx, kPx, b.data()));
    }
    const bool identical = (a == b);
    REQUIRE (identical);
}

// El PLEXUS no es byte-determinista NI SIN el fundido: k_linkEmit reparte los índices de línea con un
// atomic_fetch_add (el orden en que ganan los threads es cosa de la GPU) y el blending aditivo no es
// asociativo, así que dos renderers frescos con los MISMOS parámetros ya difieren en ~1400 bytes de 102400.
// Es una propiedad vieja del motor, no del fundido — y no toca a los goldens, que corren con linksAmt = 0
// (el default de ParticleParams). Lo que sí se puede exigir, y se exige acá, es que armar el fundido no
// agregue NADA por encima de ese piso de ruido propio.
TEST_CASE ("dissolve: con PLEXUS en reposo no agrega ruido por encima del que el propio motor ya tiene",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer withFade, plain, control;
    if (! gpuOk (withFade)) return;
    prepareWithFactory (withFade);
    prepareWithFactory (plain);
    prepareWithFactory (control);
    withFade.setDissolveSeconds (0.35);

    supernova::ParticleParams pp;
    pp.linksAmt = 0.7f; pp.depthAmt = 0.8f; pp.cutoutAmt = 0.5f;

    std::vector<uint8_t> a ((size_t) kPx * kPx * 4), b (a.size()), c (a.size());
    for (int f = 0; f < 40; ++f)
    {
        const auto af = supernova::scenarioFrame ("rms-breathe", f, 40);
        REQUIRE (withFade.renderOffscreen (af, pp, kPx, kPx, a.data()));
        REQUIRE (plain.renderOffscreen    (af, pp, kPx, kPx, b.data()));
        REQUIRE (control.renderOffscreen  (af, pp, kPx, kPx, c.data()));
    }
    // b vs c = el piso de ruido del motor (dos renderers SIN fundido). a vs b tiene que quedar en ese orden
    // de magnitud, no por encima: el fundido en reposo no aporta diferencia propia.
    const double noise = meanAbsDiff (b, c);
    const double mine  = meanAbsDiff (a, b);
    INFO ("piso de ruido del plexus (plain vs plain) = " << noise << " · con fundido armado = " << mine);
    REQUIRE (mine <= noise * 2.0 + 0.05);
}

// ---------------------------------------------------------------------------------------------
// EL FUNDIDO EN SÍ. Foto A = campo rojo, foto B = campo azul, escenario "idle" (audio en cero: lo que se
// mide es la transición, no la música). renderOffscreen avanza con un dt fijo de 1/60 → 0.7 s = 42 cuadros.
// ---------------------------------------------------------------------------------------------

namespace
{
// Campo RGBA de color plano con una leve rampa de luma (para que se formen partículas) — igual que
// ExportSmokeTest::solidField.
std::vector<uint8_t> solidField (int w, int h, uint8_t rr, uint8_t gg, uint8_t bb)
{
    std::vector<uint8_t> px ((size_t) w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float ramp = 0.55f + 0.45f * (float) x / (float) w;
            const size_t i = ((size_t) y * w + x) * 4;
            px[i + 0] = (uint8_t) std::min (255, (int) (rr * ramp));
            px[i + 1] = (uint8_t) std::min (255, (int) (gg * ramp));
            px[i + 2] = (uint8_t) std::min (255, (int) (bb * ramp));
            px[i + 3] = 255;
        }
    return px;
}

struct Chan { double r, g, b; };
Chan meanChannels (const std::vector<uint8_t>& f)
{
    double r = 0, g = 0, b = 0;
    const size_t n = f.size() / 4;
    for (size_t i = 0; i < n; ++i) { r += f[i * 4]; g += f[i * 4 + 1]; b += f[i * 4 + 2]; }
    return { r / (double) n, g / (double) n, b / (double) n };
}

// Deja el lienzo ASENTADO con la foto `img` (corte) y devuelve el último cuadro.
std::vector<uint8_t> settle (supernova::MetalRenderer& r, const std::vector<uint8_t>& img, int frames)
{
    r.uploadImage ({ img.data(), kGrid, kGrid });
    std::vector<uint8_t> out ((size_t) kPx * kPx * 4);
    supernova::ParticleParams pp;
    for (int f = 0; f < frames; ++f)
        REQUIRE (r.renderOffscreen (supernova::scenarioFrame ("idle", f, frames), pp, kPx, kPx, out.data()));
    return out;
}
}

TEST_CASE ("dissolve: el CORTE salta y el fundido no — el mayor salto entre cuadros cae >8x",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer cut, fade;
    if (! gpuOk (cut)) return;
    cut.prepare (kGrid, kGrid);
    fade.prepare (kGrid, kGrid);

    const auto warm = solidField (kGrid, kGrid, 255,  96,  40);   // foto A · fuego
    const auto cool = solidField (kGrid, kGrid,  40, 120, 255);   // foto B · hielo

    supernova::ParticleParams pp;
    std::vector<uint8_t> prev ((size_t) kPx * kPx * 4), cur (prev.size());

    // --- CORTE (setDissolveSeconds 0 = el comportamiento histórico): el cambio es un escalón ---
    prev = settle (cut, warm, 40);
    cut.uploadImage ({ cool.data(), kGrid, kGrid });
    REQUIRE (cut.renderOffscreen (supernova::scenarioFrame ("idle", 0, 1), pp, kPx, kPx, cur.data()));
    const double cutJump = meanAbsDiff (prev, cur);
    WARN ("CORTE — delta medio por píxel entre el cuadro del cambio y el anterior = " << cutJump);
    REQUIRE (cutJump > 8.0);   // el bug, medido: el escalón que Joaquín ve al grabar

    // --- FUNDIDO 0.7 s: ningún par de cuadros consecutivos salta ---
    prev = settle (fade, warm, 40);
    fade.setDissolveSeconds (0.7);
    fade.uploadImage ({ cool.data(), kGrid, kGrid });

    double worst = 0.0;
    std::vector<Chan> means;
    for (int f = 0; f < 60; ++f)   // 42 cuadros de fundido + margen de asentado
    {
        REQUIRE (fade.renderOffscreen (supernova::scenarioFrame ("idle", f, 60), pp, kPx, kPx, cur.data()));
        worst = std::max (worst, meanAbsDiff (prev, cur));
        means.push_back (meanChannels (cur));
        prev = cur;
    }
    WARN ("FUNDIDO — peor delta medio entre cuadros consecutivos = " << worst);
    REQUIRE (worst < cutJump / 8.0);   // (a) el salto seco dividido ocho, como mínimo
}

TEST_CASE ("dissolve: el color medio viaja de A hacia B sin volver atrás, y termina en B asentada",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer fade, ref;
    if (! gpuOk (fade)) return;
    fade.prepare (kGrid, kGrid);
    ref.prepare (kGrid, kGrid);

    const auto warm = solidField (kGrid, kGrid, 255,  96,  40);
    const auto cool = solidField (kGrid, kGrid,  40, 120, 255);

    supernova::ParticleParams pp;
    std::vector<uint8_t> cur ((size_t) kPx * kPx * 4);

    // El de referencia: A asentada, después B por CORTE, rindiendo los MISMOS cuadros.
    settle (ref, warm, 40);
    const auto refFinal = settle (ref, cool, 60);

    settle (fade, warm, 40);
    fade.setDissolveSeconds (0.7);
    fade.uploadImage ({ cool.data(), kGrid, kGrid });

    std::vector<Chan> means;
    for (int f = 0; f < 60; ++f)
    {
        REQUIRE (fade.renderOffscreen (supernova::scenarioFrame ("idle", f, 60), pp, kPx, kPx, cur.data()));
        means.push_back (meanChannels (cur));
    }

    // (b) monótono de A (rojo) hacia B (azul): el rojo baja y el azul sube, cuadro a cuadro, sin retrocesos
    // que se noten (tolerancia mínima por el ruido del glifo/bloom).
    for (size_t i = 1; i < means.size(); ++i)
    {
        INFO ("cuadro " << i << " r=" << means[i].r << " b=" << means[i].b);
        REQUIRE (means[i].r <= means[i - 1].r + 0.05);
        REQUIRE (means[i].b >= means[i - 1].b - 0.05);
    }
    REQUIRE (means.front().r > means.back().r + 5.0);   // se movió de verdad
    REQUIRE (means.back().b  > means.front().b + 5.0);

    // (c) a los 42+ cuadros ya está en B ASENTADA. No byte-exacto a propósito: las posiciones NUNCA se
    // resetearon (eso es justamente lo correcto), así que el lattice llega de otra historia física.
    const double vsRef = meanAbsDiff (cur, refFinal);
    WARN ("fundido terminado vs corte asentado — delta medio = " << vsRef);
    REQUIRE (vsRef < 3.0);
}

TEST_CASE ("dissolve: retarget a mitad de camino (A→B, y a mitad entra C) no salta en ningún cuadro",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer fade;
    if (! gpuOk (fade)) return;
    fade.prepare (kGrid, kGrid);

    const auto warm  = solidField (kGrid, kGrid, 255,  96,  40);   // A · fuego
    const auto cool  = solidField (kGrid, kGrid,  40, 120, 255);   // B · hielo
    const auto green = solidField (kGrid, kGrid,  40, 255,  90);   // C · verde

    supernova::ParticleParams pp;

    // Vara de medir: UN fundido solo (A→B), en el mismo lienzo y con los mismos cuadros. El retarget no
    // puede saltar más que eso — si el re-arranque metiera un escalón, se vería justo acá.
    auto worstOf = [&] (bool retarget)
    {
        supernova::MetalRenderer r;
        r.prepare (kGrid, kGrid);
        std::vector<uint8_t> prev = settle (r, warm, 40), cur (prev.size());
        r.setDissolveSeconds (0.7);
        r.uploadImage ({ cool.data(), kGrid, kGrid });
        double worst = 0.0;
        for (int f = 0; f < 80; ++f)
        {
            if (retarget && f == 21) r.uploadImage ({ green.data(), kGrid, kGrid });   // a t ≈ 0.5 entra C
            REQUIRE (r.renderOffscreen (supernova::scenarioFrame ("idle", f, 80), pp, kPx, kPx, cur.data()));
            worst = std::max (worst, meanAbsDiff (prev, cur));
            prev = cur;
        }
        return std::make_pair (worst, meanChannels (cur));
    };

    const auto plain = worstOf (false);
    const auto retgt = worstOf (true);
    WARN ("RETARGET — peor delta entre cuadros: fundido simple = " << plain.first
          << " · con retarget a mitad = " << retgt.first);
    // El horneado A ← mix(A, B, m) evita el escalón: el re-arranque no salta MÁS que un fundido cualquiera…
    REQUIRE (retgt.first <= plain.first * 1.1);
    // …y ninguno de los dos es un corte encubierto: en este fixture un corte mide ~46 (ver el test de arriba),
    // así que este techo absoluto impide que la comparación relativa pase con los dos igual de rotos.
    REQUIRE (retgt.first < 8.0);

    // …y termina en C (verde), no en B: el retarget cambia el destino, no lo apila.
    const auto m = retgt.second;
    INFO ("final r=" << m.r << " g=" << m.g << " b=" << m.b);
    REQUIRE (m.g > m.b);
    REQUIRE (m.g > m.r);
}

TEST_CASE ("dissolve: el video escribe en el juego ENTRANTE — un frame verde a mitad de A→B termina verde",
           "[supernova][dissolve][.gpu]")
{
    supernova::MetalRenderer fade;
    if (! gpuOk (fade)) return;
    fade.prepare (kGrid, kGrid);

    const auto warm  = solidField (kGrid, kGrid, 255,  96,  40);
    const auto cool  = solidField (kGrid, kGrid,  40, 120, 255);
    const auto green = solidField (kGrid, kGrid,  40, 255,  90);

    supernova::ParticleParams pp;
    std::vector<uint8_t> cur ((size_t) kPx * kPx * 4);
    settle (fade, warm, 40);
    fade.setDissolveSeconds (0.7);
    fade.uploadImage ({ cool.data(), kGrid, kGrid });   // arranca A → B

    for (int f = 0; f < 60; ++f)
    {
        if (f == 21) fade.updateColors (green.data(), kGrid, kGrid);   // el video pinta su cuadro
        REQUIRE (fade.renderOffscreen (supernova::scenarioFrame ("idle", f, 60), pp, kPx, kPx, cur.data()));
    }
    const auto m = meanChannels (cur);
    INFO ("final r=" << m.r << " g=" << m.g << " b=" << m.b);
    REQUIRE (m.g > m.b);   // terminó en VERDE (lo que pintó el video), no en el azul de la foto entrante
    REQUIRE (m.g > m.r);
}
