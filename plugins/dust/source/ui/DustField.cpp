#include "DustField.h"
#include "DustFieldStatic.h"        // capa baked (pozo profundo + polvo + estructura + overlays)
#include "params/ParameterIDs.h"   // rango log del RATE (params::kRateMinMs/MaxMs — fuente única)
#include "ui-kit/Fonts.h"
#include <cmath>

namespace dust::ui
{

namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// ---- constantes del dibujo (anti-magic-number) --------------------------------------------------
static constexpr float kFps           = 30.0f;   // fps de la base (ctor)
static constexpr float kFieldInsetPx  = detail::kFieldInsetPx;   // aire campo↔borde (espejo del baked)
static constexpr float kOriginHitPx   = 16.0f;   // radio de hover del marcador ORIGIN
static constexpr int   kIdleFrames    = 24;      // ~0.8 s sin eventos reales → el spawner idle toma la posta
static constexpr int   kTeleEvery     = 6;       // refresco de telemetría: cada 6 frames (~0.2 s a 30 fps)

DustField::DustField (std::atomic<float>& mix, std::atomic<float>& rateNorm,
                      std::atomic<float>& density, std::atomic<float>& spread,
                      std::atomic<float>& vida, std::atomic<float>& duckGr,
                      std::atomic<float>& originX, std::atomic<float>& originY,
                      std::atomic<float>& wetRms, BubbleEventFifo& events,
                      juce::RangedAudioParameter* originXParam,
                      juce::RangedAudioParameter* originYParam)
    : ovni::ui::VisualizerBase ((int) kFps),
      mixSrc (mix), rateSrc (rateNorm), densitySrc (density), spreadSrc (spread),
      vidaSrc (vida), duckGrSrc (duckGr), originXSrc (originX), originYSrc (originY),
      wetSrc (wetRms), eventsSrc (events), oxP (originXParam), oyP (originYParam)
{
    setSettleHold (60);   // las burbujas en vuelo terminan su vida antes de pausar el repaint
    setInterceptsMouseClicks (true, false);            // el campo ES superficie de control (drag del ORIGIN)
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus (true);                      // interaction-grammar: la superficie estrella
                                                       // se opera también con foco + flechas (a11y)

    // Estado inicial coherente desde los atomics (primer frame / snapshot sin "arranque en cero").
    mixSm     = mixSrc.load     (std::memory_order_relaxed);
    rateSm    = rateSrc.load    (std::memory_order_relaxed);
    densitySm = densitySrc.load (std::memory_order_relaxed);
    spreadSm  = spreadSrc.load  (std::memory_order_relaxed);
    vidaSm    = vidaSrc.load    (std::memory_order_relaxed);
    originFx  = oxP ? oxP->convertFrom0to1 (oxP->getValue()) : originXSrc.load (std::memory_order_relaxed);
    originFy  = oyP ? oyP->convertFrom0to1 (oyP->getValue()) : originYSrc.load (std::memory_order_relaxed);

    // Sembrar burbujas YA en vuelo (vidas repartidas) → el primer frame y el snapshot muestran la
    // nube viva, no un campo vacío. Semilla fija (rng del miembro) → reproducible.
    const float az0 = std::atan2 (-originFx, originFy);   // misma convención que el motor (§4)
    for (int i = 0; i < 12; ++i)
    {
        spawnBubble (az0 + (rng.nextFloat() * 2.0f - 1.0f) * spreadSm * juce::MathConstants<float>::pi,
                     0.35f + 0.55f * rng.nextFloat(), vidaSm);
        bubbles[(size_t) ((nextBubble - 1 + kMaxBubbles) % kMaxBubbles)].life = rng.nextFloat() * 0.85f;
    }
    dbgSpawns = 0;   // la siembra del ctor no cuenta como nacimientos del test

    refreshTelemetry();   // strings coherentes desde el primer frame / snapshot (no "000")
}

// ── mapeo campo ⇄ pantalla — LA fuente de verdad (dibujo Y drag la comparten) ─────────────────────
// Elíptico (Rx ≠ Ry): el campo llena el panel ancho (menos área muerta, legible a 320 px). El drag
// invierte EXACTAMENTE esto → el marcador queda 1:1 bajo el cursor (lección del bug de PULSAR).
float DustField::fieldRadiusX() const noexcept { return juce::jmax (1.0f, (float) getWidth()  * 0.5f - kFieldInsetPx); }
float DustField::fieldRadiusY() const noexcept { return juce::jmax (1.0f, (float) getHeight() * 0.5f - kFieldInsetPx); }

juce::Point<float> DustField::fieldToScreen (juce::Point<float> f) const noexcept
{
    const float cx = (float) getWidth() * 0.5f, cy = (float) getHeight() * 0.5f;
    return { cx + juce::jlimit (-1.0f, 1.0f, f.x) * fieldRadiusX(),
             cy - juce::jlimit (-1.0f, 1.0f, f.y) * fieldRadiusY() };   // y+ = frente = arriba
}

juce::Point<float> DustField::screenToField (juce::Point<float> s) const noexcept
{
    const float cx = (float) getWidth() * 0.5f, cy = (float) getHeight() * 0.5f;
    return { juce::jlimit (-1.0f, 1.0f,  (s.x - cx) / fieldRadiusX()),
             juce::jlimit (-1.0f, 1.0f, -(s.y - cy) / fieldRadiusY()) };
}

// ── interacción: drag del ORIGIN por GESTO de parámetro (automatización/undo del host) ────────────
void DustField::applyDrag (const juce::MouseEvent& e)
{
    const auto f = screenToField (e.position);
    if (oxP) oxP->setValueNotifyingHost (oxP->convertTo0to1 (f.x));
    if (oyP) oyP->setValueNotifyingHost (oyP->convertTo0to1 (f.y));
}

bool DustField::isNearOrigin (juce::Point<float> screenPos) const noexcept
{
    return fieldToScreen ({ originFx, originFy }).getDistanceFrom (screenPos) <= kOriginHitPx;
}

void DustField::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    if (oxP) oxP->beginChangeGesture();
    if (oyP) oyP->beginChangeGesture();
    applyDrag (e);
    repaint();
}

void DustField::mouseDrag (const juce::MouseEvent& e) { if (dragging) applyDrag (e); }

void DustField::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if (oxP) oxP->endChangeGesture();
    if (oyP) oyP->endChangeGesture();
    repaint();
}

void DustField::mouseMove (const juce::MouseEvent& e)
{
    const bool near = isNearOrigin (e.position);
    if (near != hoverOrigin)
    {
        hoverOrigin = near;
        setMouseCursor (near ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();   // el anillo de hover entra/sale aunque la animación esté pausada
    }
}

void DustField::mouseExit (const juce::MouseEvent&)
{
    if (hoverOrigin) { hoverOrigin = false; repaint(); }
}

// ── teclado: flechas nudgean el ORIGIN por GESTO de parámetro (automatización/undo, igual que el
// drag). Shift = fino (÷5, mismo divisor que el OvniKnob). Hallazgo de review: la superficie
// estrella no era operable sin mouse — foco + flechas es la doctrina del sello (interaction-grammar).
bool DustField::keyPressed (const juce::KeyPress& key)
{
    float dx = 0.0f, dy = 0.0f;
    if      (key.getKeyCode() == juce::KeyPress::leftKey)  dx = -1.0f;
    else if (key.getKeyCode() == juce::KeyPress::rightKey) dx =  1.0f;
    else if (key.getKeyCode() == juce::KeyPress::upKey)    dy =  1.0f;   // y+ = frente = arriba
    else if (key.getKeyCode() == juce::KeyPress::downKey)  dy = -1.0f;
    else return false;   // lo que no es flecha sigue su curso (atajos del host)

    constexpr float kKeyNudge = 0.05f;   // paso por pulsación en coords de campo [-1,1]
    const float step = key.getModifiers().isShiftDown()
                     ? kKeyNudge / (float) ovni::ui::theme::state::fineDiv
                     : kKeyNudge;

    auto nudge = [step] (juce::RangedAudioParameter* p, float delta)
    {
        if (p == nullptr || delta == 0.0f) return;
        const float current = p->convertFrom0to1 (p->getValue());   // fuente de verdad: el parámetro
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (
            juce::jlimit (-1.0f, 1.0f, current + delta * step)));
        p->endChangeGesture();
    };
    nudge (oxP, dx);
    nudge (oyP, dy);
    repaint();
    return true;
}

// ── burbujas ──────────────────────────────────────────────────────────────────────────────────────
void DustField::spawnBubble (float azimuthRad, float energy01, float vida01) noexcept
{
    Bubble& b = bubbles[(size_t) nextBubble];
    nextBubble = (nextBubble + 1) % kMaxBubbles;   // voice-stealing de la más vieja (slot circular)

    b.ox = originFx;
    b.oy = originFy;

    // Destino = dirección del azimut REAL (donde suena el eco): pantalla-derecha = canal derecho
    // (az = atan2(-x, y) en el motor → acá la inversa: dir = (-sin az, cos az)).
    const float rad = 0.45f + 0.45f * rng.nextFloat();
    b.tx = juce::jlimit (-1.0f, 1.0f, -std::sin (azimuthRad) * rad);
    b.ty = juce::jlimit (-1.0f, 1.0f,  std::cos (azimuthRad) * rad);

    b.energy   = juce::jlimit (0.0f, 1.0f, energy01);
    b.vida     = juce::jlimit (0.0f, 1.0f, vida01);
    b.life     = 0.0f;
    // Vida visual ∝ RATE (ecos lentos = burbujas que duran; rápidos = chisporroteo) + energía.
    const float lifeSecs = 1.0f + 1.8f * rateSm + 0.5f * b.energy;
    b.lifeStep = 1.0f / (kFps * lifeSecs);
    b.seed     = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    b.active   = true;
    ++dbgSpawns;
}

juce::Point<float> DustField::bubblePos (const Bubble& b) const noexcept
{
    // Trayectoria: nace en el ORIGIN y se abre hacia donde suena (ease-out) + deriva de VIDA
    // (flotación senoidal que crece con la vida transcurrida — a VIDA 0 va recta y se queda).
    const float t  = 1.0f - (1.0f - b.life) * (1.0f - b.life);
    const float wb = b.vida * 0.12f * b.life;
    return { b.ox + (b.tx - b.ox) * t + wb * std::sin (pulsePhase * 1.3f + b.seed * 3.1f),
             b.oy + (b.ty - b.oy) * t + wb * std::sin (pulsePhase * 1.7f + b.seed * 5.3f) };
}

int DustField::dbgSpawnsOverFrames (int frames) noexcept
{
    dbgSpawns = 0;
    for (int i = 0; i < frames; ++i) advanceFrame();
    return (int) dbgSpawns;
}

// ── telemetría honesta: derivada de los atomics suavizados (espejo del DSP) + estado real del campo ─
void DustField::refreshTelemetry()
{
    // signo consciente del redondeo a 2 decimales: lo que se muestra como 0.00 lleva "+" (nunca "−0.00").
    auto sgn = [] (float v) { return juce::String::fromUTF8 (v > -0.005f ? "+" : "\xe2\x88\x92"); };

    // Burbujas vivas / capacidad (mockup: "00 / 48").
    int alive = 0;
    for (const auto& b : bubbles) if (b.active) ++alive;
    teleCount = juce::String (alive).paddedLeft ('0', 2) + " / " + juce::String (kMaxBubbles);

    // APERTURA: SPREAD → grados (mockup: lerp 6..170°).
    teleSpread = juce::String (juce::roundToInt (juce::jmap (spreadSm, 6.0f, 170.0f)))
                     .paddedLeft ('0', 3) + juce::String::fromUTF8 ("\xc2\xb0");

    // WET: energía real del wet (uiWetRms suavizado), 0.00..~1 (el color lo pone paintTelemetry).
    teleWetVal = juce::jlimit (0.0f, 1.0f, wetSm);
    teleWet    = juce::String (teleWetVal, 2);

    // NACE: espaciado efectivo entre ecos (misma fórmula log 20..2000 ms del processor → honesto).
    const float rateMs = params::kRateMinMs
                       * std::pow (params::kRateMaxMs / params::kRateMinMs, juce::jlimit (0.0f, 1.0f, rateSm));
    teleRate = rateMs < 1000.0f ? juce::String (juce::roundToInt (rateMs)) + " ms"
                                : juce::String (rateMs / 1000.0f, 2) + " s";

    // VIDA: cuánto vive un eco en segundos (misma fórmula que spawnBubble: 1 + 1.8·rate s).
    teleLife = juce::String (1.0f + 1.8f * rateSm, 1) + " s";

    // ORIGIN: posición vigente del marcador (de los params; lo que el usuario arrastra).
    teleOrigin = sgn (originFx) + juce::String (std::abs (originFx), 2) + " / "
               + sgn (originFy) + juce::String (std::abs (originFy), 2);
}

// ── capa estática: POZO PROFUNDO ABIERTO (delegado al baked dust::ui::detail, mockup §buildBg) ─────
// Volumen vertical de alto contraste + ambiente cian + polvo determinístico + planos de profundidad
// + eje L/R + scanlines/vignette/rim que hunden el campo en el chasis. Se hornea UNA vez (px-físico).
void DustField::renderStatic (juce::Graphics& g, int w, int h)
{
    detail::paintFieldStatic (g, w, h);
}

// ── capa viva: burbujas (bloom bucketeado + estallido) + ORIGIN + telemetría en 4 esquinas ─────────
void DustField::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;

    // Presencia (MIX, con piso de luminosidad) · flare (energía real del wet) · DUCK real atenúa.
    const float wetPresence = juce::jmap (mixSm, 0.0f, 1.0f, 0.45f, 1.0f);
    const float flare       = juce::jlimit (0.0f, 1.0f, 0.55f + 2.2f * wetSm);
    const float duckDim     = 1.0f - 0.65f * duckGrSm;   // las burbujas se apartan cuando pega el dry

    const float pxScale = juce::jlimit (0.6f, 1.4f, juce::jmin (fieldRadiusX(), fieldRadiusY()) / 150.0f);

    // Toda la luz de las burbujas + el ORIGIN se ACUMULA en la capa additiva (mockup 'lighter') → cada
    // burbuja florece y los solapamientos revientan hacia el blanco; se vuelca de una sola vez al final.
    bloom.begin (w, h, currentScale());

    for (const auto& b : bubbles)
    {
        if (! b.active) continue;
        const float life = juce::jlimit (0.0f, 1.0f, b.life);
        const auto  p    = fieldToScreen (bubblePos (b));

        // Profundidad: cerca del frente (arriba) = piso de luminosidad alto; al fondo se atenúa hacia kLumFloor.
        const float depth = juce::jlimit (0.0f, 1.0f, 1.0f - p.y / (float) h);
        const float lum   = juce::jmap (depth, kLumFloor, 1.0f);

        // Envolvente: nace → brilla → muere (campana) con titileo propio; tamaño crece al abrirse.
        const float bell    = std::sin (life * juce::MathConstants<float>::pi);
        const float twinkle = 0.85f + 0.15f * std::sin (pulsePhase * 1.9f + b.seed);
        const float a       = juce::jlimit (0.0f, 1.0f,
                              wetPresence * (0.46f + 0.62f * b.energy) * bell * twinkle * flare * duckDim * lum);
        if (a < 0.004f) continue;

        const float t   = 1.0f - (1.0f - life) * (1.0f - life);
        const float rpx = (5.5f + 12.0f * b.energy) * (0.7f + 0.6f * t) * pxScale;

        // Bloom additivo: halo amplio + velo medio + núcleo cian denso + chispa blanca (cada burbuja
        // FLORECE con glow visible y los solapamientos revientan — mockup dust-a: campo poblado y brillante).
        bloom.addSprite (p.x, p.y, rpx * 3.0f,  th::cyan.withAlpha (a * 0.50f));
        bloom.addSprite (p.x, p.y, rpx * 1.7f,  th::cyan.withAlpha (a * 0.95f));
        bloom.addSprite (p.x, p.y, rpx,         th::cyan.withAlpha (a * 1.55f));
        bloom.addSprite (p.x, p.y, rpx * 0.42f, juce::Colours::white.withAlpha (a * 1.0f));

        // ESTALLIDO al morir: anillo que se expande y se desvanece en el último tramo de vida (additivo).
        if (life > 0.80f)
        {
            const float bt = (life - 0.80f) / 0.20f;
            const float rr = rpx * (1.0f + bt * 2.4f);
            bloom.gfx().setColour (th::cyan.withAlpha ((1.0f - bt) * a * 0.9f));
            bloom.gfx().drawEllipse (p.x - rr, p.y - rr, rr * 2.0f, rr * 2.0f, 1.2f);
        }
    }

    // Marcador del ORIGIN (la superficie de control): orbe additivo + cruz; hover/drag = anillo de agarre.
    {
        const auto oc = fieldToScreen ({ originFx, originFy });
        const bool lit = hoverOrigin || dragging;
        bloom.addSprite (oc.x, oc.y, lit ? 16.0f : 11.0f, th::cyan.withAlpha (lit ? 0.55f : 0.34f));
        auto& bg = bloom.gfx();
        bg.setColour (th::cyan.withAlpha (lit ? 0.95f : 0.65f));
        bg.drawLine (oc.x - 6.0f, oc.y, oc.x + 6.0f, oc.y, 1.2f);
        bg.drawLine (oc.x, oc.y - 6.0f, oc.x, oc.y + 6.0f, 1.2f);
        if (lit)   // feedback de drag: anillo de agarre (el campo te dice "esto se arrastra")
        {
            bg.setColour (th::cyan.withAlpha (dragging ? 0.85f : 0.55f));
            bg.drawEllipse (oc.x - 13.0f, oc.y - 13.0f, 26.0f, 26.0f, 1.4f);
        }
    }

    bloom.compositeOnto (g);   // vuelca la capa de luz acumulada sobre el campo

    // Etiqueta ORIGIN bajo el marcador (texto plano sobre el campo, mockup §drawOrigin).
    {
        const auto oc = fieldToScreen ({ originFx, originFy });
        g.setColour (th::cyan.withAlpha (0.55f));
        g.setFont (fonts::mono (8.0f));
        g.drawText ("ORIGIN", juce::Rectangle<float> (oc.x - 30.0f, oc.y + 16.0f, 60.0f, 11.0f),
                    juce::Justification::centred);
    }

    // Telemetría honesta en las 4 esquinas (valores REALES de los atomics; strings cacheadas).
    paintTelemetry (g, w, h);

    // Anillo de foco de teclado (a11y, token del sello): el campo enfocado se ve enfocado —
    // las flechas mueven el ORIGIN (ver keyPressed).
    if (hasKeyboardFocus (false))
    {
        g.setColour (th::cyan.withAlpha (th::state::focusRing * 0.5f));
        g.drawRect (getLocalBounds(), 1);
    }
}

// ── telemetría: 4 esquinas (mockup §tele). Las strings se refrescan en advanceFrame (~0.2 s). ──────
void DustField::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = fonts::mono (8.0f).withExtraKerningFactor (0.12f);
    const int lh = 13, m = 13, colW = 150;
    auto line = [&g, &lab] (const juce::String& s, int x, int y, int wd, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, wd, 11, right ? juce::Justification::centredRight
                                           : juce::Justification::centredLeft);
    };

    // TL: campo / cuenta de burbujas vivas.
    line (juce::String::fromUTF8 ("FIELD // BUBBLES"), m, 10,      colW, false, th::fnt);
    line (teleCount,                                   m, 10 + lh, colW, false, th::mut);
    // TR: apertura (SPREAD) + WET (coloreado por energía).
    line ("APERTURE " + teleSpread, w - colW - m, 10,      colW, true, th::fnt);
    g.setColour (th::cyan.interpolatedWith (th::cyanD, 1.0f - juce::jlimit (0.0f, 1.0f, teleWetVal)));
    g.setFont (lab);
    g.drawText ("WET " + teleWet, w - colW - m, 10 + lh, colW, 11, juce::Justification::centredRight);
    // BL: cadencia de nacimientos (RATE) + VIDA.
    line ("BIRTH " + teleRate, m, h - 8 - 2 * lh, colW, false, th::mut);
    line ("LIFE " + teleLife,  m, h - 8 - lh,     colW, false, th::mut);
    // BR: ORIGIN.
    line ("ORIGIN " + teleOrigin, w - colW - m, h - 8 - lh, colW, true, th::mut);
}

// ── animación: drenar eventos reales, espaciador idle al RATE, avanzar burbujas ───────────────────
bool DustField::advanceFrame()
{
    // Telemetría + de-zipper visual (one-pole POST-lectura).
    const float k = 0.18f;
    mixSm     += (mixSrc.load     (std::memory_order_relaxed) - mixSm)     * k;
    rateSm    += (rateSrc.load    (std::memory_order_relaxed) - rateSm)    * k;
    densitySm += (densitySrc.load (std::memory_order_relaxed) - densitySm) * k;
    spreadSm  += (spreadSrc.load  (std::memory_order_relaxed) - spreadSm)  * k;
    vidaSm    += (vidaSrc.load    (std::memory_order_relaxed) - vidaSm)    * k;
    duckGrSm  += (duckGrSrc.load  (std::memory_order_relaxed) - duckGrSm)  * 0.30f;   // el duck respira rápido
    wetSm     += (wetSrc.load     (std::memory_order_relaxed) - wetSm)     * 0.25f;

    // ORIGIN desde los PARAMS (sigue al drag/automatización aun sin audio corriendo; fallback atomics).
    const float ox = oxP ? oxP->convertFrom0to1 (oxP->getValue()) : originXSrc.load (std::memory_order_relaxed);
    const float oy = oyP ? oyP->convertFrom0to1 (oyP->getValue()) : originYSrc.load (std::memory_order_relaxed);
    const bool originMoved = std::abs (ox - originFx) > 1.0e-4f || std::abs (oy - originFy) > 1.0e-4f;
    originFx = ox; originFy = oy;

    pulsePhase += 0.05f;
    if (pulsePhase > 1.0e6f) pulsePhase = std::fmod (pulsePhase, juce::MathConstants<float>::twoPi);

    // 1) Eventos REALES del motor (FIFO lock-free): la burbuja nace EXACTAMENTE donde suena.
    BubbleEvent ev[32];
    const int nEv = eventsSrc.pop (ev, 32);
    if (nEv > 0)
    {
        framesSinceEvent = 0;
        for (int i = 0; i < nEv; ++i)
            spawnBubble (ev[i].azimuthRad, juce::jlimit (0.0f, 1.0f, ev[i].energy * 1.6f), ev[i].vida);
    }
    else if (++framesSinceEvent > kIdleFrames)
    {
        // 2) Espaciador IDLE (sin audio): el campo igual late al RATE — misma fórmula log del RATE
        //    efectivo (20..2000 ms, espejo exacto de uiRateNorm del processor) — y DENSIDAD suma
        //    burbujas por pulso (la regeneración se VE).
        const float rateMs = params::kRateMinMs
                           * std::pow (params::kRateMaxMs / params::kRateMinMs,
                                       juce::jlimit (0.0f, 1.0f, rateSm));
        spawnAccum = juce::jmin (3.0f, spawnAccum + (1000.0f / rateMs) / kFps);
        while (spawnAccum >= 1.0f)
        {
            spawnAccum -= 1.0f;
            const int perPulse = 1 + (int) (densitySm * 2.5f);   // DENSIDAD: 1..3 burbujas por pulso
            const float az0 = std::atan2 (-originFx, originFy);   // centro = ORIGIN (convención del motor)
            for (int i = 0; i < perPulse; ++i)
                spawnBubble (az0 + (rng.nextFloat() * 2.0f - 1.0f) * spreadSm * juce::MathConstants<float>::pi,
                             0.35f + 0.45f * rng.nextFloat(), vidaSm);
        }
    }

    // 3) Avanzar burbujas en vuelo.
    bool anyActive = false;
    for (auto& b : bubbles)
    {
        if (! b.active) continue;
        b.life += b.lifeStep;
        if (b.life >= 1.0f) b.active = false;
        else                anyActive = true;
    }

    // 4) Telemetría honesta: refresca las strings cada ~0.2 s (mockup §tele), no por frame.
    if (--teleCountdown <= 0)
    {
        teleCountdown = kTeleEvery;
        refreshTelemetry();
    }

    return anyActive || originMoved || dragging || nEv > 0 || duckGrSm > 0.01f || wetSm > 0.01f;
}

} // namespace dust::ui
