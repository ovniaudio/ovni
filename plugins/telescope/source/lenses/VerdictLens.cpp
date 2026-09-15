#include "lenses/VerdictLens.h"
#include "lenses/NoteName.h"
#include "lenses/Look.h"
#include "PluginProcessor.h"
#include "ui-kit/Fonts.h"
#include "ui-kit/Theme.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
namespace th = ovni::ui::theme;

constexpr const char* kAudioFilter = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.caf;*.wma";
const juce::String kDot = juce::String::fromUTF8 ("  \xc2\xb7  ");
const juce::String kDotTight = juce::String::fromUTF8 (" \xc2\xb7 ");   // 57d — la fila colapsada

// Las tres marcas de severidad del spec §5.8. Se dibujan como texto y no como iconos: entran en
// cualquier tamaño, se copian con el texto y no hay que hornear nada.
const juce::String kBadgeBad  = juce::String::fromUTF8 ("\xe2\x97\x8f");   // ●
const juce::String kBadgeWarn = juce::String::fromUTF8 ("\xe2\x9a\xa0");   // ⚠
const juce::String kBadgeInfo = juce::String::fromUTF8 ("\xe2\x97\x8b");   // ○
const juce::String kTickOk    = juce::String::fromUTF8 ("\xe2\x9c\x93");   // ✓
const juce::String kTickFail  = juce::String::fromUTF8 ("\xe2\x9c\x97");   // ✗

juce::Colour colourFor (rules::Severity s)
{
    switch (s)
    {
        case rules::Severity::bad:  return look::alert;
        case rules::Severity::warn: return look::caution;
        default:                    return look::txtSecondary;
    }
}

juce::String badgeFor (rules::Severity s)
{
    switch (s)
    {
        case rules::Severity::bad:  return kBadgeBad;
        case rules::Severity::warn: return kBadgeWarn;
        default:                    return kBadgeInfo;
    }
}

juce::String signed1 (float v) { return juce::String (v > 0.0f ? "+" : "") + juce::String (v, 1); }
}

const juce::ValueTree& VerdictLens::stateTree() const { return processor.apvts.state; }

juce::String VerdictLens::ph (const char* key) const
{
    return juce::String::fromUTF8 (
        Verdict::translate (key, processor.verdictLanguage().toRawUTF8()).c_str());
}

VerdictLens::VerdictLens (TelescopeProcessor& p) : Lens (12), processor (p)
{
    // 12 fps y no 30: acá no se anima nada, se relee un informe que cambia una vez por segundo. El
    // `settleHold` alto evita que la lente se duerma entre segundo y segundo.
    setSettleHold (40);
}

VerdictLens::~VerdictLens() = default;

//======================================================================================== geometría
VerdictLens::Zones VerdictLens::zonesFor (int w, int h) const
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
    body.removeFromTop (th::padIn / 2);
    // 57d — el TITULAR, fijo entre la cabecera y la lista: no se va con el scroll, porque es la cuenta de
    // todo lo que hay abajo.
    const auto m = look::metricsFor (w);
    z.headline = body.removeFromTop ((int) std::ceil (m.textBody + 1.0f) + 6);
    body.removeFromTop (4);
    z.list = body;
    return z;
}

int VerdictLens::textHeightOf (int i) const noexcept
{
    return (i >= 0 && i < (int) lines.size()) ? lines[(size_t) i].textHeight : 0;
}

int VerdictLens::wrappedLinesOf (int i) const noexcept
{
    return (i >= 0 && i < (int) lines.size()) ? lines[(size_t) i].layout.getNumLines() : 0;
}

float VerdictLens::layoutHeightOf (int i) const noexcept
{
    return (i >= 0 && i < (int) lines.size()) ? lines[(size_t) i].layout.getHeight() : 0.0f;
}

juce::String VerdictLens::lineTextOf (int i) const
{
    return (i >= 0 && i < (int) lines.size()) ? lines[(size_t) i].text : juce::String();
}

juce::String VerdictLens::headlineText() const
{
    return juce::String::fromUTF8 (rep.summary.headline.c_str());
}

void VerdictLens::resized()
{
    zones = zonesFor (getWidth(), getHeight());
    buildLines();
    Lens::resized();
}

//======================================================================================== el informe
void VerdictLens::rebuildReport()
{
    const auto lang = processor.verdictLanguage();
    Verdict::Inputs in;
    in.language          = lang.toRawUTF8();
    in.targetIndex       = processor.targetIndex();
    in.clipThresholdDbtp = processor.clipThresholdDbtp();

    const auto refFrame = processor.reference().read();

    if (processor.verdictMode() == TelescopeProcessor::verdictFile)
    {
        // MODO ARCHIVO: las filas y los agregados salen del análisis offline, con tiempos exactos.
        const auto a = processor.verdictAnalysis();
        if (a.valid && ! a.secondRows.empty())
        {
            rows.assign (a.secondRows.begin(), a.secondRows.end());
            in.aggregates  = Verdict::aggregatesFrom (a);
            in.rows        = rows.data();
            in.n           = (int) rows.size();
            in.firstSecond = 0;
            // La referencia de TONAL BALANCE se usa igual si está cargada: es la mejor línea de base que
            // hay, y no depende de por dónde entró el material que se está juzgando.
            if (refFrame.refValid) in.ref = &refFrame;
            rep = Verdict::evaluate (in);
            return;
        }
        rows.clear();
        rep = VerdictReport{};
        rep.footer = Verdict::translate ("footer", lang.toRawUTF8());
        return;
    }

    // MODO EN VIVO: la historia por segundo desde el último RESET.
    int first = 0;
    processor.secondHistory().copyLatest (rows, SecondHistory::kCapacity, &first);
    in.aggregates  = processor.analysis().read();
    in.rows        = rows.data();
    in.n           = (int) rows.size();
    in.firstSecond = first;
    if (refFrame.refValid) in.ref = &refFrame;
    rep = Verdict::evaluate (in);
}

// ========================================================================================================
// DE HALLAZGOS A LÍNEAS. Se arma una vez por informe (no por frame): son varias decenas de `juce::String`
// y medir texto es lo caro de esta lente.
// ========================================================================================================
// El texto ENVUELTO. Un TextLayout con ancho fijo y alto libre: JUCE parte por palabra donde no entra,
// que es lo que `drawFittedText` justamente NO hace (comprime la tipografía hasta que entre en las
// líneas que le pidas). Es la misma llamada que usa el que mide y el que dibuja.
juce::TextLayout VerdictLens::layoutFor (const juce::String& text, float fontHeight, int width,
                                         juce::Colour colour)
{
    juce::AttributedString a;
    a.setWordWrap (juce::AttributedString::byWord);
    a.setJustification (juce::Justification::topLeft);
    a.append (text, look::tabularFont (fontHeight), colour);

    juce::TextLayout l;
    l.createLayout (a, (float) juce::jmax (60, width));
    return l;
}

void VerdictLens::buildLines()
{
    lines.clear();
    const auto lang = processor.verdictLanguage();
    const auto m = look::metricsFor (juce::jmax (200, getWidth()));
    const int textW = textWidthFor (juce::jmax (200, zones.list.getWidth()));

    // El alto REAL del texto envuelto, redondeado hacia arriba. Devuelve el layout ya armado para que
    // el que dibuja no lo vuelva a calcular: medir y pintar son literalmente la misma cuenta.
    const auto wrap = [&] (Line& l, juce::Colour c)
    {
        l.layout = layoutFor (l.text, m.textBody - 0.5f, textW, c);
        l.textHeight = juce::jmax (14, (int) std::ceil (l.layout.getHeight()) + 2);
        return l.textHeight;
    };

    const auto addSection = [&] (rules::Section sec, const char* key)
    {
        Line l;
        l.kind   = Line::section;
        l.text   = juce::String::fromUTF8 (Verdict::translate (key, lang.toRawUTF8()).c_str());
        l.colour = look::dataLine;
        l.height = 22;
        lines.push_back (l);

        int count = 0;
        for (const auto& f : rep.findings)
        {
            if (f.section != sec) continue;
            ++count;
            Line r;
            r.kind     = (sec == rules::Section::translate) ? Line::deviceRow : Line::finding;
            r.text     = juce::String::fromUTF8 (f.text.c_str());
            r.evidence = juce::String::fromUTF8 (f.evidence.c_str());
            r.colour   = colourFor (f.severity);
            r.badge    = badgeFor (f.severity);

            if (sec == rules::Section::translate)
            {
                // Las cajas usan ✓ / ⚠ / ✗ en vez de ● ⚠ ○: no son defectos, son pronósticos.
                for (const auto& d : rep.devices)
                    if (d.id == f.ruleId)
                        r.badge = d.verdict == devices::DeviceVerdict::ok   ? kTickOk
                                : (d.verdict == devices::DeviceVerdict::fail ? kTickFail : kBadgeWarn);
            }

            r.height = wrap (r, look::txtPrimary) + 13;   // + la línea de evidencia
            lines.push_back (r);
        }

        if (count == 0)
        {
            Line n;
            n.kind   = Line::note;
            n.text   = juce::String::fromUTF8 (Verdict::translate ("none", lang.toRawUTF8()).c_str());
            n.colour = look::txtTertiary;
            n.height = wrap (n, look::txtTertiary);
            lines.push_back (n);
        }
    };

    // ===== 57d · "DENTRO DE RANGO", ANTES QUE NADA =====
    //
    // Lo que se evaluó y quedó dentro de rango se dice PRIMERO y compacto: una fila por regla, sin la línea de
    // evidencia debajo (la frase ya trae su número y su límite) y en el cuerpo chico. El ✓ va en gris
    // secundario y no en verde: es una medición, no un premio.
    //
    // CUÁNDO SE COLAPSA a UNA fila ("Within range: transients · body · …"): cuando el ALTO no alcanza. Dos
    // casos, los dos medidos sobre el tamaño LÓGICO de la lente:
    //   · métricas S (lienzo de menos de 740 px de ancho): con el cuerpo chico la sección desplegada se come
    //     casi toda la lista (594×376: 12 líneas = más del 90 %) y lo que hay que revisar queda fuera de vista;
    //   · en M y en L, si la desplegada pasa `kWithinMaxShare` del alto de la lista (820×562 con las doce
    //     líneas: 52 %, se despliega).
    // OJO con "tamaño S": los zooms S / M / L del editor ESCALAN el lienzo entero (PluginEditorBase::
    // applyZoom pone un transform sobre un canvas de tamaño base), así que en el editor la lente mide lo mismo
    // en los tres zooms y la sección va desplegada en los tres — S es M más chico, no una lista más corta. Las
    // métricas S aparecen con el lienzo flexible (setFlexibleCanvas) en una ventana chica.
    collapsedWithin = false;
    if (! rep.strengths.empty())
    {
        const auto t = [&] (const std::string& key)
        { return juce::String::fromUTF8 (Verdict::translate (key.c_str(), lang.toRawUTF8()).c_str()); };

        // La fila compacta avanza lo que mide su texto (no el mínimo de 14 de las frases con evidencia), y
        // reserva la misma caja que el resto de las filas: medir y pintar siguen siendo la misma cuenta.
        const auto compact = [&] (Line& l)
        {
            l.layout     = layoutFor (l.text, m.textSmall, textW, look::txtSecondary);
            l.textHeight = juce::jmax (14, (int) std::ceil (l.layout.getHeight()) + 2);
            l.height     = juce::jmax (12, (int) std::ceil (l.layout.getHeight()));
        };

        std::vector<Line> expanded;
        {
            Line title;
            title.kind   = Line::section;
            title.text   = t ("section.within");
            title.colour = look::dataLine;
            title.height = 22;
            expanded.push_back (title);
        }
        for (const auto& s : rep.strengths)
        {
            Line r;
            r.kind     = Line::strength;
            r.text     = juce::String::fromUTF8 (s.text.c_str());
            r.evidence = juce::String::fromUTF8 (s.evidence.c_str());
            r.colour   = look::txtSecondary;
            r.badge    = kTickOk;
            compact (r);
            expanded.push_back (r);
        }

        int expandedH = 0;
        for (const auto& l : expanded) expandedH += l.height + kRowGap;
        collapsedWithin = m.size == look::Size::s
                       || (float) expandedH > kWithinMaxShare * (float) juce::jmax (1, zones.list.getHeight());

        if (! collapsedWithin)
        {
            lines.insert (lines.end(), expanded.begin(), expanded.end());
        }
        else
        {
            juce::StringArray names;
            for (const auto& s : rep.strengths)
                names.add (t (std::string ("within.") + rules::rule (s.ruleId).name));

            Line one;
            one.kind   = Line::strength;
            one.text   = t ("section.within") + ": " + names.joinIntoString (kDotTight);
            one.colour = look::txtSecondary;
            one.badge  = kTickOk;
            compact (one);
            lines.push_back (one);
        }
    }

    addSection (rules::Section::feel,      "section.feel");
    addSection (rules::Section::translate, "section.translate");
    addSection (rules::Section::missing,   "section.missing");

    contentH = 0;
    for (const auto& l : lines) contentH += l.height + kRowGap;
    contentH += kSecGap * 2;
    scroll = juce::jlimit (0, juce::jmax (0, contentH - zones.list.getHeight()), scroll);
}

bool VerdictLens::advanceFrame()
{
    // El informe se REHACE cuando cambió algo que puede cambiarlo, no todos los frames: evaluar 21 reglas
    // sobre 600 filas doce veces por segundo sería tirar CPU para escribir lo mismo.
    const auto  frame    = processor.analysis().read();
    const auto  lang     = processor.verdictLanguage();
    const int   mode     = processor.verdictMode();
    const auto  fileRev  = processor.verdictFileRevision();
    const bool  refValid = processor.reference().read().refValid;

    const bool changed = frame.secondsAnalysed != lastSeconds || mode != lastMode
                      || lang != lastLanguage || fileRev != lastFileRev || refValid != lastRefValid;

    if (changed)
    {
        lastSeconds  = frame.secondsAnalysed;
        lastMode     = mode;
        lastLanguage = lang;
        lastFileRev  = fileRev;
        lastRefValid = refValid;
        rebuildReport();
        buildLines();
    }

    // Mientras un archivo se analiza hay que seguir repintando: la barra de progreso se mueve aunque el
    // informe todavía sea el mismo.
    return changed || processor.verdictFileBusy();
}

//======================================================================================== estado
juce::String VerdictLens::stateText() const
{
    const auto lang = processor.verdictLanguage();

    if (! dropMessage.isEmpty()) return dropMessage;

    if (processor.verdictMode() == TelescopeProcessor::verdictFile)
    {
        if (processor.verdictFileBusy())
            return ph ("ui.analysing") + processor.verdictFileName() + "  "
                 + juce::String ((int) std::lround (processor.verdictFileProgress() * 100.0f)) + " %";

        const auto err = processor.verdictFileError();
        if (err.isNotEmpty()) return ph ("ui.unreadable") + err;

        if (processor.verdictFilePath().isEmpty())
            return ph ("ui.drop");

        return juce::String::fromUTF8 (Verdict::translate ("mode.file", lang.toRawUTF8()).c_str())
             + kDot + processor.verdictFileName();
    }

    const auto s = juce::String::fromUTF8 (Verdict::translate ("mode.live", lang.toRawUTF8()).c_str());
    if (rep.summary.seconds <= 0)
        return s + kDot + ph ("ui.nosecs");
    return s + kDot + juce::String (rep.summary.seconds) + " s";
}

//======================================================================================== controles
void VerdictLens::pressControl (int control)
{
    dropMessage.clear();

    if (control == ctrlReset)
    {
        processor.resetAnalysis();
        lastSeconds = 0xffffffffu;   // el informe se rehace de cero en el próximo frame
    }
    else if (control == ctrlMode)
    {
        processor.setVerdictMode (processor.verdictMode() == TelescopeProcessor::verdictLive
                                    ? TelescopeProcessor::verdictFile : TelescopeProcessor::verdictLive);
    }
    else if (control == ctrlLoad)
    {
        chooser = std::make_unique<juce::FileChooser> (
            ph ("ui.choose"),
            juce::File(), kAudioFilter);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<VerdictLens> (this)]
                              (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto f = fc.getResult();
                                  if (f.existsAsFile()) safe->processor.loadVerdictFile (f);
                              });
    }
    else return;

    lastMode = -1;   // fuerza el rehacer del informe
    repaint();
}

void VerdictLens::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) { pressControl (i); return; }
}

void VerdictLens::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < kNumControls; ++i)
        if (zones.button[i].contains (e.getPosition())) h = i;
    if (h != hovered) { hovered = h; repaint(); }
}

void VerdictLens::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void VerdictLens::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const int max = juce::jmax (0, contentH - zones.list.getHeight());
    const int want = juce::jlimit (0, max, scroll - (int) std::lround (w.deltaY * 90.0f));
    if (want != scroll) { scroll = want; repaint(); }
}

//======================================================================================== drag & drop
bool VerdictLens::isInterestedInFileDrag (const juce::StringArray& files) { return files.size() == 1; }
void VerdictLens::fileDragEnter (const juce::StringArray&, int, int) { dragOver = true; repaint(); }
void VerdictLens::fileDragExit (const juce::StringArray&)            { dragOver = false; repaint(); }

void VerdictLens::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    dropMessage.clear();
    if (files.isEmpty()) { repaint(); return; }

    const juce::File f (files[0]);
    if (! f.existsAsFile())
    {
        dropMessage = ph ("ui.notafile");
        repaint();
        return;
    }

    // NO se filtra por extensión: un AIFF llamado .dat tiene que poder entrar. Lo que sí se hace es
    // rechazar de entrada lo que seguro no es audio, con el motivo, en vez de dejar que el analizador
    // devuelva "formato no reconocido" tres segundos después.
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".txt" || ext == ".md" || ext == ".pdf" || ext == ".json" || ext == ".xml")
    {
        dropMessage = juce::String::fromUTF8 ("\"") + f.getFileName()
                    + ph ("ui.notaudio") + ext + ")";
        repaint();
        return;
    }

    processor.loadVerdictFile (f);
    lastMode = -1;
    repaint();
}

//======================================================================================== capa estática
void VerdictLens::renderStatic (juce::Graphics& g, int width, int height)
{
    zones = zonesFor (width, height);

    const auto m = look::metricsFor (width);
    g.setColour (look::well);
    g.fillRect (0, 0, width, height);

    // El separador de la cabecera es de la jerarquía MAYOR (lo que estructura la pantalla); los de
    // sección, más abajo, son de la MENOR. Antes los dos eran `theme::line` y el panel se leía como una
    // lista plana, que es el problema exacto que Look.h vino a resolver en las otras doce lentes.
    look::hLine (g, (float) zones.head.getX(), (float) zones.head.getRight(),
                 look::snap1px ((float) zones.head.getBottom() + 2.0f, look::physicalScale (g)),
                 look::gridMajor, m.gridMajorW);
}

//======================================================================================== capa viva
// La cadena de TONALIDAD de la cabecera, como función y no como cuatro líneas adentro del pintado: es lo
// que el test compara contra lo que pone CQT en su pie. Hasta el 56b eran dos códigos distintos —acá un
// array local de letras inglesas, allá NoteName.h— y decían cosas distintas en cinco de los seis idiomas.
juce::String VerdictLens::keyText (int tonic, int mode, const juce::String& language)
{
    if (tonic < 0 || tonic > 11 || mode < 0) return "--";
    // La MISMA función que CQT y SPIRAL. El array local de letras inglesas que vivía acá hacía que la
    // misma tonalidad saliera "Do menor" en CQT y "C menor" en VERDICT, en la misma pantalla.
    return keyLabel (tonic, mode, language);
}

void VerdictLens::paintHead (juce::Graphics& g) const
{
    const auto& s = rep.summary;
    auto area = zones.head;

    const auto m = look::metricsFor (getWidth());
    // TABULAR de verdad (Look.h): son cinco cifras que cambian juntas y a distinto ancho — con una
    // proporcional, "I -14.6" y "I -8.4" corren de lugar a LRA, PLR y todo lo que sigue en cada frame.
    g.setFont (look::tabularFont (m.textNumber - 2.0f));
    g.setColour (look::txtPrimary);

    // LA CABECERA TABULAR (spec §5.8): I · LRA · PLR · correlación · tonalidad. Es el contexto sin el cual
    // las frases de abajo no significan nada — y es lo único que se muestra sin regla, porque son las
    // mediciones mismas.
    const auto lang = processor.verdictLanguage();
    const auto key = keyText (s.keyTonic, s.keyMode, lang);

    juce::String top;
    top << "I " << (s.integrated <= kSilenceDb + 1.0f ? juce::String ("--") : signed1 (s.integrated)) << " LUFS"
        << kDot << "LRA " << juce::String (s.lra, 1) << " LU"
        << kDot << "PLR " << juce::String (s.plr, 1) << " dB"
        << kDot << "corr " << juce::String (s.corr, 2)
        << kDot << key;
    g.drawText (top, area.removeFromTop (18), juce::Justification::centredLeft, false);

    g.setFont (look::tabularFont (m.textSmall));
    g.setColour (look::txtSecondary);
    // Cuántos segundos, y CONTRA QUÉ se comparó la sección 1. Lo segundo no es un detalle: es la
    // diferencia entre "no se parece al resto de tu mezcla" y "no se parece a tu referencia".
    juce::String sub = juce::String::fromUTF8 (s.text.c_str());
    sub << kDot << juce::String::fromUTF8 (Verdict::translate (s.usedReference ? "baseline.reference"
                                                                              : "baseline.trend",
                                                              lang.toRawUTF8()).c_str());
    if (s.truePeakMax > kSilenceDb + 1.0f)
        sub << kDot << "TP " << signed1 (s.truePeakMax) << " dBTP";
    g.drawText (sub, area.removeFromTop (14), juce::Justification::centredLeft, false);

    g.setColour (look::txtTertiary);
    g.drawText (stateText(), area, juce::Justification::centredLeft, false);
}

// ===== 57d · EL TITULAR =====
// La cuenta, a la izquierda y en el cuerpo del panel; su evidencia (`headline · n/m`), en gris y a la derecha,
// como la de cualquier hallazgo. Sin un segundo analizado el motor lo deja vacío y acá no se dibuja nada: la
// línea de estado de la cabecera ya dice que todavía no hay segundos.
void VerdictLens::paintHeadline (juce::Graphics& g) const
{
    if (rep.summary.headline.empty() || zones.headline.isEmpty()) return;

    const auto m = look::metricsFor (getWidth());
    auto area = zones.headline;

    const auto evidence = juce::String::fromUTF8 (rep.summary.headlineEvidence.c_str());
    g.setFont (look::tabularFont (m.textMicro + 0.5f));
    const int evW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), evidence)) + 2;
    if (evW < area.getWidth() / 3)
    {
        g.setColour (look::txtTertiary);
        g.drawText (evidence, area.removeFromRight (evW), juce::Justification::centredRight, false);
        area.removeFromRight (8);
    }

    g.setColour (look::txtPrimary);
    g.setFont (look::tabularFont (m.textBody + 1.0f));
    g.drawText (juce::String::fromUTF8 (rep.summary.headline.c_str()), area, juce::Justification::centredLeft, true);
}

void VerdictLens::paintList (juce::Graphics& g) const
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (zones.list);

    const auto m = look::metricsFor (getWidth());
    const int  textW = textWidthFor (zones.list.getWidth());
    int y = zones.list.getY() - scroll;
    const int x = zones.list.getX();
    const int w = zones.list.getWidth();

    for (const auto& l : lines)
    {
        const int bottom = y + l.height;
        if (bottom >= zones.list.getY() && y <= zones.list.getBottom())
        {
            if (l.kind == Line::section)
            {
                g.setColour (l.colour);
                g.setFont (look::labelFont (m.textSmall + 1.0f));
                g.drawText (l.text.toUpperCase(), x, y + 4, w, 14, juce::Justification::topLeft, false);
                look::hLine (g, (float) x, (float) (x + w),
                             look::snap1px ((float) (y + 20), look::physicalScale (g)),
                             look::gridMinor, m.gridMinorW);
            }
            else if (l.kind == Line::note)
            {
                l.layout.draw (g, juce::Rectangle<float> ((float) (x + kTextIndent), (float) y,
                                                          (float) textW, (float) l.textHeight));
            }
            else if (l.kind == Line::strength)
            {
                // 57d — "Dentro de rango": ✓ en gris secundario, cuerpo chico y sin evidencia debajo.
                g.setColour (l.colour);
                g.setFont (look::tabularFont (m.textSmall + 0.5f));
                g.drawText (l.badge, x, y, 18, 14, juce::Justification::topLeft, false);
                l.layout.draw (g, juce::Rectangle<float> ((float) (x + kTextIndent), (float) y,
                                                          (float) textW, (float) l.textHeight));
            }
            else
            {
                g.setColour (l.colour);
                g.setFont (look::tabularFont (m.textBody + 0.5f));
                g.drawText (l.badge, x, y, 18, 14, juce::Justification::topLeft, false);

                const int textH = l.textHeight;
                l.layout.draw (g, juce::Rectangle<float> ((float) (x + kTextIndent), (float) y,
                                                          (float) textW, (float) textH));

                // LA EVIDENCIA, en gris, debajo: el número y el id de la regla. Sin esto la frase sería
                // una opinión, y con esto es una medición con su procedencia (D-47).
                g.setColour (look::txtTertiary);
                g.setFont (look::tabularFont (m.textMicro + 0.5f));
                g.drawText (l.evidence, x + kTextIndent, y + textH, textW, 12,
                            juce::Justification::topLeft, false);
            }
        }
        y = bottom + kRowGap;
    }

    // La barra de desplazamiento, sólo si hace falta.
    const int max = contentH - zones.list.getHeight();
    if (max > 0)
    {
        const float frac = (float) zones.list.getHeight() / (float) contentH;
        const int barH = juce::jmax (24, (int) ((float) zones.list.getHeight() * frac));
        const int barY = zones.list.getY()
                       + (int) ((float) (zones.list.getHeight() - barH) * ((float) scroll / (float) max));
        g.setColour (look::dataLine.withAlpha (0.28f));
        g.fillRoundedRectangle ((float) (zones.list.getRight() - 3), (float) barY, 3.0f, (float) barH, 1.5f);
    }
}

void VerdictLens::paintButton (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label,
                               const juce::String& value, bool hovered_) const
{
    const auto m = look::metricsFor (getWidth());
    const auto r = area.toFloat();
    g.setColour (look::surfaceHi);
    g.fillRoundedRectangle (r, m.radius);
    g.setColour (look::dataLine.withAlpha (hovered_ ? 0.55f : 0.24f));
    g.drawRoundedRectangle (r.reduced (0.5f), m.radius, 1.0f);
    if (hovered_)
    {
        g.setColour (look::dataLine.withAlpha (th::state::hoverGlow));
        g.fillRoundedRectangle (r, m.radius);
    }

    g.setColour (look::txtTertiary);
    g.setFont (look::labelFont (m.textMicro));
    g.drawText (label, area.reduced (6, 2).removeFromTop (10), juce::Justification::centredLeft, false);
    g.setColour (look::dataLine);
    g.setFont (look::tabularFont (m.textBody - 0.5f));
    g.drawText (value, area.reduced (6, 2).withTrimmedTop (9), juce::Justification::centredLeft, false);
}

void VerdictLens::paintLive (juce::Graphics& g)
{
    if (zones.list.isEmpty()) { zones = zonesFor (getWidth(), getHeight()); buildLines(); }

    paintHead (g);
    paintHeadline (g);
    paintList (g);

    // La barra de progreso del análisis de archivo, sobre la lista.
    if (processor.verdictFileBusy())
    {
        auto bar = zones.list.removeFromTop (0).withY (zones.list.getBottom() - 4)
                                               .withHeight (3).withWidth (zones.list.getWidth());
        // 57b — la misma regla que el resto del instrumento: relleno con degradado y filo brillante.
        g.setColour (look::surfaceHi);
        g.fillRect (bar.toFloat());
        const float fw = (float) bar.getWidth() * processor.verdictFileProgress();
        look::dataBar (g, bar.toFloat().withWidth (fw), look::dataLine, 1.0f);
        g.setColour (look::dataLine.withAlpha (0.95f));
        g.fillRect (juce::Rectangle<float> (bar.toFloat().getX() + juce::jmax (0.0f, fw - 1.5f),
                                            bar.toFloat().getY(), 1.5f, bar.toFloat().getHeight()));
    }

    const auto m = look::metricsFor (getWidth());

    if (dragOver)
    {
        g.setColour (look::dataLine.withAlpha (0.10f));
        g.fillRect (zones.list);
        g.setColour (look::dataLine.withAlpha (0.6f));
        g.drawRect (zones.list, 2);
    }

    const auto lang = processor.verdictLanguage();
    const bool file = processor.verdictMode() == TelescopeProcessor::verdictFile;
    // Los rótulos de los botones salen de la MISMA tabla que las frases: un informe en inglés con los
    // botones en castellano sería justo la mitad de lo que D-50 vino a arreglar. (El resto de las lentes
    // pasa a esta tabla en el prompt 56.)
    const auto t = [&] (const char* key) { return juce::String::fromUTF8 (Verdict::translate (key, lang.toRawUTF8()).c_str()); };

    paintButton (g, zones.button[ctrlReset], t ("ui.reset"), t ("ui.reset.value"), hovered == ctrlReset);
    paintButton (g, zones.button[ctrlMode],  t ("ui.mode"),
                 t (file ? "mode.file" : "mode.live").upToFirstOccurrenceOf (" ", false, false),
                 hovered == ctrlMode);
    paintButton (g, zones.button[ctrlLoad],  t ("ui.file"), t ("ui.file.value"), hovered == ctrlLoad);

    // EL PIE, siempre. No es decorativo: es la parte del informe que dice qué clase de cosa es el informe.
    g.setColour (look::txtTertiary);
    g.setFont (look::tabularFont (m.textMicro + 0.5f));
    g.drawText (juce::String::fromUTF8 (rep.footer.c_str()),
                zones.list.getX(), zones.footer.getY() - 12, zones.list.getWidth(), 11,
                juce::Justification::topRight, false);
}
}
