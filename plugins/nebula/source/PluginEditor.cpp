#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace nebula
{

namespace pid = nebula::params::id;
namespace th  = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del mockup nebula-a (grid del cuerpo, coords base 900×600).
namespace metrics
{
    inline constexpr int railLW = 130;   // rail izquierdo (BREATH SYNC + specs)
    inline constexpr int utilW  = 116;   // columna de utilidad (rebaje, altura COMPLETA)
    inline constexpr int macroH = 122;   // rail inferior de macros (mockup grid-rows: 1fr 122px)
    inline constexpr int padIn  = 12;    // padding interno de los rails
}

NebulaEditor::NebulaEditor (NebulaProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("SPC·01")),   // designación tras el wordmark
      proc (p),
      // La nube lee las telemetrías que reacciona (size/decay/tone/breath + fase REAL del BreathLFO
      // del motor — respira en fase con el audio) y ESCRIBE size/decay en el drag.
      cloud (p.uiSize, p.uiDecay, p.uiTone, p.uiBreath, p.uiBreathLfo,
             p.apvts.getParameter (pid::SIZE), p.apvts.getParameter (pid::DECAY)),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::magenta),
      syncCtl (p.apvts, juce::String(), pid::BREATHSYNC, pid::BREATHDIV,
               nebula::params::sync::labels(), th::magenta,
               juce::String::fromUTF8 ("ORGANIC DRIFT"))   // rateParamID vacío = FREE orgánico sin knob;
               // el caption llena el slot en FREE (antes quedaba un hueco mudo en el rail izquierdo)
{
    setFamilyHue (th::magenta);   // Espacio/Profundidad: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup (48px)

    // Bottom-bar contextual (doctrina del sello): muestra NOMBRE + VALOR del control bajo el cursor en mono.
    // Hue de familia = magenta (Espacio/Profundidad, FDN). En reposo, un hint discreto con la designación.
    enableBottomBar (th::magenta, juce::String::fromUTF8 ("SPC\xc2\xb7" "01  \xe2\x80\x94  hover a control to read it"));
    bottomBar().setSignature (juce::String::fromUTF8 ("SIGNAL STABLE"));

    addToCanvas (cloud);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (syncCtl);

    // accesibilidad: nombres accesibles + foco de teclado en los controles del chasis/core.
    for (auto* c : { (juce::Component*) &inPhase, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    syncCtl.setTitle (juce::String::fromUTF8 ("BREATH SYNC (to tempo)"));
    cloud.setTitle   (juce::String::fromUTF8 ("NEBULA \xe2\x80\x94 nebular field (drag to sculpt Size \xc3\x97 Decay)"));

    setupKnob (size,   pid::SIZE,   "SIZE",    84);
    setupKnob (decay,  pid::DECAY,  "DECAY",   84);
    setupKnob (tone,   pid::TONE,   "TONE",    84);
    setupKnob (breath, pid::BREATH, "BREATH",  84);
    setupKnob (mix,    pid::MIX,    "MIX",     84);
    setupKnob (lowCut, pid::LOWCUT, "LOW",     50);   // par de filtros del wet (columna de utilidad, arriba de IN/OUT)
    setupKnob (hiCut,  pid::HICUT,  "HI",      50);   // HP del wet (graves) + LP del wet (agudos)
    setupKnob (in,     "inGain",    "IN",      50);   // gain del chasis (utilidad)
    setupKnob (out,    "output",    "OUT",     50);

    setBaseSize (900, 600);   // tamaño de diseño de NÉBULA (coords base; resize S/M/L gratis)
}

NebulaEditor::~NebulaEditor()
{
    // Soltar el L&F antes de destruir los sliders (regla JUCE).
    for (auto* k : { &size, &decay, &tone, &breath, &mix, &lowCut, &hiCut, &in, &out })
        k->slider.setLookAndFeel (nullptr);
}

void NebulaEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text, int textBoxW)
{
    // OvniKnob ya viene con velocity-drag + Shift-fino + cmd-click-reset + doble-click-tipear + estados.
    k.slider.setKnobLookAndFeel (&knobLaf);
    k.slider.getProperties().set ("hue", (int) th::magenta.getARGB());   // familia Espacio/Profundidad (FDN)
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxW, 15);
    k.slider.setColour (juce::Slider::textBoxTextColourId, th::fnt);     // el valor NO grita: lectura en bottom-bar

    // accesibilidad: nombre accesible + foco de teclado (el OvniKnob ya pide foco).
    k.slider.controlName = text;
    k.slider.setName  (text);
    k.slider.setTitle (text);

    // hover → bottom-bar contextual (nombre + valor); exit → vuelve al hint.
    k.slider.onHover     = [this] (const juce::String& nm, const juce::String& v) { bottomBar().show (nm, v); };
    k.slider.onHoverExit = [this] { bottomBar().clear(); };
    addToCanvas (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, th::txt);   // nombre LEGIBLE (claro), no el gris apagado
    k.label.setFont (fonts::mono (12.0f));
    addToCanvas (k.label);

    k.attach = std::make_unique<SliderAttachment> (proc.apvts, paramID, k.slider);

    // cmd/ctrl-click = reset al DEFAULT del parámetro. (doble-click NO resetea: abre el editor de texto.)
    if (auto* pr = proc.apvts.getParameter (paramID))
    {
        const auto& range = proc.apvts.getParameterRange (paramID);
        k.slider.setDoubleClickReturnValue (false, (double) range.convertFrom0to1 (pr->getDefaultValue()));
    }
}

// =================================================================================================
void NebulaEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== grid del mockup: railL 130 · campo 1fr · util 116 (altura COMPLETA) · macro 122 (cols 1-2) =====
    // REGLA DE ORO (Diccionario §4): quitar la util PRIMERO (altura completa, derecha) → regiones disjuntas,
    // imposible que el IN PHASE (en la util) se pise con el SYNC (en el rail izquierdo). Recién después
    // el rail de macros se quita del BOTTOM de lo que queda (railL + campo) → el macro NO va bajo la util.
    utilArea = body.removeFromRight (utilW);

    auto macroBar = body.removeFromBottom (macroH);   // rail inferior (cols 1-2: bajo railL + campo)

    railLArea = body.removeFromLeft (railLW);
    cloud.setBounds (body);                            // EL CAMPO NEBULAR domina (es el control principal)

    // ===== rail IZQUIERDO: BREATH SYNC (vertical) + specBlock decorativo =====
    {
        auto col = railLArea.reduced (padIn, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (18);                                    // cap "BREATH SYNC" (paint)
        // SyncControl en modo VERTICAL (rail angosto: alto > ancho): [FREE|SYNC] arriba, chips 2×2 abajo +
        // caption "DIVISION". 130px de ancho menos padding → ~106px; alto ~150 cubre toggle + chips.
        syncCtl.setBounds (col.removeFromTop (150));

        // specBlock decorativo (mockup #specBlock): credenciales del motor FDN, ancla abajo.
        col.removeFromTop (16);
        specArea = col.removeFromBottom (juce::jmin (84, col.getHeight()));
    }

    // ===== columna de UTILIDAD (altura completa): SALIDA (meter fill) · FILTRO [LOW|HI] · I/O [IN|OUT] · IN PHASE =====
    // De ABAJO hacia ARRIBA (el meter absorbe lo que sobra arriba = fill vertical, como el mockup #meterRow flex).
    {
        auto col = utilArea.reduced (8, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (16);                                    // cap "SALIDA" (paint)

        // IN PHASE al pie (modo vertical: la celda es más alta que ancha → símbolo+main+sub apilados).
        inPhase.setBounds (col.removeFromBottom (juce::jmin (62, col.getHeight())));
        col.removeFromBottom (12);

        // Fila de DOS knobs chicos lado a lado (la usan LOW/HI y IN/OUT). Cada celda: cap (paint) NO acá;
        // knob centrado (≤48 px) arriba, label legible (14 px) abajo. DRY: una sola geometría.
        auto layoutTwoKnobRow = [] (juce::Rectangle<int> row, Knob& a, Knob& b)
        {
            auto cellA = row.removeFromLeft (row.getWidth() / 2);
            auto cellB = row;
            a.label.setBounds  (cellA.removeFromBottom (14));
            a.slider.setBounds (cellA.withSizeKeepingCentre (juce::jmin (cellA.getWidth(), 48), cellA.getHeight()));
            b.label.setBounds  (cellB.removeFromBottom (14));
            b.slider.setBounds (cellB.withSizeKeepingCentre (juce::jmin (cellB.getWidth(), 48), cellB.getHeight()));
        };

        // I/O [IN | OUT] (abajo del par de filtros)
        layoutTwoKnobRow (col.removeFromBottom (74), in, out);
        col.removeFromBottom (8);   // gap + label "I/O" (paint) cae en este margen
        col.removeFromBottom (14);  // espacio del cap "I/O"

        // FILTRO [LOW | HI] (arriba de I/O, pedido del dueño)
        layoutTwoKnobRow (col.removeFromBottom (74), lowCut, hiCut);
        col.removeFromBottom (8);
        col.removeFromBottom (14);  // espacio del cap "FILTRO"

        // El meter absorbe TODO lo que queda arriba (LED + barra alta + "OUT"); modo vertical (alto > ancho).
        meter.setBounds (col.removeFromTop (juce::jmax (60, col.getHeight())));
    }

    // ===== rail INFERIOR de macros: SIZE/DECAY/TONE/BREATH/MIX (reparten el ancho) =====
    {
        auto rail = macroBar.reduced (14, 0).withTrimmedTop (8).withTrimmedBottom (10);
        Knob* knobs[] = { &size, &decay, &tone, &breath, &mix };
        const char* idx[] = { "01", "02", "03", "04", "05" };
        const int n = 5;
        const int kw = rail.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = rail.removeFromLeft (i == n - 1 ? rail.getWidth() : kw);
            knobs[i]->label.setBounds (cell.removeFromBottom (16));
            knobs[i]->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 84),
                                                                    juce::jmin (cell.getHeight(), 84)));
            knobIdx.push_back ({ idx[i], { knobs[i]->slider.getX() - 2, knobs[i]->slider.getY() - 1 } });
        }
    }
}

// =================================================================================================
void NebulaEditor::paintBody (juce::Graphics& g)
{
    const auto fL = railLArea.toFloat();
    const auto fU = utilArea.toFloat();
    const auto fC = cloud.getBounds().toFloat();

    // ===== luz volumétrica magenta que emana del campo nebular (mockup #plugin::before) =====
    {
        const auto pc = fC.getCentre();
        juce::ColourGradient halo (th::magenta.withAlpha (0.06f), pc.x, pc.y,
                                   th::magenta.withAlpha (0.0f),  pc.x, pc.y - 300.0f, true);
        halo.addColour (0.55, th::magenta.withAlpha (0.02f));
        g.setGradientFill (halo);
        g.fillRect (fC.expanded (60.0f, 30.0f));
    }

    // ===== rail IZQUIERDO ELEVADO: sheen arriba → caída, hairline + sombra hacia el campo (mockup #railL) =====
    {
        juce::ColourGradient surf (juce::Colour (0x09a0c0e0), 0.0f, fL.getY(),
                                   th::bg0.withAlpha (0.16f),  0.0f, fL.getBottom(), false);
        surf.addColour (0.34, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (fL);
        g.setColour (juce::Colour (0x12bee1ff));                       // sheen superior
        g.fillRect (fL.getX(), fL.getY(), fL.getWidth(), 1.0f);
        g.setColour (juce::Colour (0x1fa0c0e0));                       // hairline del borde al campo
        g.fillRect (fL.getRight() - 1.0f, fL.getY(), 1.0f, fL.getHeight());
        const float sw = 14.0f;                                        // caída de sombra hacia el campo
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f),  fL.getRight() - sw, 0.0f,
                                 juce::Colours::black.withAlpha (0.42f), fL.getRight(),      0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fL.getRight() - sw, fL.getY(), sw, fL.getHeight()));
    }

    // ===== columna de UTILIDAD: REBAJE del chasis (más oscura, sombra desde la izquierda — mockup #util) =====
    {
        juce::ColourGradient rec (th::bg0.withAlpha (0.34f), 0.0f, fU.getY(),
                                  th::bg0.withAlpha (0.40f), 0.0f, fU.getBottom(), false);
        rec.addColour (0.40, th::bg0.withAlpha (0.18f));
        g.setGradientFill (rec);
        g.fillRect (fU);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (fU.getX(), fU.getY(), 1.0f, fU.getHeight());
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.45f), fU.getX(), 0.0f,
                                 juce::Colours::black.withAlpha (0.0f),  fU.getX() + 12.0f, 0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fU.getX(), fU.getY(), 12.0f, fU.getHeight()));
        g.setColour (juce::Colour (0x0abee1ff));
        g.fillRect (fU.getX(), fU.getBottom() - 1.0f, fU.getWidth(), 1.0f);
    }

    // ===== caps de sección (mono chico, tracking ancho — mockup .secCap) =====
    auto cap = [&g] (const juce::String& s, int x, int y)
    {
        g.setColour (th::fnt);
        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.26f));
        g.drawText (s, x, y, 200, 12, juce::Justification::centredLeft);
    };
    cap (juce::String::fromUTF8 ("BREATH SYNC"), railLArea.getX() + 14, railLArea.getY() + 16);
    cap (juce::String::fromUTF8 ("OUTPUT"),      utilArea.getX() + 12,  utilArea.getY() + 16);

    // caps internos de la utilidad: "FILTRO" sobre [LOW|HI] · "I/O" sobre [IN|OUT] (centrados sobre cada par).
    auto capCentred = [&g] (const juce::String& s, juce::Rectangle<int> over)
    {
        g.setColour (th::fnt);
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        g.drawText (s, over.getX(), over.getY() - 13, over.getWidth(), 11, juce::Justification::centred);
    };
    capCentred (juce::String::fromUTF8 ("FILTER"), lowCut.slider.getBounds().getUnion (hiCut.slider.getBounds()));
    capCentred (juce::String::fromUTF8 ("I/O"),    in.slider.getBounds().getUnion (out.slider.getBounds()));

    // ===== specBlock decorativo (mockup #specBlock): credenciales del motor FDN =====
    if (! specArea.isEmpty())
    {
        g.setColour (th::lineSoft);
        g.fillRect (specArea.getX(), specArea.getY(), 1, specArea.getHeight());
        // credenciales HONESTAS del motor (Manifiesto #2): 8 líneas Householder, breath, allpass
        // de inyección, early reflections FIR. ("16×" y "SHIMMER" eran del mockup: mentían — el
        // shimmer es identidad de HALO, no de NÉBULA.)
        const char* const specs[] = { "FDN NET 8\xc3\x97", "MODULATION", "DIFFUSION", "EARLY FIR" };
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.22f));
        int y = specArea.getY() + 4;
        for (auto* s : specs)
        {
            g.setColour (th::magenta.withAlpha (0.4f));
            g.drawText (juce::String::fromUTF8 ("\xc2\xb7"), specArea.getX() + 8, y, 8, 14, juce::Justification::centredLeft);
            g.setColour (th::fnt);
            g.drawText (juce::String::fromUTF8 (s), specArea.getX() + 16, y, specArea.getWidth() - 16, 14,
                        juce::Justification::centredLeft);
            y += 18;
        }
    }

    // ===== índices de knob (01…05, mono fantasma — mockup .kidx) =====
    g.setColour (th::fnt);
    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.08f));
    for (const auto& ki : knobIdx)
        g.drawText (ki.idx, ki.pos.x, ki.pos.y, 18, 10, juce::Justification::centredLeft);

    // ===== hint de esculpido (mockup .hintTag): la nube ES el control, centrado al pie del campo =====
    g.setColour (th::magenta.withAlpha (0.42f));
    g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.22f));
    g.drawText (juce::String::fromUTF8 ("DRAG \xc2\xb7 \xe2\x86\x94 SIZE \xc2\xb7 \xe2\x86\x95 DECAY"),
                cloud.getX(), cloud.getBottom() - 18, cloud.getWidth(), 12, juce::Justification::centred);
}

} // namespace nebula
