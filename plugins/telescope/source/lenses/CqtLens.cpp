#include "lenses/CqtLens.h"
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

// Las cinco negras de la octava, en clases de nota (C=0): C# D# F# G# A#.
bool isBlackKey (int pitchClass) noexcept
{
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

const char* kChannelNames[Cqt::kNumChannels] = { "L", "R", "M" };
}

const juce::ValueTree& CqtLens::stateTree() const { return processor.apvts.state; }

CqtLens::CqtLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (8);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

//======================================================================================== geometría
CqtLens::Zones CqtLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    const int rowH = juce::jlimit (22, 30, h / 22);
    z.footer = body.removeFromBottom (rowH);
    body.removeFromBottom (th::padIn / 2);

    auto foot = z.footer;
    const int gap = 6;
    const int bw = juce::jmin (220, (foot.getWidth() - gap * (kNumControls - 1)) / kNumControls);
    for (int i = 0; i < kNumControls; ++i)
    {
        z.button[i] = foot.removeFromLeft (bw);
        foot.removeFromLeft (gap);
    }

    z.keyText = body.removeFromBottom (16);
    z.chroma  = body.removeFromBottom (juce::jlimit (30, 52, h / 11));
    body.removeFromBottom (th::padIn / 2);

    z.keyboard = body.removeFromBottom (kKeyboardH);
    z.dbScale  = body.removeFromLeft (kScaleW);
    z.plot     = body;

    // El teclado, el cromagrama y la línea de tonalidad comparten el ANCHO del plot: el teclado es el eje
    // del plot, y las doce barras se leen contra las mismas notas.
    z.keyboard = z.keyboard.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    z.chroma   = z.chroma.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    z.keyText  = z.keyText.withLeft (z.plot.getX()).withRight (z.plot.getRight());
    return z;
}

void CqtLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    Lens::resized();
}

//======================================================================================== mapeos
// El eje es LINEAL EN BINS, que es lineal en cuartos de tono: cada nota ocupa exactamente lo mismo en
// pantalla, en las ocho octavas. Es toda la diferencia con el eje logarítmico de SPECTRUM.
float CqtLens::xForPosition (double posInBins) const
{
    if (binsSeen <= 0 || zones.plot.isEmpty()) return (float) zones.plot.getX();
    const double t = (posInBins + 0.5) / (double) binsSeen;   // la celda del bin k va de k−0.5 a k+0.5
    return (float) zones.plot.getX() + (float) (t * (double) zones.plot.getWidth());
}

// La posición CONTINUA en bins que le corresponde a una x (puede caer entre dos bins, y de ahí salen los
// cents de la lectura).
double CqtLens::positionAtX (int x) const
{
    if (binsSeen <= 0 || zones.plot.getWidth() <= 0) return 0.0;
    return (double) (x - zones.plot.getX()) * (double) binsSeen / (double) zones.plot.getWidth() - 0.5;
}

int CqtLens::binAtX (int x) const
{
    if (binsSeen <= 0 || zones.plot.getWidth() <= 0) return -1;
    return juce::jlimit (0, binsSeen - 1, (int) std::lround (positionAtX (x)));
}

double CqtLens::freqAtX (int x) const
{
    if (binsSeen <= 0 || zones.plot.getWidth() <= 0) return 0.0;
    // Del índice del bin sólo se podrían leer 0 y 50 cents; de la posición continua sale el número útil.
    return (double) fMinSeen * std::pow (2.0, positionAtX (x) / (double) bpoSeen);
}

float CqtLens::yForDb (float db) const
{
    const float range = (float) juce::jmax (1, lastRangeDb);
    const float t = juce::jlimit (0.0f, 1.0f, (db + range) / range);
    return (float) zones.plot.getBottom() - t * (float) zones.plot.getHeight();
}

//======================================================================================== animación
bool CqtLens::advanceFrame()
{
    const int wantedRange = processor.spectrumSettings().rangeDb();
    if (wantedRange != lastRangeDb) { lastRangeDb = wantedRange; invalidateStatic(); }

    const auto& f = processor.cqt().read();
    const bool fresh = f.frameIndex != lastFrameIndex || f.numBins != binsSeen;

    if (fresh)
    {
        if (f.numBins != binsSeen || f.binsPerOctave != bpoSeen)
        {
            binsSeen = f.numBins;
            bpoSeen  = juce::jmax (1, f.binsPerOctave);
            fMinSeen = f.fMin > 0.0f ? f.fMin : (float) Cqt::kFMinHz;
            targetDb.assign ((size_t) juce::jmax (0, binsSeen), CqtFrame::kFloorDb);
            dispDb.assign   ((size_t) juce::jmax (0, binsSeen), CqtFrame::kFloorDb);
            holdDb.assign   ((size_t) juce::jmax (0, binsSeen), CqtFrame::kFloorDb);
            invalidateStatic();   // el teclado y las marcas de octava son capa estática
        }
        latencySeen    = f.lowestBinLatencySec;
        lastFrameIndex = f.frameIndex;

        for (int k = 0; k < binsSeen; ++k)
        {
            targetDb[(size_t) k] = f.magDb[k];
            holdDb[(size_t) k]   = f.holdDb[k];
        }
        for (int c = 0; c < CqtFrame::kNumClasses; ++c)
        {
            chromaNow[c] = f.chroma[c];
            chromaSm[c]  = f.chromaSmooth[c];
        }
        keyTonic        = f.keyTonic;
        keyMode         = f.keyMode;
        keyConfidence   = f.keyConfidence;
        keyTimeFraction = f.keyTimeFraction;
    }

    // SUBE de una y BAJA suave: es un medidor, y un pico que se pierde entre dos frames es un pico que no
    // existió. Con reduced-motion no hay suavizado: se dibuja el frame tal cual.
    const bool reduced = prefersReducedMotion();
    bool moved = false;
    for (size_t k = 0; k < dispDb.size(); ++k)
    {
        const float target = targetDb[k], before = dispDb[k];
        dispDb[k] = (reduced || target > before) ? target : before + (target - before) * kRelease;
        moved = moved || std::abs (dispDb[k] - before) > 0.01f;
    }
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const float target = chromaNow[c], before = dispChroma[c];
        dispChroma[c] = (reduced || target > before) ? target : before + (target - before) * kRelease;
        moved = moved || std::abs (dispChroma[c] - before) > 0.002f;
    }
    return fresh || moved;
}

//======================================================================================== capa estática
void CqtLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    lastRangeDb = processor.spectrumSettings().rangeDb();

    g.setColour (look::gridMinor);
    g.drawRect (zones.plot.expanded (1), 1);

    // ---- eje de dB: la misma escala de SPECTRUM (0 arriba, el rango elegido hacia abajo) ----
    const int step = lastRangeDb >= 120 ? 20 : (lastRangeDb >= 90 ? 15 : 10);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (int db = 0; db >= -lastRangeDb; db -= step)
    {
        const int y = juce::roundToInt (yForDb ((float) db));
        g.setColour (db == 0 ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (zones.plot.getX()), (float) (y), (float) (zones.plot.getWidth()), 1.0f });
        g.setColour (th::fnt);
        g.drawText (juce::String (db), zones.dbScale.getX(), y - 6, kScaleW - 6, 12,
                    juce::Justification::centredRight, false);
    }
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText ("dB", zones.dbScale.getX(), zones.plot.getY() + 2, kScaleW - 6, 12,
                juce::Justification::centredRight, false);

    // ---- una marca vertical en cada DO, con su octava ----
    const int semitones = binsSeen * 12 / juce::jmax (1, bpoSeen);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (int s = 0; s < semitones; ++s)
    {
        const int midi = 21 + s;                       // el semitono 0 es A0 = MIDI 21
        if (((midi % 12) + 12) % 12 != 0) continue;    // sólo los Do

        const int perSemi = juce::jmax (1, bpoSeen / 12);
        const int x = juce::roundToInt (xForPosition ((double) (s * perSemi) - 0.5 * (double) perSemi));
        g.setColour (look::gridMajor);
        look::fillSnapped (g, { (float) (x), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });
        g.setColour (th::fnt);
        g.drawText ("C" + juce::String (midi / 12 - 1), x + 3, zones.plot.getY() + 2, 26, 12,
                    juce::Justification::centredLeft, false);
    }

    paintKeyboard (g);

    // ---- el panel del cromagrama, con sus doce etiquetas ----
    g.setColour (th::surf.withAlpha (0.6f));
    g.fillRoundedRectangle (zones.chroma.toFloat(), 3.0f);
    g.setColour (look::gridMinor);
    g.drawRoundedRectangle (zones.chroma.toFloat().reduced (0.5f), 3.0f, 1.0f);

    g.setFont (ovni::ui::fonts::label (9.0f));
    const float cw = (float) zones.chroma.getWidth() / (float) CqtFrame::kNumClasses;
    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        g.setColour (th::fnt);
        g.drawText (classNameFor (c, strings::languageOf (stateTree())),
                    juce::Rectangle<float> ((float) zones.chroma.getX() + (float) c * cw,
                                            (float) zones.chroma.getBottom() - 11.0f, cw, 10.0f),
                    juce::Justification::centred, false);
    }

}

// EL TECLADO. Es lo que convierte el eje en algo que se lee sin traducir: la barra que se levanta está
// arriba de una tecla concreta, y esa tecla tiene nombre.
void CqtLens::paintKeyboard (juce::Graphics& g) const
{
    if (binsSeen <= 0 || zones.keyboard.isEmpty()) return;
    const int semitones = binsSeen * 12 / juce::jmax (1, bpoSeen);
    const int perSemi   = juce::jmax (1, bpoSeen / 12);

    for (int s = 0; s < semitones; ++s)
    {
        const int   midi = 21 + s;
        const int   pc   = ((midi % 12) + 12) % 12;
        // La tecla va CENTRADA en la nota y mide un semitono: de −perSemi/2 a +perSemi/2 alrededor del
        // bin de la nota. Así las teclas embaldosan sin huecos y cada barra cae sobre su propia tecla.
        const float x0 = xForPosition ((double) (s * perSemi) - 0.5 * (double) perSemi);
        const float x1 = xForPosition ((double) (s * perSemi) + 0.5 * (double) perSemi);
        const auto  key = juce::Rectangle<float> (x0, (float) zones.keyboard.getY(),
                                                  juce::jmax (1.0f, x1 - x0),
                                                  (float) zones.keyboard.getHeight());

        if (isBlackKey (pc))
        {
            // Las negras: más cortas y del fondo del sello, para que se lean como negras de verdad.
            g.setColour (th::bg0);
            g.fillRect (key.withHeight (key.getHeight() * 0.62f));
            g.setColour (th::surf);
            g.fillRect (key.withTrimmedTop (key.getHeight() * 0.62f));
        }
        else
        {
            g.setColour (pc == 0 ? th::mut.withAlpha (0.62f) : th::mut.withAlpha (0.35f));
            g.fillRect (key.reduced (0.0f, 0.0f));
        }

        // 57b — LA TECLA ENCENDIDA. Antes el teclado era un rótulo fijo: decía dónde está cada nota y
        // nada más. Ahora se ilumina con el cromagrama de su clase de altura, suave y desde abajo (que es
        // por donde se ilumina una tecla de verdad), así el teclado pasa a DECIR ALGO mientras suena.
        // Es el mismo dato que la fila de croma de abajo, mirado desde el eje del plot.
        // Se ilumina DESDE ABAJO con dos tramos (por lo mismo que las barras: 114 teclas × un
        // ColourGradient cada una es una tabla de color por tecla).
        if (const float lit = juce::jlimit (0.0f, 1.0f, dispChroma[pc]); lit > 0.04f)
        {
            const auto glow = (pc == keyTonic ? look::caution : look::dataLine);
            g.setColour (glow.withAlpha (0.18f * lit));
            g.fillRect (key);
            g.setColour (glow.withAlpha (0.40f * lit));
            g.fillRect (key.withTop (key.getY() + key.getHeight() * 0.55f));
        }
        g.setColour (th::bg0.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (key.getRight() - 0.5f, key.getY(), 1.0f, key.getHeight()));

        // ===== 56 ===== LA MARCA DE OCTAVA.
        //
        // El primer intento fue escribir el nombre ("C2", "C3"…) en cada Do. No entra: con 114 teclas
        // sobre el plot, una tecla mide 6.2 px LÓGICOS en tamaño M y 7.8 en L — ni "C2" a 7 px cabe, y
        // dejarlo desbordar sobre las teclas vecinas lo pondría mitad sobre una blanca clara y mitad
        // sobre una negra oscura, o sea ilegible en cualquier caso. Un rótulo que no se puede leer es
        // peor que no ponerlo: ocupa el lugar del que sí se leería.
        //
        // Lo que SÍ entra y sirve igual: un tick de acento en el borde de arriba de cada Do. Se ve a
        // cualquier ancho, y cae exactamente sobre las líneas de octava del plot, que ya están rotuladas
        // C1…C10 en el eje de arriba. O sea que la coordenada existe una sola vez y el teclado la señala.
        if (pc == 0)
        {
            g.setColour (look::accent.withAlpha (0.70f));
            g.fillRect (juce::Rectangle<float> (key.getX(), key.getY(),
                                                juce::jmax (1.0f, key.getWidth() - 0.5f), 2.0f));
        }
    }
}

//======================================================================================== capa viva
void CqtLens::paintLive (juce::Graphics& g)
{
    if (zones.plot.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); resized(); }

    if (binsSeen > 0 && ! dispDb.empty())
    {
        g.saveState();
        g.reduceClipRegion (zones.plot);

        // Una barra por bin. Con 229 bins sobre ~950 px cada barra mide 4 px: no hay que decimar nada, y
        // dibujar barras (y no una curva) es lo correcto acá — cada bin ES una nota, no una muestra de
        // una función continua.
        const float base = (float) zones.plot.getBottom();
        const int   perSemiLive = juce::jmax (1, bpoSeen / 12);   // 56: bins por semitono, para la tónica
        for (int k = 0; k < binsSeen; ++k)
        {
            const float x0 = xForBinEdge (k), x1 = xForBinEdge (k + 1);
            const float y  = yForDb (dispDb[(size_t) k]);
            if (y >= base - 0.5f) continue;

            // Los bins IMPARES son cuartos de tono (entre notas): se dibujan más tenues, porque lo que
            // aparece ahí casi siempre es la falda de la nota de al lado o algo desafinado.
            const bool onNote = (k % 2 == 0);
            // 56: la barra de la TÓNICA se destaca. Es la nota que la lente acaba de estimar, y en un
            // bosque de 229 barras encontrarla a ojo es imposible si todas son del mismo color.
            const int  pcOf = onNote ? ((21 + k / juce::jmax (1, perSemiLive)) % 12 + 12) % 12 : -1;
            const bool tonic = (keyTonic >= 0 && pcOf == keyTonic);
            g.setColour (tonic ? look::accent.withAlpha (0.95f)
                               : (onNote ? th::green : th::greenD).withAlpha (onNote ? 0.85f : 0.45f));

            const float bw = juce::jmax (1.0f, x1 - x0 - 0.5f);
            const float bh = base - y;
            // Tope REDONDEADO: una barra con esquina viva a 4 px de ancho se ve como un pixel suelto; con
            // el tope redondo se lee como barra. Sólo cuando hay alto para que el radio signifique algo.
            // 57b — DEGRADADO EN DOS TRAMOS, no con ColourGradient. La barra se apaga hacia abajo (un
            // relleno plano sobre 229 barras es una pared de color y los picos dejan de destacarse), pero
            // construir un `ColourGradient` por barra arma una tabla de color POR BARRA: medido, la lente
            // se iba de 1.27 a 3.16 ms a escala 1. Dos rectángulos y un filo dan la misma lectura por una
            // fracción del costo, que es la misma decisión que ya había tomado SPECTRUM.
            const auto rect = juce::Rectangle<float> (x0, y, bw, bh);
            const auto col  = tonic ? look::accent
                                    : (onNote ? look::dataLine : look::dataLineDim);
            const float aTop = onNote ? 0.62f : 0.38f;
            g.setColour (col.withAlpha (aTop * 0.34f));
            if (bw >= 3.0f && bh > bw) g.fillRoundedRectangle (rect, bw * 0.5f);
            else                       g.fillRect (rect);
            g.setColour (col.withAlpha (aTop));
            g.fillRect (juce::Rectangle<float> (x0, y, bw, juce::jmin (bh, bh * 0.45f)));
            g.setColour (col.withAlpha (onNote ? 0.98f : 0.62f));
            g.fillRect (juce::Rectangle<float> (x0, y, bw, juce::jmin (1.5f, bh)));
        }

        // El peak hold, línea fina encima.
        g.setColour (th::txt.withAlpha (0.55f));
        for (int k = 0; k < binsSeen; ++k)
        {
            const float yh = yForDb (holdDb[(size_t) k]);
            if (yh >= base - 0.5f) continue;
            const float x0 = xForBinEdge (k), x1 = xForBinEdge (k + 1);
            g.fillRect (juce::Rectangle<float> (x0, yh, juce::jmax (1.0f, x1 - x0 - 0.5f), 1.0f));
        }
        g.restoreState();
    }

    paintChroma (g);
    paintReadout (g);

    const auto s = processor.cqtSettings();
    paintButton (g, zones.button[ctrlChannel], tr (strings::Key::channel),
                 kChannelNames[juce::jlimit (0, (int) Cqt::kNumChannels - 1, s.channel)],
                 hovered == ctrlChannel);
    paintButton (g, zones.button[ctrlChroma], tr (strings::Key::chroma),
                 juce::String (s.chromaSeconds(), 1) + " s", hovered == ctrlChroma);

    // ---- el rótulo de LATENCIA, en el espacio libre del pie ----
    // No es decoración: dice qué momento del audio está mostrando la parte izquierda del dibujo. Va acá y
    // no arriba del plot porque ahí se le encimaba la marca de octava del extremo agudo (se vio en la
    // foto: "A0 · 1C20s" con el C9 debajo).
    const auto libre = zones.footer.withLeft (zones.button[kNumControls - 1].getRight() + 12);
    if (libre.getWidth() > 120)
    {
        g.setColour (th::fnt);
        g.setFont (ovni::ui::fonts::label (9.0f));
        g.drawText (tr (strings::Key::bassLatency), libre.withTrimmedRight (72), juce::Justification::centredRight, false);
        g.setColour (th::mut);
        g.setFont (ovni::ui::fonts::mono (10.0f));
        g.drawText ("A0 " + juce::String::fromUTF8 ("\xc2\xb7") + " "
                        + juce::String (latencySeen > 0.0f ? latencySeen : 1.241f, 2) + " s",
                    libre, juce::Justification::centredRight, false);
    }
}

// EL CROMAGRAMA Y LA TONALIDAD. La barra rellena es el instante; el contorno, la versión suavizada que es
// la que de verdad alimenta la estimación. Verlas juntas es lo que deja entender por qué la tonalidad no
// salta con cada acorde.
void CqtLens::paintChroma (juce::Graphics& g) const
{
    const float cw = (float) zones.chroma.getWidth() / (float) CqtFrame::kNumClasses;
    const float top = (float) zones.chroma.getY() + 3.0f;
    const float bottom = (float) zones.chroma.getBottom() - 12.0f;
    const float hgt = juce::jmax (1.0f, bottom - top);

    for (int c = 0; c < CqtFrame::kNumClasses; ++c)
    {
        const float x = (float) zones.chroma.getX() + (float) c * cw;
        const auto  cell = juce::Rectangle<float> (x + 2.0f, top, juce::jmax (1.0f, cw - 4.0f), hgt);

        const float v = juce::jlimit (0.0f, 1.0f, dispChroma[c]);
        // La TÓNICA estimada va en ámbar: es la única barra que el texto de abajo está nombrando.
        const auto hue = (c == keyTonic) ? th::amber : th::green;
        // 57b — dos tramos y tapa, como el resto de las barras del instrumento (doce barras: acá el
        // degradado de verdad sí entraría, pero que las tres filas de esta lente se dibujen igual vale
        // más que la diferencia).
        {
            const auto bar = cell.withTop (cell.getBottom() - v * hgt);
            g.setColour (hue.withAlpha (0.22f));
            g.fillRect (bar);
            g.setColour (hue.withAlpha (0.52f));
            g.fillRect (bar.withTrimmedBottom (bar.getHeight() * 0.55f));
            g.setColour (hue.withAlpha (0.95f));
            g.fillRect (bar.withHeight (juce::jmin (1.5f, bar.getHeight())));
        }

        const float sm = juce::jlimit (0.0f, 1.0f, chromaSm[c]);
        g.setColour (th::txt.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getBottom() - sm * hgt, cell.getWidth(), 1.0f));
    }

    // NUNCA la tonalidad sola: siempre con la confianza y el % del tiempo (ver el encabezado).
    juce::String text;
    if (keyTonic >= 0)
        text = keyLabel (keyTonic, keyMode, strings::languageOf (stateTree()))
             + juce::String::fromUTF8 ("  \xc2\xb7  ") + trLower (strings::Key::confidence) + " "
             + juce::String (keyConfidence, 2) + juce::String::fromUTF8 ("  \xc2\xb7  ")
             + juce::String (juce::roundToInt (100.0f * keyTimeFraction)) + " " + tr (strings::Key::ofTheTime);
    else
        text = tr (strings::Key::noKeyEstimated);

    g.setColour (keyTonic >= 0 ? th::txt : th::mut);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (text, zones.keyText, juce::Justification::centred, false);
}

//======================================================================================== lectura
CqtLens::Readout CqtLens::readoutAtX (int x) const
{
    Readout r;
    if (binsSeen <= 0 || dispDb.empty()) return r;
    if (! zones.plot.contains (x, zones.plot.getCentreY())) return r;

    r.bin = binAtX (x);
    if (r.bin < 0) return r;

    r.valid  = true;
    r.freqHz = freqAtX (x);
    r.note   = noteForFrequency (r.freqHz);
    r.db     = targetDb[(size_t) r.bin];   // el dato del frame, no la barra suavizada del dibujo
    return r;
}

void CqtLens::paintReadout (juce::Graphics& g) const
{
    if (cursorX < 0) return;
    const auto r = readoutAtX (cursorX);
    if (! r.valid) return;

    g.setColour (th::green.withAlpha (0.45f));
    look::fillSnapped (g, { (float) (cursorX), (float) (zones.plot.getY()), 1.0f, (float) (zones.plot.getHeight()) });

    const juce::String text = r.note.name + "  "
                            + (r.note.cents >= 0 ? "+" : "") + juce::String (r.note.cents)
                            + juce::String::fromUTF8 (" \xc2\xa2  \xc2\xb7  ")
                            + juce::String (r.freqHz, r.freqHz < 100.0 ? 2 : 1) + " Hz"
                            + juce::String::fromUTF8 ("  \xc2\xb7  ")
                            + juce::String (r.db, 1) + " dB";

    g.setFont (ovni::ui::fonts::mono (11.0f));
    const int tw = juce::jmax (170, (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 16);
    const auto box = readoutBoxFor (zones.plot, cursorX, tw);

    g.setColour (th::bg1.withAlpha (0.9f));
    g.fillRoundedRectangle (box.toFloat(), 3.0f);
    g.setColour (th::green.withAlpha (0.4f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 3.0f, 1.0f);
    g.setColour (th::txt);
    g.drawText (text, box, juce::Justification::centred, false);
}

void CqtLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
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

    auto inner = area.reduced (8, 0);
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (9.0f));
    g.drawText (label, inner.removeFromLeft (inner.getWidth() / 2), juce::Justification::centredLeft, false);
    g.setColour (hue);
    g.setFont (ovni::ui::fonts::mono (11.0f));
    g.drawText (value, inner, juce::Justification::centredRight, false);
}

//======================================================================================== interacción
void CqtLens::cycleControl (int control)
{
    auto s = processor.cqtSettings();
    if (control == ctrlChannel)      s.channel = (s.channel + 1) % (int) Cqt::kNumChannels;
    else if (control == ctrlChroma)  s.chromaSecIndex = (s.chromaSecIndex + 1) % Cqt::kNumChromaSecOptions;
    else return;
    processor.setCqtSettings (s);
}

void CqtLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { cycleControl (i); return; }
}

void CqtLens::mouseMove (const juce::MouseEvent& e)
{
    int over = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { over = i; break; }

    const int newCursor = zones.plot.contains (e.getPosition()) ? e.x : -1;
    if (over != hovered || newCursor != cursorX) { hovered = over; cursorX = newCursor; repaint(); }
}

void CqtLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1 || cursorX != -1) { hovered = -1; cursorX = -1; repaint(); }
}
}
