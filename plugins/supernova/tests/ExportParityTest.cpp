// [supernova][exportparity] — el MP4 exportado tiene que verse como la VENTANA.
//
// Bug de campo (prompt 45, evidencia en 39-supernova-video-*.md): con los MISMOS parámetros la ventana
// muestra una figura nítida y el MP4 sale como una nube de puntos apilados en los bordes, más oscura.
// Dos causas medibles acá, sin abrir la app:
//
//   H1 — la simulación NO es invariante al refresh. La app rinde a 120 fps (ProMotion) y el export simula
//        a 1/60 fijo. La física integra fuerzas con dt, pero los decaimientos son POR CUADRO
//        (explodePulse *= 0.90, reformPulse, rayPulse, v = v·momentum, la estela): a 60 fps cada golpe dura
//        el doble de segundos, entrega ~2× el impulso y el freno tarda 2× → mucho más desplazamiento por
//        kick. El contrato que este test fija: el MISMO tramo de SEGUNDOS con el MISMO audio tiene que dar
//        el MISMO movimiento, se simule a 60 o a 120 pasos por segundo.
//        El ANCLA de la física es 120 Hz (D-43): a dt = 1/120 el decaimiento toma el atajo exacto y a 1/60
//        pasa por pow(). El contrato de invariancia NO depende del ancla — lo único que el ancla decide es
//        cuál de las dos tasas es la byte-exacta, y con cuál look se quedan las dos.
//
//   H2 — el offscreen ignora la invariancia al TAMAÑO. En pantalla el glifo escala con min(w,h)/1024; el
//        camino offscreen quedaba en 1.0 → el 4K dibuja los puntos 2,1× más chicos y sale ~2,7× más oscuro
//        que el 1080p de la misma toma. El contrato: con la invariancia encendida, la luminancia media no
//        depende del tamaño del cuadro.
//
// Tag [.gpu]: se auto-saltea sin GPU Metal (CI sigue verde).
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
constexpr int kGrid = 512;                // 262 144 partículas (default del spec)
constexpr int kPx   = 128;                // render chico: acá importan las POSICIONES, no los píxeles
constexpr double kBpm = 126.0;            // el tempo de la sesión del video de prensa
constexpr double kKickPeriod = 60.0 / kBpm;

// Los coeficientes REALES del mundo Parallax (FactoryPresets.cpp:182-187 pasados por ParamMapping.h).
// No se tocan: el pedido del 45 es que el export se vea como la ventana, no que la ventana cambie.
supernova::ParticleParams parallaxParams()
{
    supernova::ParticleParams pp;
    pp.intensity    = 0.42f;                       // INTENSITY 42
    pp.chaos        = 0.14f;                       // CHAOS 14
    pp.curlScale    = 0.30f + 0.30f * 2.40f;       // CURL 30      → 1.02
    pp.homeStrength = 0.20f + 0.70f * 2.40f;       // HOME 70      → 1.88
    pp.momentum     = 0.80f + 0.42f * 0.19f;       // MOMENTUM 42  → 0.8798
    pp.radialGain   = 0.20f + 0.45f * 2.00f;       // RADIAL 45    → 1.10
    pp.jitterGain   = 0.18f * 0.32f;               // JITTER 18
    pp.breatheGain  = 0.45f * 0.56f;               // BREATHE 45
    pp.particleSize = 0.5f + 0.38f * 3.5f;         // SIZE 38
    pp.glow         = 0.56f;                       // GLOW 56
    return pp;
}

// El MISMO tramo de audio, muestreado a los cuadros de cada tasa: los kicks caen en los mismos SEGUNDOS.
supernova::AnalysisFrame frameAt (int frameIdx, double dt)
{
    const double t     = frameIdx * dt;
    const double tPrev = (frameIdx - 1) * dt;
    supernova::AnalysisFrame af;
    af.bass   = 0.55f;
    af.rms    = 0.45f;
    af.energy = 0.45f;
    af.treble = 0.30f;
    const int kicks     = (int) std::floor (t / kKickPeriod);
    const int kicksPrev = (frameIdx == 0) ? -1 : (int) std::floor (tPrev / kKickPeriod);
    af.onset      = (kicks != kicksPrev);          // un solo cuadro por golpe, en el mismo SEGUNDO
    af.onsetCount = (unsigned) std::max (0, kicks + 1);
    return af;
}

// Desplazamiento medio desde el HOGAR (= las posiciones del cuadro 0, antes de tocar la física).
float meanDisplacement (const std::vector<float>& pos, const std::vector<float>& home)
{
    double sum = 0.0; size_t n = 0;
    for (size_t i = 0; i + 1 < pos.size(); i += 2)
    {
        const float dx = pos[i] - home[i], dy = pos[i + 1] - home[i + 1];
        if (! std::isfinite (dx) || ! std::isfinite (dy)) continue;
        sum += std::sqrt ((double) dx * dx + (double) dy * dy);
        ++n;
    }
    return n ? (float) (sum / (double) n) : 0.0f;
}

struct Run
{
    float peak = 0.0f;       // desplazamiento medio máximo del tramo
    float mean = 0.0f;       // desplazamiento medio promediado en el tramo
    float settleS = 0.0f;    // segundos desde el último golpe hasta volver por debajo del 25 % del pico
    float wallFrac = 0.0f;   // fracción MÁXIMA de partículas apiladas contra la pared (los bordes brillantes)
};

// Los "dos bordes verticales brillantes" de los MP4: partículas contra el clamp duro de ±0.08.
float wallFraction (const std::vector<float>& pos)
{
    size_t hit = 0, n = 0;
    for (size_t i = 0; i + 1 < pos.size(); i += 2)
    {
        const float x = pos[i], y = pos[i + 1];
        if (! std::isfinite (x) || ! std::isfinite (y)) continue;
        ++n;
        if (x < -0.075f || x > 1.075f || y < -0.075f || y > 1.075f) ++hit;
    }
    return n ? (float) ((double) hit / (double) n) : 0.0f;
}

// Simula `seconds` de audio idéntico con el paso `dt` y devuelve las métricas EN SEGUNDOS (no en cuadros).
Run simulate (double dt, double seconds, float intensity = -1.0f)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    r.setOffscreenDt (dt);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    const unsigned pairs = (unsigned) (kGrid * kGrid);
    std::vector<float> home ((size_t) pairs * 2), pos ((size_t) pairs * 2);
    REQUIRE (r.debugReadPositions (home.data(), pairs));   // cuadro 0 = hogar exacto (uploadImage por corte)

    auto pp = parallaxParams();
    if (intensity >= 0.0f) pp.intensity = intensity;
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);

    const int total = (int) std::llround (seconds / dt);
    const int stride = std::max (1, (int) std::llround (0.05 / dt));   // muestrear cada ~50 ms de AUDIO
    Run out;
    double sum = 0.0; int samples = 0;
    std::vector<std::pair<double, float>> trace;

    for (int f = 0; f < total; ++f)
    {
        const auto af = frameAt (f, dt);
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));
        if (f % stride == 0)
        {
            REQUIRE (r.debugReadPositions (pos.data(), pairs));
            const float d = meanDisplacement (pos, home);
            trace.push_back ({ f * dt, d });
            out.peak = std::max (out.peak, d);
            out.wallFrac = std::max (out.wallFrac, wallFraction (pos));
            sum += d; ++samples;
        }
    }
    out.mean = samples ? (float) (sum / samples) : 0.0f;

    // Re-armado: desde el último golpe, cuánto TIEMPO tarda en bajar del 25 % del pico.
    const double lastKick = std::floor ((total - 1) * dt / kKickPeriod) * kKickPeriod;
    out.settleS = (float) (seconds - lastKick);
    for (const auto& [t, d] : trace)
        if (t > lastKick && d < out.peak * 0.25f) { out.settleS = (float) (t - lastKick); break; }
    return out;
}
}

TEST_CASE ("exportparity: el movimiento por golpe NO depende del refresh (60 vs 120 pasos/s)",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const double seconds = 3.0;                     // ~6 golpes a 126 BPM
    const Run a = simulate (1.0 / 60.0,  seconds);  // el paso del EXPORT (dos cuadros de ancla por cuadro)
    const Run b = simulate (1.0 / 120.0, seconds);  // la VENTANA de la Mac de Joaquín = EL ANCLA (sin pow)

    const float peakRatio = (b.peak > 0.0f) ? a.peak / b.peak : 0.0f;
    const float meanRatio = (b.mean > 0.0f) ? a.mean / b.mean : 0.0f;
    INFO ("dt=1/60  : pico " << a.peak << "  medio " << a.mean << "  re-armado " << a.settleS << " s");
    INFO ("dt=1/120 : pico " << b.peak << "  medio " << b.mean << "  re-armado " << b.settleS << " s");
    INFO ("razon 60/120: pico " << peakRatio << "  medio " << meanRatio);

    REQUIRE (b.peak > 0.001f);                      // el kick mueve algo (si no, el test no mide nada)
    // Banda apretada (MEDIUM-2 del revisor del 45): el residuo tiene origen conocido — `momentum` integra
    // la fuerza con Euler explícito (v = v·m^n + f·dt), y el estado estacionario bajo fuerza constante da
    // una razón 60/120 CERRADA que depende del ancla:
    //     ancla 60 Hz (hasta 0.3.2):  2/(1+√m) = 1,032  para el momentum 0,8798 de Parallax
    //     ancla 120 Hz (D-43, hoy):   2/(1+m)  = 1,064  para ese mismo momentum   ← la que rige acá
    // (con el ancla en 120 el lado GRUESO pasa a ser el de 60 Hz: dos pasos de ancla por cuadro.)
    // OJO, y por eso está escrito: la banda está atada al momentum de Parallax, que es el mundo que este
    // test corre. El PISO del rango del knob es 0,80 (`ParamMapping.h`), y 2/(1+0,80) = 1,111 se sale del
    // techo de 1,10: si algún día este test se extiende a un mundo de momentum bajo, hay que subir el techo
    // (1,12 alcanza para todo el rango) — no es una regresión, es la aritmética del integrador.
    // La banda vieja (0,85–1,18) detectaba el bug grande (1,4–1,6×) pero se habría tragado una regresión
    // que llevara el residuo a 15 %. Este test fija "casi exacto", no "no rota como antes".
    CHECK (peakRatio > 0.92f);  CHECK (peakRatio < 1.10f);
    CHECK (meanRatio > 0.92f);  CHECK (meanRatio < 1.10f);
    // Y el re-armado tarda los mismos SEGUNDOS (el freno es por-cuadro: a 60 tardaba el doble).
    CHECK (std::abs (a.settleS - b.settleS) < 0.20f);
}

TEST_CASE ("exportparity: a INTENSITY 65 (la de las tomas descartadas) la brecha 60/120 sigue abierta",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    // El valor que Joaquín tenía en la sesión del 7-sep (las seis tomas descartadas): con INTENSITY 65 la
    // ventana a 120 Hz se leía como figura y el MP4 salía como nube con los bordes encendidos.
    const double seconds = 3.0;
    const Run a = simulate (1.0 / 60.0,  seconds, 0.65f);
    const Run b = simulate (1.0 / 120.0, seconds, 0.65f);

    const float peakRatio = (b.peak > 0.0f) ? a.peak / b.peak : 0.0f;
    INFO ("dt=1/60  : pico " << a.peak << "  medio " << a.mean << "  pared " << a.wallFrac * 100.0f << " %");
    INFO ("dt=1/120 : pico " << b.peak << "  medio " << b.mean << "  pared " << b.wallFrac * 100.0f << " %");
    INFO ("razon 60/120 del pico: " << peakRatio);

    CHECK (peakRatio > 0.92f);  CHECK (peakRatio < 1.10f);
    // `pared` es el síntoma que se VE en los MP4 (los dos bordes verticales encendidos). Con la imagen de
    // FÁBRICA (un degradé suave) no se reproduce: da 0 % en las dos tasas — el apilamiento necesita una foto
    // de alto contraste, que es lo que recorre el motor de Contornos. Queda reportado, no aseverado; el
    // apilamiento real se mira en el smoke [.exportparity-live], con la foto del kit de prensa.
    CHECK (a.wallFrac == b.wallFrac);
}

// ---------------------------------------------------------------------------------------------------------
// H2 — invariancia al TAMAÑO en el camino offscreen.
//
// En pantalla el glifo escala con min(w,h)/1024 (`resScale`, encodeComposite) para que la figura cubra la
// MISMA fracción del cuadro en la vista chica, en inmersivo y en fullscreen — "exposición aditiva constante:
// d² crece como los píxeles" (ShaderSource.h:65-68). El camino offscreen quedaba clavado en 1,0: el 4K
// dibujaba los puntos a tamaño de 1024 sobre 4× más píxeles → el mismo clip salía ~2,7× más oscuro en 4K
// que en 1080p (medido por el verificador sobre la pareja de las 18:07).
//
// La medición se hace DENTRO del recuadro del contenido: la imagen de fábrica es cuadrada y en 16:9 entra
// con pillarbox, así que la media sobre el cuadro entero compararía barras negras, no exposición.
namespace
{
struct Exposure { float luma = 0.0f; float litFrac = 0.0f; };

// Recuadro del CONTENIDO con FIT y una imagen cuadrada: un cuadrado de lado min(w,h), centrado.
Exposure exposureInContent (const std::vector<uint8_t>& rgba, int w, int h)
{
    const int side = std::min (w, h);
    const int x0 = (w - side) / 2, y0 = (h - side) / 2;
    double sum = 0.0; long lit = 0, n = 0;
    for (int y = y0; y < y0 + side; ++y)
        for (int x = x0; x < x0 + side; ++x)
        {
            const size_t i = ((size_t) y * w + x) * 4;
            const double l = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
            sum += l; if (l > 16.0) ++lit; ++n;
        }
    return { (float) (sum / (double) n), (float) ((double) lit / (double) n) };
}

Exposure renderIdle (int w, int h, bool sizeInvariance)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    r.setOffscreenSizeInvariance (sizeInvariance);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    supernova::ParticleParams pp = parallaxParams();
    supernova::AnalysisFrame af;                 // `idle`: sin golpes, la figura asentada
    af.bass = 0.08f; af.rms = 0.30f; af.energy = 0.30f; af.treble = 0.12f;

    std::vector<uint8_t> rgba ((size_t) w * h * 4);
    for (int f = 0; f < 60; ++f) REQUIRE (r.renderOffscreen (af, pp, w, h, rgba.data()));
    return exposureInContent (rgba, w, h);
}
}

TEST_CASE ("exportparity: la exposición offscreen no depende del tamaño del cuadro",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const Exposure sq = renderIdle (512,  512,  true);
    const Exposure hd = renderIdle (1920, 1080, true);
    const Exposure uhd= renderIdle (3840, 2160, true);

    INFO ("512x512   luma " << sq.luma  << "  encendidos " << sq.litFrac  * 100.0f << " %");
    INFO ("1920x1080 luma " << hd.luma  << "  encendidos " << hd.litFrac  * 100.0f << " %");
    INFO ("3840x2160 luma " << uhd.luma << "  encendidos " << uhd.litFrac * 100.0f << " %");
    INFO ("4K/1080p: luma " << uhd.luma / hd.luma << "  encendidos " << uhd.litFrac / hd.litFrac);

    REQUIRE (hd.luma > 1.0f);
    CHECK (std::abs (uhd.luma / hd.luma - 1.0f) < 0.10f);    // el 4K salía a 0,37 del 1080p
    CHECK (std::abs (sq.luma  / hd.luma - 1.0f) < 0.10f);
    CHECK (std::abs (uhd.litFrac / hd.litFrac - 1.0f) < 0.10f);
    CHECK (std::abs (sq.litFrac  / hd.litFrac - 1.0f) < 0.10f);
}

TEST_CASE ("exportparity: la invariancia al tamaño no se prende sola, y apagada el 4K sigue oscuro",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    // El default es apagado: goldens, --render-frames y todo lo histórico siguen en resScale 1,0. Este test
    // fija DOS cosas y ninguna más: que el flag no se prende solo, y que apagado el 4K sigue saliendo
    // oscuro (el camino que los goldens fijaron; el export es el que enciende la invariancia). NO es una
    // prueba de identidad de BYTES contra el binario anterior — de eso se ocupan los goldens, que son
    // archivos versionados; se llamaba "el camino legacy no se mueve" y prometía más de lo que mide.
    CHECK_FALSE (probe.offscreenSizeInvariance());

    const Exposure hd  = renderIdle (1920, 1080, false);
    const Exposure uhd = renderIdle (3840, 2160, false);
    INFO ("legacy 1080p luma " << hd.luma << "  4K luma " << uhd.luma
          << "  razon " << uhd.luma / hd.luma);
    REQUIRE (hd.luma > 1.0f);
    CHECK (uhd.luma / hd.luma < 0.75f);      // el bug, tal cual, cuando la invariancia está apagada
}

// ---------------------------------------------------------------------------------------------------------
// H3 — el export decodificaba las fotos por un camino DISTINTO del vivo.
//
// En vivo: `DecodedImageCache::decodeBaseImage` (RGBA enderezado + saliencia Vision + máscara del sujeto)
// y la subida SIEMPRE con `source (true)` — la máscara viaja, el knob CUTOUT decide cuánto fondo borra.
// En el export: `ImageLoader::fromFile` pelado (sin Vision) y `source (cutout)`, o sea sin máscara salvo
// que CUTOUT estuviera arriba. Sin saliencia los pesos salen de la luma; sin máscara el depth cae al "modo
// foto" (relieve por luma) en vez de la almohada del sujeto → con DEPTH alto, otro volumen que la pantalla.
#include "image/DecodedImageCache.h"
#include "image/ImageLoader.h"

namespace
{
// Una lámina como las del kit de prensa: sujeto claro y nítido sobre fondo plano oscuro.
juce::File writeSubjectOnFlatBackground (int w, int h)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    {
        juce::Graphics g (img);
        g.fillAll (juce::Colour::fromRGB (6, 6, 8));                 // fondo plano (el de las láminas)
        g.setColour (juce::Colours::white);
        g.fillEllipse (w * 0.28f, h * 0.18f, w * 0.44f, h * 0.62f);  // el sujeto
        g.setColour (juce::Colour::fromRGB (20, 20, 26));
        g.fillEllipse (w * 0.40f, h * 0.34f, w * 0.20f, h * 0.26f);  // un hueco: relieve interno
    }
    const juce::File f = juce::File::createTempFile ("jpg");
    juce::FileOutputStream os (f);
    juce::JPEGImageFormat jpg; jpg.setQuality (0.92f);
    jpg.writeImageToStream (img, os); os.flush();
    return f;
}

// Cuadro asentado (sin golpes) desde una SourceImage cualquiera, con DEPTH alto como Parallax.
std::vector<uint8_t> settledFrame (const supernova::SourceImage& src, int w, int h)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    r.setOffscreenSizeInvariance (true);
    r.uploadImage (src);

    supernova::ParticleParams pp = parallaxParams();
    pp.depthAmt = 0.88f;                        // DEPTH 88: el volumen del mundo Parallax
    pp.rotYRad  = -0.279f;                      // ROT Y −16°, el que hace visible la almohada
    supernova::AnalysisFrame af;
    af.bass = 0.08f; af.rms = 0.30f; af.energy = 0.30f; af.treble = 0.12f;

    std::vector<uint8_t> rgba ((size_t) w * h * 4);
    for (int f = 0; f < 60; ++f) REQUIRE (r.renderOffscreen (af, pp, w, h, rgba.data()));
    return rgba;
}

float meanAbsDiff (const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    REQUIRE (a.size() == b.size());
    double s = 0.0; size_t n = 0;
    for (size_t i = 0; i + 3 < a.size(); i += 4)
    {
        for (int k = 0; k < 3; ++k) s += std::abs ((int) a[i + k] - (int) b[i + k]);
        n += 3;
    }
    return n ? (float) (s / (double) n) : 0.0f;
}
}

TEST_CASE ("exportparity: el export decodifica como el vivo — saliencia y máscara del sujeto",
           "[supernova][exportparity][.gpu]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const juce::File f = writeSubjectOnFlatBackground (900, 900);
    const auto pelado = supernova::ImageLoader::fromFile (f);            // el camino viejo del export
    const auto vivo   = supernova::decodeBaseImage (f);                  // el camino de la ventana
    const auto expo   = supernova::exportSlotImage (f, 0);               // el camino del export HOY

    REQUIRE (pelado.valid()); REQUIRE (vivo.valid()); REQUIRE (expo.valid());
    // El camino viejo no trae ninguno de los dos campos: eso es el defecto, medido.
    CHECK (pelado.saliency.empty());
    CHECK (pelado.subjectMask.empty());
    // El vivo trae los dos: hay algo real que el export estaba tirando.
    REQUIRE (vivo.saliency.size()    == (size_t) supernova::kParticleGrid * supernova::kParticleGrid);
    REQUIRE (vivo.subjectMask.size() == (size_t) supernova::kParticleGrid * supernova::kParticleGrid);

    // CONTRATO: la foto que el export sube tiene que traer lo mismo que la que sube la ventana.
    CHECK ((expo.rgba == vivo.rgba));
    CHECK ((expo.saliency == vivo.saliency));
    CHECK ((expo.subjectMask == vivo.subjectMask));

    // …y con la rotación manual, la rotación DERIVADA (no un decode nuevo: Vision no es equivariante).
    const auto expo1 = supernova::exportSlotImage (f, 1);
    const auto vivo1 = supernova::rotatedCopy (vivo, 1);
    CHECK ((expo1.rgba == vivo1.rgba));
    CHECK ((expo1.saliency == vivo1.saliency));
    CHECK ((expo1.subjectMask == vivo1.subjectMask));

    f.deleteFile();
}

TEST_CASE ("exportparity: con DEPTH alto la máscara del sujeto CAMBIA el cuadro (no es cosmética)",
           "[supernova][exportparity][.gpu]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const juce::File f = writeSubjectOnFlatBackground (900, 900);
    const auto vivo = supernova::decodeBaseImage (f);
    REQUIRE (vivo.valid());
    REQUIRE (! vivo.subjectMask.empty());

    const int W = 640, H = 360;
    const auto conMascara = settledFrame (vivo.source (true),  W, H);
    const auto sinMascara = settledFrame (vivo.source (false), W, H);
    const float mad = meanAbsDiff (conMascara, sinMascara);
    INFO ("MAD entre el cuadro CON máscara y el SIN máscara (DEPTH 88, ROT Y −16°): " << mad);
    CHECK (mad > 1.0f);   // el depth pasa de la almohada del sujeto al relieve por luma: se ve

    // Y el camino del export tiene que dar el cuadro CON máscara, no el otro.
    const auto delExport = settledFrame (supernova::exportSlotImage (f, 0).source (true), W, H);
    INFO ("MAD export vs con-máscara: " << meanAbsDiff (delExport, conMascara)
          << "   export vs sin-máscara: " << meanAbsDiff (delExport, sinMascara));
    CHECK (meanAbsDiff (delExport, conMascara) < meanAbsDiff (delExport, sinMascara));

    f.deleteFile();
}

// ---------------------------------------------------------------------------------------------------------
// H4 — golpes que no están en la música.
//
// El renderer nace con `lastOnsetCount = 0` y el anillo del export trae un contador de miles: el flanco
// disparaba una explosión en el CUADRO 0. Y al volver al principio del anillo (cada 12 s) el contador BAJA,
// que el flanco también leía como golpe → explosión a los 12 s y a los 24 s. Nada de eso está en el audio.
#include "video/ExportOnsets.h"

namespace
{
// La explosión se VE: el glifo infla con el kick (pointSize · (1 + explodePulse · PUMP)) y el cuadro
// FLASHEA. Medimos el pico de luminancia media en una tanda SIN un solo golpe en la música — un flash ahí
// es un golpe inventado.
float peakFlashNoMusic (const std::vector<unsigned>& counts)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    auto pp = parallaxParams();
    pp.pumpAmt = 1.2f;                       // PUMP 60: el flash del kick, bien legible
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);
    float peak = 0.0f;
    for (size_t f = 0; f < counts.size(); ++f)
    {
        supernova::AnalysisFrame af;         // música SIN golpes: hay sonido, no hay onsets
        af.bass = 0.45f; af.rms = 0.40f; af.energy = 0.40f; af.treble = 0.25f;
        af.onset = false; af.onsetCount = counts[f];
        REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));

        double sum = 0.0;
        for (size_t i = 0; i + 3 < rgba.size(); i += 4)
            sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
        peak = std::max (peak, (float) (sum / (double) (kPx * kPx)));
    }
    return peak;
}
}

TEST_CASE ("exportparity: sin golpes en la música, el clip no explota ni al empezar ni al dar la vuelta",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    // Referencia: el mismo tramo con el contador quieto en 0 (lo que ve un renderer recién nacido en un
    // stream sin onsets). Es el "cero" con el que se compara todo lo demás.
    std::vector<unsigned> quieto (60, 0u);
    const float base = peakFlashNoMusic (quieto);

    // (a) CUADRO 0: el anillo del export arranca con un contador de miles.
    std::vector<unsigned> desdeMiles (60, 1500u);
    const float arranque = peakFlashNoMusic (desdeMiles);

    // (b) VUELTA DEL ANILLO: 30 cuadros con el contador arriba y de golpe vuelve al principio de la ventana.
    std::vector<unsigned> conVuelta;
    for (int i = 0; i < 30; ++i) conVuelta.push_back (1500u);
    for (int i = 0; i < 30; ++i) conVuelta.push_back (1441u);   // el contador BAJA: el clip volvió a empezar
    const float vuelta = peakFlashNoMusic (conVuelta);

    INFO ("silencio con contador quieto : " << base);
    INFO ("silencio arrancando en 1500  : " << arranque);
    INFO ("silencio con vuelta de anillo: " << vuelta);
    CHECK (arranque < base * 1.10f + 1.0e-4f);
    CHECK (vuelta   < base * 1.10f + 1.0e-4f);
}

TEST_CASE ("exportonsets: cada golpe entra UNA vez, también al dar la vuelta al anillo",
           "[supernova][exportonsets]")
{
    // A 60 fps sobre un anillo de 30 Hz cada frame de análisis alimenta DOS cuadros del clip: sólo el
    // primero de los dos tiene que golpear.
    supernova::ExportOnsetState st;
    const int idx[] = { 0, 0, 1, 1, 2, 2, 3, 3 };
    int golpes = 0;
    for (int i : idx) golpes += supernova::exportOnsetStep (st, i).keepOnset ? 1 : 0;
    CHECK (golpes == 4);      // cuatro frames de análisis, cuatro golpes — no ocho

    // Al volver al principio del anillo el índice retrocede: ese cuadro estrena un frame de análisis, así
    // que su golpe TAMBIÉN entra (la explosión falsa de la vuelta la ataja el renderer, no esta regla).
    supernova::ExportOnsetState st2;
    const int conVuelta[] = { 0, 1, 2, 0, 0, 1 };
    int golpes2 = 0;
    for (int i : conVuelta) golpes2 += supernova::exportOnsetStep (st2, i).keepOnset ? 1 : 0;
    CHECK (golpes2 == 5);     // 0,1,2,0,1 estrenan; el segundo 0 seguido no

    // El primer cuadro del clip estrena su frame (el estado nace en −1, que no es un índice válido).
    supernova::ExportOnsetState st3;
    CHECK (supernova::exportOnsetStep (st3, 0).keepOnset);
}

// ---------------------------------------------------------------------------------------------------------
// H5 — el clip arrancaba de cero: partículas quietas en su hogar, frontal, sin las fases del mundo.
//
// El export crea un renderer FRESCO. Eso está bien para el determinismo (es el patrón de los goldens) pero
// significaba que el cuadro 0 del MP4 era la imagen QUIETA — la ventana, en cambio, lleva minutos en régimen
// e inclinada por ORBIT. Dos arreglos: un tramo de calentamiento determinista antes del cuadro 0, y heredar
// las fases acumuladas (ROTATE / ORBIT / HUE CYC) del renderer vivo.
#include "video/ExportWarmup.h"
#include "render/ViewPhases.h"

namespace
{
struct Warmed { float displacement = 0.0f; std::vector<uint8_t> frame; };

// Un renderer con `warmup` cuadros de música encima (o ninguno) + el cuadro que sigue.
Warmed warmedFrame (int warmup, supernova::ViewPhases phases, int w, int h)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    r.setOffscreenSizeInvariance (true);
    r.setViewPhases (phases);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    const unsigned pairs = (unsigned) (kGrid * kGrid);
    std::vector<float> home ((size_t) pairs * 2), pos ((size_t) pairs * 2);
    REQUIRE (r.debugReadPositions (home.data(), pairs));

    auto pp = parallaxParams();
    Warmed out;
    out.frame.assign ((size_t) w * h * 4, 0);
    for (int f = 0; f < warmup + 1; ++f)
    {
        const auto af = frameAt (f, 1.0 / 60.0);            // la misma música de siempre, a 126 BPM
        REQUIRE (r.renderOffscreen (af, pp, w, h, out.frame.data()));
    }
    REQUIRE (r.debugReadPositions (pos.data(), pairs));
    out.displacement = meanDisplacement (pos, home);
    return out;
}
}

TEST_CASE ("exportwarmup: el calentamiento son los cuadros que PRECEDEN al 0 dentro del loop",
           "[supernova][exportwarmup]")
{
    using supernova::exportWarmupVideoFrame;
    // Loop de 720 cuadros (12 s a 60 fps), 120 de calentamiento → arranca en el 600 y termina en el 719.
    CHECK (exportWarmupVideoFrame (0,   120, 720) == 600);
    CHECK (exportWarmupVideoFrame (119, 120, 720) == 719);
    // Loop más corto que el calentamiento: se dan varias vueltas, sin índices negativos ni fuera de rango.
    for (int k = 0; k < 120; ++k)
    {
        const int v = exportWarmupVideoFrame (k, 120, 50);
        CHECK (v >= 0); CHECK (v < 50);
    }
    CHECK (exportWarmupVideoFrame (119, 120, 50) == 49);   // el último paso siempre cae justo antes del 0
    CHECK (exportWarmupVideoFrame (0, 120, 0) == 0);       // sin análisis no hay loop: no explota
}

TEST_CASE ("exportparity: tras el calentamiento el cuadro 0 está en RÉGIMEN, no quieto",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const int W = 320, H = 180;
    const Warmed frio     = warmedFrame (0, {}, W, H);                              // el cuadro 0 de hoy
    const Warmed caliente = warmedFrame (supernova::kExportWarmupFrames, {}, W, H);
    const Warmed regimen  = warmedFrame (supernova::kExportWarmupFrames * 3, {}, W, H);

    INFO ("desplazamiento medio  frio " << frio.displacement
          << "  tras calentar " << caliente.displacement
          << "  regimen largo " << regimen.displacement);
    CHECK (frio.displacement < 0.002f);                        // arranca literalmente en el hogar
    REQUIRE (regimen.displacement > 0.005f);
    // Tras el calentamiento el estado es el del régimen, no el del arranque.
    CHECK (std::abs (caliente.displacement / regimen.displacement - 1.0f) < 0.25f);
}

TEST_CASE ("exportparity: el clip hereda las fases del mundo (ORBIT / ROTATE / HUE CYC)",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const int W = 320, H = 180;
    const Warmed frontal   = warmedFrame (30, {}, W, H);
    const Warmed inclinado = warmedFrame (30, { 0.0f, 0.9f, 0.0f }, W, H);   // ORBIT acumulado ≈ 52°
    const Warmed tenido    = warmedFrame (30, { 0.0f, 0.0f, 1.2f }, W, H);   // HUE CYC acumulado

    INFO ("MAD frontal vs con ORBIT: " << meanAbsDiff (frontal.frame, inclinado.frame)
          << "   frontal vs con HUE CYC: " << meanAbsDiff (frontal.frame, tenido.frame));
    CHECK (meanAbsDiff (frontal.frame, inclinado.frame) > 1.0f);   // heredar ORBIT cambia el encuadre
    CHECK (meanAbsDiff (frontal.frame, tenido.frame)    > 1.0f);   // heredar HUE CYC cambia el tono

    // Y las fases que se leen son las que se pusieron (el editor las lee del renderer vivo y las manda).
    supernova::MetalRenderer r;
    r.prepare (64, 64);
    r.setViewPhases ({ 0.25f, -0.5f, 1.75f });
    const auto ph = r.viewPhases();
    CHECK (ph.rotate == 0.25f);
    CHECK (ph.orbit  == -0.5f);
    CHECK (ph.hue    == 1.75f);
}

TEST_CASE ("exportparity: dos clips del mismo estado dan los MISMOS bytes",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const int W = 320, H = 180;
    const supernova::ViewPhases ph { 0.13f, 0.9f, 0.4f };
    const Warmed a = warmedFrame (supernova::kExportWarmupFrames, ph, W, H);
    const Warmed b = warmedFrame (supernova::kExportWarmupFrames, ph, W, H);
    CHECK ((a.frame == b.frame));                       // byte a byte, calentamiento y fases incluidos
    CHECK (a.displacement == b.displacement);
}

// LOW-2 del revisor del prompt 45: `snapToHome` (CLEAR) ponía `onsetSeeded = false`, así que el cuadro
// SIGUIENTE re-sembraba el contador sin disparar. En vivo, cuando el pipeline latest-wins pisa el bool
// `onset` (thread batch → triple buffer → lectura del render), el único camino que le queda a un kick real
// es el flanco del contador monotónico — y ese kick, si caía justo en el primer cuadro después de un CLEAR,
// se perdía. El export nunca llama a snapToHome, así que esto es de la ventana.
namespace
{
float flashTrasClear (bool golpeEnElContador)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });
    auto pp = parallaxParams();
    pp.pumpAmt = 1.2f;                        // PUMP 60: el flash del kick, bien legible
    std::vector<uint8_t> rgba ((size_t) kPx * kPx * 4);

    supernova::AnalysisFrame af;              // música sin golpes en el bool: sólo el contador habla
    af.bass = 0.45f; af.rms = 0.40f; af.energy = 0.40f; af.treble = 0.25f;
    af.onset = false; af.onsetCount = 1500u;
    for (int f = 0; f < 8; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));

    r.snapToHome();                           // CLEAR
    af.onsetCount = golpeEnElContador ? 1501u : 1500u;
    REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, rgba.data()));

    double sum = 0.0;
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
        sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
    return (float) (sum / (double) (kPx * kPx));
}
}

TEST_CASE ("exportparity: después de un CLEAR, un golpe que sólo viaja en el contador NO se pierde",
           "[supernova][exportparity][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { SUCCEED ("sin GPU Metal — paridad de export salteada (CI)"); return; }

    const float conGolpe = flashTrasClear (true);
    const float sinGolpe = flashTrasClear (false);
    INFO ("luma del cuadro siguiente al CLEAR — con golpe " << conGolpe << " · sin golpe " << sinGolpe
          << " → " << conGolpe / sinGolpe << "x");
    CHECK (conGolpe > sinGolpe * 1.05f);      // el kick se ve; antes ese cuadro salía igual que sin golpe
}

