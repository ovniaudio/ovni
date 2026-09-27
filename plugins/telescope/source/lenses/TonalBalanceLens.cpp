#include "lenses/TonalBalanceLens.h"
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
namespace th = telescope::look::tint;   // F2: el tema vigente (Look.h)

constexpr double kSixthDown = 0.8908987181403393;   // 2^(-1/6)
constexpr double kSixthUp   = 1.1224620483093730;   // 2^(+1/6)

constexpr double kLabelledHz[] = { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 };

// Los formatos que el chooser OFRECE. No es la lista de lo que se puede leer (un archivo arrastrado se
// intenta igual, tenga la extensión que tenga): es lo que se muestra por default para no hacer buscar.
constexpr const char* kAudioFilter = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.caf;*.wma";

juce::String shortHz (double hz)
{
    if (hz >= 1000.0)
    {
        const double k = hz / 1000.0;
        return juce::String (k, k < 10.0 && std::abs (k - std::round (k)) > 0.01 ? 1 : 0) + "k";
    }
    return juce::String (hz, hz < 100.0 && std::abs (hz - std::round (hz)) > 0.01 ? 1 : 0);
}

juce::String signed1 (float v) { return juce::String (v > 0.0f ? "+" : "") + juce::String (v, 1); }

const juce::String kDot = juce::String::fromUTF8 ("  \xc2\xb7  ");

// F4 (T6): segundos → «m:ss», redondeado al segundo. Es lo que se lee en el rótulo de la tira.
juce::String mmss (double seconds)
{
    const auto t = (long long) std::llround (juce::jmax (0.0, seconds));
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

// Un arrastre de menos de estos píxeles es un clic, no un tramo (y el doble clic trae dos clics).
constexpr int kMinDragPx = 4;
}

const juce::ValueTree& TonalBalanceLens::stateTree() const { return processor.apvts.state; }

TonalBalanceLens::TonalBalanceLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    // settleHold 2 (antes 30): advanceFrame cuenta como cambio todo lo que se aparta del último cuadro
    // mostrado (ver ahí), así que no hace falta cola para no perderse el final de una animación.
    setSettleHold (2);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        dispLive[b] = dispRef[b] = kNormBottomDb;
        dispDelta[b] = 0.0f;
    }
}

TonalBalanceLens::~TonalBalanceLens() = default;

//======================================================================================== geometría
TonalBalanceLens::Zones TonalBalanceLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (150, (foot.getWidth() - gap * (kNumControls - 1)) / kNumControls);
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.head = body.removeFromTop (kHeadH);
    body.removeFromTop (6);

    // F4 (T6): la tira de la referencia, entre la cabecera y las curvas. Sólo con una referencia cargada:
    // sin referencia la lente queda como estaba.
    if (wantsStrip())
    {
        z.strip = body.removeFromTop (juce::jlimit (30, 46, h / 14));
        body.removeFromTop (8);
    }

    auto axis = body.removeFromBottom (kAxisH);

    // Las curvas se llevan más alto que el delta: son las que se miran primero, y son las que tienen que
    // poder leerse como un espectro.
    const int curvesH = juce::jmax (60, (int) std::lround (0.58 * (double) body.getHeight()));
    auto top = body.removeFromTop (curvesH);
    // padIn ENTERO entre los dos paneles (no la mitad): abajo del primero termina la etiqueta "-42" y
    // arriba del segundo empieza "+12.0"; con 8 px las dos se tocaban.
    body.removeFromTop (th::padIn);

    z.scaleCurves = top.removeFromLeft (kScaleW);
    z.curves      = top;
    z.scaleDelta  = body.removeFromLeft (kScaleW);
    z.delta       = body;

    z.freqAxis = axis.withLeft (z.curves.getX()).withRight (z.curves.getRight());
    return z;
}

void TonalBalanceLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    Lens::resized();
}

//======================================================================================== ejes
float TonalBalanceLens::xForFreq (double hz) const
{
    const double t = std::log (juce::jlimit (kMinHz, kMaxHz, hz) / kMinHz) / std::log (kMaxHz / kMinHz);
    return (float) zones.curves.getX() + (float) t * (float) zones.curves.getWidth();
}

double TonalBalanceLens::freqAtX (int x) const
{
    if (zones.curves.getWidth() <= 1) return kMinHz;
    const double t = juce::jlimit (0.0, 1.0,
                                   (double) (x - zones.curves.getX()) / (double) zones.curves.getWidth());
    return kMinHz * std::pow (kMaxHz / kMinHz, t);
}

int TonalBalanceLens::bandAtX (int x) const
{
    const double hz = freqAtX (x);
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        if (hz >= kThirdOctaveHz[b] * kSixthDown && hz < kThirdOctaveHz[b] * kSixthUp) return b;
    return -1;
}

float TonalBalanceLens::yForNorm (float db) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (kNormTopDb - db) / (kNormTopDb - kNormBottomDb));
    return (float) zones.curves.getY() + t * (float) zones.curves.getHeight();
}

float TonalBalanceLens::yForDelta (float db) const
{
    const float t = juce::jlimit (0.0f, 1.0f, (kDeltaSpanDb - db) / (2.0f * kDeltaSpanDb));
    return (float) zones.delta.getY() + t * (float) zones.delta.getHeight();
}

//======================================================================================== capa estática
void TonalBalanceLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);

    const auto panel = [&] (juce::Rectangle<int> r)
    {
        g.setColour (th::surf.withAlpha (0.65f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (look::gridMinor);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    };
    panel (zones.curves);
    panel (zones.delta);

    // ---- rejilla de frecuencia: los 30 centros de ⅓ de octava, las décadas más marcadas ----
    for (const double hz : kThirdOctaveHz)
    {
        if (hz < kMinHz || hz > kMaxHz) continue;
        const bool decade = std::abs (std::log10 (hz) - std::round (std::log10 (hz))) < 1.0e-6;
        g.setColour (decade ? th::line : th::lineSoft);
        const int x = juce::roundToInt (xForFreq (hz));
        look::fillSnapped (g, { (float) (x), (float) (zones.curves.getY()), 1.0f, (float) (zones.curves.getHeight()) });
        look::fillSnapped (g, { (float) (x), (float) (zones.delta.getY()), 1.0f, (float) (zones.delta.getHeight()) });
    }

    g.setFont (look::mono (9.0f));
    g.setColour (look::txtTertiary);
    for (const double hz : kLabelledHz)
        g.drawText (shortHz (hz), juce::roundToInt (xForFreq (hz)) - 20, zones.freqAxis.getY() + 1, 40, 12,
                    juce::Justification::centred, true);

    // ---- escala de las curvas: dB RELATIVOS a la loudness, cada 10 dB, escala FIJA ----
    for (float db = kNormTopDb; db >= kNormBottomDb; db -= 6.0f)
    {
        const int y = juce::roundToInt (yForNorm (db));
        g.setColour (look::gridMinor);
        look::fillSnapped (g, { (float) (zones.curves.getX()), (float) (y), (float) (zones.curves.getWidth()), 1.0f });
        g.setColour (look::txtTertiary);
        g.setFont (look::mono (9.0f));
        g.drawText (juce::String ((int) db), zones.scaleCurves.getX(), y - 6, kScaleW - 8, 12,
                    juce::Justification::centredRight, true);
    }

    g.setColour (th::mut);
    g.setFont (look::label (10.0f));
    g.drawText ("dB / LU", zones.scaleCurves.getX(), zones.curves.getY() + 8, kScaleW - 8, 12,
                juce::Justification::centredRight, true);

    // ---- la banda de referencia de ±3 dB, ROTULADA. No es un veredicto: es una regla puesta a la vista
    //      para que el ojo tenga contra qué medir el largo de las barras.
    const auto refBand = juce::Rectangle<float> ((float) zones.delta.getX(), yForDelta (kDeltaRefDb),
                                                 (float) zones.delta.getWidth(),
                                                 yForDelta (-kDeltaRefDb) - yForDelta (kDeltaRefDb));
    g.setColour (th::green.withAlpha (0.07f));
    g.fillRect (refBand);
    g.setColour (th::green.withAlpha (0.28f));
    g.drawHorizontalLine (juce::roundToInt (yForDelta (kDeltaRefDb)), refBand.getX(), refBand.getRight());
    g.drawHorizontalLine (juce::roundToInt (yForDelta (-kDeltaRefDb)), refBand.getX(), refBand.getRight());

    g.setColour (th::mut);
    g.setFont (look::label (9.0f));
    g.drawText (tr (strings::Key::refPlusMinus) + juce::String (kDeltaRefDb, 0) + " dB", zones.delta.getX() + 6,
                juce::roundToInt (yForDelta (kDeltaRefDb)) + 2, 90, 12, juce::Justification::centredLeft, true);

    // ---- escala del delta ----
    for (const float db : { kDeltaSpanDb, kDeltaSpanDb * 0.5f, 0.0f, -kDeltaSpanDb * 0.5f, -kDeltaSpanDb })
    {
        const int y = juce::roundToInt (yForDelta (db));
        g.setColour (db == 0.0f ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (zones.delta.getX()), (float) (y), (float) (zones.delta.getWidth()), 1.0f });
        g.setColour (look::txtTertiary);
        g.setFont (look::mono (9.0f));
        g.drawText (signed1 (db), zones.scaleDelta.getX(), y - 6, kScaleW - 8, 12,
                    juce::Justification::centredRight, true);
    }

    g.setColour (th::mut);
    g.setFont (look::label (10.0f));
    g.drawText (tr (strings::Key::delta), zones.scaleDelta.getX(), zones.delta.getY() + 8, kScaleW - 8, 12,
                juce::Justification::centredRight, true);

    if (! zones.strip.isEmpty()) paintStripWave (g);   // F4 (T6)
}

//======================================================================================== datos
bool TonalBalanceLens::advanceFrame()
{
    data = processor.reference().read();

    // ---- F4 (T6): la tira aparece y desaparece con la referencia, y su onda llega de otro hilo ----
    bool stripMoved = false;
    {
        const bool strip = wantsStrip();
        waveform.load (strip ? juce::File (processor.referencePath()) : juce::File());
        if (strip != shownStrip)
        {
            shownStrip = strip;
            zones = zonesFor (getWidth(), getHeight());
            invalidateStatic();
            stripMoved = true;
        }
        if (waveform.revision() != shownWaveRev)
        {
            shownWaveRev = waveform.revision();
            invalidateStatic();
            stripMoved = true;
        }
        const auto span = processor.referenceSpan();
        if (span.whole != shownSpan.whole || span.fromS != shownSpan.fromS || span.toS != shownSpan.toS)
        {
            shownSpan = span;
            stripMoved = true;
        }
    }

    // Los bins por banda salen de la geometría de la FFT vigente: es lo que le permite a la lectura decir
    // "esta banda la mide menos de un bin" en vez de dar una diferencia sin contexto.
    //
    // 57c — POR SOLAPAMIENTO, FRACCIONARIO, con la MISMA cuenta que el motor (ver
    // analysis/FileAnalysis.h): el bin k cubre [(k−½)·binHz, (k+½)·binHz) y aporta los hercios que
    // comparte con la banda. Antes se contaban bins enteros con dos `ceil`, y por eso la banda de 40 Hz
    // daba CERO a 4 096/48 k (0.79 bins caían entre los dos redondeos) y la lente la declaraba sin
    // medición: el agujero de la foto de Joaquín.
    const auto& sp = processor.spectrum().read();
    const double binHz = sp.binHz();
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        if (binHz <= 0.0 || sp.numBins <= 0) { bins[b] = 0.0f; continue; }
        const double lo = kThirdOctaveHz[b] * kSixthDown, hi = kThirdOctaveHz[b] * kSixthUp;
        const int k0 = juce::jmax (0, (int) std::floor (lo / binHz) - 1);
        const int k1 = juce::jmin (sp.numBins - 1, (int) std::ceil (hi / binHz) + 1);
        double covered = 0.0;
        for (int k = k0; k <= k1; ++k)
            covered += juce::jmax (0.0, juce::jmin (hi, ((double) k + 0.5) * binHz)
                                            - juce::jmax (lo, ((double) k - 0.5) * binHz));
        bins[b] = (float) (covered / binHz);
    }

    const bool reduced = prefersReducedMotion();
    bool moved = false;

    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        // ========================================================================================================
        // ===== 57c · QUÉ SE PUEDE DIBUJAR =====
        //
        // «Se corta la línea» era DOS defectos con la misma foto, y éste es el segundo. La referencia de
        // Joaquín (un pad) tiene ruido entre −90 y −63 dBFS fuera de 90–350 Hz: por el criterio del 57b
        // eso es "medible" (crudo ≥ −90 dBFS), así que la lente lo dibujaba… en su normalizada de −62 LU,
        // veinte decibeles POR DEBAJO del piso del plot. `yForNorm` lo clampeaba al borde y salía una raya
        // vertical hasta el piso, una barra de delta clavada en ±12 y un readout con |delta| de 60.
        //
        // El criterio ahora tiene las DOS mitades: la banda se dibuja si el crudo es medible (≥ −90 dBFS,
        // que es lo que dice `norm > kFloorDb` desde el 57b) Y si su normalizada entra en el plot
        // (> −42 LU). Lo que no entra CORTA la curva, que es lo honesto: no se puede dibujar un valor
        // fuera de la escala sin mentir sobre dónde está.
        //
        // `> kNormBottomDb` y no `≥`: un valor exactamente en el piso no se distingue de uno que se salió,
        // y con el `>` el contrato "ningún vértice en la fila del piso" es exacto en vez de casi (lo
        // verifica TONAL[piso]).
        //
        // Se fue, en cambio, la exigencia de `bins[b] > 0` para el lado vivo: con el solapamiento
        // fraccionario ninguna banda por debajo de Nyquist queda sin cubrir, y lo que antes era "a esta
        // resolución no hay medición" pasó a ser "esta banda la mide 0.8 de un bin", que la lectura dice.
        // ========================================================================================================
        liveHas[b] = data.liveValid && data.liveNorm[b] > SpectrumFrame::kFloorDb
                                    && data.liveNorm[b] > kNormBottomDb;
        refHas[b]  = data.refValid  && data.refNorm[b]  > SpectrumFrame::kFloorDb
                                    && data.refNorm[b]  > kNormBottomDb;
        // COMPARABLE es lo del motor Y que los dos lados se puedan dibujar: un delta contra un valor que
        // no entra en el plot es el +106 dB de la foto del 57b con otra cara.
        comparable[b] = data.bandValid[b] && liveHas[b] && refHas[b];

        const float targetLive  = liveHas[b] ? data.liveNorm[b] : kNormBottomDb;
        const float targetRef   = refHas[b]  ? data.refNorm[b]  : kNormBottomDb;
        const float targetDelta = comparable[b] ? data.deltaDb[b] : 0.0f;

        const float wasLive = dispLive[b], wasRef = dispRef[b], wasDelta = dispDelta[b];
        // El PRIMER frame entra de una: un medidor que trepa desde el piso al abrirse miente durante un
        // segundo. Lo que se anima es el CAMBIO, no la llegada (misma regla que el resto de las lentes).
        const bool snap = reduced || ! primed;
        dispLive[b]  = snap ? targetLive  : wasLive  + (targetLive  - wasLive)  * kRelease;
        dispRef[b]   = snap ? targetRef   : wasRef   + (targetRef   - wasRef)   * kRelease;
        dispDelta[b] = snap ? targetDelta : wasDelta + (targetDelta - wasDelta) * kRelease;

        juce::ignoreUnused (wasLive, wasRef, wasDelta);
        moved = moved || liveHas[b] != shownLiveHas[b] || refHas[b] != shownRefHas[b]
                      || comparable[b] != shownComparable[b]
                      || std::abs (dispLive[b]  - shownLive[b])  > kShownEpsDb
                      || std::abs (dispRef[b]   - shownRef[b])   > kShownEpsDb
                      || std::abs (dispDelta[b] - shownDelta[b]) > kShownEpsDb;
    }

    // CAMBIO = LO QUE SE APARTA DEL ÚLTIMO CUADRO MOSTRADO, no del cuadro anterior (prompt 96). Hasta la F4 de
    // la 0.2, en silencio la curva viva bajaba de verdad y sin parar —el promedio de bandas es desde el reset y
    // el silencio lo diluía, mientras la integrada con compuerta no baja: 4.3/N dB por segundo con N segundos
    // acumulados—, y con el criterio viejo (0.001 dB entre cuadros, cola de 30) eso era repintar 30 veces por
    // segundo para siempre. Desde T4 (D-113) los cuadros bajo −70 LUFS no entran al promedio (Reference.h) y
    // la curva se queda quieta; el criterio sigue igual de bueno para cualquier otro movimiento lento.
    // Contra el último cuadro mostrado el error de lo que queda en pantalla está acotado a kShownEpsDb, y la
    // lente repinta cuando la curva se movió eso, no en cada cuadro.
    if (moved)
        for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        {
            shownLive[b]  = dispLive[b];  shownRef[b] = dispRef[b];  shownDelta[b] = dispDelta[b];
            shownLiveHas[b] = liveHas[b]; shownRefHas[b] = refHas[b]; shownComparable[b] = comparable[b];
        }

    primed = true;
    // Mientras se analiza, la barra de progreso se mueve: hay que seguir repintando aunque el dato no cambie.
    return moved || stripMoved || processor.referenceBusy() || waveform.busy();
}

//======================================================================================== capa viva
void TonalBalanceLens::paintLive (juce::Graphics& g)
{
    if (zones.curves.isEmpty()) zones = zonesFor (getWidth(), getHeight());

    paintHead (g);
    if (! zones.strip.isEmpty()) paintStripLive (g);   // F4 (T6)

    paintCurve (g, dispRef,  refHas,  th::magenta);   // la referencia primero: el programa va encima
    paintCurve (g, dispLive, liveHas, th::green);
    paintDeltaBars (g);
    paintReadout (g);

    // Marco al arrastrar un archivo encima: el usuario tiene que ver DÓNDE va a soltar.
    if (dragOver)
    {
        g.setColour (th::green.withAlpha (0.10f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f);
        g.setColour (th::green.withAlpha (0.7f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 4.0f, 2.0f);
    }

    paintButton (g, zones.button[ctrlReset], tr (strings::Key::reset),  hovered == ctrlReset);
    paintButton (g, zones.button[ctrlLoad],  tr (strings::Key::load),   hovered == ctrlLoad);
    paintButton (g, zones.button[ctrlClear], tr (strings::Key::remove), hovered == ctrlClear);
}

// ========================================================================================================
// LA CABECERA: el estado en una línea, la leyenda de las dos curvas con su integrado, y la barra de
// progreso mientras se analiza. Es donde se contesta "¿qué estoy mirando?" sin tener que adivinar.
// ========================================================================================================
void TonalBalanceLens::paintHead (juce::Graphics& g) const
{
    auto head = zones.head;
    auto line1 = head.removeFromTop (16);
    auto line2 = head;

    // --- línea 1: estado ---
    const bool problem = processor.referenceMissingName().isNotEmpty()
                      || (! processor.referenceBusy() && processor.referenceError().isNotEmpty());
    g.setColour (problem ? th::amber : th::txt);
    g.setFont (look::mono (11.0f));
    g.drawText (stateText(), line1, juce::Justification::centredLeft, true);

    // Barra de progreso: sólo mientras se analiza, y pegada a la derecha de la línea de estado.
    if (processor.referenceBusy())
    {
        auto bar = line1.removeFromRight (juce::jmin (160, line1.getWidth() / 3)).reduced (0, 5);
        g.setColour (th::surf2);
        g.fillRoundedRectangle (bar.toFloat(), 2.0f);
        g.setColour (th::green.withAlpha (0.8f));
        g.fillRoundedRectangle (bar.toFloat().withWidth ((float) bar.getWidth()
                                                         * juce::jlimit (0.0f, 1.0f, processor.referenceProgress())),
                                2.0f);
    }

    // --- línea 2: la leyenda de las dos curvas ---
    g.setFont (look::label (10.0f));

    const juce::String liveLabel = tr (strings::Key::program)
        + (data.liveValid ? (kDot + juce::String (data.liveIntegrated, 1) + " LUFS")
                          : (kDot + tr (strings::Key::measuring) + " " + juce::String (data.liveSeconds, 1)
                             + " / " + juce::String (ReferenceFrame::kMinLiveSeconds, 1) + " s"));

    auto left = line2.removeFromLeft (line2.getWidth() / 2);
    g.setColour (th::green);
    g.fillRect (left.removeFromLeft (10).withSizeKeepingCentre (10, 2));
    left.removeFromLeft (4);
    g.setColour (data.liveValid ? th::txt : th::mut);
    g.drawText (liveLabel, left, juce::Justification::centredLeft, true);

    if (data.refValid)
    {
        g.setColour (th::magenta);
        g.fillRect (line2.removeFromLeft (10).withSizeKeepingCentre (10, 2));
        line2.removeFromLeft (4);
        // F4 (T6): con un tramo, los segundos son los del TRAMO (lo que se midió), no los del archivo.
        const auto span = processor.referenceSpan();
        const double refS = span.whole ? (double) data.refSeconds : span.toS - span.fromS;
        g.setColour (th::txt);
        g.drawText (juce::String (data.refName) + kDot + juce::String (data.refIntegrated, 1) + " LUFS"
                        + kDot + juce::String (refS, 1) + " s",
                    line2, juce::Justification::centredLeft, true);
    }
}

juce::String TonalBalanceLens::stateText() const
{
    if (processor.referenceBusy())
        return tr (strings::Key::analysing) + " " + processor.referenceName() + kDot
             + juce::String (juce::roundToInt (100.0f * processor.referenceProgress())) + " %";

    const auto missing = processor.referenceMissingName();
    if (missing.isNotEmpty())
        return tr (strings::Key::referenceMissing) + missing;

    const auto err = processor.referenceError();
    if (err.isNotEmpty())
        return processor.referenceName() + kDot + err;

    if (! data.refValid)
        return tr (strings::Key::noReferenceDrag);

    // 56b (LOW 3 del 54): el aviso NO reemplaza al estado, CONVIVE con él. Un aviso del tipo "la
    // referencia es mono" es una salvedad sobre lo que se está comparando, no un motivo para dejar de
    // decir QUÉ se está comparando — y tapando la frase normal, el usuario perdía la única línea que
    // explica que el delta suma cero.
    const auto warn = processor.referenceWarning();
    const auto state = tr (strings::Key::tiltEqualLoudness);
    return warn.isNotEmpty() ? (warn + kDot + state) : state;
}

// ========================================================================================================
// LAS CURVAS. Un vértice por BANDA (30), no por píxel: acá no hay 16 000 bins que resumir — lo que se
// dibuja es la rejilla de ⅓ de octava, y ponerle más vértices sería inventar resolución que el dato no
// tiene. Las bandas sin medición CORTAN la curva en vez de bajarla al piso: una curva que se desploma
// donde no hay datos dibuja un agujero que no existe.
// ========================================================================================================
void TonalBalanceLens::paintCurve (juce::Graphics& g, const float* norm, const bool* has,
                                   juce::Colour hue) const
{
    g.saveState();
    g.reduceClipRegion (zones.curves);

    juce::Path p;
    bool open = false;
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        if (! has[b]) { open = false; continue; }
        const float x = xForFreq (kThirdOctaveHz[b]);
        const float y = yForNorm (norm[b]);
        if (! open) { p.startNewSubPath (x, y); open = true; }
        else        p.lineTo (x, y);
    }

    // 57b — glow barato en las dos curvas: son dos líneas del mismo grosor cruzándose sobre una rejilla,
    // y el glow es lo que deja seguir cada una sin tener que buscarle el color.
    const auto m = look::metricsFor (getWidth());
    look::glowPath (g, p, hue, m.dataW, m.glowRadius * 0.7f);
    g.setColour (hue.withAlpha (0.92f));
    g.strokePath (p, juce::PathStrokeType (m.dataW, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // Un punto por banda medida: deja ver DÓNDE están los datos de verdad.
    g.setColour (hue.withAlpha (0.55f));
    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
        if (has[b])
            g.fillEllipse (xForFreq (kThirdOctaveHz[b]) - 1.5f, yForNorm (norm[b]) - 1.5f, 3.0f, 3.0f);

    g.restoreState();
}

// Las barras van entre los bordes REALES de cada banda (fc·2^∓1/6): sobre un eje logarítmico el ancho de
// la barra ES el ancho de la banda. Es la misma regla que BAND CORRELATION y que las barras de SPECTRUM.
void TonalBalanceLens::paintDeltaBars (juce::Graphics& g) const
{
    g.saveState();
    g.reduceClipRegion (zones.delta);

    const float base = yForDelta (0.0f);

    for (int b = 0; b < ReferenceFrame::kNumBands; ++b)
    {
        const double lo = kThirdOctaveHz[b] * kSixthDown, hi = kThirdOctaveHz[b] * kSixthUp;
        if (hi <= kMinHz || lo >= kMaxHz) continue;

        const float xa = xForFreq (lo), xb = xForFreq (hi);
        const float x  = xa + 1.0f;
        const float w  = juce::jmax (2.0f, xb - xa - 2.0f);

        // BANDA NO COMPARABLE (uno de los dos lados no la midió): se marca, no se dibuja en cero. Cero
        // sería decir "coincide perfecto", que es la mentira más fácil de dibujar de este gráfico.
        if (! comparable[b])
        {
            g.setColour (look::tick.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (x, base - 1.0f, w, 2.0f));
            continue;
        }

        const float v = dispDelta[b];
        const float y = yForDelta (v);
        const float top = juce::jmin (y, base), bot = juce::jmax (y, base);
        const auto hue = std::abs (v) > kDeltaRefDb ? th::amber : th::green;

        // 57b — degradado desde la punta hacia el cero, igual que BAND CORRELATION: las dos lentes
        // dibujan barras bipolares sobre una línea de cero y tienen que verse de la misma familia.
        g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.46f), 0.0f, y,
                                                 hue.withAlpha (0.06f), 0.0f, base, false));
        g.fillRect (juce::Rectangle<float> (x, top, w, juce::jmax (1.0f, bot - top)));
        g.setColour (hue.withAlpha (0.92f));
        g.fillRect (juce::Rectangle<float> (x, y - 0.75f, w, 1.5f));
    }

    g.restoreState();
}

//======================================================================================== lectura
TonalBalanceLens::Readout TonalBalanceLens::readoutAt (juce::Point<int> p) const
{
    if (! zones.curves.contains (p) && ! zones.delta.contains (p)) return {};
    return readoutForBand (bandAtX (p.x));
}

TonalBalanceLens::Readout TonalBalanceLens::readoutForBand (int b) const
{
    Readout r;
    if (b < 0 || b >= ReferenceFrame::kNumBands) return r;

    r.valid      = true;
    r.band       = b;
    r.centreHz   = kThirdOctaveHz[b];
    r.bins       = bins[b];
    r.comparable = comparable[b];
    r.liveDrawable = liveHas[b];
    r.refDrawable  = refHas[b];
    r.liveNorm   = data.liveNorm[b];
    r.refNorm    = data.refNorm[b];
    // EL DELTA SIGUE A LA COMPARABILIDAD DE LA LENTE. El motor lo calcula en cuanto los dos lados son
    // medibles; la lente además exige que los dos ENTREN EN EL PLOT (57c), y un delta contra un valor que
    // no se puede dibujar es el +106 dB del 57b con otra cara. Donde no se compara, no hay delta — cero,
    // y el readout dice por qué.
    r.deltaDb    = comparable[b] ? data.deltaDb[b] : 0.0f;
    return r;
}

void TonalBalanceLens::paintReadout (juce::Graphics& g) const
{
    if (cursor.x < 0) return;
    const auto r = readoutAt (cursor);
    if (! r.valid) return;

    const float xa = xForFreq (r.centreHz * kSixthDown), xb = xForFreq (r.centreHz * kSixthUp);
    g.setColour (th::txt.withAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (xa, (float) zones.curves.getY(), xb - xa,
                                        (float) zones.curves.getHeight()));
    g.fillRect (juce::Rectangle<float> (xa, (float) zones.delta.getY(), xb - xa,
                                        (float) zones.delta.getHeight()));

    // 57c — la cuenta de bins es FRACCIONARIA y se dice así: por debajo de 1 la banda no es una medición
    // independiente, es la densidad del bin donde cae, y el número lo tiene que mostrar.
    const juce::String binsText = (r.bins < 10.0f ? juce::String (r.bins, 1)
                                                  : juce::String (juce::roundToInt (r.bins)))
                                + tr (strings::Key::binsAtThisFft);

    juce::String text = shortHz (r.centreHz) + " Hz" + kDot;
    if (r.bins <= 0.0f)
        text += tr (strings::Key::noBinsHere);
    else if (data.refValid && ! r.refDrawable && data.refNorm[r.band] > SpectrumFrame::kFloorDb)
        // La referencia SE MIDIÓ, pero cae fuera del plot. Decir "sin referencia en esta banda" sería
        // esconder que hay un número y que está veinte decibeles abajo.
        text += tr (strings::Key::refBelowRange) + kDot + binsText;
    else if (! r.comparable)
        text += tr (strings::Key::noReferenceInBand) + kDot + binsText;
    else
        text += trLower (strings::Key::program) + " " + signed1 (r.liveNorm) + kDot
              + trLower (strings::Key::referenceLabel) + " " + signed1 (r.refNorm) + kDot
              + trLower (strings::Key::delta) + " " + signed1 (r.deltaDb) + " dB" + kDot
              + binsText;

    g.setFont (look::mono (11.0f));
    const int tw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16;
    const auto box = readoutBoxFor (zones.curves, cursor.x, tw);

    g.setColour (th::bg1.withAlpha (0.9f));
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (th::green.withAlpha (0.4f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (th::txt);
    g.drawText (text, box, juce::Justification::centred, true);
}

void TonalBalanceLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                                    bool hovered_) const
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

    g.setColour (hue);
    g.setFont (look::label (10.0f));
    g.drawText (label, area, juce::Justification::centred, true);
}

//======================================================================================== interacción
// El chooser va ASÍNCRONO: nada modal adentro de un plugin (un diálogo modal dentro del message thread de
// un DAW es la manera más rápida de colgarle la sesión a alguien). Y por eso mismo los tests cargan por
// API, no fabricando diálogos.
void TonalBalanceLens::pressControl (int control)
{
    if (control == ctrlReset)
    {
        processor.resetAnalysis();
        primed = false;   // el promedio arranca de cero: la curva no tiene que "bajar" hasta ahí animando
    }
    else if (control == ctrlLoad)
    {
        chooser = std::make_unique<juce::FileChooser> (
            tr (strings::Key::chooseReference), juce::File(), kAudioFilter);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<TonalBalanceLens> (this)]
                              (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) safe->processor.loadReference (f);
                              });
    }
    else if (control == ctrlClear)
    {
        processor.clearReference();
    }
    else return;

    repaint();
}

void TonalBalanceLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { pressControl (i); return; }
    if (zones.strip.contains (e.getPosition())) stripBegin (e.getPosition().x);   // F4 (T6)
}

void TonalBalanceLens::mouseDrag (const juce::MouseEvent& e)        { if (dragging) stripDrag (e.getPosition().x); }
void TonalBalanceLens::mouseUp (const juce::MouseEvent& e)          { if (dragging) stripEnd (e.getPosition().x); }
void TonalBalanceLens::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (zones.strip.contains (e.getPosition())) stripDoubleClick();
}

// ========================================================================================================
// F4 (T6) · LA TIRA. La lente no mide: pide. Arrastrar deja el tramo en el estado del processor, y el processor
// lo mide en el hilo del analizador de archivo con RangeAnalyzer::measure (la función de telescope-measure).
// ========================================================================================================
bool TonalBalanceLens::wantsStrip() const
{
    return processor.referencePath().isNotEmpty() && processor.referenceMissingName().isEmpty();
}

bool TonalBalanceLens::waveformReady() const
{
    const auto o = waveform.latest();
    return o != nullptr && o->ok() && o->path == processor.referencePath();
}

double TonalBalanceLens::stripSeconds() const
{
    const auto o = waveform.latest();
    if (o != nullptr && o->ok() && o->path == processor.referencePath()) return o->seconds;
    return data.refValid ? (double) data.refSeconds : 0.0;
}

double TonalBalanceLens::secondsAtX (int x) const
{
    const auto r = zones.strip;
    if (r.getWidth() <= 1) return 0.0;
    const double t = juce::jlimit (0.0, 1.0, (double) (x - r.getX()) / (double) r.getWidth());
    return t * stripSeconds();
}

int TonalBalanceLens::xAtSeconds (double sec) const
{
    const auto r = zones.strip;
    const double total = stripSeconds();
    if (total <= 0.0) return r.getX();
    return r.getX() + juce::roundToInt (juce::jlimit (0.0, 1.0, sec / total) * (double) r.getWidth());
}

void TonalBalanceLens::stripBegin (int x)
{
    if (stripSeconds() <= 0.0) return;
    dragging  = true;
    dragFromS = dragToS = secondsAtX (x);
    repaint();
}

void TonalBalanceLens::stripDrag (int x)
{
    if (! dragging) return;
    dragToS = secondsAtX (x);
    repaint();
}

void TonalBalanceLens::stripEnd (int x)
{
    if (! dragging) return;
    dragging = false;
    dragToS  = secondsAtX (x);
    if (std::abs (xAtSeconds (dragToS) - xAtSeconds (dragFromS)) >= kMinDragPx)
        processor.setReferenceRange (juce::jmin (dragFromS, dragToS), juce::jmax (dragFromS, dragToS));
    repaint();
}

void TonalBalanceLens::stripDoubleClick()
{
    dragging = false;
    processor.clearReferenceRange();
    repaint();
}

juce::String TonalBalanceLens::stripLabel() const
{
    const double total = stripSeconds();
    double from = 0.0, to = total;
    if (dragging)                                    { from = juce::jmin (dragFromS, dragToS); to = juce::jmax (dragFromS, dragToS); }
    else if (processor.referenceRange (from, to))    { from = juce::jlimit (0.0, total, from); to = juce::jlimit (0.0, total, to); }
    else                                             { from = 0.0; to = total; }
    return "ref" + juce::String::fromUTF8 (" \xc2\xb7 ") + mmss (from) + juce::String::fromUTF8 ("\xe2\x80\x93") + mmss (to);
}

juce::String TonalBalanceLens::stripHint() const
{
    // La ayuda entra entera o no se dibuja: un texto cortado con «…» en una tira de 30 px no ayuda a nadie.
    const auto hint  = tr (strings::Key::refSpanHint);
    const auto font  = look::label (11.0f);
    const int  labW  = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, stripLabel())) + 12;
    const int  hintW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, hint)) + 12;
    return (labW + hintW + 16 <= zones.strip.getWidth()) ? hint : juce::String();
}

// La pantalla y la onda: capa estática (se rehornea cuando llega otra onda o cambia el tamaño).
void TonalBalanceLens::paintStripWave (juce::Graphics& g) const
{
    const auto r = zones.strip;
    look::drawScreenEdge (g, r);             // D-109: en claro, el filo oscuro de la pantalla
    const look::ScreenInk screen;            // y adentro, la tinta del oscuro en los dos temas
    g.setColour (th::bg1);
    g.fillRect (r);
    g.setColour (look::gridMinor);
    g.drawRect (r, 1);

    const auto o = waveform.latest();
    if (o == nullptr || ! o->ok() || o->path != processor.referencePath()) return;

    const auto area = r.reduced (2, 3).toFloat();
    const float mid = area.getCentreY(), half = 0.5f * area.getHeight();
    g.setColour (look::gridMinor);
    g.fillRect (juce::Rectangle<float> (area.getX(), mid - 0.25f, area.getWidth(), 0.5f));

    // Normalizada a su PICO: es un dibujo de la forma, no un medidor (el nivel lo dice la cabecera en LUFS), y
    // una referencia bajita se vería como una raya. Con un piso, para que el silencio no se infle hasta el borde.
    float peak = 0.0f;
    for (int b = 0; b < ReferenceWaveform::kBuckets; ++b)
        peak = juce::jmax (peak, std::abs (o->lo[(size_t) b]), std::abs (o->hi[(size_t) b]));
    const float gain = 1.0f / juce::jmax (0.05f, peak);

    // Media columna lógica por paso: a 2× es un píxel físico por columna.
    const int cols = juce::jmax (1, (int) std::ceil (area.getWidth() * 2.0f));
    const float w  = area.getWidth() / (float) cols;
    g.setColour (th::magenta.withAlpha (0.80f));
    for (int c = 0; c < cols; ++c)
    {
        const int b0 = (int) ((long long) c * ReferenceWaveform::kBuckets / cols);
        const int b1 = juce::jmax (b0 + 1, (int) ((long long) (c + 1) * ReferenceWaveform::kBuckets / cols));
        float lo = 1.0f, hi = -1.0f;
        for (int b = b0; b < b1 && b < ReferenceWaveform::kBuckets; ++b)
        {
            lo = juce::jmin (lo, o->lo[(size_t) b]);
            hi = juce::jmax (hi, o->hi[(size_t) b]);
        }
        if (hi < lo) continue;
        lo = juce::jlimit (-1.0f, 1.0f, lo * gain);
        hi = juce::jlimit (-1.0f, 1.0f, hi * gain);
        const float y0 = mid - hi * half, y1 = mid - lo * half;
        g.fillRect (juce::Rectangle<float> (area.getX() + (float) c * w, y0, w, juce::jmax (0.5f, y1 - y0)));
    }
}

// El tramo, sus bordes y el rótulo: capa viva (cambian al arrastrar).
void TonalBalanceLens::paintStripLive (juce::Graphics& g) const
{
    const auto r = zones.strip;
    const look::ScreenInk screen;   // D-109: adentro de la pantalla, la tinta del oscuro

    const double total = stripSeconds();
    double from = 0.0, to = total;
    bool whole = true;
    if (dragging)                                    { from = juce::jmin (dragFromS, dragToS); to = juce::jmax (dragFromS, dragToS); whole = false; }
    else if (processor.referenceRange (from, to))    { whole = false; }

    if (! whole && total > 0.0)
    {
        const int x0 = xAtSeconds (from), x1 = xAtSeconds (to);
        g.setColour (th::bg0.withAlpha (0.62f));   // lo que queda afuera del tramo, apagado
        g.fillRect (juce::Rectangle<int> (r.getX(), r.getY(), x0 - r.getX(), r.getHeight()));
        g.fillRect (juce::Rectangle<int> (x1, r.getY(), r.getRight() - x1, r.getHeight()));
        g.setColour (look::accent);
        look::fillSnapped (g, { (float) x0, (float) r.getY(), 1.0f, (float) r.getHeight() });
        look::fillSnapped (g, { (float) (x1 - 1), (float) r.getY(), 1.0f, (float) r.getHeight() });
    }

    // El rótulo, arriba a la izquierda, sobre su propia placa: se lee igual sobre la onda que sobre el vacío.
    const auto font  = look::label (11.0f);
    const auto label = stripLabel();
    const int  labW  = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, label)) + 12;
    const auto plate = juce::Rectangle<int> (r.getX() + 3, r.getY() + 3, labW, 15);
    g.setColour (th::bg0.withAlpha (0.88f));
    g.fillRoundedRectangle (plate.toFloat(), 2.0f);
    g.setFont (font);
    g.setColour (th::txt);
    g.drawText (label, plate, juce::Justification::centred, false);

    const auto hint = stripHint();
    if (hint.isNotEmpty())
    {
        const int hintW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, hint)) + 12;
        const auto hp = juce::Rectangle<int> (r.getRight() - 3 - hintW, r.getY() + 3, hintW, 15);
        g.setColour (th::bg0.withAlpha (0.88f));
        g.fillRoundedRectangle (hp.toFloat(), 2.0f);
        g.setColour (th::mut);
        g.drawText (hint, hp, juce::Justification::centred, false);
    }
}

void TonalBalanceLens::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int was = hovered;
    const auto wasCursor = cursor;

    hovered = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (p)) { hovered = i; break; }

    cursor = (zones.curves.contains (p) || zones.delta.contains (p)) ? p : juce::Point<int> (-1, -1);
    if (hovered != was || cursor != wasCursor) repaint();
}

void TonalBalanceLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursor.x != -1) { hovered = -1; cursor = { -1, -1 }; repaint(); }
}

// CUALQUIER archivo, no sólo las extensiones conocidas (ver el encabezado del header): si no se puede
// leer, el mensaje lo dice. Rechazar en silencio un AIFF llamado .dat sería peor.
bool TonalBalanceLens::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() == 1;
}

void TonalBalanceLens::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    if (files.isEmpty()) { repaint(); return; }

    const juce::File f (files[0]);
    if (f.existsAsFile()) processor.loadReference (f);
    repaint();
}

void TonalBalanceLens::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true;  repaint(); }
void TonalBalanceLens::fileDragExit  (const juce::StringArray&)          { dragOver = false; repaint(); }
}
