#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace dust
{

namespace pid   = dust::params::id;
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del mockup dust-a (grid del cuerpo, coords base 960×580).
namespace metrics
{
    inline constexpr int railLW  = 130;   // rail izquierdo elevado (EJE / CADENCIA = SyncControl vertical)
    inline constexpr int utilW   = 126;   // columna de utilidad (rebaje del chasis)
    inline constexpr int macrosH = 122;   // rail inferior de macros (igual que NÉBULA/AURORA/HORIZON → knobs 84 px)
    inline constexpr int padIn   = 12;    // padding interno de los rails
}

DustEditor::DustEditor (DustProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("MOV·03")),   // designación tras el wordmark
      proc (p),
      field (p.uiMix, p.uiRateNorm, p.uiDensity, p.uiSpread, p.uiVida, p.uiDuckGr,
             p.uiOriginX, p.uiOriginY, p.uiWetRms, p.bubbleEvents,
             p.apvts.getParameter (pid::ORIGINX), p.apvts.getParameter (pid::ORIGINY)),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::cyan),
      syncCtl (p.apvts, pid::RATE, pid::RATESYNC, pid::RATEDIV, dust::params::sync::labels(), th::cyan)
{
    setFamilyHue (th::cyan);   // Movimiento: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup dust-a (48px)

    // Bottom-bar contextual (doctrina del sello): NOMBRE + VALOR del control bajo el cursor.
    // Hue de familia = cian (Movimiento). El hint enseña la interacción estrella: el drag del ORIGIN.
    enableBottomBar (th::cyan,
                     juce::String::fromUTF8 ("MOV\xc2\xb7""03  -  drag the ORIGIN across the field"));
    bottomBar().setSignature (juce::String::fromUTF8 ("SIGNAL STABLE"));

    addToCanvas (field);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (syncCtl);

    // Accesibilidad: nombres accesibles + foco de teclado en los controles del chasis/core.
    for (auto* c : { (juce::Component*) &inPhase, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    syncCtl.setTitle (juce::String::fromUTF8 ("RATE (echo spacing, free/tempo)"));
    field.setTitle   (juce::String::fromUTF8 ("BUBBLE FIELD - ORIGIN of the echoes (drag)"));

    setupKnob (density, pid::DENSITY, "DENSITY");
    setupKnob (spread,  pid::SPREAD,  "SPREAD");
    setupKnob (vida,    pid::VIDA,    "LIFE");
    setupKnob (mix,     pid::MIX,     "MIX");
    setupKnob (duck,    pid::DUCK,    "DUCK");    // columna de utilidad, arriba de IN/OUT (curaduría)
    setupKnob (lowCut,  pid::LOWCUT,  "LOW");     // par de filtros de la SALIDA (HP) → utilidad, bajo el meter
    setupKnob (hiCut,   pid::HICUT,   "HI");      // par de filtros de la SALIDA (LP) → utilidad
    setupKnob (in,      "inGain",     "IN");      // gain del chasis (utilidad)
    setupKnob (out,     "output",     "OUT");

    setBaseSize (960, 580);
}

DustEditor::~DustEditor()
{
    // Soltar el L&F antes de destruir los sliders (regla JUCE; el SyncControl suelta el suyo solo).
    for (auto* k : { &density, &spread, &vida, &mix, &duck, &lowCut, &hiCut, &in, &out })
        k->slider.setLookAndFeel (nullptr);
}

void DustEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text)
{
    // OvniKnob ya viene con velocity-drag + Shift-fino + cmd-click-reset + doble-click-tipear + estados.
    k.slider.setKnobLookAndFeel (&knobLaf);
    k.slider.getProperties().set ("hue", (int) th::cyan.getARGB());   // familia Movimiento

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
    k.label.setFont (ovni::ui::fonts::mono (12.0f));
    addToCanvas (k.label);

    k.attach = std::make_unique<SliderAttachment> (proc.apvts, paramID, k.slider);

    // cmd/ctrl-click = reset al DEFAULT del parámetro (el attach ya inicializó el slider al valor actual =
    // su default al construir el editor). doble-click NO resetea: abre el editor de texto del OvniKnob.
    if (auto* pr = proc.apvts.getParameter (paramID))
    {
        const auto& range = proc.apvts.getParameterRange (paramID);
        k.slider.setDoubleClickReturnValue (false, (double) range.convertFrom0to1 (pr->getDefaultValue()));
    }
}

void DustEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== grid del mockup dust-a: railL 130 · [campo / macros] 1fr · util 126 =====
    // Regla de oro (Diccionario §4): el rail se quita PRIMERO, la utilidad DESPUÉS → regiones
    // disjuntas por construcción; el SYNC (railL) queda lejísimos del IN PHASE (util).
    railLArea = body.removeFromLeft (railLW);
    utilArea  = body.removeFromRight (utilW);

    // El rail de MACROS vive SOLO bajo el campo (columna central), no a ancho completo (mockup).
    macrosArea = body.removeFromBottom (macrosH);
    field.setBounds (body);   // el campo de burbujas domina el resto (≈60% del cuerpo)

    // ===== rail IZQUIERDO: EJE / CADENCIA (SyncControl vertical) + specBlock decorativo =====
    {
        auto col = railLArea.reduced (padIn, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (20);                          // cap "EJE / CADENCIA" (paint)
        syncCtl.setBounds (col.removeFromTop (juce::jmin (190, col.getHeight() - 80)));
        knobIdx.push_back ({ juce::String::fromUTF8 ("\xc2\xb7"),   // "·" índice del RATE (mockup)
                             { syncCtl.getX() + 2, syncCtl.getY() + 30 } });
        col.removeFromTop (14);
        specArea = col;                                  // credenciales del motor (paint)
    }

    // ===== columna de UTILIDAD/SALIDA (orden HALO, igual que el resto del sello): OUTPUT(meter arriba) →
    //       FILTER [LOW|HI] → I/O [IN|OUT] → IN PHASE. El DUCK se mudó al rail de macros (es una macro). =====
    // Test [diccionario]: meter.bottom ≤ filtros.y ≤ in.y ≤ inPhase.y (regiones apiladas, disjuntas).
    {
        auto layoutTwoKnobRow = [] (juce::Rectangle<int> row, Knob& a, Knob& b)
        {
            auto cellA = row.removeFromLeft (row.getWidth() / 2);
            auto cellB = row;
            a.label.setBounds  (cellA.removeFromBottom (16));
            a.slider.setBounds (cellA.withSizeKeepingCentre (juce::jmin (cellA.getWidth(), 50), cellA.getHeight()));
            b.label.setBounds  (cellB.removeFromBottom (16));
            b.slider.setBounds (cellB.withSizeKeepingCentre (juce::jmin (cellB.getWidth(), 50), cellB.getHeight()));
        };

        auto col = utilArea.reduced (padIn - 4, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (18);                          // cap "OUTPUT" (paint)

        // IN PHASE al pie a ANCHO COMPLETO → el ToggleButton entra en su modo HORIZONTAL (LED + "IN PHASE"
        // / "MONO SAFE"), igual que HALO/NEBULA/AURORA/HORIZON (antes lo clampeaba a 58 px = modo vertical).
        inPhase.setBounds (col.removeFromBottom (62));
        col.removeFromBottom (12);

        // I/O [IN | OUT] con su cap "I/O" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (84), in, out);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                       // espacio del cap "I/O" (paint)

        // FILTER [LOW | HI] con su cap "FILTER" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (84), lowCut, hiCut);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                       // espacio del cap "FILTER" (paint)

        // El meter OUTPUT absorbe TODO lo que queda ARRIBA (LED + barra alta + "OUT"); ancho completo
        // como el resto del sello (la barra se centra sola dentro del componente).
        meter.setBounds (col.removeFromTop (juce::jmax (60, col.getHeight())));
    }

    // ===== rail de MACROS (inferior, bajo el campo): DENSIDAD · SPREAD · VIDA · DUCK · MIX =====
    {
        auto rail = macrosArea.reduced (padIn, 0).withTrimmedTop (16).withTrimmedBottom (4);
        Knob* knobs[] = { &density, &spread, &vida, &duck, &mix };
        const char* idx[] = { "01", "02", "03", "04", "05" };
        const int n  = 5;
        const int kw = rail.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = rail.removeFromLeft (i == n - 1 ? rail.getWidth() : kw);
            knobs[i]->label.setBounds (cell.removeFromBottom (16));
            knobs[i]->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 84),
                                                                    juce::jmin (cell.getHeight(), 84)));
            knobIdx.push_back ({ idx[i], { knobs[i]->slider.getX() - 2, knobs[i]->slider.getY() - 2 } });
        }
    }
}

void DustEditor::paintBody (juce::Graphics& g)
{
    const auto fL = railLArea.toFloat();
    const auto fM = macrosArea.toFloat();
    const auto fU = utilArea.toFloat();

    // ===== luz volumétrica cian que emana del CAMPO (mockup #plugin::before) =====
    {
        const auto fc = field.getBounds().toFloat().getCentre();
        juce::ColourGradient halo (th::cyan.withAlpha (0.05f), fc.x, fc.y - 30.0f,
                                   th::cyan.withAlpha (0.0f),  fc.x, fc.y - 300.0f, true);
        halo.addColour (0.55, th::cyan.withAlpha (0.018f));
        g.setGradientFill (halo);
        g.fillRect (field.getBounds().toFloat().expanded (40.0f, 24.0f));
    }

    // ===== rail IZQUIERDO ELEVADO: sheen arriba → caída, hairline + sombra hacia el campo =====
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
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f),  fL.getRight() - 14.0f, 0.0f,
                                 juce::Colours::black.withAlpha (0.42f), fL.getRight(),         0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fL.getRight() - 14.0f, fL.getY(), 14.0f, fL.getHeight()));
    }

    // ===== rail de MACROS ELEVADO (inferior): sheen + hairline arriba + sombra de caída hacia el campo =====
    {
        juce::ColourGradient surf (juce::Colour (0x0aa0c0e0), 0.0f, fM.getY(),
                                   th::bg0.withAlpha (0.10f),  0.0f, fM.getBottom(), false);
        surf.addColour (0.50, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (fM);
        g.setColour (juce::Colour (0x12bee1ff));                       // sheen superior
        g.fillRect (fM.getX(), fM.getY(), fM.getWidth(), 1.0f);
        g.setColour (juce::Colour (0x1fa0c0e0));                       // hairline del borde al campo
        g.fillRect (fM.getX(), fM.getY(), fM.getWidth(), 1.0f);
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.42f), 0.0f, fM.getY(),
                                 juce::Colours::black.withAlpha (0.0f),  0.0f, fM.getY() + 12.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fM.getX(), fM.getY(), fM.getWidth(), 12.0f));
    }

    // ===== columna de UTILIDAD: REBAJE del chasis (más oscura, sombra desde la izquierda) =====
    {
        juce::ColourGradient rec (th::bg0.withAlpha (0.34f), 0.0f, fU.getY(),
                                  th::bg0.withAlpha (0.40f), 0.0f, fU.getBottom(), false);
        rec.addColour (0.40, th::bg0.withAlpha (0.18f));
        g.setGradientFill (rec);
        g.fillRect (fU);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (fU.getX(), fU.getY(), 1.0f, fU.getHeight());
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.45f), fU.getX(),         0.0f,
                                 juce::Colours::black.withAlpha (0.0f),  fU.getX() + 12.0f, 0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fU.getX(), fU.getY(), 12.0f, fU.getHeight()));
        g.setColour (juce::Colour (0x0abee1ff));
        g.fillRect (fU.getX(), fU.getBottom() - 1.0f, fU.getWidth(), 1.0f);
    }

    // ===== caps de sección (mono chico, tracking ancho) =====
    auto cap = [&g] (const juce::String& s, int x, int y)
    {
        g.setColour (th::fnt);
        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.24f));
        g.drawText (s, x, y, 200, 12, juce::Justification::centredLeft);
    };
    cap ("AXIS / CADENCE",                          railLArea.getX() + 14, railLArea.getY() + 16);
    cap ("MACROS",                                  macrosArea.getX() + 14, macrosArea.getY() + 7);
    cap ("OUTPUT",                                  utilArea.getX() + 12,  utilArea.getY() + 16);

    // caps internos de la utilidad: "FILTER" sobre [LOW|HI] · "I/O" sobre [IN|OUT] (centrados sobre cada par).
    {
        auto capCentred = [&g] (const juce::String& s, juce::Rectangle<int> over)
        {
            if (over.isEmpty()) return;
            g.setColour (th::fnt);
            g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
            g.drawText (s, over.getX(), over.getY() - 13, over.getWidth(), 11, juce::Justification::centred);
        };
        capCentred ("FILTER", lowCut.slider.getBounds().getUnion (hiCut.slider.getBounds()));
        capCentred ("I/O",    in.slider.getBounds().getUnion (out.slider.getBounds()));
    }

    // ===== specBlock decorativo (mockup #specBlock): credenciales del motor =====
    if (! specArea.isEmpty())
    {
        g.setColour (th::lineSoft);
        g.fillRect (specArea.getX(), specArea.getY(), 1, juce::jmin (specArea.getHeight(), 80));
        const char* const specs[] = { "BINAURAL ECHOES", "BUBBLE FIELD", "NEAR-FIELD", "LIMITER" };
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        int y = specArea.getY() + 4;
        for (auto* s : specs)
        {
            g.setColour (th::cyan.withAlpha (0.4f));
            g.drawText (juce::String::fromUTF8 ("\xc2\xb7"), specArea.getX() + 8, y, 8, 12, juce::Justification::centredLeft);
            g.setColour (th::fnt);
            g.drawText (s, specArea.getX() + 16, y, specArea.getWidth() - 16, 12, juce::Justification::centredLeft);
            y += 15;
        }
    }

    // ===== índices de knob (01…04 + "·" del RATE/DUCK, mono fantasma — mockup .kidx) =====
    g.setColour (th::fnt);
    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.08f));
    for (const auto& ki : knobIdx)
        g.drawText (ki.idx, ki.pos.x, ki.pos.y, 18, 10, juce::Justification::centredLeft);
}

juce::Rectangle<int> DustEditor::dbgRailBounds() const
{
    auto r = syncCtl.getBounds();
    for (const auto* k : { &density, &spread, &vida, &duck, &mix })
        r = r.getUnion (knobUnion (*k));
    return r;
}

// Sólo los 4 macros (rail INFERIOR, bajo el campo). Separado de dbgRailBounds porque el rail de DUST
// es de DOS regiones (SYNC en el rail izquierdo + macros abajo): la unión de ambas engloba el campo
// por bounding-box (artefacto de caja, no solape real). El test [diccionario] verifica el no-solape
// del campo contra cada región por separado (SYNC a la izquierda, macros abajo).
juce::Rectangle<int> DustEditor::dbgMacrosBounds() const
{
    auto r = knobUnion (density);
    for (const auto* k : { &spread, &vida, &duck, &mix })   // DUCK ahora es una macro (rail inferior)
        r = r.getUnion (knobUnion (*k));
    return r;
}

juce::Rectangle<int> DustEditor::dbgUtilBounds() const
{
    auto r = meter.getBounds().getUnion (inPhase.getBounds());
    for (const auto* k : { &lowCut, &hiCut, &in, &out })   // DUCK salió de la utilidad → rail de macros
        r = r.getUnion (knobUnion (*k));
    return r;
}

} // namespace dust
