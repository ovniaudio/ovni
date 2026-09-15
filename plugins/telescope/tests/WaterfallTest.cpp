// [telescope][waterfall] — la lente 5: el espectrograma EN PROFUNDIDAD, y la proyección 2.5D que comparte
// con FIELD. Sin GPU (D-46): todo por software, con conteos acotados (≤ 120 líneas × 256 puntos).
//
// Lo que se verifica, en orden de "si esto está mal, el dibujo miente":
//
//   PROYECCIÓN   monótona en los tres ejes, siempre dentro del área, y las dos esquinas donde deben estar.
//                Es lo que le permite a la oclusión confiar en el orden de profundidad (ver WaterfallLens.h).
//   COLUMNAS     el mismo anillo da SIEMPRE las mismas N columnas, y la línea 0 es la más vieja que entra.
//   BARRIDO      con un barrido log, el pico de cada línea SUBE del fondo al frente (Spearman > 0.99):
//                si el eje de profundidad estuviera dado vuelta o las columnas salieran mezcladas, no.
//   ESTADO       los settings de la lente sobreviven al round-trip, y kSpectrum sólo se enciende con la
//                lente a la vista (lente a demanda).
//   [uisnap]     las tres fotos, con el barrido adentro.
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "TestHelpers.h"
#include "TestSignals.h"
#include "analysis/SpectrogramRing.h"
#include "lenses/LensIds.h"
#include "lenses/Projection2p5.h"
#include "lenses/WaterfallLens.h"

using telescope::Projection2p5;
using telescope::SpectrogramRing;
using telescope::WaterfallLens;

namespace
{
constexpr int kLensW = 1025, kLensH = 702;   // el área de la lente en tamaño L (820×562 × 1.25)

Projection2p5 makeProj()
{
    Projection2p5 p;
    p.x0 = 30.0f; p.y0 = 12.0f; p.w = 940.0f; p.h = 420.0f;
    p.tilt = 0.38f; p.depth = 0.20f;
    return p;
}

// Coeficiente de Spearman de una serie contra su índice (mide si SUBE, no cuánto). Misma cuenta que el
// test del sonograma; se copia porque los helpers de aquel archivo son estáticos suyos.
double spearmanAgainstIndex (const std::vector<int>& y)
{
    const int n = (int) y.size();
    std::vector<int> order ((size_t) n);
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return y[(size_t) a] < y[(size_t) b]; });

    std::vector<double> rank ((size_t) n);
    for (int i = 0; i < n; )
    {
        int j = i;
        while (j + 1 < n && y[(size_t) order[(size_t) (j + 1)]] == y[(size_t) order[(size_t) i]]) ++j;
        const double r = 0.5 * (double) (i + j) + 1.0;
        for (int k = i; k <= j; ++k) rank[(size_t) order[(size_t) k]] = r;
        i = j + 1;
    }

    double sd2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double d = rank[(size_t) i] - (double) (i + 1);
        sd2 += d * d;
    }
    return 1.0 - 6.0 * sd2 / ((double) n * ((double) n * (double) n - 1.0));
}

// La captura a PNG vive en TestHelpers.h, en UNA sola copia (LOW de los tres revisores).
using telescope::test::writePng;

// Tres barridos log de 60 Hz a 16 kHz (4 s cada uno), con freno para no desbordar el bus. Es la señal que
// hace visible el eje de profundidad: la cresta tiene que verse viajando hacia el fondo.
void pushSweeps (telescope::TelescopeProcessor& proc, int howMany)
{
    constexpr double kSweepSec = 4.0, f0 = 60.0, f1 = 16000.0;
    const double lnK = std::log (f1 / f0);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const long long total = (long long) ((double) howMany * kSweepSec * 48000.0);

    for (long long done = 0; done < total; done += 512)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.clear();
        for (int i = 0; i < k; ++i)
        {
            const double t  = std::fmod ((double) (done + i) / 48000.0, kSweepSec);
            const double ph = 2.0 * juce::MathConstants<double>::pi * f0 * kSweepSec
                                  * (std::exp (lnK * t / kSweepSec) - 1.0) / lnK;
            const auto v = (float) (0.4 * std::sin (ph));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);

        const double pushedSec = (double) (done + k) / 48000.0;
        if (pushedSec - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushedSec - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
}
}

// ======================================================================================= 1 · la proyección
TEST_CASE ("telescope: la proyeccion 2.5D es monotona y no se sale del area", "[telescope][waterfall]")
{
    const auto p = makeProj();
    const float left = p.x0, right = p.x0 + p.w, top = p.y0, bottom = p.y0 + p.h;

    // ---- las dos esquinas del contrato ----
    const auto front = p.project (0.0f, 0.0f, 0.0f);
    const auto back  = p.project (1.0f, 1.0f, 1.0f);
    std::printf ("WATERFALL[proyeccion] (0,0,0) -> (%.2f, %.2f)  esperado (%.2f, %.2f)  ·  "
                 "(1,1,1) -> (%.2f, %.2f)  esperado (%.2f, %.2f)\n",
                 front.x, front.y, left, bottom,
                 back.x, back.y, right - p.w * p.depth * 0.5f, top);
    REQUIRE (std::abs (front.x - left)   < 1.0e-3f);   // esquina de abajo a la izquierda del plano de adelante
    REQUIRE (std::abs (front.y - bottom) < 1.0e-3f);
    REQUIRE (std::abs (back.y - top)     < 1.0e-3f);   // borde de arriba del plano del fondo…
    REQUIRE (std::abs (back.x - (right - p.w * p.depth * 0.5f)) < 1.0e-3f);   // …corrido por la fuga central

    // ---- monotonía y encierro, muestreando la caja entera ----
    int samples = 0;
    for (int zi = 0; zi <= 20; ++zi)
    {
        const float z = (float) zi / 20.0f;

        float lastX = -1.0e9f;
        for (int xi = 0; xi <= 40; ++xi)
        {
            const auto q = p.project ((float) xi / 40.0f, 0.5f, z);
            REQUIRE (q.x > lastX);                     // px estrictamente creciente en x a z fijo
            lastX = q.x;
            REQUIRE (q.x >= left  - 1.0e-3f);
            REQUIRE (q.x <= right + 1.0e-3f);
            ++samples;
        }

        float lastY = 1.0e9f;
        for (int yi = 0; yi <= 40; ++yi)
        {
            const auto q = p.project (0.5f, (float) yi / 40.0f, z);
            REQUIRE (q.y < lastY);                     // py estrictamente decreciente en y a z fijo
            lastY = q.y;
            REQUIRE (q.y >= top    - 1.0e-3f);
            REQUIRE (q.y <= bottom + 1.0e-3f);
            ++samples;
        }
    }

    // py estrictamente decreciente en z (y por lo tanto los suelos NUNCA se cruzan: es lo que le permite
    // a la oclusión del waterfall confiar en el orden de profundidad).
    for (int yi = 0; yi <= 4; ++yi)
    {
        float lastY = 1.0e9f;
        for (int zi = 0; zi <= 40; ++zi)
        {
            const auto q = p.project (0.3f, (float) yi / 4.0f, (float) zi / 40.0f);
            REQUIRE (q.y < lastY);
            lastY = q.y;
            ++samples;
        }
    }

    // Y la inversa horizontal vuelve al punto de partida.
    for (int zi = 0; zi <= 10; ++zi)
        for (int xi = 0; xi <= 10; ++xi)
        {
            const float z = (float) zi / 10.0f, x = (float) xi / 10.0f;
            REQUIRE (std::abs (p.unprojectX (p.project (x, 0.0f, z).x, z) - x) < 1.0e-4f);
        }

    std::printf ("WATERFALL[proyeccion] %d muestras monotonas y dentro del area  ·  inversa exacta\n", samples);
}

// ==================================================================================== 2 · las columnas
TEST_CASE ("telescope: la seleccion de columnas del waterfall es determinista", "[telescope][waterfall]")
{
    // 10 s de historia a 46.875 columnas por segundo = 468 columnas; 90 líneas.
    constexpr long long kWrite = 5000;
    constexpr int kAvail = 468, kWanted = 90;

    std::array<long long, (size_t) WaterfallLens::kMaxLines> a {}, b {};
    const int n1 = WaterfallLens::selectColumns (kWrite, kAvail, kWanted, a.data());
    const int n2 = WaterfallLens::selectColumns (kWrite, kAvail, kWanted, b.data());

    REQUIRE (n1 == kWanted);
    REQUIRE (n2 == n1);
    REQUIRE (a == b);                                   // el mismo anillo → las mismas columnas

    REQUIRE (a[0] == kWrite - kAvail);                  // la línea 0 es la MÁS VIEJA que entra
    REQUIRE (a[(size_t) (n1 - 1)] == kWrite - 1);       // y la última es "ahora"
    for (int i = 1; i < n1; ++i) REQUIRE (a[(size_t) i] > a[(size_t) (i - 1)]);   // en orden, sin repetir

    // Con el anillo a medio llenar se dibujan las que hay, no las pedidas: repetir una columna dibujaría
    // un relieve que la señal no tiene.
    const int n3 = WaterfallLens::selectColumns (40, 40, kWanted, a.data());
    REQUIRE (n3 == 40);
    REQUIRE (a[0] == 0);
    REQUIRE (a[39] == 39);

    // Y los tres topes de líneas reparten sobre la misma historia.
    for (const int lines : { 60, 90, 120 })
    {
        const int n = WaterfallLens::selectColumns (kWrite, kAvail, lines, a.data());
        REQUIRE (n == lines);
        REQUIRE (a[0] == kWrite - kAvail);
        REQUIRE (a[(size_t) (n - 1)] == kWrite - 1);
        std::printf ("WATERFALL[columnas] %3d lineas sobre %d columnas  ·  primera %lld  ultima %lld  ·  "
                     "paso medio %.2f\n", lines, kAvail, a[0], a[(size_t) (n - 1)],
                     (double) (a[(size_t) (n - 1)] - a[0]) / (double) (n - 1));
    }
    REQUIRE (WaterfallLens::selectColumns (kWrite, 0, kWanted, a.data()) == 0);   // anillo vacío: nada
}

// ======================================================================================= 3 · el barrido
// Con un barrido log subiendo, el pico de cada línea tiene que correrse a la DERECHA del fondo al frente:
// la línea del fondo es la más vieja (frecuencia baja) y la de adelante la más nueva (alta). Si el eje de
// profundidad estuviera dado vuelta, o las columnas salieran mezcladas, Spearman se cae.
TEST_CASE ("telescope: en el waterfall la cresta del barrido viaja en profundidad", "[telescope][waterfall]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);

    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::mid;
        st.historySecIndex = 0;      // 10 s: un barrido de 4 s entra entero y sobra
        proc.setSpectrumSettings (st);
    }
    proc.setWaterfallLinesIndex (1);   // 90 líneas

    // UN solo barrido de 4 s, para que la relación tiempo→frecuencia sea monótona en toda la historia.
    pushSweeps (proc, 1);
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().count() > 150; }, 8000));

    const auto& ring = proc.spectrogram();
    const int avail = juce::jmin (ring.count(), ring.capacity());
    std::array<long long, (size_t) WaterfallLens::kMaxLines> src {};
    const int n = WaterfallLens::selectColumns (ring.writeIndex(), avail, proc.waterfallLines(), src.data());
    REQUIRE (n > 40);

    // El punto (de los 256 de la línea) donde cada línea tiene su pico, del FONDO (línea 0) al FRENTE.
    std::vector<int> peaks;
    peaks.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        juce::uint8 col[SpectrogramRing::kRows];
        if (! ring.copyColumn (src[(size_t) i], col)) continue;
        int best = 0, bestV = -1;
        for (int j = 0; j < WaterfallLens::kMaxPoints; ++j)
        {
            const int v = juce::jmax ((int) col[2 * j], (int) col[2 * j + 1]);
            if (v > bestV) { bestV = v; best = j; }
        }
        peaks.push_back (best);
    }
    REQUIRE (peaks.size() > 40u);

    int nonDecreasing = 0;
    for (size_t i = 1; i < peaks.size(); ++i) if (peaks[i] >= peaks[i - 1]) ++nonDecreasing;
    const double frac = (double) nonDecreasing / (double) (peaks.size() - 1);
    const double rho  = spearmanAgainstIndex (peaks);

    std::printf ("WATERFALL[barrido] %zu lineas  ·  punto del pico: fondo %d  frente %d  ·  Spearman=%.5f"
                 "  ·  no decrecientes=%.1f %%\n",
                 peaks.size(), peaks.front(), peaks.back(), rho, 100.0 * frac);
    REQUIRE (rho > 0.99);
    REQUIRE (frac >= 0.95);
    REQUIRE (peaks.back() > peaks.front());

    proc.releaseResources();
}

// ================================================================================= 4 · settings y demanda
TEST_CASE ("telescope: los settings del waterfall sobreviven al round-trip", "[telescope][waterfall]")
{
    juce::MemoryBlock saved;
    {
        telescope::TelescopeProcessor proc;
        REQUIRE (proc.waterfallLinesIndex() == telescope::TelescopeProcessor::kDefaultWaterfallLinesIndex);
        proc.setWaterfallLinesIndex (2);    // 120 líneas
        proc.setWaterfallTiltIndex (0);
        proc.getStateInformation (saved);
    }

    telescope::TelescopeProcessor proc;
    REQUIRE (proc.waterfallLinesIndex() == telescope::TelescopeProcessor::kDefaultWaterfallLinesIndex);
    proc.setStateInformation (saved.getData(), (int) saved.getSize());

    std::printf ("WATERFALL[round-trip] lineas %d  ·  inclinacion %d\n",
                 proc.waterfallLines(), proc.waterfallTiltIndex());
    REQUIRE (proc.waterfallLinesIndex() == 2);
    REQUIRE (proc.waterfallLines() == 120);
    REQUIRE (proc.waterfallTiltIndex() == 0);

    // Y valores fuera de rango (un estado guardado por una versión futura) se acotan, no rompen.
    proc.setWaterfallLinesIndex (99);
    proc.setWaterfallTiltIndex (-3);
    REQUIRE (proc.waterfallLinesIndex() == telescope::TelescopeProcessor::kNumWaterfallLineOptions - 1);
    REQUIRE (proc.waterfallTiltIndex() == 0);
}

TEST_CASE ("telescope: lente a demanda — kSpectrum solo con WATERFALL arriba", "[telescope][waterfall]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);

    const auto select = [&] (telescope::LensId id)
    {
        auto* p = proc.apvts.getParameter ("lens");
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) id));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    };

    select (telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);

    select (telescope::LensId::waterfall);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum)    != 0u);
    REQUIRE ((proc.enabledModules() & telescope::kLoudness)    != 0u);   // el medidor no se apaga nunca
    REQUIRE ((proc.enabledModules() & telescope::kStereoBands) == 0u);   // el waterfall NO lo necesita
    REQUIRE ((proc.enabledModules() & telescope::kField)       == 0u);

    select (telescope::LensId::loudness);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) == 0u);

    select (telescope::LensId::waterfall);
    ed.reset();
    std::printf ("WATERFALL[a demanda] mascara tras cerrar = 0x%02x\n", (unsigned) proc.enabledModules());
    REQUIRE (proc.enabledModules() == telescope::kAlwaysOnModules);

    proc.releaseResources();
}

// ============================================================================================= 5 · fotos
TEST_CASE ("telescope: snapshot del editor con la lente WATERFALL en S/M/L", "[telescope][uisnap]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    auto* lensParam = proc.apvts.getParameter ("lens");
    REQUIRE (lensParam != nullptr);
    lensParam->setValueNotifyingHost (lensParam->convertTo0to1 ((float) (int) telescope::LensId::waterfall));

    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::mid;
        st.historySecIndex = 0;      // 10 s
        st.rangeDbIndex    = 1;      // 90 dB
        proc.setSpectrumSettings (st);
    }
    proc.setWaterfallLinesIndex (1);   // 90 líneas
    proc.setWaterfallTiltIndex (1);

    // El EDITOR primero: es el que enciende kSpectrum (lente a demanda). Sin él el anillo queda vacío.
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* tel = dynamic_cast<telescope::TelescopeEditor*> (ed.get());
    REQUIRE (tel != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    REQUIRE ((proc.enabledModules() & telescope::kSpectrum) != 0u);

    pushSweeps (proc, 3);   // 12 s: la historia de 10 s entra llena, con la cresta viajando al fondo
    REQUIRE (telescope::test::waitUntil (
        [&] { return proc.spectrogram().count() >= proc.spectrogram().capacity(); }, 10000));

    // Que la foto tenga DATO adentro, no sólo el tamaño correcto.
    {
        juce::uint8 col[SpectrogramRing::kRows];
        REQUIRE (proc.spectrogram().copyColumn (proc.spectrogram().writeIndex() - 1, col));
        int worst = 0;
        for (const auto v : col) worst = juce::jmax (worst, (int) v);
        std::printf ("UISNAP waterfall: %d columnas de %d, %.2f col/s  ·  pico de la ultima columna = %d/255\n",
                     proc.spectrogram().count(), proc.spectrogram().capacity(),
                     proc.spectrogram().columnsPerSecond(), worst);
        REQUIRE (worst > 64);
    }

    tel->pumpLensFrames (5);

    const struct { ovni::PluginEditorBase::Zoom zoom; const char* path; } shots[] = {
        { ovni::PluginEditorBase::Zoom::small,  "/tmp/ovni_telescope_waterfall_S.png" },
        { ovni::PluginEditorBase::Zoom::medium, "/tmp/ovni_telescope_waterfall_M.png" },
        { ovni::PluginEditorBase::Zoom::large,  "/tmp/ovni_telescope_waterfall_L.png" },
    };
    for (const auto& s : shots)
    {
        tel->applyZoom (s.zoom);
        tel->pumpLensFrames (3);
        writePng (*tel, s.path);
    }

    proc.releaseResources();
}

// ======================================================================================= 6 · la lectura
TEST_CASE ("telescope: WATERFALL lee la frecuencia y el nivel de la linea de adelante", "[telescope][waterfall]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);

    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::mid;
        st.historySecIndex = 0;
        st.rangeDbIndex    = 1;      // 90 dB
        proc.setSpectrumSettings (st);
    }

    // Tono estable de 1 kHz a -20 dBFS: la lectura sobre su columna tiene que dar 1 kHz y ~-20 dB.
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    const float peak = std::pow (10.0f, -20.0f / 20.0f);
    for (int blk = 0; blk < (int) (4.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const auto v = (float) (peak * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0
                                                     * (double) n / 48000.0));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
        const double pushed = (double) n / 48000.0;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().count() > 150; }, 8000));

    WaterfallLens lens (proc);
    lens.setSize (kLensW, kLensH);
    lens.pumpFrames (2);
    { juce::Image img (juce::Image::ARGB, kLensW, kLensH, true); juce::Graphics g (img);
      lens.paintEntireComponent (g, false); }   // pintar arma las líneas que la lectura consulta

    REQUIRE (lens.lineCount() > 0);

    // Se barre el ancho del plot y se busca el píxel cuya lectura da el nivel más alto: tiene que caer
    // en 1 kHz. Comparar contra una x calculada a mano probaría la fórmula contra sí misma.
    double bestHz = 0.0;
    float  bestDb = -1000.0f;
    int    valid  = 0;
    for (int x = 0; x < kLensW; ++x)
    {
        const auto r = lens.readoutAt ({ x, kLensH / 3 });
        if (! r.valid) continue;
        ++valid;
        if (r.db > bestDb) { bestDb = r.db; bestHz = r.freqHz; }
    }

    std::printf ("WATERFALL[lectura] %d px con dato  ·  pico en %.0f Hz a %.1f dB  ·  %d lineas  ·  %.2f s\n",
                 valid, bestHz, bestDb, lens.lineCount(), lens.visibleSeconds());
    REQUIRE (valid > 100);
    REQUIRE (std::abs (bestHz / 1000.0 - 1.0) < 0.10);
    REQUIRE (bestDb > -30.0f);
    REQUIRE (bestDb < -10.0f);
    REQUIRE (lens.visibleSeconds() > 3.0);

    // Fuera del plot no hay lectura (y no se inventa un número).
    REQUIRE_FALSE (lens.readoutAt ({ -5, kLensH / 3 }).valid);
    REQUIRE_FALSE (lens.readoutAt ({ kLensW / 2, kLensH - 4 }).valid);

    proc.releaseResources();
}

// ========================================================================================================
// 56b · LOS RÓTULOS DE TIEMPO NO SE APOYAN SOBRE EL RELLENO
//
// El eje de tiempo va en la PROFUNDIDAD, o sea ENCIMA del dibujo (el waterfall es opaco: debajo no se
// vería). La caja de fondo estaba a alpha 0.78, así que el verde del relleno se colaba por atrás y los
// rótulos de la esquina de abajo a la izquierda —-2 s, -4 s, -6 s, que son los que caen sobre la parte
// más llena— se leían como agujeros sucios en el dato en vez de como rótulos.
//
// El criterio es directo: bajo la caja de un rótulo no puede quedar NI UN píxel del relleno. La lente
// dibuja el relleno con la familia Espectral (verde por encima del azul); la caja y el fondo no. El mismo
// criterio de color que usa [field] para separar la paleta de las hairlines.
// ========================================================================================================
TEST_CASE ("telescope: los rotulos de tiempo del waterfall no dejan ver el relleno debajo",
           "[telescope][waterfall]")
{
    telescope::TelescopeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.setEnabledModules (telescope::kSpectrum | telescope::kLoudness);
    // 57b — LA RAMPA DEL SELLO A PROPÓSITO. Lo que este test mira es si se ve el DATO debajo de un
    // rótulo, y para decidir "esto es dato" usa un atajo de color (verde por encima de azul). Ese atajo
    // vale para `ovni` y no para `inferno`, que es el default desde el 57b: sus niveles bajos son púrpura
    // (azul por encima de verde) y el test se quedaría mirando una pantalla que para él está vacía. Se
    // fija la rampa en vez de aflojar el criterio — lo que se verifica es la opacidad del rótulo, no la
    // paleta, y con `ovni` la afirmación sigue siendo exactamente la misma.
    proc.setPaletteIndex ((int) telescope::look::PaletteId::ovni);
    {
        auto st = proc.spectrumSettings();
        st.channel         = telescope::Spectrum::mid;
        st.historySecIndex = 0;
        proc.setSpectrumSettings (st);
    }

    // Ruido rosa: llena el plot de lado a lado, que es el caso donde los rótulos caen sobre el relleno.
    telescope::test::Pink pink { telescope::test::kPinkSeedA };
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    long long n = 0;
    for (int blk = 0; blk < (int) (12.0 * 48000.0 / 512.0); ++blk)
    {
        for (int i = 0; i < 512; ++i, ++n)
        {
            const float v = 0.35f * pink.next();
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc.processBlock (buf, midi);
        const double pushed = (double) n / 48000.0;
        if (pushed - proc.analysis().read().timeSeconds > 2.0)
            REQUIRE (telescope::test::waitUntil (
                [&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, 8000));
    }
    REQUIRE (telescope::test::waitUntil ([&] { return proc.spectrogram().count() > 400; }, 10000));

    WaterfallLens lens (proc);
    lens.setSize (kLensW, kLensH);
    lens.pumpFrames (3);
    juce::Image img (juce::Image::ARGB, kLensW, kLensH, true);
    { juce::Graphics g (img); lens.paintEntireComponent (g, false); }

    // La caja de cada rótulo, con la MISMA geometría que usa el dibujo: la lente la expone para que el
    // test no la vuelva a calcular (y no mida su propia copia).
    const auto boxes = lens.timeLabelBoxes();
    REQUIRE (boxes.size() >= 3);          // con 10 s de historia hay al menos "now", -2 s y -4 s

    int fillUnder = 0, painted = 0;
    for (const auto& b : boxes)
        for (int y = b.getY(); y < b.getBottom(); ++y)
            for (int x = b.getX(); x < b.getRight(); ++x)
            {
                if (! img.getBounds().contains (x, y)) continue;
                ++painted;
                const auto c = img.getPixelAt (x, y);
                if (c.getAlpha() > 0 && (int) c.getGreen() > (int) c.getBlue() + 8) ++fillUnder;
            }

    std::printf ("WATERFALL[rotulos] %d cajas, %d px inspeccionados  ·  %d px de relleno visibles debajo\n",
                 boxes.size(), painted, fillUnder);
    REQUIRE (painted > 400);              // hay cajas de verdad: si no, el criterio no mira nada
    REQUIRE (fillUnder == 0);

    // Y la contraprueba de que el relleno EXISTE donde no hay caja: si el plot estuviera vacío, el test de
    // arriba pasaría solo. Se mira la franja de abajo del plot, que con rosa está siempre llena.
    const auto plot = lens.plotArea();
    int fillElsewhere = 0;
    for (int y = plot.getBottom() - 40; y < plot.getBottom() - 4; ++y)
        for (int x = plot.getCentreX(); x < plot.getRight() - 8; ++x)
        {
            const auto c = img.getPixelAt (x, y);
            if (c.getAlpha() > 0 && (int) c.getGreen() > (int) c.getBlue() + 8) ++fillElsewhere;
        }
    std::printf ("WATERFALL[rotulos] relleno fuera de las cajas = %d px (la contraprueba)\n", fillElsewhere);
    REQUIRE (fillElsewhere > 1000);

    proc.releaseResources();
}
