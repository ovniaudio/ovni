#include "lenses/LoudnessLens.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "data/StreamingTargets.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr float kSmoothing = 0.25f;      // por frame; con reduced-motion se salta
constexpr float kNoReading = -100.0f;    // por debajo de esto no hay medición que mostrar

bool hasReading (float lufs) noexcept { return lufs > kNoReading; }

juce::String fmt1 (float v)  { return hasReading (v) ? juce::String (v, 1) : juce::String ("--.-"); }
juce::String fmtLu (float v) { return juce::String (v, 1); }
}

const juce::ValueTree& LoudnessLens::stateTree() const { return processor.apvts.state; }

LoudnessLens::LoudnessLens (TelescopeProcessor& p) : Lens (30), processor (p)
{
    setSettleHold (90);   // 3 s de repintado tras el último cambio: la historia sigue corriendo
    historyBuf.reserve ((size_t) kHistoryPoints);
}

//======================================================================================== geometría
LoudnessLens::Zones LoudnessLens::zonesFor (int w, int h) const
{
    Zones z;
    auto body = juce::Rectangle<int> (0, 0, w, h).reduced (th::padIn);

    z.header = body.removeFromTop (juce::jmax (96, h / 6));
    z.footer = body.removeFromBottom (juce::jmax (56, h / 11));

    body.removeFromTop (th::padIn);
    body.removeFromBottom (th::padIn);

    // 57c — de w/6 a w/4: son CUATRO barras, no dos. El piso de 190 es para el tamaño S, donde w/4 no
    // alcanza para que los rótulos de las cuatro entren sin pisarse.
    z.meters  = body.removeFromLeft (juce::jmax (190, w / 4));
    body.removeFromLeft (th::padIn);
    z.history = body;

    // Los botones viven a la derecha del pie.
    auto btns = z.footer.withTrimmedLeft (z.footer.getWidth() * 3 / 5);
    const int bw = juce::jmin (110, btns.getWidth() / 2 - 8);
    z.pauseBtn = btns.removeFromRight (bw).reduced (0, 10);
    btns.removeFromRight (10);
    z.resetBtn = btns.removeFromRight (bw).reduced (0, 10);

    return z;
}

LoudnessLens::MeterCols LoudnessLens::meterColsFor (juce::Rectangle<int> meters) noexcept
{
    MeterCols c;
    auto body = meters;
    // Dos filas de rótulo, ADENTRO: el nombre y la unidad.
    c.units = body.removeFromBottom (12);
    c.names = body.removeFromBottom (14);
    c.scale = body.removeFromLeft (30);

    constexpr int gap = 6;
    const int barW = juce::jmax (8, (body.getWidth() - gap * (kNumBars - 1)) / kNumBars);
    for (int i = 0; i < kNumBars; ++i)
        c.bar[i] = { body.getX() + i * (barW + gap), body.getY(), barW, body.getHeight() };

    // El rótulo más largo es el de SHORT-TERM (cinco letras en varios idiomas). Que entre en el ancho de
    // la barra es lo que decide el cuerpo de la tipografía, no al revés: un rótulo recortado a la mitad
    // es peor que uno chico.
    c.labelFontSize = juce::jlimit (7.5f, 11.0f, (float) barW * 0.30f);
    return c;
}

float LoudnessLens::toScale01 (float lufs) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (lufs - kScaleBottom) / (kScaleTop - kScaleBottom));
}

//======================================================================================== capa estática
void LoudnessLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);
    const auto hue = th::green;

    // Superficies: el header y el pie apoyados sobre una superficie apenas elevada, con hairline.
    const auto surface = [&g] (juce::Rectangle<int> r, float alpha)
    {
        g.setColour (th::surf.withAlpha (alpha));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (th::lineSoft);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);
    };
    surface (zones.header, 0.75f);
    surface (zones.history, 0.55f);
    surface (zones.footer, 0.45f);

    // ---- escala de los medidores: marcas cada 6 LU, 0 arriba ----
    const auto cols = meterColsFor (zones.meters);
    auto scaleCol = cols.scale;
    const auto meters = cols.bar[0].getUnion (cols.bar[kNumBars - 1]);
    const int labelW = scaleCol.getWidth();

    // Se itera por ÍNDICE de marca, no acumulando el valor en un float: la marca de arriba se reconoce
    // por ser la primera (i == 0), no por `v == kScaleTop` — comparar floats con == es exactamente el
    // tipo de igualdad que deja de ser cierta cuando alguien cambia el paso a 6.5 (-Wfloat-equal, nit del
    // revisor del 49).
    g.setFont (ovni::ui::fonts::mono (9.0f));
    const int marks = (int) ((kScaleTop - kScaleBottom) / kScaleStep);
    for (int i = 0; i <= marks; ++i)
    {
        const float v = kScaleTop - (float) i * kScaleStep;
        const float t = toScale01 (v);
        const int   y = scaleCol.getBottom() - juce::roundToInt (t * (float) scaleCol.getHeight());

        g.setColour (th::fnt);
        g.drawText (juce::String ((int) v), scaleCol.getX(), y - 6, labelW - 6, 12,
                    juce::Justification::centredRight, false);

        g.setColour (i == 0 ? th::line : th::lineSoft);
        look::fillSnapped (g, { (float) (meters.getX()), (float) (y), (float) (meters.getWidth()), 1.0f });
    }

    // ========================================================================================================
    // ===== 57c · CUATRO BARRAS, Y NINGUNA SE LLAMA CON UNA LETRA =====
    //
    // «Sólo es M–S, también debería poder ser L y R» (Joaquín, 12-sep). Tenía razón por partida doble:
    // faltaban L y R, y las dos que había estaban MAL ROTULADAS. Eran momentary y short-term rotuladas
    // "M" y "S", y para cualquier ingeniero M/S debajo de dos medidores quiere decir mid/side — que es
    // otra cosa, que esta misma pantalla podría mostrar, y que está al lado en SCOPE. Ahora van MOM y
    // SHORT (traducidas), con "LUFS" debajo, y las dos nuevas L y R con "dBTP".
    //
    // Comparten la escala 0 … −60 con las LUFS: es lo que hace que las cuatro se lean de un vistazo.
    // ========================================================================================================
    const strings::Key barNames[kNumBars] = { strings::Key::momentaryBar, strings::Key::shortTermBar,
                                              strings::Key::left, strings::Key::right };
    for (int i = 0; i < kNumBars; ++i)
    {
        const auto r = cols.bar[i];
        g.setColour (th::bg1);
        g.fillRect (r);
        g.setColour (th::lineSoft);
        g.drawRect (r, 1);

        g.setColour (hue.withAlpha (0.75f));
        g.setFont (ovni::ui::fonts::label (cols.labelFontSize));
        g.drawText (tr (barNames[i]), r.getX(), cols.names.getY(), r.getWidth(), cols.names.getHeight(),
                    juce::Justification::centred, false);
    }

    // La unidad, UNA vez por par: repetirla cuatro veces es ruido.
    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (juce::jmax (8.0f, cols.labelFontSize - 1.5f)));
    const auto lufsSpan = cols.bar[0].getUnion (cols.bar[1]);
    const auto tpSpan   = cols.bar[2].getUnion (cols.bar[3]);
    g.drawText ("LUFS", lufsSpan.getX(), cols.units.getY(), lufsSpan.getWidth(), cols.units.getHeight(),
                juce::Justification::centred, false);
    g.drawText ("dBTP", tpSpan.getX(), cols.units.getY(), tpSpan.getWidth(), cols.units.getHeight(),
                juce::Justification::centred, false);

    // ---- grilla de la historia: líneas cada 6 LU y marcas de minuto ----
    auto hist = zones.history.reduced (th::padIn / 2);
    g.setFont (ovni::ui::fonts::mono (9.0f));
    for (float v = kScaleTop; v >= kScaleBottom; v -= kScaleStep * 2.0f)
    {
        const int y = hist.getBottom() - juce::roundToInt (toScale01 (v) * (float) hist.getHeight());
        g.setColour (th::lineSoft);
        look::fillSnapped (g, { (float) (hist.getX()), (float) (y), (float) (hist.getWidth()), 1.0f });
        g.setColour (th::fnt);
        g.drawText (juce::String ((int) v), hist.getRight() - 26, y - 6, 24, 12,
                    juce::Justification::centredRight, false);
    }
    for (int min = 1; min <= 3; ++min)
    {
        const int x = hist.getRight() - juce::roundToInt ((float) min / 3.0f * (float) hist.getWidth());
        g.setColour (th::lineSoft);
        look::fillSnapped (g, { (float) (x), (float) (hist.getY()), 1.0f, (float) (hist.getHeight()) });
        g.setColour (th::fnt);
        // Al PIE del gráfico: arriba chocaban con el título de la sección.
        g.drawText ("-" + juce::String (min) + " min", x + 4, hist.getBottom() - 13, 48, 12,
                    juce::Justification::left, false);
    }

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (tr (strings::Key::shortTerm) + juce::String::fromUTF8 (" \xc2\xb7 ")
                    + tr (strings::Key::lastMinutes3),
                hist.getX(), hist.getY() + 2, hist.getWidth(), 14, juce::Justification::left, false);
}

//======================================================================================== capa viva
void LoudnessLens::paintLive (juce::Graphics& g)
{
    if (zones.header.isEmpty()) zones = zonesFor (getWidth(), getHeight());
    const auto hue = th::green;

    // ---------------- header en tres columnas: héroe · M/S · objetivo ----------------
    auto head = zones.header.reduced (th::padIn / 2);
    const int colW = head.getWidth() / 10;

    // 1) el integrado, número héroe: es el que decide si el tema pasa la puerta de una plataforma.
    auto hero = head.removeFromLeft (colW * 4);
    paintReadout (g, hero, tr (strings::Key::integrated), dispI, "LUFS", (float) juce::jmin (54, hero.getHeight() - 34),
                  latest.integratedValid);

    // 2) M y S, secundarios, apilados.
    auto side = head.removeFromLeft (colW * 3);
    const int sideH = side.getHeight() / 2;
    // 57c — mientras la ventana se llena el número es el PARCIAL y se dibuja apagado (`th::mut`), que es
    // exactamente lo que `paintReadout` hace con `valid = false`. Antes decía "--.-" durante 400 ms y 3 s.
    paintReadout (g, side.removeFromTop (sideH), tr (strings::Key::momentary), dispM, "LUFS", 22.0f,
                  hasReading (dispM) && ! momentaryIsPartial);
    paintReadout (g, side, tr (strings::Key::shortTerm), dispS, "LUFS", 22.0f,
                  hasReading (dispS) && ! shortTermIsPartial);

    // 3) el objetivo de plataforma con su delta: el diferencial de la lente, no una nota al pie.
    auto tcol = head;
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (tr (strings::Key::target), tcol.getX(), tcol.getY(), tcol.getWidth(), 12, juce::Justification::left, false);

    g.setColour (hue);
    g.setFont (ovni::ui::fonts::body (15.0f));
    g.drawFittedText (targetLine(), tcol.getX(), tcol.getY() + 16, tcol.getWidth(),
                      tcol.getHeight() - 20, juce::Justification::topLeft, 3);

    // ---------------- las cuatro barras: MOMENTARY · SHORT-TERM · L · R ----------------
    const auto cols = meterColsFor (zones.meters);
    const float clip = processor.clipThresholdDbtp();
    paintBar (g, cols.bar[0], dispM,   latest.momentaryMax, momentaryIsPartial,
              kCautionLufs, kAlertLufs, kNoReading);
    paintBar (g, cols.bar[1], dispS,   latest.shortTermMax, shortTermIsPartial,
              kCautionLufs, kAlertLufs, kNoReading);
    // Las de pico usan los umbrales del dominio del pico: ámbar donde DYNAMICS cuenta un clip y alerta
    // en 0 dBTP. La línea del umbral se dibuja además como marca, que es lo que la hace accionable.
    paintBar (g, cols.bar[2], dispTpL, tpMaxL, false, clip, kTpAlertDbtp, clip);
    paintBar (g, cols.bar[3], dispTpR, tpMaxR, false, clip, kTpAlertDbtp, clip);

    // ---------------- historia ----------------
    auto hist = zones.history.reduced (th::padIn / 2);
    if (historyBuf.size() >= 2)
    {
        const auto n = (int) historyBuf.size();
        // Se dibuja pegada al borde derecho: el ahora está a la derecha, como en un sismógrafo.
        const auto xOf = [&] (int i)
        {
            return (float) hist.getRight()
                 - (float) (n - 1 - i) / (float) kHistoryPoints * (float) hist.getWidth();
        };
        const auto yOf = [&] (float v)
        {
            return (float) hist.getBottom() - toScale01 (v) * (float) hist.getHeight();
        };

        juce::Path momentaryPath, shortPath;
        bool startedM = false, startedS = false;
        for (int i = 0; i < n; ++i)
        {
            const auto& p = historyBuf[(size_t) i];
            if (hasReading (p.momentary))
            {
                const float x = xOf (i), y = yOf (p.momentary);
                if (startedM) momentaryPath.lineTo (x, y);
                else          { momentaryPath.startNewSubPath (x, y); startedM = true; }
            }
            if (hasReading (p.shortTerm))
            {
                const float x = xOf (i), y = yOf (p.shortTerm);
                if (startedS) shortPath.lineTo (x, y);
                else          { shortPath.startNewSubPath (x, y); startedS = true; }
            }
        }
        // ===== 56 ===== la historia era dos trazos de 1 px sobre negro y se leía como un garabato. El
        // short-term —la curva que de verdad se mira— ahora va RELLENA hasta el piso: el área da la
        // sensación de "cuánto" que una línea sola no da, y de paso separa las dos curvas sin tener que
        // mirar el grosor.
        if (startedS)
        {
            juce::Path area = shortPath;
            area.lineTo ((float) hist.getRight(), (float) hist.getBottom());
            area.lineTo (xOf (0), (float) hist.getBottom());
            area.closeSubPath();
            // 57b — el área bajo la curva con DEGRADADO, no plana: un bloque de un solo alpha se lee
            // como un bloque; degradándose hacia abajo, el ojo encuentra el borde (que es el dato) solo.
            g.saveState();
            g.reduceClipRegion (area);
            look::verticalFill (g, hist.toFloat(), hue, 0.30f, 0.02f);
            g.restoreState();
        }
        g.setColour (hue.withAlpha (0.22f));                       // momentary: el fondo agitado
        g.strokePath (momentaryPath, juce::PathStrokeType (1.0f));
        // 57b — el short-term lleva EL glow de la lente: es la curva que se lee de las dos, y el glow es
        // lo que la separa del momentary sin subirle el alpha a nada.
        const auto met = look::metricsFor (getWidth());
        look::glowPath (g, shortPath, hue, met.dataW, met.glowRadius);
        g.setColour (hue);                                          // short-term: la línea que se lee
        g.strokePath (shortPath, juce::PathStrokeType (met.dataW, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    }

    // La línea del objetivo cruzando la historia (si hay objetivo).
    const auto& tgt = streamingTarget (processor.targetIndex());
    if (tgt.hasTarget)
    {
        const int y = hist.getBottom() - juce::roundToInt (toScale01 (tgt.targetLufs) * (float) hist.getHeight());
        g.setColour (th::amber.withAlpha (0.55f));
        for (int x = hist.getX(); x < hist.getRight(); x += 6)      // punteada: es una referencia, no un dato
            look::fillSnapped (g, { (float) (x), (float) (y), (float) (3), 1.0f });

        // 56: la línea del objetivo ROTULADA. Una línea punteada sin número obliga a acordarse de qué
        // plataforma está elegida y a qué altura quedó — que es exactamente el trabajo que esta lente
        // tendría que estar ahorrando.
        const auto tag = juce::String (tgt.targetLufs, 1) + " LUFS";
        g.setFont (look::tabularFont (9.0f));
        const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag)) + 8;
        const juce::Rectangle<int> box (hist.getX() + 3, y - 13, tw, 12);
        g.setColour (th::bg0.withAlpha (0.85f));
        g.fillRoundedRectangle (box.toFloat(), 2.0f);
        g.setColour (th::amber);
        g.drawText (tag, box, juce::Justification::centred, false);
    }

    // ---------------- pie: lectura fina + botones ----------------
    auto foot = zones.footer.reduced (th::padIn / 2, 0);
    auto readouts = foot.withTrimmedRight (foot.getWidth() * 2 / 5);   // las celdas no se pisan

    g.setFont (ovni::ui::fonts::mono (10.0f));
    const juce::String cells[] = {
        "LRA "     + fmtLu (latest.lra) + " LU",
        "TP MAX "  + fmt1 (latest.truePeakMax) + " dBTP",
        "M MAX "   + fmt1 (latest.momentaryMax),
        "S MAX "   + fmt1 (latest.shortTermMax),
    };
    const int cellW = readouts.getWidth() / 4;
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (th::mut);
        g.drawText (cells[i], readouts.getX() + i * cellW, readouts.getY(), cellW, readouts.getHeight(),
                    juce::Justification::centredLeft, false);
    }

    paintButton (g, zones.resetBtn, tr (strings::Key::reset), false, hovered == 0);
    paintButton (g, zones.pauseBtn, tr (processor.isAnalysisPaused() ? strings::Key::resume : strings::Key::pause),
                 processor.isAnalysisPaused(), hovered == 1);

    // Aviso de análisis atrasado: si el bus tuvo que descartar, se dice. No se esconde.
    if (dropped > 0)
    {
        g.setColour (th::amber);
        g.setFont (ovni::ui::fonts::label (10.0f));
        g.drawText (juce::String::fromUTF8 ("\xe2\x9a\xa0 ") + tr (strings::Key::analysisBehind),
                    zones.footer.getX(), zones.footer.getY() - 14, 200, 12, juce::Justification::left, false);
    }
}

void LoudnessLens::paintReadout (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                                 float value, const juce::String& unit, float heroSize, bool valid) const
{
    g.setColour (th::fnt);
    g.setFont (ovni::ui::fonts::label (10.0f));
    g.drawText (label, area.getX(), area.getY(), area.getWidth(), 12, juce::Justification::left, false);

    // 57c — `valid` decide el ESTILO, no si hay número: un valor parcial es una medición honesta de menos
    // audio, y esconderlo detrás de "--.-" durante tres segundos es lo que Joaquín llamó "el retraso al
    // cargar el medidor". Sólo se escribe "--.-" cuando de verdad no hay nada que leer.
    const auto text = hasReading (value) ? fmt1 (value) : juce::String ("--.-");
    g.setColour (valid ? th::txt : th::mut);
    g.setFont (ovni::ui::fonts::mono (heroSize));
    const int textY = area.getY() + 14;
    const int textH = juce::roundToInt (heroSize * 1.2f);
    const auto textW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text)) + 4;
    g.drawText (text, area.getX(), textY, textW, textH, juce::Justification::left, false);

    g.setColour (th::mut);
    g.setFont (ovni::ui::fonts::label (juce::jmax (9.0f, heroSize * 0.28f)));
    g.drawText (unit, area.getX() + textW + 6, textY + textH - 16, 60, 14, juce::Justification::left, false);
}

void LoudnessLens::paintBar (juce::Graphics& g, juce::Rectangle<int> area, float value, float peakHold,
                             bool partial, float cautionDb, float alertDb, float clipDbtp) const
{
    // ===== 57c · LA LÍNEA DEL UMBRAL DE CLIP =====
    //
    // Va ANTES del `return`: el umbral existe aunque todavía no haya señal, y un medidor que muestra su
    // umbral sólo cuando ya lo cruzaste llega tarde.
    if (clipDbtp > kNoReading)
    {
        const int y = area.getBottom() - juce::roundToInt (toScale01 (clipDbtp) * (float) area.getHeight());
        g.setColour (th::amber.withAlpha (0.55f));
        for (int x = area.getX(); x < area.getRight(); x += 5)   // punteada: es un umbral, no una medición
            look::fillSnapped (g, { (float) x, (float) y, 3.0f, 1.0f });
    }

    if (! hasReading (value)) return;

    const float t = toScale01 (value);
    const int   h = juce::roundToInt (t * (float) area.getHeight());
    auto fill = juce::Rectangle<int> (area.getX() + 1, area.getBottom() - h, area.getWidth() - 2, h);

    // ===== 56: ZONAS DE COLOR =====
    // El medidor era verde a cualquier nivel: había que LEER el número para saber si quedaba aire. Ahora
    // el color lo dice — verde con aire, ámbar cerca del objetivo/techo, alerta cuando ya no queda. Los
    // umbrales son en LUFS contra el techo de la escala, no un adorno: es la misma lectura que hace el
    // ojo en cualquier consola. (57c: las barras de pico pasan los suyos, que son los del dominio del
    // pico — el umbral de clip de DYNAMICS y 0 dBTP.)
    const auto zone = look::levelZone (value, cautionDb, alertDb);

    // ===== 57c · LA VENTANA QUE TODAVÍA SE ESTÁ LLENANDO =====
    //
    // Se dibuja el PARCIAL, no un hueco — pero se dibuja distinto: relleno al 55 % y un contorno fino
    // alrededor. Es la convención de un medidor que todavía no tiene su ventana entera (Insight, Youlean)
    // y es lo único honesto: el número es real, lo que le falta es audio.
    const float alphaTop = partial ? 0.52f : 0.95f, alphaBot = partial ? 0.16f : 0.30f;
    g.setGradientFill (juce::ColourGradient (zone.withAlpha (alphaTop), 0.0f, (float) area.getY(),
                                             zone.withAlpha (alphaBot), 0.0f, (float) area.getBottom(), false));
    g.fillRect (fill);
    if (partial)
    {
        g.setColour (zone.withAlpha (0.55f));
        g.drawRect (fill, 1);
    }

    // El TICK DE PICO: la marca fina que se queda arriba. Sin ella, un pico que pasó hace medio segundo
    // no dejó rastro y el medidor sólo cuenta el presente.
    if (hasReading (peakHold))
    {
        const int py = area.getBottom()
                     - juce::roundToInt (toScale01 (peakHold) * (float) area.getHeight());
        // 57b — la marca de pico con HALO: un filo de 2 px sobre un degradado se pierde; con un halo
        // detrás se encuentra de reojo, que es para lo que está.
        const auto pz = look::levelZone (peakHold, cautionDb, alertDb);
        g.setColour (pz.withAlpha (0.22f));
        look::fillSnapped (g, { (float) area.getX(), (float) (py - 3), (float) area.getWidth(), 6.0f });
        g.setColour (pz.withAlpha (0.95f));
        look::fillSnapped (g, { (float) area.getX(), (float) (py - 1), (float) area.getWidth(), 2.0f });
    }

    // El objetivo, como línea ámbar cruzando la barra. Sólo en las de LUFS: un objetivo de plataforma
    // sobre un medidor de pico sería comparar dos cosas que no se comparan.
    const auto& tgt = streamingTarget (processor.targetIndex());
    if (tgt.hasTarget && clipDbtp <= kNoReading)
    {
        const int y = area.getBottom() - juce::roundToInt (toScale01 (tgt.targetLufs) * (float) area.getHeight());
        g.setColour (th::amber.withAlpha (0.8f));
        look::fillSnapped (g, { (float) (area.getX()), (float) (y), (float) (area.getWidth()), 1.0f });
    }

    // Peak-hold: la marca del máximo desde el último RESET.
    if (hasReading (peakHold))
    {
        const int y = area.getBottom() - juce::roundToInt (toScale01 (peakHold) * (float) area.getHeight());
        g.setColour (th::txt.withAlpha (0.18f));
        look::fillSnapped (g, { (float) area.getX(), (float) (y - 2), (float) area.getWidth(), 6.0f });
        g.setColour (th::txt.withAlpha (0.85f));
        look::fillSnapped (g, { (float) area.getX(), (float) y, (float) area.getWidth(), 2.0f });
    }
}

void LoudnessLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text,
                                bool active, bool hovered_) const
{
    const auto hue = th::green;
    const auto r = area.toFloat();

    g.setColour (active ? hue.withAlpha (0.18f) : th::surf2);
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (hue.withAlpha (active ? 0.9f : (hovered_ ? 0.55f : 0.28f)));
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

    if (hovered_)   // el glow de hover del sello (state::hoverGlow), sin mover nada de layout
    {
        g.setColour (hue.withAlpha (th::state::hoverGlow));
        g.fillRoundedRectangle (r, 3.0f);
    }

    g.setColour (active ? hue : th::txt);
    g.setFont (ovni::ui::fonts::label (11.0f));
    g.drawText (text, area, juce::Justification::centred, false);
}

juce::String LoudnessLens::targetLine() const
{
    const auto& tgt = streamingTarget (processor.targetIndex());
    if (! tgt.hasTarget)             return tr (strings::Key::noTarget);
    if (! latest.integratedValid)    return juce::String (tgt.name) + juce::String::fromUTF8 (" \xc2\xb7 ")
                                          + tr (strings::Key::targetLower) + " "
                                          + juce::String (tgt.targetLufs, 0) + " LUFS";

    const float delta = latest.integrated - tgt.targetLufs;
    juce::String s (tgt.name);
    s += juce::String::fromUTF8 (" \xc2\xb7 ");

    if (std::abs (delta) < 0.05f)
        s += tr (strings::Key::matchesTarget);
    else if (delta > 0.0f)
        // Bajar lo fuerte lo hacen todas: es la definición de normalizar a un objetivo.
        s += tr (strings::Key::theyTurnYouDown) + " " + juce::String (delta, 1) + " dB";
    else if (tgt.attenuatesOnly)
        // Verificado que NO suben (Apple Sound Check, Amazon): decir "te suben" sería mentir.
        s += tr (strings::Key::youAre) + juce::String (-delta, 1)
           + tr (strings::Key::dbBelowNoRaise);
    else
        // Sin fuente verificable de qué hace con lo bajo: se dice la distancia medida, nada más.
        s += tr (strings::Key::youAre) + juce::String (-delta, 1) + tr (strings::Key::dbBelow);

    if (! tgt.official) s += " (" + tr (strings::Key::estimated) + ")";   // ver data/StreamingTargets.h
    return s;
}

//======================================================================================== animación
bool LoudnessLens::advanceFrame()
{
    const auto& f = processor.analysis().read();
    latest  = f.loudness;
    dropped = f.droppedSamples;

    processor.loudnessHistory().copyLatest (historyBuf, kHistoryPoints);

    const bool snap = prefersReducedMotion();
    const auto ease = [snap] (float& disp, float target)
    {
        if (! hasReading (target)) { disp = target; return false; }
        const float before = disp;
        disp = snap ? target : (disp <= kNoReading ? target : disp + (target - disp) * kSmoothing);
        return std::abs (disp - before) > 1.0e-3f;
    };

    // ===== 57c · MIENTRAS LA VENTANA SE LLENA, SE MUESTRA LO PARCIAL =====
    //
    // `momentary` no existe hasta los 400 ms y `shortTerm` hasta los 3 s: hasta entonces el frame lleva el
    // piso y la lente imprimía "--.-". Los parciales son la MISMA cuenta sobre los hops que hay (ver
    // analysis/modules/Loudness.h) y valen desde el primero. La bandera dice cuál de los dos se está
    // mostrando, para que la barra y el número lo digan también.
    momentaryIsPartial = ! hasReading (f.loudness.momentary) && hasReading (f.momentaryPartial);
    shortTermIsPartial = ! hasReading (f.loudness.shortTerm) && hasReading (f.shortTermPartial);
    const float mTarget = momentaryIsPartial ? f.momentaryPartial : f.loudness.momentary;
    const float sTarget = shortTermIsPartial ? f.shortTermPartial : f.loudness.shortTerm;

    tpMaxL = f.truePeakMaxL;
    tpMaxR = f.truePeakMaxR;

    bool changed = false;
    changed |= ease (dispM, mTarget);
    changed |= ease (dispS, sTarget);
    changed |= ease (dispI, f.loudness.integrated);
    changed |= ease (dispTpL, f.truePeakHopL);
    changed |= ease (dispTpR, f.truePeakHopR);
    return changed;
}

//======================================================================================== interacción
void LoudnessLens::mouseDown (const juce::MouseEvent& e)
{
    if (zones.resetBtn.contains (e.getPosition()))      processor.resetAnalysis();
    else if (zones.pauseBtn.contains (e.getPosition())) processor.setAnalysisPaused (! processor.isAnalysisPaused());
    else return;
    repaint();
}

void LoudnessLens::mouseMove (const juce::MouseEvent& e)
{
    const int was = hovered;
    hovered = zones.resetBtn.contains (e.getPosition()) ? 0
            : (zones.pauseBtn.contains (e.getPosition()) ? 1 : -1);
    if (hovered != was) repaint();
}

void LoudnessLens::mouseExit (const juce::MouseEvent&)
{
    if (hovered != -1) { hovered = -1; repaint(); }
}
}
