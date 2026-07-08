#include "PluginEditor.h"
#include "params/ParameterIDs.h"

namespace horizon
{

namespace pid   = horizon::params::id;
namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del mockup horizon-a (grid del cuerpo, coords base 960×580).
namespace metrics
{
    inline constexpr int railLW = 150;   // rail izquierdo (SYNC / RE-TRIGGER + specs)
    inline constexpr int utilW  = 118;   // columna de utilidad (rebaje, orden HALO: OUTPUT · FILTER · I/O · IN PHASE)
    inline constexpr int macroH = 122;   // rail inferior de macros (franja propia, como NÉBULA → knobs 84 px)
    inline constexpr int padIn  = 12;    // padding interno de los rails
}

HorizonEditor::HorizonEditor (HorizonProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("SPL·02")),   // designación tras el wordmark
      proc (p),
      field (p.uiWhisper, p.uiSpread, p.uiDuck, p.uiMix, p.uiFreeze,
             p.uiGateAmp, p.uiGatePhase, p.uiWetEnergy, p.uiDuckEnv, p.uiRateNorm, p.uiSpectrum),
      freezeBtn (p.apvts, pid::FREEZE,
                 juce::String::fromUTF8 ("FREEZE"), juce::String::fromUTF8 ("HOLD"), th::green),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::green),
      // SYNC (core del sello): FREE muestra la perilla RATE (rate, Hz — la velocidad del latido);
      // SYNC muestra los 6 chips de división (1/16…2 bar). Verde (familia Espectral).
      syncCtl (p.apvts, pid::RATE, pid::RATESYNC, pid::RATEDIV,
               horizon::params::sync::labels(), th::green)
{
    setFamilyHue (th::green);   // Espectral: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup (48px)

    // Bottom-bar contextual (doctrina del sello): NOMBRE + VALOR del control bajo el cursor en mono.
    enableBottomBar (th::green, juce::String::fromUTF8 ("SPL\xc2\xb7""02  \xe2\x80\x94  hover a control to read it"));
    bottomBar().setSignature (juce::String::fromUTF8 ("SIGNAL STABLE"));

    addToCanvas (field);
    addToCanvas (freezeBtn);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (syncCtl);

    // accesibilidad: nombres accesibles + foco de teclado.
    for (auto* c : { (juce::Component*) &freezeBtn, (juce::Component*) &inPhase, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    freezeBtn.setName  (juce::String::fromUTF8 ("FREEZE"));
    freezeBtn.setTitle (juce::String::fromUTF8 ("FREEZE (hold the moment)"));
    inPhase.setName  (juce::String::fromUTF8 ("IN PHASE"));
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    syncCtl.setName  (juce::String::fromUTF8 ("SYNC"));
    syncCtl.setTitle (juce::String::fromUTF8 ("SYNC (freeze re-trigger)"));
    field.setName    (juce::String::fromUTF8 ("HORIZON"));
    field.setTitle   (juce::String::fromUTF8 ("HORIZON \xe2\x80\x94 the suspended spectrum that pulses"));
    meter.setName    (juce::String::fromUTF8 ("OUTPUT"));
    meter.setTitle   (juce::String::fromUTF8 ("OUTPUT (output meter)"));

    // Las 4 macros perilla de la curaduría (rail, hue de familia VERDE) + IN/OUT del chasis
    // (utilidad, hue NEUTRO — mockup data-fam="util").
    setupKnob (whisper, pid::WHISPER, "WHISPER", th::green);
    setupKnob (spread,  pid::SPREAD,  "SPREAD",  th::green);
    setupKnob (duck,    pid::DUCK,    "DUCK",    th::green);
    setupKnob (mix,     pid::MIX,     "MIX",     th::green);
    setupKnob (lowCut,  pid::LOWCUT,  "LOW",     th::utilHue);   // par de filtros de la SALIDA (HP) → utilidad
    setupKnob (hiCut,   pid::HICUT,   "HI",      th::utilHue);   // par de filtros de la SALIDA (LP) → utilidad
    setupKnob (in,      "inGain",     "IN",      th::utilHue);
    setupKnob (out,     "output",     "OUT",     th::utilHue);

    setBaseSize (960, 580);   // tamaño de diseño de HORIZON (coords base; resize S/M/L gratis)
}

HorizonEditor::~HorizonEditor()
{
    for (auto* k : { &whisper, &spread, &duck, &mix, &lowCut, &hiCut, &in, &out })
        k->slider.setLookAndFeel (nullptr);
}

void HorizonEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
                               juce::Colour hue)
{
    k.slider.setKnobLookAndFeel (&knobLaf);
    k.slider.getProperties().set ("hue", (int) hue.getARGB());   // familia Espectral (verde) o utilidad (neutro)

    k.slider.controlName = text;
    k.slider.setName  (text);
    k.slider.setTitle (text);

    k.slider.onHover     = [this] (const juce::String& nm, const juce::String& v) { bottomBar().show (nm, v); };
    k.slider.onHoverExit = [this] { bottomBar().clear(); };
    addToCanvas (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, th::txt);
    k.label.setFont (fonts::mono (12.0f));
    addToCanvas (k.label);

    k.attach = std::make_unique<SliderAttachment> (proc.apvts, paramID, k.slider);

    // cmd/ctrl-click = reset al DEFAULT (doble-click NO resetea: abre el textbox).
    if (auto* pr = proc.apvts.getParameter (paramID))
    {
        const auto& range = proc.apvts.getParameterRange (paramID);
        k.slider.setDoubleClickReturnValue (false, (double) range.convertFrom0to1 (pr->getDefaultValue()));
    }
}

void HorizonEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== grid del mockup: railL 150 · campo 1fr · util 96 =====
    // REGLA DE ORO (Diccionario §4): quitá el rail PRIMERO, la utilidad DESPUÉS → regiones
    // disjuntas, imposible que se pisen. El campo (con FREEZE + macros ENCIMA) vive en el medio.
    railLArea = body.removeFromLeft (railLW);
    utilArea  = body.removeFromRight (utilW);
    // Rail de MACROS en una franja PROPIA bajo el campo (igual que HALO/DUST/NÉBULA) — ya no flotan sobre
    // el espectro: quedan alineados con el resto del sello. Se quita del centro DESPUÉS de railL/util.
    macroArea = body.removeFromBottom (macroH);
    field.setBounds (body);   // EL espectro congelado domina el centro (gancho visual), AHORA sobre el rail

    // ===== rail IZQUIERDO: SYNC / RE-TRIGGER (vertical) arriba · specBlock decorativo abajo =====
    {
        auto col = railLArea.reduced (padIn, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (20);                                      // cap "SYNC / RE-TRIGGER" (paint)
        // SyncControl en modo VERTICAL (alto > ancho): [FREE|SYNC] + RATE (FREE) / chips 2×3 (SYNC).
        syncCtl.setBounds (col.removeFromTop (juce::jmin (190, col.getHeight())));

        // specBlock decorativo (credenciales del motor) abajo de todo (paint).
        col.removeFromTop (14);
        specArea = col.removeFromTop (juce::jmin (96, col.getHeight()));
    }

    // ===== FREEZE: el gesto central — toggle GRANDE, prominente, centrado arriba del campo (en el campo) =====
    {
        auto f = field.getBounds();
        const int fzW = 132, fzH = 48;
        freezeBtn.setBounds (f.getCentreX() - fzW / 2, f.getY() + 14, fzW, fzH);
    }

    // ===== rail de MACROS (franja inferior propia, bajo el campo): WHISPER/SPREAD/DUCK/MIX repartidos
    //       a ancho completo (alineado con el resto del sello, no flotando ni amontonado). =====
    {
        auto rail = macroArea.reduced (10, 0).withTrimmedTop (8).withTrimmedBottom (12);
        Knob* knobs[] = { &whisper, &spread, &duck, &mix };
        const char* idx[] = { "01", "02", "03", "04" };
        const int n = (int) (sizeof (knobs) / sizeof (knobs[0]));
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

    // ===== columna de UTILIDAD/SALIDA (orden HALO, de ABAJO hacia ARRIBA): IN PHASE · I/O [IN|OUT] ·
    //       FILTER [LOW|HI] · meter (absorbe lo que sobra ARRIBA = fill vertical, modo OUT) =====
    // Test [diccionario]: rail/campo NUNCA pisan la unión (meter ∪ filtros ∪ IN/OUT ∪ IN PHASE).
    {
        // Fila de DOS knobs chicos lado a lado (la usan LOW/HI y IN/OUT): knob centrado (≤48) arriba,
        // label legible (13) abajo. DRY: una sola geometría (espeja NÉBULA/HALO).
        auto layoutTwoKnobRow = [] (juce::Rectangle<int> row, Knob& a, Knob& b)
        {
            auto cellA = row.removeFromLeft (row.getWidth() / 2);
            auto cellB = row;
            a.label.setBounds  (cellA.removeFromBottom (13));
            a.slider.setBounds (cellA.withSizeKeepingCentre (juce::jmin (cellA.getWidth(), 48), cellA.getHeight()));
            b.label.setBounds  (cellB.removeFromBottom (13));
            b.slider.setBounds (cellB.withSizeKeepingCentre (juce::jmin (cellB.getWidth(), 48), cellB.getHeight()));
        };

        auto col = utilArea.reduced (8, 0).withTrimmedTop (14).withTrimmedBottom (10);
        col.removeFromTop (16);                                      // cap "OUTPUT" (paint)

        // IN PHASE al pie (toggle vertical: símbolo ø + "IN PHASE" + "MONO SAFE" apilados).
        inPhase.setBounds (col.removeFromBottom (juce::jmin (62, col.getHeight())));
        col.removeFromBottom (12);

        // I/O [IN | OUT] (gain del chasis), con su cap "I/O" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (72), in, out);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                                   // espacio del cap "I/O" (paint)

        // FILTER [LOW | HI] (filtros de la salida), con su cap "FILTER" en el margen de arriba.
        layoutTwoKnobRow (col.removeFromBottom (72), lowCut, hiCut);
        col.removeFromBottom (8);
        col.removeFromBottom (14);                                   // espacio del cap "FILTER" (paint)

        // El meter absorbe TODO lo que queda ARRIBA (LED + barra alta + "OUT"); modo vertical.
        meter.setBounds (col.removeFromTop (juce::jmax (60, col.getHeight())));
    }
}

void HorizonEditor::paintBody (juce::Graphics& g)
{
    const auto fL = railLArea.toFloat();
    const auto fU = utilArea.toFloat();

    // ===== luz volumétrica VERDE que emana del campo (mockup #plugin::before) =====
    {
        const auto fc = field.getBounds().toFloat().getCentre();
        juce::ColourGradient halo (th::green.withAlpha (0.05f), fc.x, fc.y,
                                   th::green.withAlpha (0.0f),  fc.x, fc.y - 300.0f, true);
        halo.addColour (0.55, th::green.withAlpha (0.016f));
        g.setGradientFill (halo);
        g.fillRect (field.getBounds().toFloat().expanded (60.0f, 30.0f));
    }

    // ===== rail IZQUIERDO ELEVADO: sheen arriba → caída, hairline + sombra hacia el campo =====
    {
        juce::ColourGradient surf (juce::Colour (0x09a0c0e0), 0.0f, fL.getY(),
                                   th::bg0.withAlpha (0.16f),  0.0f, fL.getBottom(), false);
        surf.addColour (0.34, juce::Colour (0x03a0c0e0));
        g.setGradientFill (surf);
        g.fillRect (fL);
        g.setColour (juce::Colour (0x12bee1ff));                      // sheen superior
        g.fillRect (fL.getX(), fL.getY(), fL.getWidth(), 1.0f);
        g.setColour (juce::Colour (0x1fa0c0e0));                      // hairline del borde al campo
        g.fillRect (fL.getRight() - 1.0f, fL.getY(), 1.0f, fL.getHeight());
        const float sw = 14.0f;                                       // caída de sombra hacia el campo
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.0f),  fL.getRight() - sw, 0.0f,
                                 juce::Colours::black.withAlpha (0.42f), fL.getRight(),      0.0f, false);
        g.setGradientFill (sh);
        g.fillRect (juce::Rectangle<float> (fL.getRight() - sw, fL.getY(), sw, fL.getHeight()));
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
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.45f), fU.getX(), 0.0f,
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
        juce::ColourGradient shM (juce::Colours::black.withAlpha (0.42f), 0.0f, fM.getY(),
                                  juce::Colours::black.withAlpha (0.0f),  0.0f, fM.getY() + 12.0f, false);
        g.setGradientFill (shM);
        g.fillRect (juce::Rectangle<float> (fM.getX(), fM.getY(), fM.getWidth(), 12.0f));
    }

    // ===== caps de sección (mono chico, tracking ancho) =====
    auto cap = [&g] (const juce::String& s, int x, int y)
    {
        g.setColour (th::fnt);
        g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.24f));
        g.drawText (s, x, y, 200, 12, juce::Justification::centredLeft);
    };
    cap (juce::String::fromUTF8 ("SYNC / RE-TRIGGER"), railLArea.getX() + 14, railLArea.getY() + 16);
    cap ("OUTPUT",                                     utilArea.getX() + 10,  utilArea.getY() + 16);

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

    // ===== specBlock decorativo (mockup #specBlock): credenciales del motor =====
    if (! specArea.isEmpty())
    {
        g.setColour (th::lineSoft);
        g.fillRect (specArea.getX(), specArea.getY(), 1, specArea.getHeight());
        const char* const specs[] = { "SPECTRAL FREEZE", "STFT 2048",
                                      "24 LAYERS", "RE-TRIGGER", "LIMITER" };
        g.setFont (fonts::mono (7.0f).withExtraKerningFactor (0.20f));
        int y = specArea.getY() + 4;
        for (auto* s : specs)
        {
            g.setColour (th::green.withAlpha (0.4f));
            g.drawText (juce::String::fromUTF8 ("\xc2\xb7"), specArea.getX() + 8, y, 8, 12, juce::Justification::centredLeft);
            g.setColour (th::fnt);
            g.drawText (juce::String::fromUTF8 (s), specArea.getX() + 16, y, specArea.getWidth() - 16, 12, juce::Justification::centredLeft);
            y += 17;
        }
    }

    // ===== índices de knob (01…04, mono fantasma — mockup .kidx) =====
    g.setColour (th::fnt);
    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.08f));
    for (const auto& ki : knobIdx)
        g.drawText (ki.idx, ki.pos.x, ki.pos.y, 18, 10, juce::Justification::centredLeft);
}

} // namespace horizon
