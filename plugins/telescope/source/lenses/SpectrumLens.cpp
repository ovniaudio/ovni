#include "lenses/SpectrumLens.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "lenses/LensReadout.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

// Las etiquetas de frecuencia que sí se leen. La rejilla tiene los 30 centros de ⅓ de octava; rotularlos
// todos sería una pared de números.
constexpr double kLabelledHz[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };

juce::String shortHz (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        return juce::String (k, k < 10.0 && std::abs (k - std::round (k)) > 0.01 ? 1 : 0) + "k";
    }
    return juce::String ((int) std::lround (hz));
}

const char* windowLabel (int w)
{
    return w == Spectrum::hann ? "HANN" : (w == Spectrum::blackmanHarris4 ? "BH4" : "KAISER");
}

const char* channelLabel (int c)
{
    switch (c)
    {
        case Spectrum::left:  return "L";
        case Spectrum::right: return "R";
        case Spectrum::mid:   return "M";
        case Spectrum::side:  return "S";
        default:              return "L+R";
    }
}

const char* bandsLabel (int b)
{
    return b == Spectrum::bandsThird ? "1/3 OCT" : (b == Spectrum::bandsBark ? "BARK" : "FFT");
}
}

const juce::ValueTree& SpectrumLens::stateTree() const { return processor.apvts.state; }

SpectrumLens::SpectrumLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (30);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//======================================================================================== geometría
SpectrumLens::Zones SpectrumLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    // Dos filas de controles: cinco arriba (lo que define el ANÁLISIS) y cuatro abajo (lo que define la
    // PRESENTACIÓN). Agruparlos por lo que hacen es más rápido de leer que nueve botones en fila.
    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH * 2 + 6);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    auto top  = foot.removeFromTop (rowH);
    foot.removeFromTop (6);
    auto bottom = foot.removeFromTop (rowH);

    const auto share = [] (juce::Rectangle<int>& row, int n, juce::Rectangle<int>* dst)
    {
        const int gap = 6;
        const int bw  = (row.getWidth() - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
        {
            dst[i] = row.removeFromLeft (bw);
            if (i < n - 1) row.removeFromLeft (gap);
        }
    };
    juce::Rectangle<int> analysis[5], display[5];
    share (top, 5, analysis);
    share (bottom, 5, display);   // 57b: el quinto es el suavizado de pantalla

    z.button[fftSize]   = analysis[0];
    z.button[window]    = analysis[1];
    z.button[overlap]   = analysis[2];
    z.button[channel]   = analysis[3];
    z.button[bandsMode] = analysis[4];
    z.button[slope]     = display[0];
    z.button[average]   = display[1];
    z.button[hold]      = display[2];
    z.button[range]     = display[3];
    z.button[smooth]    = display[4];

    z.freqAxis = body.removeFromBottom (kAxisH);
    z.dbScale  = body.removeFromLeft (kScaleW);
    z.plot     = body;
    z.freqAxis = z.freqAxis.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

void SpectrumLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    const auto n = (size_t) juce::jmax (0, zones.plot.getWidth());
    for (int s = 0; s < SpectrumFrame::kMaxSpectra; ++s)
    {
        colDb[s].assign (n, SpectrumFrame::kFloorDb);
        colHold[s].assign (n, SpectrumFrame::kFloorDb);
        dispDb[s].assign (n, SpectrumFrame::kFloorDb);
    }
    Lens::resized();
}

//======================================================================================== ejes
double SpectrumLens::freqAtX (int x) const
{
    if (zones.plot.getWidth() <= 1) return kMinHz;
    const double t = juce::jlimit (0.0, 1.0, (double) (x - zones.plot.getX()) / (double) zones.plot.getWidth());
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

float SpectrumLens::xForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return (float) zones.plot.getX() + (float) t * (float) zones.plot.getWidth();
}

float SpectrumLens::yForDb (float db) const
{
    const float rangeDb = (float) sets.rangeDb();   // cacheado: ver la nota del header
    const float t = juce::jlimit (0.0f, 1.0f, (0.0f - db) / rangeDb);   // 0 dB arriba
    return (float) zones.plot.getY() + t * (float) zones.plot.getHeight();
}

//======================================================================================== capa estática
void SpectrumLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    sets = processor.spectrumSettings();
    const auto& s = sets;
    lastRangeDb = s.rangeDb();

    g.setColour (th::surf.withAlpha (0.65f));
    g.fillRoundedRectangle (zones.plot.toFloat(), 3.0f);
    g.setColour (look::gridMinor);
    g.drawRoundedRectangle (zones.plot.toFloat().reduced (0.5f), 3.0f, 1.0f);

    // ---- rejilla de frecuencia: los 30 centros de ⅓ de octava, y las décadas más marcadas ----
    for (const double hz : kThirdOctaveHz)
    {
        if (hz < kMinHz || hz > kMaxHz) continue;
        const bool decade = std::abs (std::log10 (hz) - std::round (std::log10 (hz))) < 1.0e-6;
        g.setColour (decade ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (juce::roundToInt (xForFreq (hz))), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });
    }

    g.setFont (ovni::ui::fonts::mono (9.0f));
    g.setColour (th::fnt);
    for (const double hz : kLabelledHz)
    {
        const int x = juce::roundToInt (xForFreq (hz));
        g.drawText (shortHz (hz), x - 20, zones.freqAxis.getY() + 1, 40, 12,
                    juce::Justification::centred, false);
    }

    // ---- rejilla de dB: 0 arriba, hacia abajo el rango elegido ----
    const int step = s.rangeDb() == 60 ? 6 : (s.rangeDb() == 90 ? 10 : 20);
    for (int db = 0; db >= -s.rangeDb(); db -= step)
    {
        const int y = juce::roundToInt (yForDb ((float) db));
        g.setColour (db == 0 ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (zones.plot.getX()), (float) (y), (float) (zones.plot.getWidth()), 1.0f });
        g.setColour (th::fnt);
        g.drawText (juce::String (db), zones.dbScale.getX(), y - 6, kScaleW - 8, 12,
                    juce::Justification::centredRight, false);
    }

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("dBFS", zones.dbScale.getX(), zones.plot.getY() + 2, kScaleW - 8, 12,
                juce::Justification::centredRight, false);
}

//======================================================================================== datos
// De los bins del frame a UNA entrada por columna de píxel. Cada columna toma el máximo de los bins que
// le tocan; donde una columna cae entre dos bins (graves con FFT chica), interpola LINEALMENTE EN dB, que
// es lo que hace que la curva no escalone en los graves.
void SpectrumLens::rebuildColumns (const SpectrumFrame& f)
{
    const int width = zones.plot.getWidth();
    if (width <= 0 || f.numBins <= 1) return;

    const double binHz = f.binHz();
    const int    nSpec = spectrumNumSpectra (f.channelMode);
    const float  slopeDb = sets.slopeDbPerOct;

    for (int s = 0; s < SpectrumFrame::kMaxSpectra; ++s)
    {
        if (s >= nSpec)
        {
            std::fill (colDb[s].begin(), colDb[s].end(), SpectrumFrame::kFloorDb);
            std::fill (colHold[s].begin(), colHold[s].end(), SpectrumFrame::kFloorDb);
            continue;
        }

        for (int x = 0; x < width; ++x)
        {
            const double f0 = freqAtX (zones.plot.getX() + x);
            const double f1 = freqAtX (zones.plot.getX() + x + 1);
            const double centre = std::sqrt (f0 * f1);         // el centro geométrico del píxel

            const int k0 = (int) std::ceil (f0 / binHz);
            const int k1 = (int) std::ceil (f1 / binHz);

            float db = SpectrumFrame::kFloorDb, hd = SpectrumFrame::kFloorDb;
            if (k1 > k0)
            {
                for (int k = juce::jmax (0, k0); k < juce::jmin (f.numBins, k1); ++k)
                {
                    db = juce::jmax (db, f.magDb[s][k]);
                    hd = juce::jmax (hd, f.holdDb[s][k]);
                }
            }
            else
            {
                const double pos = centre / binHz;
                const int    ka  = juce::jlimit (0, f.numBins - 1, (int) std::floor (pos));
                const int    kb  = juce::jlimit (0, f.numBins - 1, ka + 1);
                const auto   t   = (float) juce::jlimit (0.0, 1.0, pos - (double) ka);
                db = f.magDb[s][ka]  + t * (f.magDb[s][kb]  - f.magDb[s][ka]);
                hd = f.holdDb[s][ka] + t * (f.holdDb[s][kb] - f.holdDb[s][ka]);
            }

            colDb[s][(size_t) x]   = spectrumDisplayDb (db, centre, slopeDb);
            colHold[s][(size_t) x] = spectrumDisplayDb (hd, centre, slopeDb);
        }

        std::copy (f.bands[s],     f.bands[s]     + SpectrumFrame::kNumThird, bandDb[s]);
        std::copy (f.bandsHold[s], f.bandsHold[s] + SpectrumFrame::kNumThird, bandHold[s]);
        std::copy (f.bark[s],      f.bark[s]      + SpectrumFrame::kNumBark,  barkDb[s]);
    }
}

bool SpectrumLens::advanceFrame()
{
    sets = processor.spectrumSettings();

    // Referencia, no copia: el SpectrumFrame son ~260 KB y sólo hace falta hasta el final de esta función.
    const auto& f = processor.spectrum().read();
    const bool  fresh = f.frameIndex != lastFrameIndex || f.fftSize != fftSizeSeen
                                                       || f.channelMode != channelSeen;
    if (fresh)
    {
        lastFrameIndex = f.frameIndex;
        fftSizeSeen = f.fftSize;
        numBinsSeen = f.numBins;
        channelSeen = f.channelMode;
        srSeen      = f.sr;
        rebuildColumns (f);
    }

    // Si cambió el rango de dB hay que rehornear la rejilla: es capa estática.
    if (sets.rangeDb() != lastRangeDb) invalidateStatic();

    // La curva SUBE de una y BAJA suave: es un medidor, y un pico que se pierde entre dos frames es un
    // pico que no existió. Con reduced-motion no hay suavizado: se dibuja el frame tal cual.
    const bool reduced = prefersReducedMotion();
    bool moved = false;
    for (int s = 0; s < SpectrumFrame::kMaxSpectra; ++s)
        for (size_t x = 0; x < dispDb[s].size(); ++x)
        {
            const float target = colDb[s][x];
            const float before = dispDb[s][x];
            dispDb[s][x] = (reduced || target > before) ? target : before + (target - before) * kRelease;
            moved = moved || std::abs (dispDb[s][x] - before) > 0.01f;
        }

    return fresh || moved;
}

//======================================================================================== capa viva
void SpectrumLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); resized(); }

    const auto& s = sets;
    const int   nSpec = spectrumNumSpectra (channelSeen);
    const juce::Colour hues[SpectrumFrame::kMaxSpectra] = { th::green, th::magenta };

    if (s.bandsMode == Spectrum::bandsFft)
    {
        // 57b — la curva se rasteriza a mano sobre una caché a escala FÍSICA (ver el bloque grande de
        // abajo) y vuelve al plano lógico de un solo blit.
        const float ps = look::physicalScale (g);
        curveCache.prepare (ps, zones.plot.getWidth(), zones.plot.getHeight());
        rasteriseCurves (ps);
        curveCache.blit (g, zones.plot.getX(), zones.plot.getY());
    }
    else
    {
        g.saveState();
        g.reduceClipRegion (zones.plot);
        for (int i = 0; i < nSpec; ++i) paintBars (g, i, hues[i]);
        g.restoreState();
    }

    paintReadout (g);

    // ---- la tira de controles ----
    const juce::String avgValue = s.avgMode == Spectrum::avgNone ? tr (strings::Key::off)
                                : (s.avgMode == Spectrum::avgInfinite ? juce::String::fromUTF8 ("\xe2\x88\x9e")
                                                                      : juce::String (s.avgSeconds, 1) + " s");
    const juce::String holdValue = ! s.peakHold ? tr (strings::Key::off)
                                 : (s.holdDecayDbPerSec <= 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x9e")
                                                                : juce::String ((int) s.holdDecayDbPerSec) + " dB/s");

    paintButton (g, zones.button[fftSize],   tr (strings::Key::fftSize),  juce::String (s.fftSize()),      hovered == fftSize);
    paintButton (g, zones.button[window],    tr (strings::Key::windowFn), windowLabel (s.window),          hovered == window);
    paintButton (g, zones.button[overlap],   tr (strings::Key::overlap),  juce::String ((int) std::lround (s.overlap() * 100.0f)) + " %", hovered == overlap);
    paintButton (g, zones.button[channel],   tr (strings::Key::channel),  channelLabel (s.channel),        hovered == channel);
    paintButton (g, zones.button[bandsMode], tr (strings::Key::bands),    bandsLabel (s.bandsMode),        hovered == bandsMode);
    paintButton (g, zones.button[slope],     tr (strings::Key::slope),    juce::String (s.slopeDbPerOct, 1) + " dB/oct", hovered == slope);
    paintButton (g, zones.button[average],   tr (strings::Key::average),  avgValue,                        hovered == average);
    paintButton (g, zones.button[hold],      tr (strings::Key::hold),     holdValue,                       hovered == hold);
    paintButton (g, zones.button[range],     tr (strings::Key::range),    juce::String (s.rangeDb()) + " dB", hovered == range);

    const int smoothDenom = TelescopeProcessor::kSpectrumSmoothDenom[
                                juce::jlimit (0, TelescopeProcessor::kNumSpectrumSmoothOptions - 1,
                                              processor.spectrumSmoothIndex())];
    paintButton (g, zones.button[smooth], tr (strings::Key::smoothing),
                 smoothDenom <= 0 ? tr (strings::Key::off) : "1/" + juce::String (smoothDenom) + " oct",
                 hovered == smooth);
}

//======================================================================================== 57b · el rasterizador
// ========================================================================================================
// EL RASTERIZADOR PROPIO DE SPECTRUM, y por qué esta lente dejó de dibujar con JUCE.
//
// ============================== QUÉ ESTABA MAL, dicho por quien lo vio ===================================
//
// «Spectrum se ve viejo, pixelado» (Joaquín, 9-sep, contra Insight). Y era literal: la curva se dibujaba
// con UN fillRect de 1 px de ancho POR COLUMNA para el área y otro para el trazo. Rectángulos enteros, sin
// antialiasing: en cada tramo inclinado la línea sube en escalones de un píxel entero. El comentario que
// estaba acá explicaba por qué se había hecho así —las alternativas con Path y gradiente de JUCE costaban
// 6 a 10 ms— y el dato sigue siendo cierto. Lo que cambió es que ya no hace falta elegir entre las dos:
// se escribe el píxel a mano.
//
// ===================================== CÓMO SE DIBUJA AHORA =============================================
//
//   1 · LA CURVA PASA A RESOLUCIÓN DE DISPOSITIVO. `dispDb` tiene una entrada por columna LÓGICA; entre
//       ellas se interpola con Hermite MONÓTONA (Fritsch-Carlson 1980), que es la que no se pasa de los
//       valores que interpola. Importa justo en graves, donde un bin abarca varias columnas y la curva
//       viene en escalones: una cúbica común les inventaría un sobrepico a cada escalón — un pico que no
//       midió nadie, en la lente donde la gente busca picos.
//   2 · SUAVIZADO DE PANTALLA, como SPAN: promedio móvil sobre un ancho fijo en OCTAVAS. Sobre este eje
//       (log de frecuencia, lineal en x) una fracción de octava ES un ancho fijo en píxeles, así que sale
//       de una suma corrida. Se aplica SÓLO a lo que se dibuja; el readout y el hold siguen crudos.
//   3 · EL TRAZO, con cobertura vertical exacta. Por columna se toma el SEGMENTO entre y[x] e y[x+1] y se
//       mide la distancia de cada fila a ese segmento: donde la curva es empinada el trazo se ensancha
//       solo, que es exactamente lo que hace un rasterizador de verdad y lo que el fillRect no hacía.
//   4 · GLOW BARATO. El prompt lo pide como tres pasadas (7 px al 12 %, 3 al 30 %, 1.5 al 100 %). Acá son
//       las MISMAS tres anchuras expresadas como UN perfil tabulado: el resultado es el mismo píxel y se
//       escribe una vez en vez de tres. Nada de blur por frame.
//   5 · EL RELLENO SE DEGRADA DESDE LA CURVA, no por fila absoluta: alpha = 0.42·(1−d)^1.6 con d la
//       distancia relativa a la curva. Sale de una tabla de 256 entradas; el bucle interior es una resta,
//       una multiplicación y un store.
//
// ========================== POR QUÉ EL RELLENO VA POR FILAS Y EL TRAZO POR COLUMNAS =======================
//
// El relleno son ~1.4 millones de píxeles por espectro: recorrido por columnas, cada store cae en otra
// línea de caché y se paga memoria, no cuentas. Por filas se escriben 16 píxeles por línea de caché. El
// trazo, en cambio, son ~12 filas por columna (~23 000 píxeles): ahí el recorrido por columnas no se nota
// y es el único que conoce el segmento.
//
// LA IMAGEN ES TRANSPARENTE Y PREMULTIPLICADA. Transparente porque la rejilla y el pozo son capa ESTÁTICA
// y viven debajo: una caché opaca los taparía. Premultiplicada porque es lo que guarda juce::Image::ARGB,
// y escribir ARGB derecho ahí sale mal en los bordes translúcidos, que es todo el trazo.
// ========================================================================================================
namespace
{
// Un color ya premultiplicado por su alpha, listo para escribir en una juce::Image::ARGB.
inline juce::uint32 premul (juce::Colour c, float a) noexcept
{
    const auto A = (juce::uint32) juce::jlimit (0, 255, (int) std::lround (a * 255.0f));
    const auto ch = [A] (juce::uint8 v) { return (juce::uint32) ((v * A + 127u) / 255u); };
    return (A << 24) | (ch (c.getRed()) << 16) | (ch (c.getGreen()) << 8) | ch (c.getBlue());
}

// `src` SOBRE `dst`, los dos premultiplicados. dst = src + dst·(1−αsrc).
inline juce::uint32 overPremul (juce::uint32 src, juce::uint32 dst) noexcept
{
    const juce::uint32 ia = 255u - (src >> 24);
    if (ia == 0u) return src;
    const auto ch = [ia] (juce::uint32 s, juce::uint32 d) { return s + ((d * ia + 127u) / 255u); };
    return (juce::jmin (255u, ch (src >> 24, dst >> 24)) << 24)
         | (juce::jmin (255u, ch ((src >> 16) & 0xffu, (dst >> 16) & 0xffu)) << 16)
         | (juce::jmin (255u, ch ((src >> 8) & 0xffu, (dst >> 8) & 0xffu)) << 8)
         |  juce::jmin (255u, ch (src & 0xffu, dst & 0xffu));
}

// EL PERFIL DEL TRAZO, tabulado. Son las tres anchuras del glow en una sola curva (ver el punto 4 del
// bloque de arriba): núcleo de 1.5 px al 100 %, halo de 3 px al 30 %, resplandor de 7 px al 12 %, con
// medio píxel de rampa en cada borde para que ninguno de los tres deje un anillo duro.
constexpr int   kStrokeLutN = 72;      // hasta 4.5 px de distancia, en pasos de 1/16 px
constexpr float kStrokeLutStep = 16.0f;

float strokeProfile (float d) noexcept
{
    const auto band = [d] (float halfWidth, float level)
    {
        return level * juce::jlimit (0.0f, 1.0f, (halfWidth + 0.5f - d) * 2.0f);
    };
    return juce::jmax (band (0.75f, 1.0f), juce::jmax (band (1.5f, 0.30f), band (3.5f, 0.12f)));
}

// INTERPOLACIÓN HERMITE MONÓTONA (Fritsch-Carlson): las pendientes se limitan para que el trozo entre dos
// puntos NUNCA se salga del intervalo que forman. Es la diferencia entre suavizar una curva y agregarle
// picos que no midió nadie. `v` está en una grilla de paso 1.
void monotoneSlopes (const float* v, int n, float* m) noexcept
{
    if (n <= 1) { if (n == 1) m[0] = 0.0f; return; }
    for (int i = 0; i < n; ++i)
    {
        const float dPrev = i > 0     ? v[i] - v[i - 1] : v[1] - v[0];
        const float dNext = i < n - 1 ? v[i + 1] - v[i] : v[n - 1] - v[n - 2];
        m[i] = 0.5f * (dPrev + dNext);
    }
    for (int i = 0; i < n - 1; ++i)
    {
        const float d = v[i + 1] - v[i];
        if (std::abs (d) < 1.0e-7f) { m[i] = 0.0f; m[i + 1] = 0.0f; continue; }
        const float a = m[i] / d, b = m[i + 1] / d;
        const float q = a * a + b * b;
        if (q > 9.0f)
        {
            const float tau = 3.0f / std::sqrt (q);
            m[i]     = tau * a * d;
            m[i + 1] = tau * b * d;
        }
    }
}

inline float hermite (float v0, float v1, float m0, float m1, float t) noexcept
{
    const float t2 = t * t, t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + (t3 - 2.0f * t2 + t) * m0
         + (-2.0f * t3 + 3.0f * t2) * v1 + (t3 - t2) * m1;
}
}

// Arma la curva de UN espectro en columnas de DISPOSITIVO: interpola, suaviza y la pasa a y de píxel.
void SpectrumLens::buildDeviceCurve (int slot, int deviceW, float physScale, int smoothRadius)
{
    const int n = (int) dispDb[slot].size();
    devY[slot].assign ((size_t) deviceW, (float) curveCache.deviceH());
    devHoldY[slot].assign ((size_t) deviceW, (float) curveCache.deviceH());
    if (n <= 1 || deviceW <= 0) return;

    devSlope.assign ((size_t) n, 0.0f);
    devScratch.assign ((size_t) deviceW, 0.0f);

    const float invS  = 1.0f / juce::jmax (1.0e-3f, physScale);
    const float plotY = (float) zones.plot.getY();
    const float dev   = [&] { return (float) curveCache.deviceH() / juce::jmax (1.0f, (float) zones.plot.getHeight()); }();

    // ---- (a) dB por columna de dispositivo, Hermite monótona sobre las columnas lógicas ----
    const auto resample = [&] (const std::vector<float>& src, std::vector<float>& dst)
    {
        monotoneSlopes (src.data(), n, devSlope.data());
        for (int x = 0; x < deviceW; ++x)
        {
            const float u = juce::jlimit (0.0f, (float) (n - 1), ((float) x + 0.5f) * invS - 0.5f);
            const int   i = juce::jlimit (0, n - 2, (int) u);
            const float t = juce::jlimit (0.0f, 1.0f, u - (float) i);
            dst[(size_t) x] = hermite (src[(size_t) i], src[(size_t) (i + 1)],
                                       devSlope[(size_t) i], devSlope[(size_t) (i + 1)], t);
        }
    };

    resample (dispDb[slot], devScratch);

    // ---- (b) suavizado de pantalla: promedio móvil de ancho fijo en octavas (ver el bloque de arriba) ----
    if (smoothRadius > 0)
    {
        std::vector<float>& v = devScratch;
        double run = 0.0;
        const int w = smoothRadius;
        std::vector<float> out ((size_t) deviceW, 0.0f);
        for (int x = -w; x <= w; ++x) run += v[(size_t) juce::jlimit (0, deviceW - 1, x)];
        for (int x = 0; x < deviceW; ++x)
        {
            out[(size_t) x] = (float) (run / (double) (2 * w + 1));
            run -= v[(size_t) juce::jlimit (0, deviceW - 1, x - w)];
            run += v[(size_t) juce::jlimit (0, deviceW - 1, x + w + 1)];
        }
        v.swap (out);
    }

    // ---- (c) a y de píxel de dispositivo, relativa al plot ----
    for (int x = 0; x < deviceW; ++x)
        devY[slot][(size_t) x] = (yForDb (devScratch[(size_t) x]) - plotY) * dev;

    if (sets.peakHold)
    {
        resample (colHold[slot], devScratch);   // el HOLD no se suaviza: es una marca, no una curva
        for (int x = 0; x < deviceW; ++x)
            devHoldY[slot][(size_t) x] = (yForDb (devScratch[(size_t) x]) - plotY) * dev;
    }
}

// Pinta LOS DOS espectros sobre la caché, a resolución de dispositivo.
void SpectrumLens::rasteriseCurves (float physScale)
{
    const int W = curveCache.deviceW(), H = curveCache.deviceH();
    if (W <= 1 || H <= 1) return;

    auto& img = curveCache.image();
    img.clear (img.getBounds());   // transparente: la rejilla vive DEBAJO, en la capa estática

    const int nSpec = spectrumNumSpectra (channelSeen);
    const juce::Colour hues[SpectrumFrame::kMaxSpectra] = { th::green, th::magenta };

    // El radio del suavizado: una fracción de octava sobre un eje log ES un ancho fijo en píxeles.
    const int denom = TelescopeProcessor::kSpectrumSmoothDenom[
                          juce::jlimit (0, TelescopeProcessor::kNumSpectrumSmoothOptions - 1,
                                        processor.spectrumSmoothIndex())];
    const double octaves = std::log2 (kMaxHz / kMinHz);
    const int smoothRadius = denom <= 0 ? 0
                           : juce::jmax (1, (int) std::lround ((double) W / (octaves * (double) denom) * 0.5));

    // Las tablas: 256 alphas del relleno y el perfil del trazo. Se arman por espectro (dos hues) y no por
    // píxel — que es de lo que vive el presupuesto de esta lente.
    std::array<juce::uint32, 256> fillLut {};
    std::array<juce::uint32, (size_t) kStrokeLutN> strokeLut {}, holdLut {};

    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
    if (bd.pixelStride != 4) return;

    for (int s = 0; s < nSpec; ++s)
    {
        buildDeviceCurve (s, W, physScale, smoothRadius);

        const auto hue = hues[s];
        // El relleno arranca en 0.42 CONTRA LA CURVA y se apaga hacia abajo con (1−d)^1.6 (57b). Con L+R
        // los dos rellenos se SUPERPONEN, y dos capas al 42 % dan un velo lechoso que se come el pozo:
        // ahí cada una lleva su parte, así que el velo combinado es el mismo que el de un espectro solo.
        const float fillPeak = 0.42f / (float) juce::jmax (1, nSpec);
        for (int i = 0; i < 256; ++i)
        {
            const float d = (float) i / 255.0f;
            fillLut[(size_t) i] = premul (hue, fillPeak * std::pow (1.0f - d, 1.6f));
        }
        for (int i = 0; i < kStrokeLutN; ++i)
        {
            const float d = (float) i / kStrokeLutStep;
            strokeLut[(size_t) i] = premul (hue, strokeProfile (d));
            holdLut[(size_t) i]   = premul (hue, 0.45f * juce::jlimit (0.0f, 1.0f, (1.0f - d) * 2.0f));
        }

        const float* yc = devY[s].data();

        // ---- RELLENO, por filas (ver el bloque de arriba) ----
        {
            float topY = (float) H;
            for (int x = 0; x < W; ++x) topY = juce::jmin (topY, yc[x]);
            const int y0 = juce::jlimit (0, H - 1, (int) std::floor (topY));

            // 255/(H − yc): la constante por columna que convierte "cuánto bajé" en índice de la tabla.
            devScratch.assign ((size_t) W, 0.0f);
            for (int x = 0; x < W; ++x)
                devScratch[(size_t) x] = 255.0f / juce::jmax (1.0f, (float) H - yc[x]);
            const float* kScale = devScratch.data();

            for (int y = y0; y < H; ++y)
            {
                auto* row = (juce::uint32*) bd.getLinePointer (y);
                const float fy = (float) y + 0.5f;
                if (s == 0)
                {
                    // El primer espectro cae sobre una imagen recién limpiada: es un store y nada más.
                    for (int x = 0; x < W; ++x)
                    {
                        const float d = fy - yc[x];
                        if (d < 0.0f) continue;
                        row[x] = fillLut[(size_t) juce::jlimit (0, 255, (int) (d * kScale[x]))];
                    }
                }
                else
                {
                    for (int x = 0; x < W; ++x)
                    {
                        const float d = fy - yc[x];
                        if (d < 0.0f) continue;
                        row[x] = overPremul (fillLut[(size_t) juce::jlimit (0, 255, (int) (d * kScale[x]))],
                                             row[x]);
                    }
                }
            }
        }

        // ---- TRAZO + GLOW, por columnas: el segmento entre y[x] e y[x+1] con cobertura exacta ----
        const float reach = (float) (kStrokeLutN - 1) / kStrokeLutStep;
        for (int x = 0; x < W; ++x)
        {
            const float a = yc[x], b = yc[juce::jmin (W - 1, x + 1)];
            const float lo = juce::jmin (a, b), hi = juce::jmax (a, b);
            const int   y0 = juce::jmax (0,     (int) std::floor (lo - reach));
            const int   y1 = juce::jmin (H - 1, (int) std::ceil  (hi + reach));
            for (int y = y0; y <= y1; ++y)
            {
                const float fy = (float) y + 0.5f;
                const float d  = juce::jmax (0.0f, juce::jmax (lo - fy, fy - hi));
                const int   i  = (int) (d * kStrokeLutStep);
                if (i >= kStrokeLutN) continue;
                auto* px = (juce::uint32*) bd.getLinePointer (y) + x;
                *px = overPremul (strokeLut[(size_t) i], *px);
            }
        }

        // ---- PEAK HOLD: línea fina antialiaseada, sin relleno ni glow. Es una referencia, no el dato ----
        if (sets.peakHold)
        {
            const float* yh = devHoldY[s].data();
            for (int x = 0; x < W; ++x)
            {
                const float a = yh[x], b = yh[juce::jmin (W - 1, x + 1)];
                const float lo = juce::jmin (a, b), hi = juce::jmax (a, b);
                const int   y0 = juce::jmax (0,     (int) std::floor (lo - 1.0f));
                const int   y1 = juce::jmin (H - 1, (int) std::ceil  (hi + 1.0f));
                for (int y = y0; y <= y1; ++y)
                {
                    const float fy = (float) y + 0.5f;
                    const float d  = juce::jmax (0.0f, juce::jmax (lo - fy, fy - hi));
                    const int   i  = (int) (d * kStrokeLutStep);
                    if (i >= kStrokeLutN) continue;
                    auto* px = (juce::uint32*) bd.getLinePointer (y) + x;
                    *px = overPremul (holdLut[(size_t) i], *px);
                }
            }
        }
    }
}

// ⅓ de octava y Bark: barras entre los bordes REALES de cada banda, así el ancho de la barra ES el ancho
// de la banda. Barras de ancho igual sobre un eje log serían un gráfico que miente sobre el eje.
//
// EL SLOPE NO SE APLICA ACÁ, y eso no es un olvido. El slope existe para compensar que un bin de FFT mide
// una franja de ancho FIJO (Δf = sr/N): sobre ruido rosa, cuya densidad cae 3 dB por octava, la curva
// cruda baja. Una banda de ⅓ de octava, en cambio, ya integra un ancho PROPORCIONAL a la frecuencia — o
// sea que ya hace por construcción lo que el slope hace a mano. Aplicar los dos inclinaría el rosa 3 dB
// por octava hacia ARRIBA, que es exactamente el error que este comentario existe para que nadie repita.
void SpectrumLens::paintBars (juce::Graphics& g, int slot, juce::Colour hue) const
{
    const auto& s = sets;
    const bool third = s.bandsMode == Spectrum::bandsThird;
    const int  n = third ? SpectrumFrame::kNumThird : SpectrumFrame::kNumBark;
    const int  nSpec = spectrumNumSpectra (channelSeen);

    constexpr double kSixthDown = 0.8908987181403393, kSixthUp = 1.1224620483093730;
    const double binHz = fftSizeSeen > 0 ? srSeen / (double) fftSizeSeen : 0.0;

    for (int b = 0; b < n; ++b)
    {
        const double lo = third ? kThirdOctaveHz[b] * kSixthDown : kBarkEdgesHz[b];
        const double hi = third ? kThirdOctaveHz[b] * kSixthUp   : kBarkEdgesHz[b + 1];
        if (hi <= kMinHz || lo >= kMaxHz) continue;

        const float db = third ? bandDb[slot][b] : barkDb[slot][b];
        const float xa = xForFreq (lo), xb = xForFreq (hi);
        const float y  = yForDb (db);

        // Con L+R las dos barras van LADO A LADO dentro de la banda: superpuestas, la segunda tapa a la
        // primera y el modo estéreo no mostraría nada del primer canal.
        const float full = juce::jmax (2.0f, xb - xa - 2.0f);
        const float w    = nSpec > 1 ? full * 0.5f : full;
        const float x    = xa + 1.0f + (nSpec > 1 ? (float) slot * w : 0.0f);

        // Banda SIN NINGÚN BIN adentro (con FFT de 4 096 a 48 k, la de 40 Hz mide 9.3 Hz y el bin 11.7).
        // No es que no haya energía: es que a esta resolución la banda no se puede medir. Se marca
        // distinto de una barra en cero — dibujar cero sería decir "no hay nada", que no es lo mismo.
        if (binHz > 0.0 && std::ceil (hi / binHz) <= std::ceil (lo / binHz))
        {
            g.setColour (th::fnt.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x, (float) zones.plot.getBottom() - 3.0f, w, 3.0f));
            continue;
        }

        g.setColour (hue.withAlpha (0.30f));
        g.fillRect (juce::Rectangle<float> (x, y, w, (float) zones.plot.getBottom() - y));
        g.setColour (hue.withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x, y, w, 1.5f));

        if (third && s.peakHold)
        {
            const float yh = yForDb (bandHold[slot][b]);
            g.setColour (hue.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x, yh, w, 1.0f));
        }
    }
}

//======================================================================================== lectura
SpectrumLens::Readout SpectrumLens::readoutAtX (int x) const
{
    Readout r;
    if (! zones.plot.contains (x, zones.plot.getCentreY())) return r;

    const int col = juce::jlimit (0, juce::jmax (0, zones.plot.getWidth() - 1), x - zones.plot.getX());
    if ((int) colDb[0].size() <= col) return r;

    const auto& s = sets;
    r.valid  = true;
    r.freqHz = std::sqrt (freqAtX (zones.plot.getX() + col) * freqAtX (zones.plot.getX() + col + 1));
    r.note   = noteFor (r.freqHz);

    if (s.bandsMode == Spectrum::bandsFft)
    {
        r.db = colDb[0][(size_t) col];
    }
    else
    {
        // En modo de bandas lo que hay bajo el cursor es una BANDA, no un bin: se reporta la banda.
        const bool third = s.bandsMode == Spectrum::bandsThird;
        const int  n = third ? SpectrumFrame::kNumThird : SpectrumFrame::kNumBark;
        constexpr double kSixthDown = 0.8908987181403393, kSixthUp = 1.1224620483093730;
        r.db = SpectrumFrame::kFloorDb;
        for (int b = 0; b < n; ++b)
        {
            const double lo = third ? kThirdOctaveHz[b] * kSixthDown : kBarkEdgesHz[b];
            const double hi = third ? kThirdOctaveHz[b] * kSixthUp   : kBarkEdgesHz[b + 1];
            if (r.freqHz >= lo && r.freqHz < hi)
            {
                r.db = third ? bandDb[0][b] : barkDb[0][b];   // sin slope: ver la nota de paintBars
                break;
            }
        }
    }
    return r;
}

void SpectrumLens::paintReadout (juce::Graphics& g) const
{
    if (cursorX < 0) return;
    const auto r = readoutAtX (cursorX);
    if (! r.valid) return;

    // ===== 56 ===== crosshair de verdad, no una línea vertical suelta: la vertical dice DÓNDE está el
    // cursor y la horizontal, con el punto, dice a QUÉ ALTURA cae el valor que dice el readout. Sin la
    // horizontal hay que estimar el nivel a ojo contra la rejilla, que es lo que el readout venía a
    // resolver. Snappeado a píxel físico (Look.h) para que la cruz no salga en gris sucio en Retina.
    look::drawCrosshair (g, zones.plot, cursorX, juce::roundToInt (yForDb (r.db)));

    const juce::String text = shortHz (r.freqHz) + " Hz  \xc2\xb7  " + r.note.name + "  "
                            + (r.note.cents >= 0 ? "+" : "") + juce::String (r.note.cents)
                            + juce::String::fromUTF8 (" \xc2\xa2") + "  \xc2\xb7  "
                            + juce::String (r.db, 1) + " dB";

    const auto m = look::metricsFor (getWidth());
    g.setFont (look::tabularFont (m.textSmall));
    const int tw = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16);
    // La caja se ACOTA al plot antes de ubicarse: con la lente angosta el texto puede ser más ancho que el
    // área y la cuenta directa la sacaba por el borde izquierdo (nit del revisor del 50). Ver LensReadout.h.
    // 56: la GEOMETRÍA sigue siendo la de LensReadout.h (única fuente de esa cuenta); el DIBUJO ahora sale
    // de Look.h, así las cinco lentes que tienen readout usan exactamente la misma cajita.
    look::drawReadoutBox (g, readoutBoxFor (zones.plot, cursorX, tw), text, m);
}

void SpectrumLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                                const juce::String& value, bool hovered_) const
{
    const auto hue = th::green;
    const auto r = area.toFloat();

    g.setColour (th::surf2);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (hue.withAlpha (hovered_ ? 0.55f : 0.24f));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    if (hovered_)
    {
        g.setColour (hue.withAlpha (th::state::hoverGlow));
        g.fillRoundedRectangle (r, 3.0f);
    }

    auto inner = area.reduced (7, 0);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.0f));
    g.drawText (label, inner.removeFromLeft (inner.getWidth() * 2 / 5), juce::Justification::centredLeft, false);
    g.setColour (hue);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (value, inner, juce::Justification::centredRight, false);
}

//======================================================================================== interacción
void SpectrumLens::cycleControl (int control)
{
    auto s = processor.spectrumSettings();
    switch (control)
    {
        case fftSize:
            s.fftOrder = s.fftOrder >= Spectrum::kMaxFftOrder ? Spectrum::kMinFftOrder : s.fftOrder + 1;
            break;
        case window:    s.window       = (s.window + 1) % Spectrum::kNumWindows; break;
        case overlap:   s.overlapIndex = (s.overlapIndex + 1) % Spectrum::kNumOverlaps; break;
        case channel:   s.channel      = (s.channel + 1) % Spectrum::kNumChannels; break;
        case bandsMode: s.bandsMode    = (s.bandsMode + 1) % Spectrum::kNumBandModes; break;
        case slope:
            s.slopeDbPerOct += Spectrum::kSlopeStep;
            if (s.slopeDbPerOct > Spectrum::kMaxSlopeDbPerOct + 1.0e-4f) s.slopeDbPerOct = Spectrum::kMinSlopeDbPerOct;
            break;
        case average:   s.avgMode      = (s.avgMode + 1) % Spectrum::kNumAverages; break;
        case hold:
            // OFF → 12 dB/s → 30 → ∞ (0) → OFF. Lo que un peak hold necesita elegir es cuánto TARDA en
            // olvidarse, y el infinito (que es lo que uno quiere para un barrido) tiene que estar.
            if (! s.peakHold)                        { s.peakHold = true;  s.holdDecayDbPerSec = 12.0f; }
            else if (s.holdDecayDbPerSec >= 30.0f)   { s.holdDecayDbPerSec = 0.0f; }
            else if (s.holdDecayDbPerSec <= 0.0f)    { s.peakHold = false; s.holdDecayDbPerSec = 12.0f; }
            else                                     { s.holdDecayDbPerSec = 30.0f; }
            break;
        case range:     s.rangeDbIndex = (s.rangeDbIndex + 1) % Spectrum::kNumRanges; break;
        // 57b — el suavizado es de DIBUJO: no va en los settings del espectro (no viaja al motor).
        case smooth:
            processor.setSpectrumSmoothIndex ((processor.spectrumSmoothIndex() + 1)
                                              % TelescopeProcessor::kNumSpectrumSmoothOptions);
            repaint();
            return;
        default: return;
    }
    processor.setSpectrumSettings (s);
    sets = processor.spectrumSettings();   // el módulo puede haber acotado algo: se cachea lo REAL
    invalidateStatic();                    // la rejilla depende del rango y de las etiquetas
    repaint();
}

void SpectrumLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void SpectrumLens::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int was = hovered, wasX = cursorX;

    hovered = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (p)) { hovered = i; break; }

    cursorX = zones.plot.contains (p) ? p.x : -1;
    if (hovered != was || cursorX != wasX) repaint();
}

void SpectrumLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursorX != -1) { hovered = -1; cursorX = -1; repaint(); }
}
}
