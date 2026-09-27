// [telescope][editorbudget] — lo que cuesta la VENTANA, no la lente sola (prompt 96).
//
// El bug de campo (un usuario, 24-sep, M2 Pro + Ableton): con TELESCOPE abierto se traba la interfaz
// ENTERA del DAW, y al cerrar la ventana se arregla. [budget] no lo podía ver porque pinta la lente
// sobre una imagen propia: nunca pasa por el editor. En un host, en cambio, el repaint de la lente (30
// por segundo) llega al editor como una región sucia, y JUCE pinta TODO lo que está debajo de esa región
// — como la lente no es opaca, eso incluye el paintCanvas del editor, que rehacía el fondo entero (una
// imagen del tamaño de la ventana en resolución física, con gradientes y grano) en cada cuadro.
//
// Tres mediciones, las tres sobre el editor REAL que arma el host (proc.createEditor()):
//
//   1 · HORNEADOS DEL FONDO. Con el tamaño y la escala quietos, 30 cuadros de la lente hornean el Panel a
//       lo sumo UNA vez (la primera). Cambiar de tamaño o de escala lo rehornea exactamente una vez.
//   2 · COSTO POR CUADRO CON EL CLIP DE LA LENTE. Se pinta el editor entero sobre una imagen a escala
//       física con `reduceClipRegion (rectángulo de la lente)` —lo mismo que hace el peer con un repaint
//       de la lente— y se compara, muestra a muestra, contra la lente sola pintada en el mismo lugar.
//       Para las 13 lentes, en S/M/L y a escalas 1, 1.25, 1.5 (el Windows de un usuario) y 2 (Retina).
//   3 · REPOSO. Con silencio (ceros) entrando y el transporte parado, cada lente deja de pedir repaint:
//       en los 60 ticks del timer que siguen a 2 s de silencio más su propio settleHold, cero repaints.
//
// CRITERIO DEL COSTO (2): sobrecosto = mediana de (editor con clip − lente sola) ≤ 1 ms + 2 unidades de
// composición, con UNIDAD = lo que tarda, en esta máquina y en este momento, rellenar el área del clip y
// componer encima una imagen opaca del tamaño de la ventana (un fill + un blit del clip).
// Fundamento: la lente no es opaca, así que DEBAJO de ella el editor tiene que componer algo sí o sí — el
// relleno del editor, el del canvas y el blit del fondo ya horneado: ~2 unidades. Eso es el piso de un
// editor correcto, y crece con el área física del clip (en L a escala 2 son 2.9 Mpx) y con el renderer
// (CoreGraphics en la Mac, Direct2D/WARP o software en Windows): por eso el criterio no es un número de
// milisegundos pelado. La primera versión decía "≤ 1 ms" fijo y lo pasaban 150 de 156 casos: los 6 que
// no, todos en L a 1.5 y 2, daban 1.00–1.31 ms, que es justamente ese piso de composición. El 1 ms
// extra es el margen para el header, el bisel y el ruido de una máquina compartida. Rehornear el fondo
// —el bug— cuesta decenas de unidades (medido: 12–80 ms contra unidades de décimas), así que el criterio
// lo caza con un orden de magnitud de aire. Y el contador de horneados (1) lo caza sin cronómetro.

// Las muestras van EMPAREJADAS (lente, editor, lente, editor…) y la diferencia se toma por muestra: así
// las dos ven la misma carga de la máquina, que es lo que hace medible un sobrecosto de décimas en una Mac
// con otras sesiones compilando al lado. Cada métrica se queda con su mínimo de tres tandas, igual que
// bestOfThree de [budget] (ver la nota allá: filtra desalojos del scheduler, no el costo del código).
//
// Portable a propósito (corre también en el runner de Windows): nada de M_PI ni rutas /tmp.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <typeinfo>
#include <vector>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "lenses/LensIds.h"

namespace
{
using Zoom = ovni::PluginEditorBase::Zoom;

constexpr double kSr = 48000.0;

struct ZoomCase { Zoom zoom; const char* name; };
constexpr ZoomCase kZooms[] = { { Zoom::small, "S" }, { Zoom::medium, "M" }, { Zoom::large, "L" } };
constexpr float    kScales[] = { 1.0f, 1.25f, 1.5f, 2.0f };

constexpr double kOverheadSlackMs = 1.0, kOverheadUnits = 2.0;   // ver CRITERIO DEL COSTO en el encabezado

// Un processor preparado, con la lente `id` elegida ANTES de abrir el editor (el ctor del editor lee el
// valor crudo del parámetro, así que abre directo en esa lente y le pone al motor su máscara de módulos).
struct Rig
{
    telescope::TelescopeProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed;
    telescope::TelescopeEditor* tel = nullptr;
    telescope::Lens* lens = nullptr;
    long long samplesFed = 0;

    explicit Rig (telescope::LensId id)
    {
        proc.prepareToPlay (kSr, 512);
        auto* lensParam = proc.apvts.getParameter ("lens");
        REQUIRE (lensParam != nullptr);
        lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) id));
        ed.reset (proc.createEditor());
        REQUIRE (ed != nullptr);
        tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
        REQUIRE (tel != nullptr);
        lens = tel->activeLens();
        REQUIRE (lens != nullptr);
        REQUIRE (lens->id() == id);
    }
    ~Rig()
    {
        ed.reset();
        proc.releaseResources();
    }

    // F2b de la 0.2 · LA MEDICIÓN ARRANCA CON EL MOTOR QUIETO. Antes empujaba 3 s en bloques de 512 (30,08 hops) y
    // esperaba «t >= 2,85 s»: la primera medición de cada lente —S a escala 1, la primera del bucle— arrancaba con
    // el hilo de análisis todavía comiendo la cola, y los dos se disputaban la CPU. Dentro de la suite entera eso
    // sacaba de criterio a las 13 lentes en S@1,0 (la 102: 14 de 156; la F2b: 13 de 156, 1,4–3,7 ms contra
    // ~1,2), y solo casi nunca. Ahora: hops enteros y waitDigested (TestHelpers.h), la regla de [uisnap].
    void feedSignal()
    {
        const float peak = std::pow (10.0f, -20.0f / 20.0f);
        telescope::test::Pink a { telescope::test::kPinkSeedA }, b { telescope::test::kPinkSeedB };
        const auto pushed = telescope::test::pushExact (proc, 3 * (long long) kSr, kSr,
                                                        [&] (juce::AudioBuffer<float>& buf, int k)
        {
            for (int i = 0; i < k; ++i) { buf.setSample (0, i, peak * a.next()); buf.setSample (1, i, peak * b.next()); }
        });
        samplesFed += pushed;
        telescope::test::waitDigested (proc, pushed, kSr);
        tel->pumpLensFrames (10);
    }

    // Un tick del timer de SILENCIO: los ceros que el host sigue mandando con el transporte parado, 1/fps
    // segundos por tick (48000 / 30 = 1600 muestras; VERDICT corre a 12 fps), en bloques de hasta 400.
    // Espera a que el motor digiera todo hop completo antes de volver, así cada tick ve el estado que
    // vería en tiempo real.
    void feedSilenceTick (int fps)
    {
        juce::AudioBuffer<float> buf (2, 400);
        juce::MidiBuffer midi;
        const int total = juce::roundToInt (kSr / (double) juce::jmax (1, fps));
        for (int done = 0; done < total;)
        {
            const int n = juce::jmin (400, total - done);
            juce::AudioBuffer<float> blk (buf.getArrayOfWritePointers(), 2, n);
            blk.clear();
            proc.processBlock (blk, midi);
            done += n;
        }
        samplesFed += total;
        const double fed = (double) samplesFed / kSr;
        REQUIRE (telescope::test::waitUntil ([&] { return proc.analysis().read().timeSeconds >= fed - 0.101; }, 5000));
    }

    // El rectángulo de la lente en coordenadas del EDITOR (atraviesa el transform del zoom del Canvas).
    juce::Rectangle<float> lensRect() const
    {
        return ed->getLocalArea (lens, lens->getLocalBounds().toFloat());
    }
};

// Pinta el editor entero como lo pinta el peer cuando lo único sucio es la lente.
struct EditorPainter
{
    juce::AudioProcessorEditor& ed;
    juce::Rectangle<float>      clip;
    float                       scale;
    juce::Image                 img;

    EditorPainter (juce::AudioProcessorEditor& e, juce::Rectangle<float> c, float s)
        : ed (e), clip (c), scale (s),
          img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) e.getWidth() * s)),
               juce::jmax (1, juce::roundToInt ((float) e.getHeight() * s)), true)
    {}

    void paint()
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));   // la escala física del host (DPI)
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
        ed.paintEntireComponent (g, false);
    }
};

// La lente SOLA: nada debajo, pero pintada exactamente donde la pinta el editor — misma imagen, mismo
// clip, misma transformación (escala del host × zoom del Canvas, y el origen de la lente dentro del
// Canvas). Así la resta editor − lente es lo que agrega el editor y nada más.
//
// Por qué no en el origen (0,0), que era la primera versión: a una escala efectiva no entera (S a 1.5 =
// 1.2, L a 1 = 1.25…) el origen de la lente cae en un píxel físico FRACCIONARIO y su capa estática se
// re-muestrea en cada cuadro; en el origen no. Esa diferencia la paga la lente esté donde esté el
// editor, y medida en el origen se le cargaba al editor (medido: 3–9 ms). Se imprime aparte
// (`lenteEnOrigen`) porque es un costo real en el host, aunque no sea del fondo.
struct LensPainter
{
    juce::Component&       lens;
    juce::Rectangle<float> clip;
    float                  scale, zoom;
    juce::Image            img;

    LensPainter (juce::AudioProcessorEditor& e, juce::Component& l, juce::Rectangle<float> c, float s, float z)
        : lens (l), clip (c), scale (s), zoom (z),
          img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) e.getWidth() * s)),
               juce::jmax (1, juce::roundToInt ((float) e.getHeight() * s)), true)
    {}

    void paint()
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
        g.addTransform (juce::AffineTransform::translation ((float) lens.getX(), (float) lens.getY()).scaled (zoom));
        lens.paintEntireComponent (g, false);
    }
};

// La lente en el origen de una imagen propia, como la pinta [budget]: referencia (ver arriba).
//
// T2 (F2 de la 0.2): la lente del editor se alinea al píxel físico contando desde su ANCLA, el editor
// (Lens::pixelAlignment). Acá no se la pinta dentro del editor sino en el origen de otra imagen, donde ya
// cae entera: sin sacarle el ancla, se correría el resto de una posición que en esta imagen no tiene.
struct LensAtOriginPainter
{
    telescope::Lens&  lens;
    juce::Component*  anchor;
    float             scale;
    juce::Image       img;

    LensAtOriginPainter (telescope::Lens& l, juce::Component* a, float s)
        : lens (l), anchor (a), scale (s),
          img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) l.getWidth() * s)),
               juce::jmax (1, juce::roundToInt ((float) l.getHeight() * s)), true)
    {}

    void paint()
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        lens.setPixelAnchor (nullptr);
        lens.paintEntireComponent (g, false);
        lens.setPixelAnchor (anchor);
    }
};

// La UNIDAD DE COMPOSICIÓN (ver CRITERIO DEL COSTO): rellenar el clip y componer encima una imagen opaca
// del tamaño de la ventana, a la escala física — lo mínimo que un editor correcto hace debajo de una
// lente transparente, medido con el mismo renderer y en el mismo momento que el editor.
struct CompositeUnitPainter
{
    juce::Rectangle<float> clip;
    float                  scale;
    juce::Image            img, src;

    CompositeUnitPainter (juce::AudioProcessorEditor& e, juce::Rectangle<float> c, float s)
        : clip (c), scale (s),
          img (juce::Image::ARGB, juce::jmax (1, juce::roundToInt ((float) e.getWidth() * s)),
               juce::jmax (1, juce::roundToInt ((float) e.getHeight() * s)), true),
          src (juce::Image::ARGB, img.getWidth(), img.getHeight(), true)
    {
        juce::Graphics g (src);
        g.fillAll (juce::Colour (0xff05070b));
    }

    void paint()
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
        g.fillAll (juce::Colour (0xff030406));
        g.drawImageTransformed (src, juce::AffineTransform::scale (1.0f / scale));
    }
};

double timeMs (const std::function<void()>& f)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    f();
    return juce::Time::getMillisecondCounterHiRes() - t0;
}

double quantile (std::vector<double> v, double q)
{
    std::sort (v.begin(), v.end());
    return v[(size_t) ((double) (v.size() - 1) * q)];
}

struct Paired
{
    double lensMedian = 1.0e9, editorMedian = 1.0e9, overheadMedian = 1.0e9, editorP95 = 1.0e9,
           originMedian = 1.0e9, unitMedian = 1.0e9;
};

constexpr int kWarm = 4, kPerRun = 12, kRuns = 3;

Paired measurePaired (Rig& rig, EditorPainter& ep, LensPainter& lp, LensAtOriginPainter& op,
                      CompositeUnitPainter& up)
{
    for (int i = 0; i < kWarm; ++i) { rig.tel->pumpLensFrames (1); lp.paint(); ep.paint(); op.paint(); up.paint(); }

    Paired best;
    for (int rep = 0; rep < kRuns; ++rep)
    {
        std::vector<double> l, e, d, o, u;
        for (int i = 0; i < kPerRun; ++i)
        {
            rig.tel->pumpLensFrames (1);
            const double tl = timeMs ([&] { lp.paint(); });
            const double te = timeMs ([&] { ep.paint(); });
            o.push_back (timeMs ([&] { op.paint(); }));
            u.push_back (timeMs ([&] { up.paint(); }));
            l.push_back (tl); e.push_back (te); d.push_back (te - tl);
        }
        best.originMedian   = std::min (best.originMedian,   quantile (o, 0.5));
        best.unitMedian     = std::min (best.unitMedian,     quantile (u, 0.5));
        best.lensMedian     = std::min (best.lensMedian,     quantile (l, 0.5));
        best.editorMedian   = std::min (best.editorMedian,   quantile (e, 0.5));
        best.overheadMedian = std::min (best.overheadMedian, quantile (d, 0.5));
        best.editorP95      = std::min (best.editorP95,      quantile (e, 0.95));
    }
    return best;
}
}

// ========================================================================================================
// 1 · HORNEADOS DEL FONDO
// ========================================================================================================
TEST_CASE ("telescope: repintar la lente no rehornea el fondo del editor", "[telescope][editorbudget]")
{
    Rig rig (telescope::LensId::scope);   // la que NUNCA paraba: 30 repaints por segundo sin descanso
    rig.feedSignal();

    for (const auto& z : kZooms)
    {
        rig.tel->applyZoom (z.zoom);
        for (const float s : kScales)
        {
            EditorPainter ep (*rig.ed, rig.lensRect(), s);
            const int before = rig.tel->backgroundRenderCount();
            ep.paint();                                     // el primer cuadro a este tamaño/escala: hornea
            const int afterFirst = rig.tel->backgroundRenderCount();
            for (int i = 0; i < 29; ++i) { rig.tel->pumpLensFrames (1); ep.paint(); }
            const int after30 = rig.tel->backgroundRenderCount();

            std::printf ("EDITORBUDGET_PANEL size=%s scale=%.2f  horneados en 30 cuadros=%d  (en el primero=%d)\n",
                         z.name, (double) s, after30 - before, afterFirst - before);
            // "A lo sumo 1": el primer cuadro puede hornear (cambió el tamaño o la escala); los otros 29 no.
            CHECK (after30 - before <= 1);
            CHECK (after30 - afterFirst == 0);
        }
    }

    // Un cambio de tamaño o de escala SÍ tiene que rehornear (si no, el fondo quedaría borroso o cortado):
    // la guarda no puede comerse la invalidación.
    rig.tel->applyZoom (Zoom::medium);
    EditorPainter epM (*rig.ed, rig.lensRect(), 2.0f);
    epM.paint();
    const int c0 = rig.tel->backgroundRenderCount();
    rig.tel->applyZoom (Zoom::large);
    EditorPainter epL (*rig.ed, rig.lensRect(), 2.0f);
    epL.paint();
    const int c1 = rig.tel->backgroundRenderCount();
    EditorPainter epL1 (*rig.ed, rig.lensRect(), 1.0f);
    epL1.paint();
    const int c2 = rig.tel->backgroundRenderCount();
    epL1.paint();
    const int c3 = rig.tel->backgroundRenderCount();
    std::printf ("EDITORBUDGET_PANEL invalidacion: M->L %d horneado(s), escala 2->1 %d, repetir %d\n",
                 c1 - c0, c2 - c1, c3 - c2);
    CHECK (c1 - c0 == 1);
    CHECK (c2 - c1 == 1);
    CHECK (c3 - c2 == 0);
}

// ========================================================================================================
// 2 · COSTO POR CUADRO CON EL CLIP DE LA LENTE
// ========================================================================================================
TEST_CASE ("telescope: el editor con el clip de la lente cuesta lo que la lente sola", "[telescope][editorbudget]")
{
    // Qué renderer usa la imagen de prueba: en macOS CoreGraphics; en Windows (JUCE 8) Direct2D si hay
    // dispositivo —WARP en un runner sin GPU— o software. Los números dependen de eso: se imprime.
    {
        juce::Image probe (juce::Image::ARGB, 8, 8, true);
        juce::Graphics g (probe);
        const auto& pixelData = *probe.getPixelData();
        const auto& context   = g.getInternalContext();
        std::printf ("EDITORBUDGET_RENDERER imagen=%s contexto=%s\n", typeid (pixelData).name(), typeid (context).name());
    }

    int outside = 0;
    for (int li = 0; li < telescope::kNumLenses; ++li)
    {
        const auto id = (telescope::LensId) li;
        Rig rig (id);
        rig.feedSignal();

        for (const auto& z : kZooms)
        {
            rig.tel->applyZoom (z.zoom);
            const auto  rect  = rig.lensRect();
            const float zoomF = rect.getWidth() / (float) rig.lens->getWidth();

            for (const float s : kScales)
            {
                EditorPainter       ep (*rig.ed, rect, s);
                LensPainter         lp (*rig.ed, *rig.lens, rect, s, zoomF);
                LensAtOriginPainter op (*rig.lens, rig.ed.get(), s * zoomF);
                CompositeUnitPainter up (*rig.ed, rect, s);

                const int rc0 = rig.tel->backgroundRenderCount();
                const auto m  = measurePaired (rig, ep, lp, op, up);
                const int bakes = rig.tel->backgroundRenderCount() - rc0;
                const double limitMs = kOverheadSlackMs + kOverheadUnits * m.unitMedian;
                const double clipMpx = (double) rect.getWidth() * rect.getHeight() * s * s / 1.0e6;

                const bool ok = m.overheadMedian <= limitMs && bakes <= 1;
                if (! ok) ++outside;
                std::printf ("EDITORBUDGET lens=%-18s size=%s scale=%.2f  lente=%7.3f ms  editor=%7.3f ms  "
                             "sobrecosto=%7.3f ms (limite %.3f)  p95editor=%7.3f ms  horneados=%d/%d  "
                             "lenteEnOrigen=%7.3f ms  unidad=%.3f ms  clip=%.2f Mpx%s\n",
                             telescope::lensName (id), z.name, (double) s, m.lensMedian, m.editorMedian,
                             m.overheadMedian, limitMs, m.editorP95, bakes, kWarm + kRuns * kPerRun,
                             m.originMedian, m.unitMedian, clipMpx, ok ? "" : "  <-- FUERA");
                CHECK (m.overheadMedian <= limitMs);
                CHECK (bakes <= 1);

                // ¿Podría la lente declararse opaca (setOpaque) para que JUCE no pinte el editor debajo? Sólo
                // si pinta TODO su rectángulo opaco; si deja un píxel transparente, debajo quedaría basura.
                // Se cuenta una vez por lente (M, escala 1): es el dato que decide, se imprime y no se exige.
                if (z.zoom == Zoom::medium && s == 1.0f)
                {
                    const juce::Image::BitmapData bd (op.img, juce::Image::BitmapData::readOnly);
                    int seeThrough = 0;
                    for (int y = 0; y < bd.height; ++y)
                        for (int x = 0; x < bd.width; ++x)
                            if (bd.getPixelColour (x, y).getAlpha() < 255) ++seeThrough;
                    std::printf ("EDITORBUDGET_OPACA lens=%-18s pixeles no opacos=%d de %d (%.1f %%)\n",
                                 telescope::lensName (id), seeThrough, bd.width * bd.height,
                                 100.0 * seeThrough / juce::jmax (1, bd.width * bd.height));
                }
            }
        }
    }
    std::printf ("EDITORBUDGET resumen: %d de %d casos fuera del criterio (sobrecosto <= %.1f ms + %.0f unidades, "
                 "<= 1 horneado)\n", outside, telescope::kNumLenses * 3 * 4, kOverheadSlackMs, kOverheadUnits);
}

// ========================================================================================================
// 2b · LA LENTE EN SU LUGAR, A ESCALA NO ENTERA (T2, F2 de la 0.2)
// ========================================================================================================
// El segundo cuello del veredicto 96: a escala no entera la lente cae en un píxel FÍSICO fraccionario
// (M a 1.25: x = 182 × 1.25 = 227.5) y sus imágenes —la capa estática de la base y las cachés raster de
// SPECTROGRAM, WATERFALL, FIELD, SCOPE y SPECTRUM— se re-muestreaban en CADA cuadro con filtro bilineal.
// Medido antes del arreglo, en esta Mac: 3–6 ms por cuadro en M a 1.25 contra 0.2–2.3 ms de la misma
// lente en el origen (hasta 16×). [budget] no lo ve porque pinta la lente en el origen, donde el píxel
// es entero; acá se pinta donde la pone el editor.
//
// CRITERIO: en su lugar ≤ 1.3 × en el origen + 0.3 ms, con las dos mediciones EMPAREJADAS (misma carga).
// Fundamento: lo único que cambia entre las dos es en qué píxel físico cae el origen de la lente. A escala
// entera (S a 1.25 = 1.0, M a 1) la razón medida es 0.99–1.05; el 30 % y los 0.3 ms son el ruido de una
// máquina compartida. El defecto da 2–16× (2.5–6 ms de más): un orden de magnitud por encima del margen.
// Se mide en S/M/L a 1.25 y 1.5 (el Windows de un usuario): S a 1.5 = 1.2 y L a 1.25 = 1.5625 también caen
// en píxel fraccionario, y M a 1.5 no (273 es entero) — por eso M a 1.5 no era rojo ni antes.
TEST_CASE ("telescope: a escala no entera la lente en su lugar cuesta lo que en el origen",
           "[telescope][editorbudget]")
{
    constexpr float  kFracScales[] = { 1.25f, 1.5f };
    constexpr double kRatio = 1.3, kSlackMs = 0.3;

    int outside = 0, total = 0;
    for (int li = 0; li < telescope::kNumLenses; ++li)
    {
        const auto id = (telescope::LensId) li;
        Rig rig (id);
        rig.feedSignal();

        for (const auto& z : kZooms)
        {
            rig.tel->applyZoom (z.zoom);
            const auto  rect  = rig.lensRect();
            const float zoomF = rect.getWidth() / (float) rig.lens->getWidth();

            for (const float s : kFracScales)
            {
                LensPainter         lp (*rig.ed, *rig.lens, rect, s, zoomF);
                LensAtOriginPainter op (*rig.lens, rig.ed.get(), s * zoomF);
                for (int i = 0; i < kWarm; ++i) { rig.tel->pumpLensFrames (1); lp.paint(); op.paint(); }

                double inPlace = 1.0e9, atOrigin = 1.0e9;
                for (int rep = 0; rep < kRuns; ++rep)
                {
                    std::vector<double> l, o;
                    for (int i = 0; i < kPerRun; ++i)
                    {
                        rig.tel->pumpLensFrames (1);
                        l.push_back (timeMs ([&] { lp.paint(); }));
                        o.push_back (timeMs ([&] { op.paint(); }));
                    }
                    inPlace  = std::min (inPlace,  quantile (l, 0.5));
                    atOrigin = std::min (atOrigin, quantile (o, 0.5));
                }

                const double limit = kRatio * atOrigin + kSlackMs;
                const float  physX = (float) rect.getX() * s, physY = (float) rect.getY() * s;
                const bool   ok    = inPlace <= limit;
                ++total;
                if (! ok) ++outside;
                std::printf ("EDITORBUDGET_PIXEL lens=%-18s size=%s scale=%.2f efectiva=%.4f  origen fisico=(%.3f, %.3f)  "
                             "en su lugar=%7.3f ms  en el origen=%7.3f ms  razon=%5.2f  (limite %.3f)%s\n",
                             telescope::lensName (id), z.name, (double) s, (double) (s * zoomF), (double) physX,
                             (double) physY, inPlace, atOrigin, inPlace / juce::jmax (1.0e-6, atOrigin), limit,
                             ok ? "" : "  <-- FUERA");
                CHECK (inPlace <= limit);
            }
        }
    }
    std::printf ("EDITORBUDGET_PIXEL resumen: %d de %d casos fuera (en su lugar <= %.1f x en el origen + %.1f ms)\n",
                 outside, total, kRatio, kSlackMs);
}

// ========================================================================================================
// 3 · REPOSO: con silencio y el transporte parado, ninguna lente pide repaint
// ========================================================================================================
// El host sigue llamando a processBlock con ceros cuando el transporte está parado (Ableton lo hace), así
// que el motor sigue publicando cuadros: silenciosos, pero cuadros nuevos. Una lente que cuente "llegó un
// cuadro" como "cambió algo" repinta 30 veces por segundo para siempre, aunque la pantalla no cambie.
// (TELESCOPE no lee el transporte en ningún lado —ni getPlayHead ni isPlaying—: "parado" es sólo ceros.)
//
// A cada lente se le da el silencio que su dibujo tarda EN SERIO en asentarse (ver restFor), más su
// propio settleHold (la cola que la base sigue repintando a propósito), y ahí se cuentan los repaints de
// 60 ticks. Para diez lentes tienen que ser CERO. Tres cambian de verdad en silencio, y lo que se les
// exige es que repinten cuando cambia algo y no en cada tick: su techo y su razón están en restFor.
namespace
{
struct Rest
{
    double      settleSeconds;   // lo que el dibujo tarda en asentarse después del último sonido
    int         maxInWindow;     // repaints admitidos en los 60 ticks (0 = quieta del todo)
    const char* why;
};

// Los tiempos salen de lo que DIBUJA cada lente (medidos en esta Mac el 24-sep con los arreglos del 96).
// A DYNAMICS y TONAL BALANCE se las mira a los 35 s: al principio del silencio su cambio real es de cuadro
// a cuadro (la barra del borde crece rápido; la curva se hunde 4.3/N dB/s) y se frena después. A los 35 s
// repintaban 6 y 30 de 60 ticks; antes del 96, 60 de 60 para siempre.
Rest restFor (telescope::LensId id)
{
    using L = telescope::LensId;
    switch (id)
    {
        case L::loudness:          return { 3.5, 0, "el short-term es una ventana de 3 s (EBU R128): el numero S baja 3 s" };
        case L::dynamics:          return { 35.0, 45, "cambio real: el short-term del silencio cae en el bin del borde "
                                                     "(-60 LUFS) y esa barra crece; repinta cuando una barra cambia de pixel" };
        case L::spectrum:          return { 9.0, 0, "el hold baja 12 dB/s: cruzar los 90 dB del plot y 20 de margen" };
        case L::spectrogram:       return { 32.0, 0, "la ventana visible (30 s de historia) tiene que llenarse de silencio" };
        case L::waterfall:         return { 32.0, 0, "la ventana visible (30 s de historia) tiene que llenarse de silencio" };
        case L::cqt:               return { 36.0, 0, "el % de tonalidad baja de a un punto hasta que la tonalidad se pierde (~34 s)" };
        case L::spiral:            return { 36.0, 0, "el % de tonalidad baja de a un punto hasta que la tonalidad se pierde (~34 s)" };
        case L::scope:             return { 3.5, 0, "el rotulo de la escala (dBFS) baja hasta el piso de la auto-escala" };
        case L::bandCorrelation:   return { 2.0, 0, "los suavizados por banda llegan al valor del silencio" };
        case L::stereoSpectrogram: return { 33.0, 0, "la ventana visible (30 s de historia) tiene que llenarse de silencio" };
        case L::field:             return { 23.0, 0, "el campo decae exponencial: cae bajo -120 dB de energia a los ~21 s" };
        case L::tonalBalance:      return { 35.0, 45, "cambio real: el promedio de bandas es desde el reset y el silencio lo "
                                                     "diluye (4.3/N dB/s) mientras la integrada no baja; repinta cada 0.02 dB" };
        case L::verdict:           return { 2.0, 18, "cambio real: el estado dice 'N s' analizados y cuenta tambien el "
                                                     "silencio, un cambio por segundo -> (5 s + 1) x (hold 2 + 1)" };
        default:                   return { 2.0, 0, "" };
    }
}
}

TEST_CASE ("telescope: en silencio y con el transporte parado las lentes dejan de repintar",
           "[telescope][editorbudget]")
{
    telescope::Lens::setReducedMotion (false);
    int moving = 0;
    for (int li = 0; li < telescope::kNumLenses; ++li)
    {
        const auto id = (telescope::LensId) li;
        Rig rig (id);
        rig.feedSignal();

        const int fps     = rig.lens->framesPerSecond();
        const int hold    = rig.lens->settleHoldFrames();
        const auto rest   = restFor (id);
        const int settle  = (int) std::ceil (rest.settleSeconds * fps) + hold;
        const int window  = 60;
        int firstTwoSec = 0, inWindow = 0, lastRepaint = -1;
        for (int t = 0; t < settle + window; ++t)
        {
            rig.feedSilenceTick (fps);
            const bool rep = rig.lens->tickForTest();
            if (rep) lastRepaint = t;
            if (rep && t < 2 * fps) ++firstTwoSec;
            if (rep && t >= settle) ++inWindow;
        }
        const bool ok = inWindow <= rest.maxInWindow;
        if (! ok) ++moving;
        std::printf ("EDITORBUDGET_REPOSO lens=%-18s fps=%2d hold=%3d asentamiento=%5.1f s  repaints en 60 ticks=%2d (max %2d)  "
                     "en los primeros 2 s=%3d de %3d  ultimo repaint a los %6.2f s de silencio%s  [%s]\n",
                     telescope::lensName (id), fps, hold, rest.settleSeconds, inWindow, rest.maxInWindow, firstTwoSec,
                     2 * fps, (double) (lastRepaint + 1) / fps, ok ? "" : "  <-- NO PARA", rest.why);
        CHECK (inWindow <= rest.maxInWindow);
    }
    std::printf ("EDITORBUDGET_REPOSO resumen: %d de %d lentes repintan en silencio mas de lo que cambia\n", moving,
                 telescope::kNumLenses);
}
