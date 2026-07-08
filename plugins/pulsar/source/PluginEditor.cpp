#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include <cmath>

namespace pulsar
{

namespace pid = pulsar::params::id;
namespace th  = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Métricas del layout ÓRBITA (coords base 960×580): POZO grande izquierda · bahía derecha · meter.
namespace metrics
{
    inline constexpr int bayW   = 448;   // bahía de controles (derecha) — más ancha: entra la fila de 6 knobs
    inline constexpr int meterW = 28;    // tira de meter (borde derecho)
    inline constexpr int bayGap = 8;     // separación pozo ↔ bahía
    inline constexpr int padIn  = 16;    // padding interno de la bahía
}

PulsarEditor::PulsarEditor (PulsarProcessor& p)
    : ovni::PluginEditorBase (p, juce::String::fromUTF8 ("MOV·02")),   // MOV·02
      proc (p),
      pad (p.uiAzimuth, p.uiDepth, p.uiHeat, p.uiDistance, p.uiMotion, p.uiSmear, p.uiShape,
           p.apvts.getParameter (pid::FIELDX), p.apvts.getParameter (pid::FIELDY)),
      morph (p.apvts, pid::SHAPE, th::cyan),
      meter (p.uiOutPeak, p.uiClip),
      inPhase (p.apvts, "monoSafe",
               juce::String::fromUTF8 ("IN PHASE"), juce::String::fromUTF8 ("MONO SAFE"), th::cyan),
      syncCtl (p.apvts, pid::RATE, pid::SYNC, pid::DIVISION, pulsar::params::sync::labels(), th::cyan)
      // el RATE (Hz) vive DENTRO del TEMPO (SyncControl): knob en FREE, chips de división en SYNC
{
    setFamilyHue (th::cyan);   // Movimiento: el fondo (Panel) + header respetan el color de familia
    meter.setSampleRate (p.getSampleRate() > 0.0 ? p.getSampleRate() : 48000.0);

    headerHeight = 48;   // header del mockup (48px)

    // Bottom-bar contextual (doctrina del sello): NOMBRE + VALOR del control bajo el cursor en mono.
    enableBottomBar (th::cyan, juce::String::fromUTF8 ("MOV·02"));   // sin copy de tutorial (se ve placeholder)
    bottomBar().setSignature (juce::String::fromUTF8 ("SIGNAL STABLE"));

    addToCanvas (pad);
    addToCanvas (morph);
    addToCanvas (meter);
    addToCanvas (inPhase);
    addToCanvas (syncCtl);

    // el morph (HÉROE) alimenta la bottom-bar contextual igual que los knobs
    morph.onHover     = [this] (const juce::String& nm, const juce::String& v) { bottomBar().show (nm, v); };
    morph.onHoverExit = [this] { bottomBar().clear(); };
    morph.setTitle (juce::String::fromUTF8 ("SHAPE \xe2\x80\x94 attractor morph (Orbit\xc2\xb7Pendulum\xc2\xb7Lorenz\xc2\xb7R\xc3\xb6ssler)"));

    // accesibilidad: nombres accesibles + foco de teclado en los controles del chasis/core.
    for (auto* c : { (juce::Component*) &inPhase, (juce::Component*) &syncCtl })
        c->setWantsKeyboardFocus (true);
    inPhase.setTitle (juce::String::fromUTF8 ("IN PHASE (mono safe)"));
    syncCtl.setTitle (juce::String::fromUTF8 ("SYNC (motion rate)"));
    pad.setTitle     (juce::String::fromUTF8 ("STELLAR FIELD — drag the attractor center"));

    // los macros (rotary, MISMO tamaño) — grilla ALINEADA (SHAPE=morph héroe, RATE=adentro del TEMPO)
    setupKnob (motion, pid::MOTION, "MOTION", th::cyan, 76);
    setupKnob (smear,  pid::SMEAR,  "SMEAR",  th::cyan, 76);
    setupKnob (width,  pid::WIDTH,  "WIDTH",  th::cyan, 76);   // RATE formatea "Hz" desde el param (no acá)
    setupKnob (lowCut, pid::LOWCUT, juce::String::fromUTF8 ("LOW CUT"), th::cyan, 76);   // PRO: filtros de tono
    setupKnob (hiCut,  pid::HICUT,  juce::String::fromUTF8 ("HI CUT"),  th::cyan, 76);

    // riel de salida (faders horizontales, estilo ÓRBITA): MIX (uni) · IN/OUT (bip, dB)
    setupFader (mixF, pid::MIX,  "MIX", "pct", false);
    setupFader (inF,  "inGain",  "IN",  "db",  true);
    setupFader (outF, "output",  "OUT", "db",  true);

    setBaseSize (960, 580);   // layout ÓRBITA: house standard del sello (igual que AURORA/HORIZON/ÓRBITA)
}

PulsarEditor::~PulsarEditor()
{
    // Soltar el L&F antes de destruir los sliders (regla JUCE).
    for (auto* k : { &motion, &smear, &width, &lowCut, &hiCut })
        k->slider.setLookAndFeel (nullptr);
    for (auto* f : { &mixF, &inF, &outF })
        f->slider.setLookAndFeel (nullptr);
}

void PulsarEditor::setupKnob (Knob& k, const juce::String& paramID, const juce::String& text,
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

void PulsarEditor::setupFader (Fader& f, const juce::String& paramID, const juce::String& name,
                              const juce::String& fmt, bool bipolar)
{
    f.fmt = fmt; f.bipolar = bipolar;
    auto& s = f.slider;
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.getProperties().set ("bip", bipolar);
    s.getProperties().set ("hue", (int) th::cyan.getARGB());
    s.setLookAndFeel (&railLaf);
    s.setName (name);  s.setTitle (name);
    s.setWantsKeyboardFocus (true);
    addToCanvas (s);
    f.attach = std::make_unique<SliderAttachment> (proc.apvts, paramID, s);

    // nombre (izquierda) + valor vivo (derecha): igual que el riel de ÓRBITA.
    f.nameLabel.setText (name, juce::dontSendNotification);
    f.nameLabel.setJustificationType (juce::Justification::centredLeft);
    f.nameLabel.setColour (juce::Label::textColourId, th::mut);
    f.nameLabel.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.16f));
    addToCanvas (f.nameLabel);

    f.valueLabel.setJustificationType (juce::Justification::centredRight);
    f.valueLabel.setColour (juce::Label::textColourId, th::txt);
    f.valueLabel.setFont (fonts::mono (8.5f));
    addToCanvas (f.valueLabel);

    auto* fp = &f;
    s.onValueChange = [this, fp] { fp->valueLabel.setText (faderValueText (*fp), juce::dontSendNotification); };
    f.valueLabel.setText (faderValueText (f), juce::dontSendNotification);   // valor inicial
}

juce::String PulsarEditor::faderValueText (const Fader& f) const
{
    const float v = (float) f.slider.getValue();
    if (f.fmt == "db")
        return (v >= 0.0f ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (v), 1) + " dB";
    return juce::String (juce::roundToInt (v)) + "%";
}

void PulsarEditor::layoutBody (juce::Rectangle<int> body)
{
    using namespace metrics;
    knobIdx.clear();

    // ===== COPIA ÓRBITA: POZO grande IZQUIERDA · bahía DERECHA · tira de meter al borde =====
    meterArea = body.removeFromRight (meterW);
    meter.setBounds (meterArea.withTrimmedTop (16).withTrimmedBottom (12));
    body.removeFromRight (bayGap);
    bayArea = body.removeFromRight (bayW);
    pad.setBounds (body);                                  // EL POZO domina la izquierda (alto completo)

    // ritmo de 2 niveles (anti-sparse): gap intra-sección chico, gap inter-sección medio + rib hairline.
    auto bay = bayArea.reduced (padIn, 12);
    bay.removeFromTop (12);   // sin rib "MOVEMENT" (era título sin contenido debajo): arrancamos en SHAPE

    // --- § SHAPE: MorphSelector héroe (íconos ORBIT·PENDULUM·LORENZ·RÖSSLER + marcador) ---
    morphCapArea = bay.removeFromTop (15);                 // rib "SHAPE // ATTRACTOR" (paint)
    bay.removeFromTop (6);
    morph.setBounds (bay.removeFromTop (84));
    bay.removeFromTop (20);

    // --- § TEMPO: SyncControl = [FREE|SYNC] + RATE knob (FREE) / chips de división (SYNC) ---
    tempoLblArea = bay.removeFromTop (15);                 // rib "TEMPO" (paint)
    bay.removeFromTop (6);
    syncCtl.setBounds (bay.removeFromTop (84));            // alto: el SyncControl muestra el knob RATE adentro
    bay.removeFromTop (20);

    // --- RIEL inferior anclado: faders MIX · IN · OUT + IN PHASE (estilo ÓRBITA) ---
    {
        const int railH = 62, gap = 14, monoW = 80;
        railArea = bay.removeFromBottom (railH);
        bay.removeFromBottom (22);

        auto rr = railArea;
        auto monoCell = rr.removeFromRight (monoW);
        inPhase.setBounds (monoCell.withSizeKeepingCentre (monoW, juce::jmin (rr.getHeight(), 46)));
        rr.removeFromRight (gap);

        const int slotW = (rr.getWidth() - gap * 2) / 3;
        auto placeFader = [&] (Fader& f, juce::Rectangle<int> cell)
        {
            auto lbl = cell.removeFromTop (13);
            f.nameLabel.setBounds  (lbl.removeFromLeft (lbl.getWidth() * 45 / 100));
            f.valueLabel.setBounds (lbl);
            cell.removeFromTop (6);
            f.slider.setBounds (cell.removeFromTop (16));
        };
        placeFader (mixF, rr.removeFromLeft (slotW)); rr.removeFromLeft (gap);
        placeFader (inF,  rr.removeFromLeft (slotW)); rr.removeFromLeft (gap);
        placeFader (outF, rr);
    }

    // --- § MACROS: fila ALINEADA de 5 (MISMO tamaño): MOTION·SMEAR·WIDTH·LOW CUT·HI CUT ---
    macroCapArea = bay.removeFromTop (15);                 // rib "MACROS" (paint)
    bay.removeFromTop (8);
    {
        constexpr int kD = 62;                            // diámetro ÚNICO de los 5 knobs
        const int blockH = kD + 16 + 15;                  // knob + textbox + label
        auto row = bay.removeFromTop (blockH);            // pegados al rib (sin centrar -> sin hueco central)
        Knob* const ks[5] = { &motion, &smear, &width, &lowCut, &hiCut };
        const int cw = row.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
        {
            auto cell = row.removeFromLeft (i == 4 ? row.getWidth() : cw);
            ks[i]->label.setBounds (cell.removeFromTop (15));    // NOMBRE arriba (convención ÓRBITA)
            cell.removeFromTop (1);
            ks[i]->slider.setBounds (cell.withSizeKeepingCentre (kD, kD + 16));   // knob + valor (textbox) debajo
        }
    }
}

void PulsarEditor::paintBody (juce::Graphics& g)
{
    const auto fBay = bayArea.toFloat();

    // ===== luz volumétrica cian que emana del POZO (izquierda, holgado) =====
    {
        const auto pc = pad.getBounds().toFloat().getCentre();
        juce::ColourGradient halo (th::cyan.withAlpha (0.05f), pc.x, pc.y,
                                   th::cyan.withAlpha (0.0f),  pc.x, pc.y - 320.0f, true);
        halo.addColour (0.55, th::cyan.withAlpha (0.018f));
        g.setGradientFill (halo);
        g.fillRect (pad.getBounds().toFloat().expanded (80.0f, 40.0f));
    }

    // ===== BAHÍA elevada: panel CONTINUO y parejo hasta ABAJO (no se desvanece a negro) + bisel + cierre =====
    {
        // wash frío uniforme: leve tinte cian de arriba a abajo (mismo panel para SHAPE…faders, no franja colgada)
        juce::ColourGradient cg (juce::Colour (0xffa0c0e0).withAlpha (0.060f),
                                 fBay.getX(), fBay.getY(),
                                 juce::Colour (0xffa0c0e0).withAlpha (0.016f), fBay.getX(), fBay.getBottom(), false);
        cg.addColour (0.5, juce::Colour (0xffa0c0e0).withAlpha (0.030f));
        g.setGradientFill (cg);
        g.fillRect (fBay);

        juce::ColourGradient edge (th::cyan.withAlpha (0.06f), fBay.getX(), fBay.getCentreY(),
                                   th::cyan.withAlpha (0.0f), fBay.getX() + 160.0f, fBay.getCentreY(), false);
        g.setGradientFill (edge);
        g.fillRect (fBay);

        g.setColour (juce::Colour (0xffbee1ff).withAlpha (0.07f));
        g.drawHorizontalLine (bayArea.getY(), fBay.getX(), fBay.getRight());        // inset highlight SUPERIOR
        g.setColour (juce::Colour (0x2aa0c0e0));
        g.drawHorizontalLine (bayArea.getBottom() - 1, fBay.getX(), fBay.getRight()); // borde INFERIOR (cierra el panel)
        g.setColour (juce::Colour (0x66a0c0e0));
        g.drawVerticalLine (bayArea.getX(), fBay.getY(), fBay.getBottom());          // filo (bahía elevada sobre el pozo)
        g.setColour (juce::Colour (0x14bee1ff));
        g.drawVerticalLine (bayArea.getX() + 1, fBay.getY(), fBay.getBottom());      // highlight interno del bisel
    }

    // ===== RIEL: sólo una costilla de separación; los faders viven SOBRE el panel continuo (no franja colgada) =====
    {
        g.setColour (th::cyan.withAlpha (0.14f));
        g.drawHorizontalLine (railArea.getY() - 8, fBay.getX(), fBay.getRight());
    }

    // ===== tira del METER: rebaje del chasis (borde derecho) =====
    {
        const auto fM = meterArea.toFloat();
        juce::ColourGradient rec (th::bg0.withAlpha (0.34f), fM.getX(), fM.getY(),
                                  th::bg0.withAlpha (0.42f), fM.getX(), fM.getBottom(), false);
        g.setGradientFill (rec);
        g.fillRect (fM);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (fM.getX(), fM.getY(), 1.0f, fM.getHeight());
        g.setColour (juce::Colour (0x0abee1ff));
        g.fillRect (fM.getX(), fM.getBottom() - 1.0f, fM.getWidth(), 1.0f);
    }

    // ===== ribs de sección (costillas ÓRBITA): tick índice + LABEL near-white // sub cyan + hairline =====
    auto rib = [&g] (juce::Rectangle<int> a, const juce::String& main, const juce::String& sub)
    {
        auto f = fonts::mono (8.5f).withExtraKerningFactor (0.20f);
        g.setFont (f);
        auto strW = [&f] (const juce::String& s)
        {
            juce::GlyphArrangement ga; ga.addLineOfText (f, s, 0.0f, 0.0f);
            return (float) ga.getBoundingBox (0, -1, true).getWidth();
        };
        const float cy = (float) a.getCentreY();
        float x = (float) a.getX();
        g.setColour (th::cyan.withAlpha (0.55f));                 // tick índice (acento)
        g.fillRect (x, cy - 4.5f, 2.0f, 9.0f);
        x += 9.0f;
        g.setColour (th::txt);                                    // LABEL = tier 1 (near-white)
        g.drawText (main, juce::Rectangle<float> (x, (float) a.getY(), strW (main) + 4.0f, (float) a.getHeight()),
                    juce::Justification::centredLeft);
        x += strW (main) + 7.0f;
        if (sub.isNotEmpty())
        {
            g.setColour (th::cyanD.withAlpha (0.6f));
            g.drawText (sub, juce::Rectangle<float> (x, (float) a.getY(), strW (sub) + 6.0f, (float) a.getHeight()),
                        juce::Justification::centredLeft);
            x += strW (sub) + 8.0f;
        }
        g.setColour (th::cyan.withAlpha (0.13f));                 // hairline hasta el borde de la bahía
        g.fillRect (x, cy - 0.5f, (float) a.getRight() - x, 1.0f);
    };
    rib (morphCapArea, "SHAPE",  "// ATTRACTOR");
    rib (tempoLblArea, "TEMPO",  {});
    rib (macroCapArea, "MACROS", {});
}

} // namespace pulsar
