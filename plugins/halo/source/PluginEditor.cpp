#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace halo
{

namespace pid   = halo::params::id;
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del mockup halo-a (grid del cuerpo, coords base 900×600).
namespace metrics
{
    inline constexpr int railLW = 138;   // rail izquierdo (NÚCLEO: FREEZE + ÓRBITA + specs)
    inline constexpr int utilW  = 132;   // columna de utilidad/SALIDA (rebaje)
    inline constexpr int railH  = 138;   // rail inferior de macros (6 knobs)
    inline constexpr int padIn  = 12;    // padding interno de los rails
}

HaloEditor::HaloEditor (HaloProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("SPC·02")),   // designación tras el wordmark
      proc (p),
      // El funnel LEE los atomics reales del DSP (honestidad motor↔visual). NO interactivo.
      halo (p.uiShimmer, p.uiDecay, p.uiSize, p.uiTone, p.uiOrbit, p.uiMix, p.uiFreeze, p.uiLoopRms, p.uiRateNorm),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::magenta),
      freeze (p.apvts, pid::FREEZE,
              juce::String::fromUTF8 ("FREEZE"), juce::String::fromUTF8 ("FREEZE THE CLOUD"), th::magenta),
      // SYNC (core del sello): FREE muestra la perilla RATE (orbitRate, Hz → SEG/VUELTA) para ELEGIR la velocidad
      // de la órbita; SYNC muestra los chips de división (1 bar … 8 bars). Magenta (familia FDN). Divisiones de HALO.
      syncCtl (p.apvts, pid::ORBITRATE, pid::ORBITSYNC, pid::ORBITDIV,
               halo::params::sync::labels(), th::magenta)
{
    setFamilyHue (th::magenta);   // Espacio/Profundidad: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup (48px)

    // Bottom-bar contextual (doctrina del sello): NOMBRE + VALOR del control bajo el cursor en mono. Hue magenta.
    enableBottomBar (th::magenta, juce::String::fromUTF8 ("SPC·02  —  hover a control to read it"));
    bottomBar().setSignature (juce::String::fromUTF8 ("HALO STABLE"));

    addToCanvas (halo);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (freeze);
    addToCanvas (syncCtl);

    // accesibilidad: nombres accesibles + foco de teclado en los controles del chasis/core.
    for (auto* c : { (juce::Component*) &inPhase, (juce::Component*) &freeze, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    freeze.setTitle  (juce::String::fromUTF8 ("FREEZE (captures the cloud)"));
    syncCtl.setTitle (juce::String::fromUTF8 ("SYNC (orbit rate)"));
    halo.setTitle    (juce::String::fromUTF8 ("HALO — halo funnel of the spatial shimmer"));

    // Las 6 macros: hue de familia MAGENTA. El par de filtros + IN/OUT: hue NEUTRO (utilidad, mockup data-fam="util").
    setupKnob (mix,     pid::MIX,     "MIX",     th::magenta, 76);
    setupKnob (size,    pid::SIZE,    "SIZE",    th::magenta, 76);
    setupKnob (decay,   pid::DECAY,   "DECAY",   th::magenta, 76);
    setupKnob (shimmer, pid::SHIMMER, "SHIMMER", th::magenta, 76);
    setupKnob (tone,    pid::TONE,    "TONE",    th::magenta, 76);
    setupKnob (orbit,   pid::ORBIT,   "ORBIT",   th::magenta, 76);
    setupKnob (lowCut,  pid::LOWCUT,  "LOW",     th::utilHue, 50);   // par de filtros del WET (HP) → utilidad
    setupKnob (hiCut,   pid::HICUT,   "HI",      th::utilHue, 50);   // par de filtros del WET (LP) → utilidad
    setupKnob (in,      "inGain",     "IN",      th::utilHue, 50);   // gain del chasis (utilidad)
    setupKnob (out,     "output",     "OUT",     th::utilHue, 50);

    setBaseSize (900, 600);   // tamaño de diseño de HALO (coords base; resize S/M/L gratis)
}

HaloEditor::~HaloEditor()
{
    // Soltar el L&F antes de destruir los sliders (regla JUCE).
    for (auto* k : { &mix, &size, &decay, &shimmer, &tone, &orbit, &lowCut, &hiCut, &in, &out })
        k->slider.setLookAndFeel (nullptr);
}

void HaloEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                            juce::Colour hue, int textBoxW)
{
    // OvniKnob ya viene con velocity-drag + Shift-fino + cmd-click-reset + doble-click-tipear + estados.
    k.slider.setKnobLookAndFeel (&knobLaf);
    k.slider.getProperties().set ("hue", (int) hue.getARGB());
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxW, 15);
    k.slider.setColour (juce::Slider::textBoxTextColourId, th::fnt);   // el valor NO grita: lectura en bottom-bar

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

void HaloEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== grid del mockup: railL 138 (NÚCLEO) · funnel 1fr · util 132 (SALIDA) · rail inferior 138 =====
    // REGLA DE ORO (Diccionario §4): quitar el rail PRIMERO, la utilidad DESPUÉS → regiones disjuntas, imposible
    // que FREEZE/SYNC (rail izquierdo) pisen al IN PHASE (columna de utilidad, derecha).
    railLArea = body.removeFromLeft (railLW);
    utilArea  = body.removeFromRight (utilW);

    // El rail INFERIOR de macros toma el ancho central restante (entre el NÚCLEO y la SALIDA).
    auto rail = body.removeFromBottom (railH);

    // El funnel domina el resto del cuerpo central (es el gancho visual).
    halo.setBounds (body);

    // ===== rail IZQUIERDO (NÚCLEO): FREEZE prominente arriba · ÓRBITA (SyncControl) · specBlock abajo =====
    {
        auto col = railLArea.reduced (padIn, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (16);                                    // cap "NÚCLEO" (paint)

        // FREEZE: gesto prominente arriba-izquierda (alto, con ícono + main + sub apilados → modo vertical).
        freeze.setBounds (col.removeFromTop (58));
        col.removeFromTop (16);

        col.removeFromTop (14);                                    // cap "ÓRBITA" (paint)
        // SyncControl en modo VERTICAL (rail angosto): [FREE|SYNC] arriba, RATE/chips abajo + caption.
        syncCtl.setBounds (col.removeFromTop (juce::jmin (160, col.getHeight())));

        // specBlock decorativo abajo (credenciales del motor FDN; mockup #specBlock).
        col.removeFromTop (14);
        specArea = col.removeFromTop (juce::jmin (74, col.getHeight()));
    }

    // ===== banda SYNC/FREEZE ya posicionada dentro de railLArea → garantiza la disjunción con la utilidad =====

    // ===== columna de UTILIDAD/SALIDA: meter · [LOW CUT|HI CUT] · [IN|OUT] · IN PHASE (de ARRIBA hacia ABAJO) ==
    auto layoutTwoKnobs = [] (juce::Rectangle<int> row, Knob& a, Knob& b)
    {
        auto aCell = row.removeFromLeft (row.getWidth() / 2);
        auto bCell = row;
        a.label.setBounds  (aCell.removeFromBottom (14));
        a.slider.setBounds (aCell.withSizeKeepingCentre (juce::jmin (aCell.getWidth(), 54), aCell.getHeight()));
        b.label.setBounds  (bCell.removeFromBottom (14));
        b.slider.setBounds (bCell.withSizeKeepingCentre (juce::jmin (bCell.getWidth(), 54), bCell.getHeight()));
    };
    {
        auto col = utilArea.reduced (8, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (16);                                    // cap "SALIDA" (paint)

        // IN PHASE abajo del todo (vertical), IN/OUT y filtros se arman de ABAJO hacia ARRIBA → el meter toma
        // lo que sobra ARRIBA (sin solapes). Mismo criterio que el layout anterior, alineado al mockup.
        inPhase.setBounds (col.removeFromBottom (54));
        col.removeFromBottom (10);

        layoutTwoKnobs (col.removeFromBottom (84), in, out);      // [IN | OUT] (gain del chasis)
        ioCapArea = col.removeFromBottom (12);                    // cap "I/O" (paint), justo encima de IN/OUT
        col.removeFromBottom (4);

        layoutTwoKnobs (col.removeFromBottom (84), lowCut, hiCut); // [LOW CUT | HI CUT] (filtros del WET)
        filterCapArea = col.removeFromBottom (12);                // cap "FILTRO" (paint), justo encima del par
        col.removeFromBottom (6);

        // Meter ARRIBA: el resto (cap 200 para que el LED + barra + "OUT" entren cómodos).
        meter.setBounds (col.removeFromTop (juce::jmin (200, col.getHeight())));
    }

    // ===== rail INFERIOR: 6 knobs reparten el ancho central (MIX/SIZE/DECAY/SHIMMER/TONE/ORBIT) =====
    {
        auto col = rail.reduced (10, 0).withTrimmedTop (8).withTrimmedBottom (8);
        // MIX al FINAL como en el resto del catálogo (NEBULA/DUST/AURORA/HORIZON lo llevan último;
        // acá estaba primero — coherencia de catálogo, QA 2026-07-16 con OK de Joaquín).
        Knob* knobs[] = { &size, &decay, &shimmer, &tone, &orbit, &mix };
        const char* idx[] = { "01", "02", "03", "04", "05", "06" };
        const int n  = 6;
        const int kw = col.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = col.removeFromLeft (i == n - 1 ? col.getWidth() : kw);
            auto lbl  = cell.removeFromBottom (16);
            knobs[i]->label.setBounds (lbl);
            knobs[i]->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 84),
                                                                    juce::jmin (cell.getHeight(), 90)));
            knobIdx.push_back ({ idx[i], { knobs[i]->slider.getX() - 2, knobs[i]->slider.getY() - 2 } });
        }
    }
}

void HaloEditor::paintBody (juce::Graphics& g)
{
    const auto fL = railLArea.toFloat();
    const auto fU = utilArea.toFloat();

    // ===== luz volumétrica MAGENTA que emana del funnel (mockup #plugin::before): asciende del centro =====
    {
        const auto pc = halo.getBounds().toFloat().getCentre();
        juce::ColourGradient glow (th::magenta.withAlpha (0.065f), pc.x, pc.y,
                                   th::magenta.withAlpha (0.0f),   pc.x, pc.y - 300.0f, true);
        glow.addColour (0.55, th::magenta.withAlpha (0.02f));
        g.setGradientFill (glow);
        g.fillRect (halo.getBounds().toFloat().expanded (50.0f, 30.0f));
    }

    // ===== rail IZQUIERDO ELEVADO (NÚCLEO): sheen arriba → caída, hairline + sombra hacia el funnel =====
    {
        juce::ColourGradient surf (juce::Colour (0x09a0c0e0), 0.0f, fL.getY(),
                                   th::bg0.withAlpha (0.16f),  0.0f, fL.getBottom(), false);
        surf.addColour (0.34, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (fL);
        g.setColour (juce::Colour (0x12bee1ff));                  // sheen superior
        g.fillRect (fL.getX(), fL.getY(), fL.getWidth(), 1.0f);
        g.setColour (juce::Colour (0x1fa0c0e0));                  // hairline del borde al funnel (derecha)
        g.fillRect (fL.getRight() - 1.0f, fL.getY(), 1.0f, fL.getHeight());
        const float sw = 14.0f;                                   // caída de sombra hacia el funnel
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f),  fL.getRight() - sw, 0.0f,
                                 juce::Colours::black.withAlpha (0.42f), fL.getRight(),      0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fL.getRight() - sw, fL.getY(), sw, fL.getHeight()));
    }

    // ===== columna de utilidad (SALIDA): REBAJE del chasis (más oscura, sombra desde la izquierda) =====
    {
        juce::ColourGradient rec (th::bg0.withAlpha (0.34f), 0.0f, fU.getY(),
                                  th::bg0.withAlpha (0.40f), 0.0f, fU.getBottom(), false);
        rec.addColour (0.40, th::bg0.withAlpha (0.18f));
        g.setGradientFill (rec);
        g.fillRect (fU);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (fU.getX(), fU.getY(), 1.0f, fU.getHeight());
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.45f), fU.getX(),          0.0f,
                                 juce::Colours::black.withAlpha (0.0f),  fU.getX() + 12.0f,  0.0f, false);
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
        g.drawText (s, x, y, 220, 12, juce::Justification::centredLeft);
    };
    cap ("CORE",   railLArea.getX() + 14, railLArea.getY() + 16);
    cap ("ORBIT",  syncCtl.getX(), syncCtl.getY() - 16);
    cap ("OUTPUT", utilArea.getX() + 12, utilArea.getY() + 16);
    // Caps "FILTRO" / "I/O" centrados sobre cada par de knobs de la columna de utilidad (mockup .utilLab).
    auto utilLab = [&g] (const juce::String& s, juce::Rectangle<int> r)
    {
        if (r.isEmpty()) return;
        g.setColour (th::fnt);
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        g.drawText (s, r, juce::Justification::centred);
    };
    utilLab ("FILTER", filterCapArea);
    utilLab ("I/O",    ioCapArea);

    // ===== specBlock decorativo (mockup #specBlock): credenciales del motor FDN =====
    if (! specArea.isEmpty())
    {
        g.setColour (th::lineSoft);
        g.fillRect (specArea.getX(), specArea.getY(), 1, specArea.getHeight());
        // credenciales HONESTAS del motor (Manifiesto #2): el FDN compartido tiene kN=8 líneas
        // ("16x" venía del mockup y mentía — mismo bug que tenía NÉBULA).
        const char* const specs[] = { "FDN NET 8x", "SHIMMER +12 / +7", "HALOS", "ORBIT TEMPO" };
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        int y = specArea.getY() + 4;
        for (auto* s : specs)
        {
            g.setColour (th::magenta.withAlpha (0.4f));
            g.drawText (juce::String::fromUTF8 ("\xc2\xb7"), specArea.getX() + 8, y, 8, 12, juce::Justification::centredLeft);
            g.setColour (th::fnt);
            g.drawText (s, specArea.getX() + 16, y, specArea.getWidth() - 16, 12, juce::Justification::centredLeft);
            y += 16;
        }
    }

    // ===== índices de knob (01…06, mono fantasma — mockup .kidx) =====
    g.setColour (th::fnt);
    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.08f));
    for (const auto& ki : knobIdx)
        g.drawText (ki.idx, ki.pos.x, ki.pos.y, 18, 10, juce::Justification::centredLeft);
}

} // namespace halo
