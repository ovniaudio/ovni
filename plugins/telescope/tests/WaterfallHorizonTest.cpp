// [telescope][waterfall][horizon] — LA OCLUSIÓN DEL WATERFALL, contra el algoritmo del pintor.
//
// WaterfallLens dibuja del FRENTE hacia el FONDO con un HORIZONTE (ver el encabezado de WaterfallLens.h):
// cada línea pinta sólo lo que queda por encima de todo lo que ya se pintó, y después baja el horizonte.
// Es equivalente al algoritmo del pintor —de atrás hacia adelante, cada línea rellenando con el fondo desde
// su curva hasta el suelo y trazándose encima— y cuesta un orden de magnitud menos. La equivalencia es la
// razón por la que el dibujo se puede pagar; este test la VERIFICA en vez de confiar en ella.
//
// EL BUG QUE ESTE TEST CAZA (MEDIUM del revisor del 53, WaterfallLens.cpp:207). El horizonte se bajaba con
// `y` —la altura de la curva en ESTA columna de píxel— cuando lo que la línea realmente pintó va de `top`
// = min(y, prevY) hasta `bot`. Con una pendiente suave los dos coinciden y no se nota; con un ESCALÓN
// (un tono puro cae 40 dB de un píxel al otro) `prevY` queda muy por encima de `y`, el horizonte registra
// `y` y una lámina LEJANA vuelve a pintar sobre las filas [top, y−1] que la cercana ya había pintado. En
// pantalla: una línea del fondo cruzando por encima de una del frente.
//
// CÓMO SE COMPARA. El pintor bruto se construye acá con la MISMA geometría de la lente (proyección,
// selección de columnas y decimación del anillo son públicas o reproducibles), se rasteriza de atrás hacia
// adelante con relleno de verdad, y se compara PÍXEL A PÍXEL contra lo que la lente REAL pintó — no contra
// una transcripción de su algoritmo, que no probaría nada. Si la reconstrucción de la geometría estuviera
// mal, las imágenes no coincidirían: que el test pase prueba las dos cosas a la vez.
//
// Los píxeles que la lente dibuja ENCIMA del waterfall (el suelo del escenario, los rótulos de tiempo y el
// del canal) se excluyen con una máscara generosa: son idénticos con el bug y sin él, así que no aportan
// señal, y reproducir su antialias sería reproducir el rasterizador de JUCE.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "PluginProcessor.h"
#include "lenses/Look.h"   // el trazo frontal y el relleno del 56 usan sus tokens
#include "TestHelpers.h"
#include "lenses/Projection2p5.h"
#include "lenses/WaterfallLens.h"
#include "ui-kit/Theme.h"

namespace
{
namespace th = ovni::ui::theme;

// El plot tiene que salir de 160×90: la imagen chica donde el pintor bruto SÍ entra. Los números salen de
// WaterfallLens::zonesFor (padIn=16 · footer=jlimit(22,30,h/22) · padIn/2 · kFreqH=15 · kAxisW=44), y el
// test los REQUIERE, así que si la geometría de la lente cambia esto se entera acá y no en silencio.
constexpr int kCompW = 236, kCompH = 167;
constexpr int kPlotW = 160, kPlotH = 90;

juce::Rectangle<int> plotRectFor (int w, int h)
{
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);
    body.removeFromBottom (juce::jlimit (22, 30, h / 22));   // footer
    body.removeFromBottom (th::padIn / 2);
    body.removeFromBottom (15);                              // WaterfallLens::kFreqH
    body.removeFromLeft (44);                                // WaterfallLens::kAxisW
    return body;
}

// Un tono puro que SALTA de nivel: el escalón en frecuencia (la falda de un seno cae decenas de dB de un
// píxel al otro) más el escalón en tiempo (la amplitud alterna 40 dB cada 0.4 s), o sea las dos formas de
// que una lámina lejana quede por encima de una cercana en unas columnas y por debajo en otras.
void pushSteppedTone (telescope::TelescopeProcessor& proc, double seconds)
{
    constexpr double sr = 48000.0, hz = 997.0, stepSec = 0.4;
    const float loud = std::pow (10.0f, -6.0f / 20.0f), quiet = std::pow (10.0f, -46.0f / 20.0f);

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const auto total = (long long) std::llround (seconds * sr);

    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i, ++n)
        {
            const bool  hi = ((long long) ((double) n / (sr * stepSec)) % 2) == 0;
            const float a  = hi ? loud : quiet;
            const auto  v  = (float) (a * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) n / sr));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);

        const double pushed = (double) n / sr;
        if (pushed - proc.analysis().read().timeSeconds > 1.5)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 0.6; }, 8000));
    }
}

// ========================================================================================================
// EL PINTOR BRUTO: de ATRÁS hacia ADELANTE, cada línea rellenando con el fondo desde su curva hasta el
// suelo de la imagen y trazándose encima. Es el dibujo de referencia — caro e imposible de pagar en vivo
// con 120 líneas sobre 950×400, trivial acá sobre 160×90.
//
// Todo lo que no es el orden de dibujo es IDÉNTICO a la lente: mismas columnas del anillo (selectColumns),
// misma decimación (máximo del par de filas), misma interpolación al eje log, mismo `top`/`bot` por
// columna de píxel, mismo color por profundidad.
// ========================================================================================================
struct BruteResult
{
    juce::Image img;
    int         painted = 0;   // píxeles que quedaron con color de línea (no fondo)
};

BruteResult brutePaint (telescope::TelescopeProcessor& proc, const telescope::Projection2p5& local,
                        int w, int h)
{
    using telescope::WaterfallLens;
    constexpr int kRows = telescope::SpectrogramRing::kRows;

    const auto& ring = proc.spectrogram();
    const int avail = juce::jmin (ring.count(), ring.capacity());

    std::vector<long long> srcIdx ((size_t) WaterfallLens::kMaxLines, 0);
    const int lines = WaterfallLens::selectColumns (ring.writeIndex(), avail, proc.waterfallLines(),
                                                    srcIdx.data());
    REQUIRE (lines > 1);

    const auto bgCol  = th::bg1.withAlpha (1.0f);
    const auto bgArgb = bgCol.getARGB();
    // 57b — el COLOR sale de WaterfallLens::Shading, que es donde vive la fórmula. Este pintor
    // re-implementa el ORDEN de dibujo (que es lo que el test compara), no la paleta: copiar la fórmula
    // acá sería garantizar que una de las dos copias se quede vieja y el test se ponga rojo por un motivo
    // que no es el suyo.
    const auto pal = telescope::look::paletteFromIndex (proc.paletteIndex());
    std::array<juce::uint32, 256> lineLut {};
    std::array<juce::uint32, (size_t) WaterfallLens::Shading::kFillRampN
                            * WaterfallLens::Shading::kFillLevels> fillLut {};

    juce::Image img (juce::Image::ARGB, w, h, false);
    img.clear (img.getBounds(), bgCol);

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    REQUIRE (bd.pixelStride == 4);

    juce::uint8 col[kRows];
    juce::uint8 pts[WaterfallLens::kMaxPoints];

    // FONDO → FRENTE (al revés que la lente): el pintor.
    for (int i = 0; i < lines; ++i)
    {
        if (! ring.copyColumn (srcIdx[(size_t) i], col)) continue;
        for (int j = 0; j < WaterfallLens::kMaxPoints; ++j)
            pts[j] = juce::jmax (col[2 * j], col[2 * j + 1]);
        WaterfallLens::smoothPoints (pts, WaterfallLens::kMaxPoints);   // 57b, igual que la lente

        const float z = lines > 1 ? (float) (lines - 1 - i) / (float) (lines - 1) : 0.0f;

        const int xL = juce::jmax (0,     (int) std::floor (local.leftX (z)));
        const int xR = juce::jmin (w - 1, (int) std::ceil  (local.rightX (z)));
        if (xR < xL) continue;

        const bool isFront = (i == lines - 1);

        // ===== fusión 55 + 56 + 57b ===== El pintor tiene que dibujar lo MISMO que la lente o el test
        // compararía dos escenas distintas: cada columna se RELLENA desde la curva hasta el suelo con el
        // color de su nivel (aclarándose en las filas pegadas a la línea) y el trazo va encima. El pintor
        // lo hace de atrás hacia adelante, y lo de adelante tapa; la equivalencia píxel a píxel sigue
        // siendo la misma afirmación.
        const WaterfallLens::Shading sh { pal, WaterfallLens::Shading::kMaxFog * z, isFront };
        sh.buildLuts (lineLut.data(), fillLut.data());
        int prevY = -1;

        for (int px = xL; px <= xR; ++px)
        {
            const float x01 = local.unprojectX ((float) px + 0.5f, z);
            const double jf = juce::jlimit (0.0, (double) (WaterfallLens::kMaxPoints - 1),
                                            ((double) x01 * (double) (kRows - 1) - 0.5) * 0.5);
            const int    j0 = juce::jlimit (0, WaterfallLens::kMaxPoints - 1, (int) jf);
            const int    j1 = juce::jmin (WaterfallLens::kMaxPoints - 1, j0 + 1);
            const double fr = jf - (double) j0;
            const double v  = (1.0 - fr) * (double) pts[j0] + fr * (double) pts[j1];

            const int y   = (int) std::lround (local.project (x01, (float) (v / 255.0), z).y);
            const int top = juce::jmax (0, juce::jmin (y, prevY < 0 ? y : prevY));
            const int bot = juce::jmin (h - 1, juce::jmax (y, prevY < 0 ? y : prevY) + (isFront ? 1 : 0));

            // 1 · el RELLENO: desde la curva hasta el suelo de la imagen, con el color de SU nivel (57b).
            //     El índice del degradado se cuenta desde `bot + 1`, que es donde la lente lo arranca
            //     (`max (top, bot + 1)`, y bot ≥ top siempre). Es lo que tapa a todo lo dibujado antes.
            const int lv = juce::jlimit (0, 255, (int) std::lround (v));
            // El origen del degradado es EL MISMO que el de la lente: `max (top, bot + 1)`. No alcanza con
            // `bot + 1`: cuando la curva se va por arriba de la imagen (y < 0) el clamp deja bot POR
            // DEBAJO de top, y ahí los dos orígenes dejan de coincidir. Se ve con la inclinación más
            // pronunciada, que es la que recorta más líneas contra el techo.
            const int fillFrom = juce::jmax (top, bot + 1);
            const int fv = WaterfallLens::Shading::fillIndex (lv);
            for (int yy = top; yy < h; ++yy)
            {
                const int k = juce::jlimit (0, WaterfallLens::Shading::kFillRampN - 1, yy - fillFrom);
                *((juce::uint32*) bd.getLinePointer (yy) + px)
                    = fillLut[(size_t) (k * WaterfallLens::Shading::kFillLevels + fv)];
            }
            // 2 · el TRAZO de la curva, encima de su propio relleno.
            for (int yy = top; yy <= bot; ++yy)
                *((juce::uint32*) bd.getLinePointer (yy) + px) = lineLut[(size_t) lv];

            prevY = y;
        }
    }

    BruteResult r;
    r.img = img;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (*((const juce::uint32*) bd.getLinePointer (y) + x) != bgArgb) ++r.painted;
    return r;
}

// Lo que la lente pinta ENCIMA de la imagen del waterfall (paintLive): el suelo del escenario, los
// rótulos de tiempo con su fondo y el rótulo de canal. Se enmascara con holgura: son idénticos con el bug
// y sin él, así que excluirlos no le saca ni un gramo de señal al test.
std::vector<bool> overlayMask (const telescope::Projection2p5& comp, juce::Rectangle<int> plot,
                               double spanSeconds)
{
    const int w = plot.getWidth(), h = plot.getHeight();
    std::vector<bool> mask ((size_t) (w * h), false);

    const auto markRect = [&] (int x0, int y0, int rw, int rh)
    {
        for (int y = juce::jmax (0, y0); y < juce::jmin (h, y0 + rh); ++y)
            for (int x = juce::jmax (0, x0); x < juce::jmin (w, x0 + rw); ++x)
                mask[(size_t) (y * w + x)] = true;
    };
    // Una línea gruesa entre dos puntos EN COORDENADAS DE COMPONENTE, pasada a locales del plot.
    const auto markLine = [&] (juce::Point<float> a, juce::Point<float> b, int pad)
    {
        const int steps = (int) std::ceil (a.getDistanceFrom (b)) + 1;
        for (int s = 0; s <= steps; ++s)
        {
            const float t = steps > 0 ? (float) s / (float) steps : 0.0f;
            const auto  p = a + (b - a) * t;
            markRect ((int) std::lround (p.x) - plot.getX() - pad,
                      (int) std::lround (p.y) - plot.getY() - pad, 2 * pad + 1, 2 * pad + 1);
        }
    };

    // drawStage: los cuatro bordes del escenario.
    const auto fl = comp.project (0.0f, 0.0f, 0.0f), fr = comp.project (1.0f, 0.0f, 0.0f);
    const auto bl = comp.project (0.0f, 0.0f, 1.0f), br = comp.project (1.0f, 0.0f, 1.0f);
    markLine (bl, br, 2);
    markLine (fl, bl, 2);
    markLine (fr, br, 2);
    markLine (fl, fr, 2);

    // 57b — y la GRILLA DEL SUELO: las tres décadas proyectadas de adelante hacia el fondo. Se dibujan
    // encima de la imagen igual que los bordes del escenario, así que van en la máscara por lo mismo.
    for (const double hz : { 100.0, 1000.0, 10000.0 })
    {
        const auto x01 = (float) (std::log (hz / telescope::SpectrogramRing::kMinHz)
                                  / std::log (telescope::SpectrogramRing::kMaxHz
                                              / telescope::SpectrogramRing::kMinHz));
        markLine (comp.project (x01, 0.0f, 0.0f), comp.project (x01, 0.0f, 1.0f), 2);
    }

    // Los rótulos de tiempo en la profundidad (mismo paso que paintLive) con su fondo y su tick.
    if (spanSeconds > 0.05)
    {
        const double stepSec = spanSeconds <= 12.0 ? 2.0 : (spanSeconds <= 34.0 ? 5.0 : 10.0);
        for (double t = 0.0; t <= spanSeconds + 1.0e-6; t += stepSec)
        {
            const auto p = comp.project (0.0f, 0.0f, (float) (t / spanSeconds));
            // El rótulo: tick en (tx, ty, 5, 1), fondo en (tx+5, ty−7, tw, 13) y el texto adentro, con
            // tw = ancho del string + 8 (≈ 35 px para "ahora" en mono de 9 pt). Se enmascara hasta
            // tx+55 y ±9 en vertical: alcanza para el texto más largo del paso y no se come el plot.
            markRect ((int) std::lround (p.x) - plot.getX() - 2,
                      (int) std::lround (p.y) - plot.getY() - 9, 57, 19);
        }
    }

    // "CANAL L+R", arriba a la derecha del plot.
    markRect (w - 92, 0, 92, 16);
    return mask;
}
}

TEST_CASE ("telescope: la oclusion por horizonte de WATERFALL da el MISMO dibujo que el pintor",
           "[telescope][waterfall][horizon]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);

    auto st = proc.spectrumSettings();
    st.channel         = telescope::Spectrum::leftRight;
    st.historySecIndex = 0;    // 10 s de anillo: sobra para las líneas y deja el `span` en pocos segundos
    proc.setSpectrumSettings (st);
    proc.setWaterfallLinesIndex (2);   // 120 líneas: el peor caso de oclusión
    proc.setWaterfallTiltIndex (2);    // la inclinación más profunda: las láminas se tapan de verdad

    pushSteppedTone (proc, 4.0);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= 3.8; }, 10000));
    // EL ANILLO TIENE QUE ESTAR QUIETO antes de las dos lecturas: el pintor bruto y la lente lo leen en
    // momentos distintos, y una columna que entra entre medio hace que comparen dos escenas distintas
    // (es lo que ponía rojo este test con la suite entera al lado, no la oclusión).
    REQUIRE (telescope::test::waitStable ([&] { return (double) proc.spectrogram().writeIndex(); },
                                          250, 30000));
    REQUIRE (proc.spectrogram().count() > 120);

    telescope::WaterfallLens lens (proc);
    lens.setSize (kCompW, kCompH);

    // La geometría reconstruida TIENE que ser la de la lente (si no, el resto del test no diría nada).
    const auto plot = plotRectFor (kCompW, kCompH);
    REQUIRE (plot.getWidth()  == kPlotW);
    REQUIRE (plot.getHeight() == kPlotH);
    REQUIRE (lens.lineCount() >= 0);

    telescope::Projection2p5 comp;
    comp.x0 = (float) plot.getX();  comp.y0 = (float) plot.getY();
    comp.w  = (float) plot.getWidth(); comp.h = (float) plot.getHeight();
    comp.tilt  = telescope::WaterfallLens::kTiltOptions [proc.waterfallTiltIndex()];
    comp.depth = telescope::WaterfallLens::kDepthOptions[proc.waterfallTiltIndex()];

    auto local = comp;
    local.x0 = 0.0f; local.y0 = 0.0f;

    const auto brute = brutePaint (proc, local, kPlotW, kPlotH);
    REQUIRE (brute.painted > 1500);   // el pintor bruto dibujó algo de verdad, no una imagen vacía

    // La lente REAL, pintada tal cual (el waterfall va primero y los rótulos encima).
    juce::Image shot (juce::Image::ARGB, kCompW, kCompH, true);
    {
        juce::Graphics g (shot);
        lens.pumpFrames (2);
        lens.paintEntireComponent (g, false);
    }
    REQUIRE (lens.lineCount() == 120);

    const auto mask = overlayMask (comp, plot, lens.visibleSeconds());

    const juce::Image::BitmapData bruteBd (brute.img, juce::Image::BitmapData::readOnly);
    const juce::Image::BitmapData shotBd  (shot,      juce::Image::BitmapData::readOnly);
    const auto bgArgb = th::bg1.withAlpha (1.0f).getARGB();

    int compared = 0, curves = 0, diff = 0, firstX = -1, firstY = -1;
    for (int y = 0; y < kPlotH; ++y)
        for (int x = 0; x < kPlotW; ++x)
        {
            if (mask[(size_t) (y * kPlotW + x)]) continue;
            ++compared;

            const auto want = *((const juce::uint32*) bruteBd.getLinePointer (y) + x);
            const auto got  = *((const juce::uint32*) shotBd.getLinePointer (y + plot.getY()) + x + plot.getX());
            if (want != bgArgb) ++curves;
            if (want != got && ++diff == 1) { firstX = x; firstY = y; }
        }

    std::printf ("WATERFALL[horizonte] %d px comparados (%d de curva) de %d  ·  %d lineas  ·  %.2f s"
                 "  ·  diferencias = %d%s\n",
                 compared, curves, kPlotW * kPlotH, lens.lineCount(), lens.visibleSeconds(), diff,
                 diff > 0 ? (juce::String ("  primera en (") + juce::String (firstX) + ", "
                             + juce::String (firstY) + ")").toRawUTF8() : "");

    // El test no puede pasar por vacío: se compara la mayor parte del plot y hay curvas adentro.
    REQUIRE (compared > 8000);
    REQUIRE (curves   > 1200);
    REQUIRE (diff == 0);
}
