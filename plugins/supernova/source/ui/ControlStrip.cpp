#include "ui/ControlStrip.h"
#include "params/ParameterIDs.h"
#include "params/ParamMapping.h"   // detail::varNoise — la misma matemática determinista de VARIATION
#include "presets/CanvasState.h"
#include "color/LookRamps.h"
#include <iterator>
#include "ui-kit/Theme.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace supernova
{
namespace pid = params::id;
namespace th  = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

namespace
{
// Los 4 DOMINIOS de la franja + el DADO. El TINTE del dominio pinta nombre de grupo, labels, knobs,
// steppers y botones (pedido: "que los colores de los nombres sean los mismos de los knobs") — de un
// vistazo se sabe QUÉ toca cada control. El nombre del grupo lo lleva el LABEL del hotknob del riel.
inline juce::Colour groupTint (int row)
{
    switch (row)
    {
        case 0:  return juce::Colour (0xffff7a3d);   // MOVEMENT · fuego (la fila héroe, el hue de familia)
        case 1:  return juce::Colour (0xff7de8c9);   // MATTER · menta
        case 2:  return juce::Colour (0xff7fd0ff);   // CAMERA · cielo
        default: return juce::Colour (0xffff7ae0);   // COLOR · rosa
    }
}
inline juce::Colour dadoTint() { return juce::Colour (0xffb48cff); }   // DADO · violeta OVNI

constexpr int kRailW   = 76;   // riel izquierdo: la celda del HOTKNOB del dominio (label = nombre del grupo)
constexpr int kHintH   = 16;   // barra de ayuda inferior (angosta: una línea de mono 11)
constexpr int kCols    = 8;    // LA CUADRÍCULA: mismas 8 columnas en las 4 filas (todo alineado)
constexpr int kMaxContentW = 1240;   // en la app (full-bleed) la franja corre a lo ancho de la ventana; el
                                     // grid se CAPEA y centra acá para que los knobs no queden desparramados
                                     // en ventanas anchas (el fondo sigue full-bleed; el cluster va centrado).
constexpr int kLabelH  = 14;   // alto del label de celda (knobs y steppers comparten métrica)
constexpr int kValueH  = 15;   // alto del textbox del knob (los steppers se centran contra la misma zona)
}

// ChoiceStepper — selector de choice en la línea del ui-kit: ◂ VALOR ▸. Click en el valor o wheel también
// ciclan (wrap). Usa ParameterAttachment → sincroniza con automatización/preset y notifica al host con gesture.
// Comparte la métrica de celda con los knobs (label arriba, cuerpo centrado contra la zona del círculo) para
// que la cuadrícula quede ÓPTICAMENTE alineada.
class ChoiceStepper : public juce::Component
{
public:
    ChoiceStepper (juce::RangedAudioParameter& p, const juce::String& text, juce::Colour tintColour)
        : param (p), tint (tintColour),
          attach (p, [this] (float) { refresh(); }, nullptr)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, tint);
        label.setFont (fonts::mono (11.0f));
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        auto styleArrow = [this] (juce::TextButton& b, int dir)
        {
            b.setColour (juce::TextButton::buttonColourId,  th::bg1);
            b.setColour (juce::TextButton::textColourOffId, tint);
            b.setWantsKeyboardFocus (false);
            b.onClick = [this, dir] { step (dir); };
            addAndMakeVisible (b);
        };
        styleArrow (prev, -1);
        styleArrow (next, +1);

        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, tint);
        value.setColour (juce::Label::backgroundColourId, th::bg1);
        value.setFont (fonts::mono (12.0f));
        value.setInterceptsMouseClicks (false, false);   // el click cae al stepper (si no, el Label se lo come)
        addAndMakeVisible (value);

        setTitle (text);
        attach.sendInitialUpdate();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (kLabelH));
        // Centrado contra la zona del CÍRCULO del knob (excluye el textbox de abajo) → misma línea óptica.
        auto area = r.withTrimmedBottom (kValueH);
        auto row  = area.withSizeKeepingCentre (area.getWidth(), swatchPainter ? 30 : 24);
        prev.setBounds (row.removeFromLeft (20));
        next.setBounds (row.removeFromRight (20));
        value.setBounds (row.reduced (2, 0));
    }

    // SWATCH visual (PALETTE): pinta el degradé REAL del look debajo del nombre — la paleta se VE, no se
    // lee. El painter recibe (g, bounds del value, índice actual). Activa texto blanco + fondo transparente.
    void enableSwatch (std::function<void (juce::Graphics&, juce::Rectangle<int>, int)> painter)
    {
        swatchPainter = std::move (painter);
        value.setColour (juce::Label::textColourId, juce::Colours::white);
        value.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (swatchPainter)
            swatchPainter (g, value.getBounds(), currentIndex());
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        // Acumular con umbral: el trackpad manda ráfagas de deltas chicos + inercia → sin esto un gesto
        // dispara varios steps (selección incontrolable y spam de gestures al host). La cola de momentum
        // (isInertial) se ignora entera: al levantar los dedos, la selección queda donde la dejaste.
        if (w.isInertial) return;
        wheelAccum += w.deltaY;
        if (std::abs (wheelAccum) < 0.25f) return;
        step (wheelAccum > 0.0f ? -1 : 1);
        wheelAccum = 0.0f;
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (value.getBounds().contains (e.getPosition())) step (1);   // click en el valor = ciclar
    }

private:
    int  numChoices() const
    {
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&param)) return c->choices.size();
        return 2;
    }
    int  currentIndex() const
    {
        return (int) std::round (param.convertFrom0to1 (param.getValue()));
    }
    void step (int dir)
    {
        const int n = numChoices();
        const int idx = (currentIndex() + dir + n) % n;               // wrap: cicla como un selector de DJ
        attach.setValueAsCompleteGesture ((float) idx);               // valor desnormalizado (índice)
        refresh();
    }
    void refresh()
    {
        value.setText (param.getCurrentValueAsText(), juce::dontSendNotification);
        if (swatchPainter) repaint();                                 // el degradé sigue al valor
    }

    juce::RangedAudioParameter& param;
    juce::Colour tint;
    float wheelAccum = 0.0f;
    juce::Label  label, value;
    juce::TextButton prev { juce::String::fromUTF8 ("\xE2\x97\x82") },   // ◂
                     next { juce::String::fromUTF8 ("\xE2\x96\xB8") };   // ▸
    std::function<void (juce::Graphics&, juce::Rectangle<int>, int)> swatchPainter;
    juce::ParameterAttachment attach;
};

ControlStrip::ControlStrip (juce::AudioProcessorValueTreeState& state, juce::Colour familyHue)
    : apvts (state), hue (familyHue)
{
    auto makeStepper = [this] (const char* id, const char* text, juce::Colour tint, const char* hintText)
    {
        auto s = std::make_unique<ChoiceStepper> (*apvts.getParameter (id), text, tint);
        addAndMakeVisible (*s);
        registerHint (*s, hintText);
        return s;
    };

    // MOVEMENT · cómo vive la materia — orden por IMPACTO MEDIDO (tools/supernova-knob-impact.py);
    // INTENSITY primero por diseño: con el camino de quietud es el fader de vida (0 = foto congelada).
    const auto tMov = groupTint (0);
    motion = makeStepper (pid::MOTION, "MOTION", tMov, "MOTION - the movement engine: how matter lives");
    setupKnob (intensity, pid::INTENSITY, "INTENSITY", "INTENSITY - master life fader (0 = frozen still)", tMov);
    setupKnob (speed,     pid::SPEED,     "SPEED",     "SPEED - slow motion to double time (x0.25 - x4)", tMov);
    setupKnob (gravityK,  pid::GRAVITY,   "GRAVITY",   "GRAVITY - constant pull (down / up)", tMov);
    setupKnob (pump,      pid::PUMP,      "PUMP",      "PUMP - how much the kick inflates (visual sidechain)", tMov);
    setupKnob (chaos,     pid::CHAOS,     "CHAOS",     "CHAOS - turbulence of the flow", tMov);
    setupKnob (breathe,   pid::BREATHE_GAIN, "BREATHE", "BREATHE - how much the level makes it simmer", tMov);
    setupKnob (blast,     pid::RADIAL_GAIN, "BLAST",   "BLAST - kick explosion force", tMov);

    // MATTER · de qué está hecho — DENSITY/SCATTER mandan (medido 78/31)
    const auto tMat = groupTint (1);
    shape = makeStepper (pid::SHAPE, "SHAPE", tMat, "SHAPE - the glyph: what every point is made of");
    setupKnob (density, pid::DENSITY,       "DENSITY", "DENSITY - how many particles are drawn", tMat);
    setupKnob (scatter, pid::SCATTER,       "SCATTER", "SCATTER - image to dispersed cloud (re-formable)", tMat);
    setupKnob (size,    pid::PARTICLE_SIZE, "SIZE",    "SIZE - glyph size (pointillism to paint blobs)", tMat);
    setupKnob (trails,  pid::TRAILS,        "TRAILS",  "TRAILS - glyphs paint fading paths", tMat);
    setupKnob (cutout,  pid::CUTOUT,        "CUTOUT",  "CUTOUT - erase the background, keep the sculpture", tMat);
    setupKnob (links,   pid::LINKS,         "LINKS",   "LINKS - plexus: connections between neighbours", tMat);

    // CAMERA · desde dónde lo ves — la vuelta (ROT Y/X) medida arriba de todo
    const auto tCam = groupTint (2);
    figure = makeStepper (pid::FIGURE, "FIGURE", tCam, "FIGURE - target figure: sphere, spiral, rings...");
    setupKnob (rotY,   pid::ROT_Y,  "ROT Y",  "ROT Y - turn it around (yaw +/-180)", tCam);
    setupKnob (rotX,   pid::ROT_X,  "ROT X",  "ROT X - tilt it (pitch +/-180)", tCam);
    setupKnob (orbit,  pid::ORBIT,  "ORBIT",  "ORBIT - auto-orbit (deg/s)", tCam);
    setupKnob (rotate, pid::ROTATE, "ROTATE", "ROTATE - 2D view spin (roll)", tCam);
    setupKnob (form,   pid::FORM,   "FORM",   "FORM - how hard the figure pulls (image to figure)", tCam);
    setupKnob (depth,  pid::DEPTH,  "DEPTH",  "DEPTH - 3D volume of the sculpture", tCam);
    kaleido = makeStepper (pid::KALEIDO, "KALEIDO", tCam, "KALEIDO - kaleidoscope mirrors (the lens)");

    // COLOR · la luz — y el DADO al final (separado por el divisor, en violeta OVNI)
    const auto tCol = groupTint (3);
    palette = makeStepper (pid::PALETTE, "PALETTE", tCol, "PALETTE - the look: the palette as an instrument");
    // SWATCH visual: el stepper muestra el DEGRADÉ real del look (stops de LookRamps) bajo el nombre.
    palette->enableSwatch ([] (juce::Graphics& g, juce::Rectangle<int> r, int idx)
    {
        idx = juce::jlimit (0, color::kNumLooks - 1, idx);
        const auto& look = color::kLooks[(size_t) idx];
        const auto rf = r.toFloat();
        juce::ColourGradient grad (juce::Colour (0xff000000u | look.stops[0].rgb),
                                   rf.getX(), rf.getCentreY(),
                                   juce::Colour (0xff000000u | look.stops[look.numStops - 1].rgb),
                                   rf.getRight(), rf.getCentreY(), false);
        for (int s = 1; s < look.numStops - 1; ++s)
            grad.addColour ((double) look.stops[s].pos, juce::Colour (0xff000000u | look.stops[s].rgb));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (rf, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.34f));   // scrim: el nombre se lee sobre cualquier look
        g.fillRoundedRectangle (rf, 5.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawRoundedRectangle (rf.reduced (0.5f), 5.0f, 1.0f);
    });
    setupKnob (colorAmt, pid::COLOR_AMOUNT, "AMOUNT",  "AMOUNT - how much palette (0 = your image's colors)", tCol);
    setupKnob (hueKnob,  pid::HUE,          "HUE",     "HUE - tone rotation", tCol);
    setupKnob (sat,      pid::SAT,          "SAT",     "SAT - saturation (B/W to vivid)", tCol);
    setupKnob (hueCycle, pid::HUE_CYCLE,    "HUE CYC", "HUE CYC - the tone drifts on its own (deg/s)", tCol);
    setupKnob (glow,     pid::GLOW,         "GLOW",    "GLOW - bloom / radiance", tCol);
    setupKnob (variation, pid::VARIATION,   "VARIATION",
               "VARIATION - MASTER take: shifts every row at once (same number = same take, 0 = pure preset)",
               dadoTint());

    // HOTKNOBS de variación POR DOMINIO (los rieles): celdas IDÉNTICAS a las demás (mismo tamaño de knob,
    // label = nombre del grupo, textbox con el %). Girarlos ESCRIBE los params reales de su fila → los
    // knobs del grupo SE MUEVEN (pedido de campo). Volver a 0 restaura la base.
    setupKnob (hotMov, pid::VAR_MOVEMENT, "MOVEMENT",
               "MOVEMENT VAR - takes of this row: MOVES its knobs (0 = back to base)", tMov);
    setupKnob (hotMat, pid::VAR_MATTER,   "MATTER",
               "MATTER VAR - takes of this row: MOVES its knobs (0 = back to base)", tMat);
    setupKnob (hotCam, pid::VAR_CAMERA,   "CAMERA",
               "CAMERA VAR - takes of this row: MOVES its knobs (0 = back to base)", tCam);
    setupKnob (hotCol, pid::VAR_COLOR,    "COLOR",
               "COLOR VAR - takes of this row: MOVES its knobs (0 = back to base)", tCol);
    hotMov.slider.onValueChange = [this] { applyDomainTake (0, (float) hotMov.slider.getValue()); };
    hotMat.slider.onValueChange = [this] { applyDomainTake (1, (float) hotMat.slider.getValue()); };
    hotCam.slider.onValueChange = [this] { applyDomainTake (2, (float) hotCam.slider.getValue()); };
    hotCol.slider.onValueChange = [this] { applyDomainTake (3, (float) hotCol.slider.getValue()); };
    // El riel mide 76 px: el textbox estándar (72) desbordaría la tarjeta — angostarlo para que quede adentro.
    for (auto* hot : { &hotMov, &hotMat, &hotCam, &hotCol })
        hot->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, kValueH);

    // RANDOM: el dado TOTAL — randomiza el mundo entero (motor, materia, cámara y color) dentro de rangos
    // curados: nunca pantalla muerta, siempre un mundo nuevo de verdad.
    randomBtn = std::make_unique<juce::TextButton> ("RANDOM");
    randomBtn->setColour (juce::TextButton::buttonColourId,  th::bg1);
    randomBtn->setColour (juce::TextButton::textColourOffId, dadoTint());
    randomBtn->setColour (juce::TextButton::textColourOnId,  th::bg0);
    randomBtn->setTitle ("RANDOM (randomize the whole world)");
    randomBtn->onClick = [this] { randomizeWorld(); };
    addAndMakeVisible (*randomBtn);
    registerHint (*randomBtn, "RANDOM - total dice: a brand new world every press (bounded sane)");

    // Barra de AYUDA: describe el control bajo el mouse (escuchamos los hijos con el listener recursivo).
    hintBar.setJustificationType (juce::Justification::centredLeft);
    hintBar.setColour (juce::Label::textColourId, th::mut);
    hintBar.setFont (fonts::mono (11.0f));
    hintBar.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (hintBar);
    updateHint (nullptr);
    addMouseListener (this, true);   // los move/exit de TODOS los hijos pasan por acá
    hotReady = true;                 // recién ahora los hotknobs escriben (el sync inicial ya pasó)
}

ControlStrip::~ControlStrip()
{
    removeMouseListener (this);
    // Regla JUCE: soltar el L&F antes de destruir los sliders (evita dangling en el LookAndFeel).
    for (auto* c : { &intensity, &chaos, &speed, &pump, &blast, &gravityK, &breathe,
                     &size, &density, &scatter, &trails, &links, &cutout,
                     &form, &depth, &rotX, &rotY, &orbit, &rotate,
                     &colorAmt, &sat, &hueKnob, &hueCycle, &glow, &variation,
                     &hotMov, &hotMat, &hotCam, &hotCol })
        c->slider.setLookAndFeel (nullptr);
}

void ControlStrip::setupKnob (KnobCell& cell, const juce::String& paramID, const juce::String& text,
                              const juce::String& hintText, juce::Colour tint)
{
    cell.slider.setKnobLookAndFeel (&knobLaf);
    cell.slider.getProperties().set ("hue", (int) tint.getARGB());   // el knob toma el TINTE del dominio
    cell.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, kValueH);
    cell.slider.setColour (juce::Slider::textBoxTextColourId, th::mut);
    cell.slider.controlName = text;
    cell.slider.setName  (text);
    cell.slider.setTitle (text);
    addAndMakeVisible (cell.slider);

    cell.label.setText (text, juce::dontSendNotification);
    cell.label.setJustificationType (juce::Justification::centred);
    cell.label.setColour (juce::Label::textColourId, tint);          // el nombre, del MISMO color que el knob
    cell.label.setFont (fonts::mono (11.0f));
    cell.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (cell.label);

    cell.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, cell.slider);
    knobParams[&cell.slider] = paramID;                 // MIDI-learn: slider → paramID
    cell.slider.addMouseListener (this, false);         // el click-derecho llega a ControlStrip::mouseDown

    if (auto* pr = apvts.getParameter (paramID))
    {
        const auto& range = apvts.getParameterRange (paramID);
        cell.slider.setDoubleClickReturnValue (false, (double) range.convertFrom0to1 (pr->getDefaultValue()));
    }

    registerHint (cell.slider, hintText);
}

// HOTKNOB del riel: mini-knob de variación (sin textbox ni label — el nombre del grupo ya está arriba y el
// hint lo describe). Comparte el tinte del dominio; doble-click = 0 (neutro).
// ============================ HOTKNOBS: tomas que MUEVEN los knobs de su fila ============================
// El hotknob escribe los params REALES de su dominio: newVal = base + span·envolvente·seno(seed,field,v).
// Determinista (mismo preset + mismo valor = misma toma), reversible (0 restaura la base capturada al salir
// de 0), y a prueba de choques: al volver a 0 solo restaura los params cuyo valor actual sigue siendo el que
// ESTE hotknob escribió (si un preset/CLEAR/mano los pisó, se respeta lo nuevo y solo se suelta la base).
namespace
{
struct TakeAxis { const char* id; float span; int field; float cycles; };

// Rangos CURADOS en unidades de param (mismo espíritu que applyVariation/RANDOM: acentos, nunca mareo).
// CAMERA usa desvíos ABSOLUTOS a propósito: sus ejes suelen estar en 0 y un desvío relativo no haría NADA
// (el hallazgo de la medición) — acá una toma de cámara inclina/gira/da volumen de verdad.
const TakeAxis kMovementAxes[] = {
    { supernova::params::id::INTENSITY,     18.0f, 8,  1.0f }, { supernova::params::id::CHAOS,        30.0f, 7,  2.0f },
    { supernova::params::id::SPEED,         14.0f, 17, 1.0f }, { supernova::params::id::GRAVITY,      22.0f, 2,  1.0f },
    { supernova::params::id::PUMP,          28.0f, 19, 2.0f }, { supernova::params::id::RADIAL_GAIN,  28.0f, 4,  1.0f },
    { supernova::params::id::BREATHE_GAIN,  28.0f, 6,  1.0f }, { supernova::params::id::CURL_SCALE,   24.0f, 0,  1.0f },
    { supernova::params::id::HOME_STRENGTH, 20.0f, 1,  2.0f }, { supernova::params::id::MOMENTUM,     18.0f, 3,  2.0f },
    { supernova::params::id::JITTER_GAIN,   24.0f, 5,  2.0f } };
const TakeAxis kMatterAxes[] = {
    { supernova::params::id::PARTICLE_SIZE, 24.0f, 9,  1.0f }, { supernova::params::id::DENSITY,      28.0f, 15, 1.0f },
    { supernova::params::id::SCATTER,       22.0f, 16, 2.0f }, { supernova::params::id::TRAILS,       26.0f, 11, 1.0f },
    { supernova::params::id::LINKS,         24.0f, 12, 2.0f } };
const TakeAxis kCameraAxes[] = {
    { supernova::params::id::ROT_X,         38.0f, 24, 1.0f }, { supernova::params::id::ROT_Y,        38.0f, 25, 1.0f },
    { supernova::params::id::DEPTH,         35.0f, 21, 1.0f }, { supernova::params::id::ORBIT,        18.0f, 22, 2.0f },
    { supernova::params::id::ROTATE,        14.0f, 18, 1.0f } };
const TakeAxis kColorAxes[] = {
    { supernova::params::id::HUE,           55.0f, 13, 1.0f }, { supernova::params::id::SAT,          22.0f, 14, 2.0f },
    { supernova::params::id::GLOW,          22.0f, 10, 2.0f }, { supernova::params::id::HUE_CYCLE,    12.0f, 20, 1.0f },
    { supernova::params::id::COLOR_AMOUNT,  18.0f, 26, 1.0f } };

std::pair<const TakeAxis*, int> axesOf (int domain)
{
    switch (domain)
    {
        case 0:  return { kMovementAxes, (int) std::size (kMovementAxes) };
        case 1:  return { kMatterAxes,   (int) std::size (kMatterAxes) };
        case 2:  return { kCameraAxes,   (int) std::size (kCameraAxes) };
        default: return { kColorAxes,    (int) std::size (kColorAxes) };
    }
}
}

void ControlStrip::applyDomainTake (int domain, float value)
{
    if (! hotReady) return;                                    // sync inicial del attachment: no escribir
    auto [axes, nAxes] = axesOf (domain);
    auto& take = takes[(size_t) domain];
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    if (value <= 0.01f)                                        // 0 = volver a la base (la toma se apaga)
    {
        if (take.captured)
            for (int i = 0; i < nAxes; ++i)
            {
                // Guard anti-choque: restaurar SOLO si el valor actual sigue siendo el que escribimos
                // (un preset/CLEAR/RANDOM/mano en el medio manda — no se pisa lo nuevo con la base vieja).
                const float cur = raw (axes[i].id);
                if (std::abs (cur - take.lastWritten[(size_t) i].second) < 1.0f)
                    canvas::setParamAsGesture (apvts, axes[i].id, take.base[(size_t) i].second);
            }
        take = {};
        return;
    }

    if (! take.captured)                                       // saliendo de 0: capturar la BASE de la fila
    {
        take.captured = true;
        take.base.clear();
        take.lastWritten.clear();
        for (int i = 0; i < nAxes; ++i)
        {
            take.base.emplace_back (axes[i].id, raw (axes[i].id));
            take.lastWritten.emplace_back (axes[i].id, raw (axes[i].id));
        }
    }

    // La toma: base + span·envolvente·seno — misma matemática determinista que VARIATION (varNoise), con
    // la semilla del preset activo (mismo preset + mismo número = la misma toma, recuperable).
    const int   seed = (int) raw (pid::PRESET);
    const float v01  = value / 100.0f;
    const float env  = std::min (1.0f, v01 / 0.12f);
    for (int i = 0; i < nAxes; ++i)
    {
        const auto& range = apvts.getParameterRange (axes[i].id);
        const float dev = axes[i].span * env * detail::varNoise (seed, axes[i].field, v01, axes[i].cycles);
        const float val = juce::jlimit (range.start, range.end, take.base[(size_t) i].second + dev);
        canvas::setParamAsGesture (apvts, axes[i].id, val);
        take.lastWritten[(size_t) i].second = val;
    }
}

void ControlStrip::registerHint (juce::Component& c, const juce::String& text)
{
    hints[&c] = text;
}

void ControlStrip::updateHint (juce::Component* under)
{
    // Subir por la jerarquía hasta encontrar un control registrado (el evento puede venir de un sub-hijo
    // del stepper o del textbox del slider).
    for (auto* c = under; c != nullptr && c != this; c = c->getParentComponent())
    {
        auto it = hints.find (c);
        if (it != hints.end()) { hintBar.setText (it->second, juce::dontSendNotification); return; }
    }
    hintBar.setText ("hover a control - double-click = default - everything automatable from the DAW",
                     juce::dontSendNotification);
}

// RANDOM — el dado TOTAL: randomiza motor + materia + cámara + color dentro de RANGOS CURADOS (la imagen
// nunca muere: intensidad/densidad con piso, speed cerca de ×1, kaleido/órbita mayormente apagados). NO toca
// preset/cutout/bypass/explode (la escultura y el mundo elegido son del usuario). VARIATION vuelve a 0 (este
// dado ES la variación). Escribe por setValueNotifyingHost → el host lo registra y los knobs saltan.
void ControlStrip::randomizeWorld()
{
    if (onBeforeEdit) onBeforeEdit();   // UNDO (Phase C): captura el estado ANTES del dado → RANDOM es reversible
    auto& rng = juce::Random::getSystemRandom();
    auto set = [this] (const char* id, float v) { canvas::setParamAsGesture (apvts, id, v); };
    auto uni   = [&rng] (float lo, float hi) { return lo + rng.nextFloat() * (hi - lo); };
    auto maybe = [&rng] (float prob) { return rng.nextFloat() < prob; };
    // Re-tira un CHOICE excluyendo el valor actual → siempre CAMBIA de verdad (nunca cae en lo mismo).
    auto reroll = [this, &rng] (const char* id, int n)
    {
        auto* p = apvts.getParameter (id);
        const int cur = (p != nullptr) ? (int) std::round (p->convertFrom0to1 (p->getValue())) : 0;
        int v = rng.nextInt (n);
        if (n > 1 && v == cur) v = (v + 1 + rng.nextInt (n - 1)) % n;
        canvas::setParamAsGesture (apvts, id, (float) v);
    };

    // MOVEMENT — motor y física con MÁS rango (que se note): motion siempre cambia
    reroll (pid::MOTION, 5);
    set (pid::INTENSITY,    uni (30.0f, 85.0f));
    set (pid::CHAOS,        uni (8.0f, 85.0f));
    set (pid::SPEED,        uni (32.0f, 68.0f));                          // cerca de ×1: musical
    set (pid::PUMP,         uni (15.0f, 85.0f));
    set (pid::RADIAL_GAIN,  uni (30.0f, 90.0f));
    set (pid::GRAVITY,      maybe (0.55f) ? uni (-12.0f, 12.0f) : uni (-35.0f, 35.0f));
    set (pid::BREATHE_GAIN, uni (20.0f, 80.0f));
    // la física sin knob también juega (curl/home/momentum/shimmer): el mundo cambia de verdad
    set (pid::CURL_SCALE,    uni (20.0f, 85.0f));
    set (pid::HOME_STRENGTH, uni (25.0f, 90.0f));
    set (pid::MOMENTUM,      uni (25.0f, 90.0f));
    set (pid::JITTER_GAIN,   uni (5.0f, 70.0f));

    // MATTER — glifo siempre cambia; efectos con MÁS chance de encender
    reroll (pid::SHAPE, 7);
    set (pid::PARTICLE_SIZE, uni (22.0f, 78.0f));
    set (pid::DENSITY, uni (40.0f, 100.0f));
    set (pid::SCATTER, maybe (0.45f) ? 0.0f : uni (8.0f, 55.0f));
    set (pid::TRAILS,  maybe (0.25f) ? 0.0f : uni (15.0f, 75.0f));         // on 75%
    set (pid::LINKS,   maybe (0.30f) ? 0.0f : uni (15.0f, 70.0f));         // on 70%

    // CAMERA — el 3D entra más seguido como acento (pero acotado, sin mareo)
    set (pid::FIGURE,  maybe (0.40f) ? 0.0f : (float) (1 + rng.nextInt (5)));   // on 60%
    set (pid::FORM,    uni (55.0f, 100.0f));
    set (pid::DEPTH,   maybe (0.30f) ? 0.0f : uni (25.0f, 85.0f));         // on 70%
    set (pid::ROT_X,   uni (-30.0f, 30.0f));
    set (pid::ROT_Y,   uni (-30.0f, 30.0f));
    set (pid::ORBIT,   maybe (0.45f) ? 0.0f : uni (-22.0f, 22.0f));        // on 55%
    set (pid::ROTATE,  maybe (0.55f) ? 0.0f : uni (-15.0f, 15.0f));        // on 45%
    set (pid::KALEIDO, maybe (0.50f) ? 0.0f : (float) (1 + rng.nextInt (4)));   // on 50%

    // COLOR — paleta siempre cambia (a un look distinto)
    reroll (pid::PALETTE, color::kNumLooks);
    set (pid::COLOR_AMOUNT, uni (55.0f, 100.0f));
    set (pid::SAT,          uni (25.0f, 85.0f));
    set (pid::HUE,          uni (-180.0f, 180.0f));
    set (pid::HUE_CYCLE,    maybe (0.50f) ? 0.0f : uni (-30.0f, 30.0f));   // on 50%
    set (pid::GLOW,         uni (35.0f, 85.0f));
    set (pid::BG,           maybe (0.60f) ? 0.0f : uni (15.0f, 65.0f));    // on 40%

    // Los dados de variación (master + hotknobs) → 0: ESTE dado ES la variación total.
    set (pid::VARIATION,    0.0f);
    set (pid::VAR_MOVEMENT, 0.0f);
    set (pid::VAR_MATTER,   0.0f);
    set (pid::VAR_CAMERA,   0.0f);
    set (pid::VAR_COLOR,    0.0f);
}

// MIDI-LEARN por click-derecho (Phase B UI): busca el knob bajo el click en el mapa slider→paramID y ofrece
// "MIDI Learn" (arma el próximo CC entrante) o "Forget CC n" (si ya está mapeado). El editor cablea las
// callbacks al MidiCcMap del processor. Un menú vacío si el click no fue sobre un knob conocido.
void ControlStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    auto it = knobParams.find (e.eventComponent);
    if (it == knobParams.end()) return;
    const juce::String id = it->second;
    const int cc = midiCcForParam ? midiCcForParam (id) : -1;

    juce::PopupMenu m;
    m.addSectionHeader (id);
    m.addItem (1, cc < 0 ? "MIDI Learn (move a CC next)" : "Re-learn MIDI (move a CC next)");
    if (cc >= 0) m.addItem (2, "Forget MIDI (CC " + juce::String (cc) + ")");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (e.eventComponent),
                     [this, id] (int r)
    {
        if (r == 1 && onMidiLearn)  onMidiLearn (id);
        if (r == 2 && onMidiForget) onMidiForget (id);
    });
}

void ControlStrip::mouseMove (const juce::MouseEvent& e) { updateHint (e.eventComponent); }
void ControlStrip::mouseExit (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
    // Solo limpiar cuando el mouse sale de la franja entera (los exits de hijos llegan todos por el listener).
    if (! getLocalBounds().contains (getMouseXYRelative()))
        updateHint (nullptr);
}

void ControlStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 4);
    if (r.getWidth() > kMaxContentW)                 // full-bleed en la app → cluster capeado + centrado
        r = r.withSizeKeepingCentre (kMaxContentW, r.getHeight());
    hintBar.setBounds (r.removeFromBottom (kHintH).withTrimmedLeft (kRailW - 8));
    const int rowH = r.getHeight() / 4;

    // LA CUADRÍCULA: riel + 8 columnas IDÉNTICAS en las 4 filas — cada control vive en su celda y las
    // columnas quedan alineadas verticalmente (los 4 steppers comparten la columna 0).
    auto cellOf = [] (juce::Rectangle<int> row, int col, int span = 1)
    {
        const int colW = row.getWidth() / kCols;
        return juce::Rectangle<int> (row.getX() + col * colW, row.getY(), colW * span, row.getHeight());
    };
    auto placeKnob = [] (KnobCell& cell, juce::Rectangle<int> col)
    {
        cell.label.setBounds (col.removeFromTop (kLabelH));
        cell.slider.setBounds (col.reduced (6, 0));
    };
    auto placeStepper = [] (ChoiceStepper& s, juce::Rectangle<int> col) { s.setBounds (col.reduced (2, 4)); };

    // MOVEMENT: stepper + 7 knobs (fila llena, orden por impacto; INTENSITY = fader de vida primero)
    {
        auto row = r.removeFromTop (rowH);
        rowRects[0] = row;
        placeKnob (hotMov, row.removeFromLeft (kRailW));   // hotknob = celda idéntica (mismo tamaño de knob)
        placeStepper (*motion, cellOf (row, 0));
        placeKnob (intensity, cellOf (row, 1));
        placeKnob (speed,     cellOf (row, 2));
        placeKnob (gravityK,  cellOf (row, 3));
        placeKnob (pump,      cellOf (row, 4));
        placeKnob (chaos,     cellOf (row, 5));
        placeKnob (breathe,   cellOf (row, 6));
        placeKnob (blast,     cellOf (row, 7));
    }

    // MATTER: stepper + 6 knobs
    {
        auto row = r.removeFromTop (rowH);
        rowRects[1] = row;
        placeKnob (hotMat, row.removeFromLeft (kRailW));
        placeStepper (*shape, cellOf (row, 0));
        placeKnob (density, cellOf (row, 1));
        placeKnob (scatter, cellOf (row, 2));
        placeKnob (size,    cellOf (row, 3));
        placeKnob (trails,  cellOf (row, 4));
        placeKnob (cutout,  cellOf (row, 5));
        placeKnob (links,   cellOf (row, 6));
    }

    // CAMERA: stepper + 6 knobs + stepper (fila llena)
    {
        auto row = r.removeFromTop (rowH);
        rowRects[2] = row;
        placeKnob (hotCam, row.removeFromLeft (kRailW));
        placeStepper (*figure, cellOf (row, 0));
        placeKnob (rotY,   cellOf (row, 1));
        placeKnob (rotX,   cellOf (row, 2));
        placeKnob (orbit,  cellOf (row, 3));
        placeKnob (rotate, cellOf (row, 4));
        placeKnob (form,   cellOf (row, 5));
        placeKnob (depth,  cellOf (row, 6));
        placeStepper (*kaleido, cellOf (row, 7));
    }

    // COLOR: stepper + 5 knobs ‖ DADO (VARIATION + MUTATE tras el divisor)
    {
        auto row = r;
        rowRects[3] = row;
        placeKnob (hotCol, row.removeFromLeft (kRailW));
        placeStepper (*palette, cellOf (row, 0));
        placeKnob (colorAmt, cellOf (row, 1));
        placeKnob (hueKnob,  cellOf (row, 2));
        placeKnob (sat,      cellOf (row, 3));
        placeKnob (hueCycle, cellOf (row, 4));
        placeKnob (glow,     cellOf (row, 5));
        dadoX = cellOf (row, 6).getX() + 2;              // divisor: acá empieza el DADO
        placeKnob (variation, cellOf (row, 6));          // VARIATION = master (todas las filas)
        // RANDOM (el dado TOTAL) centrado en la última celda.
        {
            auto zone = cellOf (row, 7).withTrimmedTop (kLabelH).withTrimmedBottom (kValueH);
            randomBtn->setBounds (zone.withSizeKeepingCentre (juce::jmin (zone.getWidth() - 12, 100), 32));
        }
    }
}

void ControlStrip::paint (juce::Graphics& g)
{
    // Fondo de la franja + hairline superior de familia (separa la franja del pozo de partículas).
    g.setColour (th::bg0);
    g.fillRect (getLocalBounds());
    g.setColour (hue.withAlpha (0.18f));
    g.fillRect (getLocalBounds().removeFromTop (1));

    // Señalética de DOMINIOS: riel izquierdo con el nombre + tinte, banda sutil por fila y hairline entre
    // filas. El tinte es identidad de grupo (el mismo color que llevan sus knobs), no decoración.
    for (int i = 0; i < 4; ++i)
    {
        const auto row = rowRects[(size_t) i];
        if (row.isEmpty()) continue;
        const auto tint = groupTint (i);

        if (i > 0)   // hairline separadora entre dominios
        {
            g.setColour (juce::Colour (0xff171a22));
            g.fillRect (row.getX(), row.getY(), row.getWidth(), 1);
        }

        // Tarjeta del riel: cubre la CELDA ENTERA del hotknob (label del grupo + knob + valor adentro,
        // nada pisado) — el HOTKNOB del dominio vive acá como una celda más, del MISMO tamaño que el resto.
        auto rail = row.withWidth (kRailW).reduced (3, 2);
        g.setColour (tint.withAlpha (0.10f));
        g.fillRoundedRectangle (rail.toFloat(), 6.0f);
    }

    // Divisor del DADO (VARIATION/RANDOM no son color: son el multiplicador de mundos, en violeta OVNI).
    if (dadoX > 0 && ! rowRects[3].isEmpty())
    {
        g.setColour (dadoTint().withAlpha (0.25f));
        g.fillRect (dadoX, rowRects[3].getY() + 10, 1, rowRects[3].getHeight() - 20);
    }
}
}
