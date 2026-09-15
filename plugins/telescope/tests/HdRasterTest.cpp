// [telescope][visual][hd] — LA NITIDEZ A ESCALA FÍSICA, medida.
//
// ================================ QUÉ SE MIDE, Y POR QUÉ ASÍ =============================================
//
// Las lentes que rasterizan a mano (SPECTROGRAM, STEREO SPECTROGRAM, WATERFALL, FIELD, la estela de SCOPE
// y —desde el punto 3 de este mismo prompt— SPECTRUM) asignaban su `juce::Image` en píxeles LÓGICOS y la
// dibujaban 1:1. En una Retina el sistema la estira al doble, así que la mitad de la resolución de la
// pantalla se tiraba: eso es el "pixelado" que Joaquín señaló el 9-sep mirando TELESCOPE contra Insight.
// Desde el 57b la caché se hornea a `look::physicalScale (g)` (ver lenses/Raster.h).
//
// El test compara las DOS cosas sobre la misma señal y el mismo tamaño de lente:
//
//   ANTES  la lente pintada a escala 1 y AMPLIADA ×2 con filtro bilineal — que es lo que el sistema le
//          hace a un buffer de 1× cuando lo muestra en un panel de 2×;
//   AHORA  la lente pintada a escala 2 de verdad.
//
// La medida es la ENERGÍA del gradiente sobre la luma: (Δx² + Δy²) promediado sobre todos los píxeles.
//
// ============ POR QUÉ BILINEAL Y AL CUADRADO, Y NO VECINO-MÁS-CERCANO CON |Δ| (57b, medido) ==============
//
// El prompt 57b pedía "escalado ×2 vecino-más-cercano" con "(|Δx|+|Δy|)" y criterio ≥ 1.5, justificándolo
// así: «un borde escalado reparte su salto en 2 px: la razón teórica es 2». Las dos mitades de esa frase
// no pueden ser ciertas a la vez, y la implementación lo confirmó:
//
//   · el vecino-más-cercano NO reparte el salto — lo DUPLICA. Un escalón de A sigue siendo un escalón de
//     A, sólo que ahora hay dos filas iguales antes y dos después. La variación total (Σ|Δ|) de una imagen
//     es invariante ante ese estirado, así que la razón teórica con esa combinación es 1, no 2;
//   · el que "reparte el salto en 2 px" es el filtro SUAVE (bilineal), que es además el que usa de verdad
//     el sistema — y repartir un escalón de A en dos de A/2 deja Σ|Δ| igual (A) pero baja ΣΔ² a la mitad
//     (2·(A/2)² = A²/2 contra A²). O sea: la razón 2 del prompt sale con bilineal Y al cuadrado.
//
// Medido sobre las cinco lentes del punto 1, con la caché YA a escala física (2026-09-12, M4):
//
//     lente                 NN/|Δ|   NN/Δ²   BILIN/|Δ|   BILIN/Δ²
//     SPECTROGRAM            1.13    1.09      1.21        3.22
//     STEREO SPECTROGRAM     1.07    1.04      1.17        3.08
//     WATERFALL              1.29    1.29      1.53        4.69
//     FIELD                  1.06    0.97      1.18        2.86
//     SCOPE                  0.88    0.76      1.13        3.19
//
// Con la combinación del prompt el criterio ≥ 1.5 es inalcanzable POR LA MÉTRICA, no por el código: la
// columna NN/|Δ| no llega a 1.5 ni con la caché arreglada, porque mide variación total y la variación
// total no cambia al duplicar píxeles. Con la combinación que corresponde a su propia justificación
// (bilineal, al cuadrado) el piso teórico es 2 y lo medido va de 2.86 a 4.69: el criterio de 1.5 queda
// donde el prompt lo quiso, con margen para las zonas planas y por debajo de todo lo medido.
//
// Se imprime TAMBIÉN el número con la métrica literal del prompt (NN/|Δ|), para que la auditora vea de
// dónde sale la diferencia sin tener que recompilar nada.
//
// NO es un test de "se ve lindo": si alguien vuelve a asignar una caché en píxeles lógicos, la razón cae a
// ~1 y esto se pone rojo.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "lenses/FieldLens.h"
#include "lenses/Lens.h"
#include "lenses/ScopeLens.h"
#include "lenses/SpectrogramLens.h"
#include "lenses/SpectrumLens.h"
#include "lenses/StereoSpectrogramLens.h"
#include "lenses/WaterfallLens.h"

namespace
{
constexpr int kLensW = 1025, kLensH = 702;   // el mismo tamaño L del banco de presupuesto
constexpr int kFrames = 8;                   // frames de calentamiento antes de la foto
// ========================================================================================================
// ===== 57c · EL CRITERIO ESTABA POR DEBAJO DEL PISO DE SU PROPIA MÉTRICA =====
//
// La mutación de la auditora: con `sNew = 1.0f` en `raster::Cache::prepare` —la caché de vuelta a píxeles
// lógicos, EL defecto que este test existe para vigilar— `VISUAL[hd]` quedaba VERDE. Dos motivos, y los
// dos se arreglan acá:
//
//   1 · se medía el PANEL ENTERO y no la caché (ver lumaPlane, arriba);
//   2 · el criterio era 1.50 y el PISO de esta métrica es 2.0. Está dicho en el encabezado de arriba con
//       todas las letras —«repartir un escalón de A en dos de A/2 baja ΣΔ² a la mitad», o sea que el
//       cociente entre "sin repartir" y "repartido" vale 2— pero el número se copió del prompt 57b sin
//       bajarlo de ahí. Un criterio por debajo del piso de su métrica no puede fallar nunca.
//
// Medido sobre las seis lentes, recortando a la caché, con y sin la mutación (M4 de la casa, 2026-09-12):
//
//     lente                  con sNew = 1.0f      sano
//     SPECTROGRAM                  1.65           3.24
//     STEREO SPECTROGRAM           1.68           3.12
//     WATERFALL                    2.21           4.34
//     FIELD                        2.20           3.78
//     SCOPE                        2.55           2.91
//     SPECTRUM                     2.06           4.11
//
// 2.75 está por encima de todo lo mutado y por debajo de todo lo sano. Y no viaja solo: el test verifica
// ADEMÁS, estructuralmente, que la caché de cada lente se haya horneado a la escala física del pintado
// (`cacheScaleForTest`). Con la mutación eso da 1.0 donde tiene que dar 2.0, en las seis, sin depender de
// ninguna estadística.
constexpr double kMinRatio = 2.75;

// El ruido rosa ESTÉREO INDEPENDIENTE: la señal que más celdas enciende en las cinco lentes a la vez.
void pushPink (telescope::TelescopeProcessor& proc, double seconds, float peak)
{
    constexpr double sr = 48000.0;
    constexpr int    blockSize = 512;
    telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    for (int blk = 0; blk < (int) std::ceil (seconds * sr / blockSize); ++blk)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            buf.setSample (0, i, peak * a.next());
            buf.setSample (1, i, peak * b.next());
        }
        proc.processBlock (buf, midi);
    }
}

double luma (juce::Colour c) noexcept
{
    return 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue();
}

// ========================================================================================================
// ===== 57c · SE MIDE SÓLO ADENTRO DEL RECTÁNGULO DE LA CACHÉ =====
//
// La mutación de la auditora sobre el 57b: poniendo `sNew = 1.0f` en `raster::Cache::prepare` —o sea la
// caché de vuelta a píxeles lógicos, EL defecto que este test existe para vigilar— `VISUAL[hd]` quedó
// VERDE, con SPECTRUM en 3.17. El motivo: el gradiente se medía sobre el PANEL ENTERO, y ahí viven la
// rejilla, los rótulos, los ejes y los trazos vectoriales, que se dibujan a escala física SIEMPRE. Esos
// bordes duros son la mayor parte de la energía del gradiente y tapan a la caché, que es lo único que la
// regla de Raster.h gobierna.
//
// Ahora cada lente expone el rectángulo LÓGICO que cubre su caché (`cacheAreaForTest`, como ya exponía
// `hemiPlotAreaForTest`) y el plano de luma se recorta ahí adentro, en el espacio de píxeles de cada
// render. Lo que queda medido es la caché y nada más.
// ========================================================================================================
struct Plane
{
    std::vector<double> v;
    int w = 0, h = 0;
};

// La luma del RECTÁNGULO pedido, ya desempaquetada: el gradiente la lee dos veces por píxel y hacerlo
// desde `getPixelAt` costaba más que todo el resto del test junto.
Plane lumaPlane (const juce::Image& img, juce::Rectangle<int> logical, float scale)
{
    const auto toDev = [scale] (int v) { return (int) std::lround ((double) v * (double) scale); };
    auto r = juce::Rectangle<int> (toDev (logical.getX()), toDev (logical.getY()),
                                   toDev (logical.getWidth()), toDev (logical.getHeight()))
                 .getIntersection (img.getBounds());
    Plane out;
    out.w = r.getWidth();
    out.h = r.getHeight();
    out.v.resize ((size_t) (out.w * out.h));
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            out.v[(size_t) (y * out.w + x)] = luma (bd.getPixelColour (r.getX() + x, r.getY() + y));
    return out;
}

// Energía del gradiente sobre una luma de w×h. `squared` elige la métrica (ver el encabezado): Δ² es la
// que distingue un borde nítido de uno repartido; |Δ| no, porque la variación total es la misma.
double gradientEnergy (const std::vector<double>& p, int w, int h, bool squared)
{
    double sum = 0.0;
    for (int y = 0; y < h - 1; ++y)
        for (int x = 0; x < w - 1; ++x)
        {
            const double dx = p[(size_t) (y * w + x + 1)] - p[(size_t) (y * w + x)];
            const double dy = p[(size_t) ((y + 1) * w + x)] - p[(size_t) (y * w + x)];
            sum += squared ? (dx * dx + dy * dy) : (std::abs (dx) + std::abs (dy));
        }
    return sum / (double) ((w - 1) * (h - 1));
}

// El estirado ×2 que le hace el SISTEMA a un buffer de 1× en un panel de 2×: bilineal, muestreando en el
// centro del píxel de destino (de ahí el −0.5), con los bordes pegados.
std::vector<double> upscaleBilinear (const std::vector<double>& p, int w, int h)
{
    const int W = w * 2, H = h * 2;
    std::vector<double> out ((size_t) (W * H));
    const auto clamp = [] (int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); };
    for (int Y = 0; Y < H; ++Y)
        for (int X = 0; X < W; ++X)
        {
            const double sx = ((double) X + 0.5) * 0.5 - 0.5, sy = ((double) Y + 0.5) * 0.5 - 0.5;
            const int    x0 = (int) std::floor (sx), y0 = (int) std::floor (sy);
            const double fx = sx - (double) x0, fy = sy - (double) y0;
            const auto   S  = [&] (int xx, int yy) { return p[(size_t) (clamp (yy, h) * w + clamp (xx, w))]; };
            out[(size_t) (Y * W + X)] = S (x0, y0)     * (1.0 - fx) * (1.0 - fy)
                                      + S (x0 + 1, y0) * fx         * (1.0 - fy)
                                      + S (x0, y0 + 1) * (1.0 - fx) * fy
                                      + S (x0 + 1, y0 + 1) * fx     * fy;
        }
    return out;
}

// El estirado por VECINO MÁS CERCANO, sólo para imprimir la métrica literal del prompt al lado.
std::vector<double> upscaleNearest (const std::vector<double>& p, int w, int h)
{
    const int W = w * 2, H = h * 2;
    std::vector<double> out ((size_t) (W * H));
    for (int Y = 0; Y < H; ++Y)
        for (int X = 0; X < W; ++X) out[(size_t) (Y * W + X)] = p[(size_t) ((Y / 2) * w + (X / 2))];
    return out;
}

// Pinta la lente a la escala dada, con la MISMA transformación que pone el host en Retina.
juce::Image renderAt (juce::Component& comp, telescope::Lens& pump, float s)
{
    juce::Image img (juce::Image::ARGB, (int) std::lround ((double) kLensW * (double) s),
                     (int) std::lround ((double) kLensH * (double) s), true);
    for (int i = 0; i < kFrames; ++i)
    {
        pump.pumpFrames (1);
        juce::Graphics g (img);
        if (std::abs (s - 1.0f) > 1.0e-4f) g.addTransform (juce::AffineTransform::scale (s));
        comp.paintEntireComponent (g, false);
    }
    return img;
}

// El ciclo entero para una lente. La lente se construye DOS VECES, una por escala: varias acumulan estado
// entre frames (la estela de fósforo, el sonograma ya desplazado) y compartir la instancia haría que la
// segunda foto arrancara de donde terminó la primera.
template <typename Make>
void requireSharperAtRetina (const char* name, telescope::TelescopeProcessor& proc, Make make)
{
    std::vector<double> stretched, nearest;
    int sw = 0, sh = 0, cw = 0, ch = 0;
    float scale1 = 0.0f, scale2 = 0.0f;
    juce::Rectangle<int> area;
    {
        auto lens = make (proc);
        lens->setSize (kLensW, kLensH);
        const auto img = renderAt (*lens, *lens, 1.0f);
        area = lens->cacheAreaForTest();
        REQUIRE (area.getWidth() > 16);
        REQUIRE (area.getHeight() > 16);
        scale1 = lens->cacheScaleForTest();
        const auto p = lumaPlane (img, area, 1.0f);
        sw = p.w * 2;
        sh = p.h * 2;
        stretched = upscaleBilinear (p.v, p.w, p.h);
        nearest   = upscaleNearest  (p.v, p.w, p.h);
    }

    double e1 = 0.0, e2 = 0.0, litAntes = 0.0, litDespues = 0.0;
    {
        auto lens = make (proc);
        lens->setSize (kLensW, kLensH);
        const auto img = renderAt (*lens, *lens, 2.0f);
        const auto p   = lumaPlane (img, lens->cacheAreaForTest(), 2.0f);
        cw = p.w;
        ch = p.h;
        scale2 = lens->cacheScaleForTest();
        e1 = gradientEnergy (stretched, sw, sh, true);
        e2 = gradientEnergy (p.v, p.w, p.h, true);
        litAntes   = gradientEnergy (nearest, sw, sh, false);
        litDespues = gradientEnergy (p.v, p.w, p.h, false);
    }

    const double ratio = e1 > 0.0 ? e2 / e1 : 0.0;
    const double lit   = litAntes > 0.0 ? litDespues / litAntes : 0.0;
    std::printf ("VISUAL[hd] %-20s  cache %dx%d logicos -> %dx%d a 2x  ·  1x estirado = %.2f   "
                 "2x real = %.2f   razon = %.2f  (criterio %.2f)   [metrica literal del 57b, "
                 "vecino+|d|: %.2f]\n",
                 name, area.getWidth(), area.getHeight(), cw, ch, e1, e2, ratio, kMinRatio, lit);
    std::printf ("VISUAL[hd] %-20s  cache horneada a escala %.2f en el pintado de 1x y %.2f en el de 2x\n",
                 name, scale1, scale2);
    CHECK (ratio >= kMinRatio);
    // LA COMPROBACIÓN ESTRUCTURAL: la caché se hornea a la escala del pintado, no a 1. Es la regla de
    // lenses/Raster.h dicha como aserción en vez de como estadística.
    CHECK (std::abs (scale1 - 1.0f) < 1.0e-3f);
    CHECK (std::abs (scale2 - 2.0f) < 1.0e-3f);
}
}

TEST_CASE ("telescope: las lentes de pixeles son mas nitidas a escala fisica", "[telescope][visual][hd]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kStereoBands | telescope::kField
                            | telescope::kStereo | telescope::kLoudness);
    proc.setBandsWindowIndex (0);
    proc.setFieldDecayIndex (2);

    auto st = proc.spectrumSettings();
    st.historySecIndex = 0;             // 10 s: el anillo se llena rápido y las cinco lentes tienen dato
    proc.setSpectrumSettings (st);

    for (int i = 1; i <= 4; ++i)
    {
        pushPink (proc, 3.0, std::pow (10.0f, -16.0f / 20.0f));
        const double want = 3.0 * (double) i - 0.15;
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= want; }, 8000));
    }
    REQUIRE (proc.spectrogram().count() >= proc.spectrogram().capacity());

    // El motor tiene que estar QUIETO entre las dos fotos: si sigue entrando audio, la de escala 2 mira
    // otro tramo de historia y la comparación deja de ser de nitidez.
    REQUIRE (telescope::test::waitStable ([&] { return proc.analysis().read().timeSeconds; }, 250, 20000));

    requireSharperAtRetina ("SPECTROGRAM", proc,
                            [] (auto& p) { return std::make_unique<telescope::SpectrogramLens> (p); });
    requireSharperAtRetina ("STEREO SPECTROGRAM", proc,
                            [] (auto& p) { return std::make_unique<telescope::StereoSpectrogramLens> (p); });
    requireSharperAtRetina ("WATERFALL", proc,
                            [] (auto& p) { return std::make_unique<telescope::WaterfallLens> (p); });
    requireSharperAtRetina ("FIELD", proc,
                            [] (auto& p) { return std::make_unique<telescope::FieldLens> (p); });
    requireSharperAtRetina ("SCOPE", proc,
                            [] (auto& p) { return std::make_unique<telescope::ScopeLens> (p); });
    // 57b/punto 3 — SPECTRUM entra a esta lista porque desde este prompt TAMBIÉN rasteriza a mano sobre
    // una caché propia (antes dibujaba con fillRect de 1 px por columna). El prompt nombraba cinco lentes
    // porque en su momento eran cinco las que cacheaban; son seis.
    requireSharperAtRetina ("SPECTRUM", proc,
                            [] (auto& p) { return std::make_unique<telescope::SpectrumLens> (p); });

    proc.releaseResources();
}
