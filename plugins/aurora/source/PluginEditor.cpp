#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace aurora
{

namespace pid = aurora::params::id;
namespace th  = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del mockup aurora-a (grid del cuerpo, coords base 960×580).
namespace metrics
{
    inline constexpr int railLW   = 150;   // rail izquierdo (SYNC / MOTION + specBlock)
    inline constexpr int utilW    = 118;   // columna de utilidad (rebaje del chasis, orden HALO: OUTPUT · FILTER · I/O · IN PHASE)
    inline constexpr int padIn    = 13;    // padding interno de los rails
    inline constexpr int macroH   = 122;   // alto del rail de macros (franja propia, como NÉBULA → knobs 84 px)
}

AuroraEditor::AuroraEditor (AuroraProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("SPL·01")),   // designación tras el wordmark
      proc (p),
      field (p.uiSpread, p.uiTilt, p.uiMotion, p.uiMonoSafe, p.uiDuck, p.uiMix,
             p.uiGamma, p.uiDuckEnv, p.uiRateNorm, p.uiBandEnergy, p.uiBandPos),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::green),
      // SYNC (core del sello): FREE muestra la perilla RATE (motionRate, Hz — la velocidad del abanico);
      // SYNC muestra los 4 chips de división (1/4 · 1/2 · 1 bar · 2 bar). Verde (familia Espectral).
      syncCtl (p.apvts, pid::MOTIONRATE, pid::MOTIONSYNC, pid::MOTIONDIV,
               aurora::params::sync::labels(), th::green)
{
    setFamilyHue (th::green);   // Espectral: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup (48px)

    // Bottom-bar contextual (doctrina del sello): NOMBRE + VALOR del control bajo el cursor en mono.
    // Hue de familia = verde (Espectral). En reposo, el hint del mockup + la firma a la derecha.
    enableBottomBar (th::green, juce::String::fromUTF8 ("SPL\xc2\xb7""01  \xe2\x80\x94  hover a control to read it"));
    bottomBar().setSignature ("SIGNAL STABLE");

    // PDC real en la telemetría del campo (Manifiesto #3: la latencia STFT se publica, no se esconde).
    field.pdcProvider = [this] { return proc.getLatencySamples(); };
    field.refreshTelemetryNow();

    addToCanvas (field);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (syncCtl);

    // accesibilidad: nombres accesibles + foco de teclado en los controles del chasis/core.
    for (auto* c : { (juce::Component*) &inPhase, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    inPhase.setName  (juce::String::fromUTF8 ("IN PHASE"));
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    syncCtl.setName  ("SYNC");
    syncCtl.setTitle ("SYNC (orbit rate)");
    field.setName    ("AURORA");
    field.setTitle   ("AURORA (spectrum unfolded by position)");
    meter.setName    ("OUTPUT");
    meter.setTitle   ("OUTPUT (output meter)");

    // Las 6 macros de la curaduría (rail, hue verde) + IN/OUT del chasis (utilidad, hue NEUTRO).
    setupKnob (spread,   pid::SPREAD,      "SPREAD",    th::green);
    setupKnob (tilt,     pid::TILT,        "TILT",      th::green);
    setupKnob (motion,   pid::MOTION,      "MOTION",    th::green);
    setupKnob (monoSafe, pid::MONOSAFEAMT, "MONO SAFE", th::green);
    setupKnob (duck,     pid::DUCK,        "DUCK",      th::green);
    setupKnob (mix,      pid::MIX,         "MIX",       th::green);
    setupKnob (lowCut,   pid::LOWCUT,      "LOW",       th::utilHue);   // par de filtros de la SALIDA (HP) → utilidad
    setupKnob (hiCut,    pid::HICUT,       "HI",        th::utilHue);   // par de filtros de la SALIDA (LP) → utilidad
    setupKnob (in,       "inGain",         "IN",        th::utilHue);   // gain del chasis (utilidad, mockup data-fam="util")
    setupKnob (out,      "output",         "OUT",       th::utilHue);

    setBaseSize (960, 580);   // tamaño de diseño de AURORA (coords base; resize S/M/L gratis)
}

AuroraEditor::~AuroraEditor()
{
    for (auto* k : { &spread, &tilt, &motion, &monoSafe, &duck, &mix, &lowCut, &hiCut, &in, &out })
        k->slider.setLookAndFeel (nullptr);
}

void AuroraEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text, juce::Colour hue)
{
    // OvniKnob ya viene con velocity-drag + Shift-fino + cmd-click-reset + doble-click-tipear + estados.
    k.slider.setKnobLookAndFeel (&knobLaf);
    k.slider.getProperties().set ("hue", (int) hue.getARGB());   // verde (Espectral) o neutro (utilidad)

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

    // cmd/ctrl-click = reset al DEFAULT del parámetro (doble-click NO resetea: abre el textbox).
    if (auto* pr = proc.apvts.getParameter (paramID))
    {
        const auto& range = proc.apvts.getParameterRange (paramID);
        k.slider.setDoubleClickReturnValue (false, (double) range.convertFrom0to1 (pr->getDefaultValue()));
    }
}

void AuroraEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== grid del mockup aurora-a: railL 150 · campo 1fr · util 96 =====
    // REGLA DE ORO (Diccionario §4): quitar la UTILIDAD del `body` PRIMERO (derecha), DESPUÉS el
    // rail (izquierda) → el campo + el rail de macros + el SYNC viven en regiones disjuntas de la
    // utilidad. Imposible pisar el IN PHASE (regresión del bug de NÉBULA), verificado por bounds.
    utilArea  = body.removeFromRight (utilW);
    railLArea = body.removeFromLeft (railLW);
    // Rail de MACROS en una franja PROPIA bajo el campo (igual que HALO/DUST/NÉBULA) — ya no flota sobre
    // el visualizador: queda alineado con el resto del sello. Se quita del centro DESPUÉS de railL/util.
    macroArea = body.removeFromBottom (macroH);
    fieldArea = body;                 // el campo domina el centro (gancho visual), AHORA sobre el rail
    field.setBounds (fieldArea);

    // ===== rail IZQUIERDO: SYNC / MOTION arriba (autocontenido) · specBlock decorativo abajo =====
    // SyncControl en modo VERTICAL (rail angosto → selector [FREE|SYNC] arriba, RATE/chips 2×2 abajo).
    // Anclado DENTRO de `body` (la util ya se quitó) y a la IZQUIERDA → lejísimos del IN PHASE.
    {
        auto col = railLArea.reduced (padIn, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (20);                                   // cap "SYNC / MOTION" (paint)
        syncCtl.setBounds (col.removeFromTop (juce::jmin (160, col.getHeight())));   // [FREE|SYNC] + RATE / chips

        // specBlock decorativo (credenciales del motor STFT) — el resto del rail.
        col.removeFromTop (16);
        specArea = col.withTrimmedBottom (juce::jmax (0, col.getHeight() - 90));
    }

    // ===== columna de UTILIDAD/SALIDA (orden HALO, de ABAJO hacia ARRIBA): IN PHASE · I/O [IN|OUT] ·
    //       FILTER [LOW|HI] · meter (absorbe lo que sobra ARRIBA = fill vertical, modo OUT) =====
    // Test [diccionario]: rail/campo NUNCA pisan la unión (meter ∪ filtros ∪ IN/OUT ∪ IN PHASE).
    {
        // Fila de DOS knobs chicos lado a lado (la usan LOW/HI y IN/OUT): knob centrado (≤48) arriba,
        // label legible (14) abajo. DRY: una sola geometría (espeja NÉBULA/HALO).
        auto layoutTwoKnobRow = [] (juce::Rectangle<int> row, Knob& a, Knob& b)
        {
            auto cellA = row.removeFromLeft (row.getWidth() / 2);
            auto cellB = row;
            a.label.setBounds  (cellA.removeFromBottom (14));
            a.slider.setBounds (cellA.withSizeKeepingCentre (juce::jmin (cellA.getWidth(), 48), cellA.getHeight()));
            b.label.setBounds  (cellB.removeFromBottom (14));
            b.slider.setBounds (cellB.withSizeKeepingCentre (juce::jmin (cellB.getWidth(), 48), cellB.getHeight()));
        };

        auto col = utilArea.reduced (8, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (16);                                   // cap "OUTPUT" (paint)

        // IN PHASE al pie (modo vertical: símbolo ø + "IN PHASE" + "MONO SAFE" apilados).
        inPhase.setBounds (col.removeFromBottom (juce::jmin (62, col.getHeight())));
        col.removeFromBottom (12);

        // I/O [IN | OUT] (gain del chasis), con su cap "I/O" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (74), in, out);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                               // espacio del cap "I/O" (paint)

        // FILTER [LOW | HI] (filtros de la salida), con su cap "FILTER" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (74), lowCut, hiCut);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                               // espacio del cap "FILTER" (paint)

        // El meter absorbe TODO lo que queda ARRIBA (LED + barra alta + "OUT"); modo vertical.
        meter.setBounds (col.removeFromTop (juce::jmax (60, col.getHeight())));
    }

    // ===== rail de MACROS (franja inferior propia, bajo el campo): 6 knobs verdes repartidos =====
    {
        auto rail = macroArea.reduced (10, 0).withTrimmedTop (8).withTrimmedBottom (12);
        Knob* knobs[] = { &spread, &tilt, &motion, &monoSafe, &duck, &mix };
        const char* idx[] = { "01", "02", "03", "04", "05", "06" };
        const int n  = (int) (sizeof (knobs) / sizeof (knobs[0]));
        const int kw = rail.getWidth() / n;
        for (int i = 0; i < n; ++i)
        {
            auto cell = rail.removeFromLeft (i == n - 1 ? rail.getWidth() : kw);
            auto lbl  = cell.removeFromBottom (16);
            knobs[i]->label.setBounds (lbl);
            knobs[i]->slider.setBounds (cell.withSizeKeepingCentre (juce::jmin (cell.getWidth(), 84),
                                                                    juce::jmin (cell.getHeight(), 84)));
            knobIdx.push_back ({ idx[i], { knobs[i]->slider.getX() + 1, knobs[i]->slider.getY() - 1 } });
        }
    }
}

void AuroraEditor::paintBody (juce::Graphics& g)
{
    const auto fL = railLArea.toFloat();
    const auto fU = utilArea.toFloat();

    // ===== luz volumétrica VERDE que emana del campo (mockup #plugin::before) =====
    {
        const auto fc = fieldArea.toFloat().getCentre();
        juce::ColourGradient halo (th::green.withAlpha (0.055f), fc.x, fc.y,
                                   th::green.withAlpha (0.0f),   fc.x, fc.y - 300.0f, true);
        halo.addColour (0.55, th::green.withAlpha (0.018f));
        g.setGradientFill (halo);
        g.fillRect (fieldArea.toFloat().expanded (60.0f, 30.0f));
    }

    // ===== rail IZQUIERDO ELEVADO: sheen arriba → caída, hairline + sombra de caída hacia el campo =====
    {
        const auto r = fL;
        juce::ColourGradient surf (juce::Colour (0x09a0c0e0), 0.0f, r.getY(),
                                   th::bg0.withAlpha (0.16f),  0.0f, r.getBottom(), false);
        surf.addColour (0.34, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (r);
        g.setColour (juce::Colour (0x12bee1ff));                      // sheen superior
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
        g.setColour (juce::Colour (0x1fa0c0e0));                      // hairline del borde al campo
        g.fillRect (r.getRight() - 1.0f, r.getY(), 1.0f, r.getHeight());
        const float sw = 14.0f;                                       // caída de sombra hacia el campo
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f),  r.getRight() - sw, 0.0f,
                                 juce::Colours::black.withAlpha (0.42f), r.getRight(),      0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (r.getRight() - sw, r.getY(), sw, r.getHeight()));
    }

    // ===== columna de utilidad: REBAJE del chasis (más oscura, sombra desde la izquierda) =====
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

    // ===== rail de MACROS ELEVADO (franja inferior propia, igual que HALO/DUST/NÉBULA): sheen + hairline
    //       arriba + sombra de caída hacia el campo → los knobs viven en su banda, alineados, no flotando =====
    {
        const auto fM = macroArea.toFloat();
        juce::ColourGradient surf (juce::Colour (0x0aa0c0e0), 0.0f, fM.getY(),
                                   th::bg0.withAlpha (0.10f),  0.0f, fM.getBottom(), false);
        surf.addColour (0.50, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (fM);
        g.setColour (juce::Colour (0x16bee1ff));                       // sheen + hairline superior
        g.fillRect (fM.getX(), fM.getY(), fM.getWidth(), 1.0f);
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.42f), 0.0f, fM.getY(),
                                 juce::Colours::black.withAlpha (0.0f),  0.0f, fM.getY() + 12.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fM.getX(), fM.getY(), fM.getWidth(), 12.0f));
    }

    // ===== caps de sección (mono chico, tracking ancho) =====
    auto cap = [&g] (const juce::String& s, int x, int y)
    {
        g.setColour (th::fnt);
        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.26f));
        g.drawText (s, x, y, 200, 12, juce::Justification::centredLeft);
    };
    cap (juce::String::fromUTF8 ("SYNC / MOTION"), railLArea.getX() + metrics::padIn, railLArea.getY() + 16);
    cap ("OUTPUT",                                 utilArea.getX() + 8,               utilArea.getY() + 16);

    // caps internos de la utilidad: "FILTER" sobre [LOW|HI] · "I/O" sobre [IN|OUT] (centrados sobre cada par).
    auto capCentred = [&g] (const juce::String& s, juce::Rectangle<int> over)
    {
        if (over.isEmpty()) return;
        g.setColour (th::fnt);
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        g.drawText (s, over.getX(), over.getY() - 13, over.getWidth(), 11, juce::Justification::centred);
    };
    capCentred ("FILTER", lowCut.slider.getBounds().getUnion (hiCut.slider.getBounds()));
    capCentred ("I/O",    in.slider.getBounds().getUnion (out.slider.getBounds()));

    // ===== specBlock decorativo (mockup #specBlock): credenciales del motor STFT =====
    if (! specArea.isEmpty())
    {
        g.setColour (th::lineSoft);
        g.fillRect (specArea.getX(), specArea.getY(), 1, specArea.getHeight());
        // credenciales HONESTAS del motor (Manifiesto #2): "24 BANDS" era el conteo del visualizador
        // (el motor panea POR BIN, ~1025 bins); lo real y distintivo es la red mono-safe (60→700 Hz).
        const char* const specs[] = { "STFT 2048", "SPECTRAL PAN", "MONO-SAFE NET", "LIMITER" };
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.22f));
        int y = specArea.getY() + 4;
        for (auto* s : specs)
        {
            g.setColour (th::green.withAlpha (0.4f));
            g.drawText (juce::String::fromUTF8 ("\xc2\xb7"), specArea.getX() + 8, y, 8, 12, juce::Justification::centredLeft);
            g.setColour (th::fnt);
            g.drawText (s, specArea.getX() + 16, y, specArea.getWidth() - 16, 12, juce::Justification::centredLeft);
            y += 16;
        }
    }

    // ===== índices de knob (01…06 sobre el rail de macros, I/O en la utilidad — mockup .kidx) =====
    g.setColour (th::fnt);
    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.08f));
    for (const auto& ki : knobIdx)
        g.drawText (ki.idx, ki.pos.x, ki.pos.y, 22, 10, juce::Justification::centredLeft);
}

} // namespace aurora
