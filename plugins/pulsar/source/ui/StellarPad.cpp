#include "StellarPad.h"
#include "StellarPadStatic.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace pulsar::ui
{

namespace th = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// Paleta del trail (familia Movimiento = cian; calienta a magenta/ámbar con el Doppler).
static const juce::Colour kCold = th::cyan;
static const juce::Colour kMid  = th::magenta;
static const juce::Colour kHot  = th::amber;

// La cruz del CAMPO vive a media escala del pozo (mockup: crossX = fx * 0.5 en unidades de R).
static constexpr float kFieldScale = 0.5f;

StellarPad::StellarPad (std::atomic<float>& azimuth, std::atomic<float>& depth,
                        std::atomic<float>& heat, std::atomic<float>& distance,
                        std::atomic<float>& motion, std::atomic<float>& smear,
                        std::atomic<float>& shape,
                        juce::RangedAudioParameter* fieldX, juce::RangedAudioParameter* fieldY)
    : ovni::ui::VisualizerBase (30),
      azSrc (azimuth), depthSrc (depth), heatSrc (heat), distSrc (distance),
      motionSrc (motion), smearSrc (smear), shapeSrc (shape),
      fxP (fieldX), fyP (fieldY)
{
    setSettleHold (30);
    setInterceptsMouseClicks (true, false);          // el pad ES el control: captura el drag
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    if (fxP) fieldXNow = fxP->convertFrom0to1 (fxP->getValue());
    if (fyP) fieldYNow = fyP->convertFrom0to1 (fyP->getValue());
    refreshTelemetry();
}

// Drag = reposicionar el CENTRO del atractor (apuntar). Invierte EXACTO el mapeo con el que se
// dibuja la cruz (kFieldScale × R) → la cruz queda bajo el cursor (drag 1:1, natural).
void StellarPad::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }
void StellarPad::mouseDrag (const juce::MouseEvent& e)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float rr = juce::jmax (1.0f, detail::padRadius (w, h) * kFieldScale);
    const float nx = juce::jlimit (-1.0f, 1.0f, (e.position.x - cx) / rr);
    const float ny = juce::jlimit (-1.0f, 1.0f, -(e.position.y - cy) / rr);
    if (fxP) fxP->setValueNotifyingHost (fxP->convertTo0to1 (nx));
    if (fyP) fyP->setValueNotifyingHost (fyP->convertTo0to1 (ny));
}

juce::Point<float> StellarPad::cvToPad (float x, float y, float dist01, int w, int h) const noexcept
{
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float maxR = detail::padRadius (w, h);
    // La distancia escala el radio: cerca (0.15 m) ≈ 30% (pegado al oyente) · lejos (15 m) = al borde.
    const float ds = 0.30f + 0.70f * juce::jlimit (0.0f, 1.0f, dist01);
    return { cx + juce::jlimit (-1.0f, 1.0f, x) * maxR * ds,
             cy - juce::jlimit (-1.0f, 1.0f, y) * maxR * ds };
}

juce::Colour StellarPad::heatColour (float heat, float alpha) const noexcept
{
    // Ramp warm-biased (gamma<1): la transición cian→magenta→ámbar se ALCANZA antes → el gradiente
    // del cometa se LEE fuerte (mockup pulsar-a), sin falsear el heat (sigue siendo monótona del dato real).
    const float t = std::pow (juce::jlimit (0.0f, 1.0f, heat), 0.62f);
    juce::Colour c = (t < 0.5f) ? kCold.interpolatedWith (kMid, t * 2.0f)
                                : kMid.interpolatedWith (kHot, (t - 0.5f) * 2.0f);
    return c.withAlpha (alpha);
}

void StellarPad::renderStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    detail::paintWellStatic (g, w, h);
    detail::paintWellOverlays (g, w, h);
}

void StellarPad::paintFieldCross (juce::Graphics& g, int w, int h) const
{
    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float R  = detail::padRadius (w, h);
    const float fx = cx + juce::jlimit (-1.0f, 1.0f, fieldXNow) * R * kFieldScale;
    const float fy = cy - juce::jlimit (-1.0f, 1.0f, fieldYNow) * R * kFieldScale;

    // cruz con gap (mockup): 4 trazos cortos alrededor del centro
    g.setColour (th::cyan.withAlpha (0.5f));
    g.drawLine (fx - 9.0f, fy, fx - 3.0f, fy, 1.0f);
    g.drawLine (fx + 3.0f, fy, fx + 9.0f, fy, 1.0f);
    g.drawLine (fx, fy - 9.0f, fx, fy - 3.0f, 1.0f);
    g.drawLine (fx, fy + 3.0f, fx, fy + 9.0f, 1.0f);

    // orbe del campo: crece con MOTION (energía)
    const float orbR = 5.0f + juce::jlimit (0.0f, 1.0f, motionNow) * 22.0f;
    g.setColour (th::cyan.withAlpha (0.22f));
    g.drawEllipse (fx - orbR, fy - orbR, orbR * 2.0f, orbR * 2.0f, 1.0f);
}

// FÓSFORO (larga exposición) — el corazón del rediseño. Capa persistente `phosphor` que se desvanece de a
// poco cada frame (multiplicación de todo el buffer) + la traza nueva (alpha-over, color heat). Acumulada
// frame a frame, la forma del atractor se DIBUJA SOLA, como un osciloscopio. Corre en advanceFrame (NO en
// paint): sin mutación en paint. El buffer va a supersample fijo (kPhosScale), INDEPENDIENTE de la escala
// de paint → funciona también headless (el shot 4K pumpea frames antes de cualquier paint). Se compone
// aditivo sobre el pozo oscuro en paintLive (drawImageTransformed: alpha-over sobre casi-negro ⇒ lee glow).
void StellarPad::updatePhosphor (int w, int h)
{
    if (w <= 0 || h <= 0 || trailLen <= 0) return;
    const int pw = juce::jmax (1, juce::roundToInt ((float) w * kPhosScale));
    const int ph = juce::jmax (1, juce::roundToInt ((float) h * kPhosScale));
    if (phosphor.isNull() || phosphor.getWidth() != pw || phosphor.getHeight() != ph)
    {
        phosphor = juce::Image (juce::Image::ARGB, pw, ph, true);
        phosSkipSeg = true;                       // recién creado: no unir a una posición vieja
    }

    // 1) DESVANECER: multiplica todo el buffer por `keep` (premultiplicado → escalar los 4 canales preserva
    //    la premultiplicación). "Media" en régimen normal; fade fuerte durante el reset suave (rebuild).
    const float keepF = (rebuildFrames > 0) ? kKeepReset : kKeepNormal;
    if (rebuildFrames > 0) --rebuildFrames;
    {
        const juce::uint32 k = (juce::uint32) juce::roundToInt (keepF * 65536.0f);
        juce::Image::BitmapData bd (phosphor, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < bd.height; ++y)
        {
            auto* row = bd.getLinePointer (y);
            const int rowBytes = bd.width * bd.pixelStride;
            for (int b = 0; b < rowBytes; ++b)
                row[b] = (juce::uint8) (((juce::uint32) row[b] * k) >> 16);
        }
    }   // SOLTAR la BitmapData antes de crear el Graphics (no deben coexistir sobre el mismo Image: COW)

    // 2) TRAZA NUEVA: une la posición previa con la actual (color heat = velocidad/Doppler). En un salto
    //    (preset/jump) o recién creado, NO une: sólo siembra un punto en la posición nueva.
    {
        juce::Graphics gp (phosphor);
        gp.addTransform (juce::AffineTransform::scale (kPhosScale));    // dibujo en coords lógicas
        const auto& head = trail[trailHead];
        const juce::Point<float> curPt = cvToPad (head.x, head.y, head.dist, w, h);
        gp.setColour (heatColour (head.heat, 0.55f));
        if (phosSkipSeg || trailLen < 2)
        {
            gp.fillEllipse (curPt.x - 1.0f, curPt.y - 1.0f, 2.0f, 2.0f);
            phosSkipSeg = false;
        }
        else
        {
            const auto& prev = trail[(trailHead - 1 + kTrailMax) % kTrailMax];
            const juce::Point<float> prevPt = cvToPad (prev.x, prev.y, prev.dist, w, h);
            gp.drawLine ({ prevPt, curPt }, 1.7f);
        }
    }
}

// ESTELA RECIENTE NÍTIDA (alpha-over, sobre g DESPUÉS del bloom). Es el "ahora" del movimiento: un tramo
// CORTO (kCoreTrail) que se desvanece a 0 hacia atrás → un trazo crujiente sobre el fósforo de fondo. El
// fósforo aporta la FORMA acumulada (suave, larga); este pase aporta nitidez al gesto reciente. Filo blanco
// fino cerca de la cabeza para separar del fondo en cualquier zona (incluido el glow central).
void StellarPad::paintTrailCore (juce::Graphics& g, int w, int h)
{
    if (trailLen <= 1) return;
    const float smr = juce::jlimit (0.0f, 1.0f, smearNow);
    const int   n   = juce::jmin (trailLen, kCoreTrail);

    juce::Point<float> prev;
    bool havePrev = false;
    for (int i = n - 1; i >= 0; --i)
    {
        const int idx = (trailHead - i + kTrailMax) % kTrailMax;
        const auto& p = trail[idx];
        const float t = (float) (n - i) / (float) n;             // 0 = cola del tramo, 1 = cabeza
        const juce::Point<float> pt = cvToPad (p.x, p.y, p.dist, w, h);
        if (havePrev)
        {
            const float fade = std::pow (t, 1.15f);              // sólo el "ahora" nítido (la forma la da el fósforo)
            g.setColour (heatColour (p.heat, fade * juce::jmap (smr, 0.92f, 0.78f)));
            g.drawLine ({ prev, pt }, juce::jmap (t, 0.8f, 2.4f));
            if (t > 0.6f)
            {
                g.setColour (juce::Colours::white.withAlpha ((t - 0.6f) / 0.4f * 0.30f * fade));
                g.drawLine ({ prev, pt }, juce::jmap (t, 0.6f, 1.1f));
            }
        }
        prev = pt; havePrev = true;
    }
}

// HALO de la cabeza (capa ADDITIVA, mockup §fuente): bloom apilado de 3 capas (halo amplio + medio +
// glow denso) SUMADAS → la estrella revienta hacia el blanco. Es el glow atmosférico; el núcleo nítido
// va en paintSourceCore() sobre g (alpha-over) para que la cabeza lea sobre el centro brillante.
void StellarPad::paintSource (ovni::ui::Bloom& bl, int w, int h)
{
    if (trailLen <= 0) return;
    const auto& head = trail[trailHead];
    const juce::Point<float> hp = cvToPad (head.x, head.y, head.dist, w, h);

    // cerca = grande y brillante; piso de luminosidad 0.80 (NUNCA se apaga, mockup §fuente — que reviente)
    const float prox  = 1.0f - juce::jlimit (0.0f, 1.0f, head.dist);
    const float haloR = juce::jmap (prox, 20.0f, 40.0f);
    const float bri   = juce::jmap (prox, 0.80f, 1.0f);

    // Halo de color saturado (cian/magenta/ámbar según heat). Acotado → no se lava a un globo gris.
    bl.addSprite (hp.x, hp.y, haloR * 1.6f, heatColour (head.heat, 0.34f * bri));
    bl.addSprite (hp.x, hp.y, haloR,        heatColour (head.heat, 0.62f * bri));
    bl.addSprite (hp.x, hp.y, haloR * 0.5f, heatColour (head.heat, 0.95f * bri));
}

// NÚCLEO de la cabeza (alpha-over normal, sobre g DESPUÉS del bloom). Es el punto que SIEMPRE lee, incluso
// sobre el glow central: un disco de color saturado opaco + un núcleo blanco intenso, ambos con alpha alto
// (no aditivo) → la cabeza-cometa se impone sobre cualquier fondo.
void StellarPad::paintSourceCore (juce::Graphics& g, int w, int h)
{
    if (trailLen <= 0) return;
    const auto& head = trail[trailHead];
    const juce::Point<float> hp = cvToPad (head.x, head.y, head.dist, w, h);

    const float prox  = 1.0f - juce::jlimit (0.0f, 1.0f, head.dist);
    const float coreR = juce::jmap (prox, 3.5f, 6.5f);
    const float bri   = juce::jmap (prox, 0.85f, 1.0f);

    // anillo de color saturado opaco-ish: separa la cabeza del fondo aunque el fondo sea brillante
    g.setColour (heatColour (head.heat, 0.85f * bri));
    g.fillEllipse (hp.x - coreR * 1.3f, hp.y - coreR * 1.3f, coreR * 2.6f, coreR * 2.6f);
    // núcleo blanco intenso (corona + punto) — el centro nítido de la estrella
    g.setColour (juce::Colours::white.withAlpha (0.85f * bri));
    g.fillEllipse (hp.x - coreR * 0.85f, hp.y - coreR * 0.85f, coreR * 1.7f, coreR * 1.7f);
    g.setColour (juce::Colours::white.withAlpha (0.98f * bri));
    g.fillEllipse (hp.x - coreR * 0.42f, hp.y - coreR * 0.42f, coreR * 0.84f, coreR * 0.84f);
}

// MEDIDOR DE MORPH (T3, rediseño 2026-06) — reemplaza el label FALSO ("ATR // LISSAJOUS-Δ", que ni
// siquiera era uno de los 4 atractores reales). Una regla continua Orbit·Pendulum·Lorenz·Rössler con un
// marcador en la posición de SHAPE: nombra los 4 Y revela la mezcla. A diferencia de ORBIT (selector
// discreto: el usuario ELIGE la forma), acá el preset morphea SHAPE en silencio y el medidor lo LEE.
void StellarPad::paintMorphMeter (juce::Graphics& g, int w, int h) const
{
    const float trackW = (float) w * 0.52f;
    const float x0 = ((float) w - trackW) * 0.5f;
    const float ty = (float) h - 30.0f;

    g.setColour (th::fnt.withAlpha (0.55f));
    g.fillRoundedRectangle (x0, ty - 1.0f, trackW, 2.0f, 1.0f);

    const juce::String names[4] = { "ORBIT", "PENDULUM", "LORENZ", juce::String::fromUTF8 ("R\xc3\x96SSLER") };
    const float kPos[4] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
    const float sh = juce::jlimit (0.0f, 1.0f, shapeNow);
    const int   active = juce::jlimit (0, 3, juce::roundToInt (sh * 3.0f));

    g.setFont (fonts::mono (8.0f).withExtraKerningFactor (0.06f));
    for (int i = 0; i < 4; ++i)
    {
        const float tx = x0 + kPos[i] * trackW;
        g.setColour ((i == active ? th::cyan : th::fnt).withAlpha (i == active ? 0.9f : 0.5f));
        g.fillRect (tx - 0.5f, ty - 4.0f, 1.0f, 8.0f);

        const auto just = (i == 0) ? juce::Justification::centredLeft
                        : (i == 3) ? juce::Justification::centredRight
                                   : juce::Justification::centred;
        const int lw = 86;
        const int lx = (i == 0) ? (int) tx - 2
                     : (i == 3) ? (int) tx - lw + 2
                                : juce::roundToInt (tx - (float) lw * 0.5f);
        g.setColour (i == active ? th::txt : th::fnt);
        g.drawText (names[i], lx, (int) ty + 7, lw, 11, just);
    }

    // marcador (glow + núcleo) en la posición de SHAPE
    const float mx = x0 + sh * trackW;
    g.setColour (th::cyan.withAlpha (0.22f)); g.fillEllipse (mx - 6.0f, ty - 6.0f, 12.0f, 12.0f);
    g.setColour (th::cyan.withAlpha (0.85f)); g.fillEllipse (mx - 3.0f, ty - 3.0f, 6.0f,  6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.95f)); g.fillEllipse (mx - 1.4f, ty - 1.4f, 2.8f, 2.8f);
}

// TELEMETRÍA REAL en las dos esquinas SUPERIORES (la inferior es del medidor de morph). Valores honestos
// de los atomics: θ/VEL/HEAT a la izquierda · AZ/DST/FIELD a la derecha. El HEAT colorea su línea.
void StellarPad::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = fonts::mono (8.0f).withExtraKerningFactor (0.10f);
    const int lh = 13, colW = 170;
    auto line = [&] (const juce::String& s, int x, int y, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, colW, 11, right ? juce::Justification::centredRight
                                             : juce::Justification::centredLeft);
    };
    // TL: fase / velocidad / heat (el heat COLOREA su línea)
    line (teleTheta,          12, 10,            false, th::mut);
    line ("VEL "  + teleVel,  12, 10 + lh,       false, th::fnt);
    line ("HEAT " + teleHeat, 12, 10 + 2 * lh,   false, heatColour (teleHeatVal, 1.0f));
    // TR: azimut / distancia / campo
    line ("AZ "    + teleAz,    w - colW - 12, 10,          true, th::mut);
    line ("DST "   + teleDst,   w - colW - 12, 10 + lh,     true, th::fnt);
    line ("FIELD " + teleField, w - colW - 12, 10 + 2 * lh, true, th::fnt);
}

void StellarPad::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;

    // 0) FÓSFORO (larga exposición): la forma del atractor ACUMULADA. Compuesto sobre el pozo casi-negro
    //    (alpha-over ⇒ lee aditivo, mismo truco que Bloom). Se actualiza en advanceFrame (NO acá: sin
    //    mutación en paint).
    if (phosphor.isValid())
        g.drawImageTransformed (phosphor, juce::AffineTransform::scale (1.0f / kPhosScale));

    paintFieldCross (g, w, h);

    // 1) HALO de la cabeza (additivo, mockup 'lighter') = el glow de la fuente.
    bloom.begin (w, h, currentScale());
    paintSource (bloom, w, h);
    bloom.compositeOnto (g);

    // 2) NÍTIDO encima (alpha-over): estela reciente corta + núcleo de la cabeza → el "ahora" se impone
    //    sobre el fósforo y sobre el glow central (z-order correcto, va último).
    paintTrailCore (g, w, h);
    paintSourceCore (g, w, h);

    // 3) IDENTIDAD: telemetría real en las dos esquinas. (El medidor de morph se PROMOVIÓ a la bahía
    //    como control HÉROE — MorphSelector con íconos — así el pozo queda limpio para el visual.)
    paintTelemetry  (g, w, h);
}

void StellarPad::refreshTelemetry()
{
    auto sgn = [] (float v) { return juce::String::fromUTF8 (v >= 0.0f ? "+" : "\xe2\x88\x92"); };
    const auto& head = trail[trailHead];

    // θ: ángulo instantáneo de la posición (0° = frente, crece horario) — derivado de la sim REAL
    const float thetaDeg = std::fmod (juce::radiansToDegrees (std::atan2 (head.x, head.y)) + 360.0f, 360.0f);
    teleTheta = juce::String::fromUTF8 ("\xce\xb8 ")
              + juce::String (thetaDeg, 1).paddedLeft ('0', 5) + juce::String::fromUTF8 ("\xc2\xb0");

    const float azDeg = juce::radiansToDegrees (std::atan2 (head.x, head.y));
    teleAz  = sgn (azDeg) + juce::String ((int) std::abs (azDeg)).paddedLeft ('0', 3)
            + juce::String::fromUTF8 ("\xc2\xb0");
    teleDst = juce::String (juce::jlimit (0.0f, 1.0f, head.dist), 2);

    const auto& prevPt = trail[(trailHead - 1 + kTrailMax) % kTrailMax];
    const float vel = std::hypot (head.x - prevPt.x, head.y - prevPt.y) * 30.0f;
    teleVel     = juce::String (vel, 2);
    teleHeatVal = head.heat;
    teleHeat    = juce::String (head.heat, 2);
    teleField   = sgn (fieldXNow) + juce::String (std::abs (fieldXNow), 2) + " / "
                + sgn (fieldYNow) + juce::String (std::abs (fieldYNow), 2);
}

bool StellarPad::advanceFrame()
{
    const float x = azSrc.load (std::memory_order_relaxed);
    const float y = depthSrc.load (std::memory_order_relaxed);
    const float heat = heatSrc.load (std::memory_order_relaxed);
    const float dist = distSrc.load (std::memory_order_relaxed);

    trailHead = (trailHead + 1) % kTrailMax;
    trail[trailHead] = { x, y, heat, dist };
    if (trailLen < kTrailMax) ++trailLen;

    // SHAPE → medidor de morph + transición HÍBRIDA: un salto grande de SHAPE (cargar preset / mover el
    // knob de golpe) dispara el "reset suave" (fade fuerte del fósforo + no unir la traza vieja con la
    // nueva); un cambio chico y continuo (knob a mano) deja que el fósforo MORPHEE la acumulación sin cortar.
    const float sh = shapeSrc.load (std::memory_order_relaxed);
    const float dShape = std::abs (sh - shapeLast);
    if (dShape > kShapeJump) { rebuildFrames = kRebuildFrames; phosSkipSeg = true; }
    shapeLast = sh; shapeNow = sh;

    // FÓSFORO: sólo evoluciona cuando la trayectoria se MUEVE (o durante un reset) → en reposo se CONGELA
    // (la forma queda visible) y la CPU baja a ~0 (no corre el fade del buffer). El primer frame entra
    // siempre (lastX sentinel = 1e9) para sembrar el buffer.
    const bool trajMoved = std::abs (x - lastX) > 1.0e-4f || std::abs (y - lastY) > 1.0e-4f;
    if (trajMoved || rebuildFrames > 0)
        updatePhosphor (getWidth(), getHeight());

    // Reflejar el centro del campo + MOTION/SMEAR (cruz/orbe del centro · SMEAR tiñe la estela reciente).
    const float fx = fxP ? fxP->convertFrom0to1 (fxP->getValue()) : 0.0f;
    const float fy = fyP ? fyP->convertFrom0to1 (fyP->getValue()) : 0.0f;
    const float mo = motionSrc.load (std::memory_order_relaxed);
    const float sm = smearSrc.load (std::memory_order_relaxed);

    const bool fieldMoved = std::abs (fx - fieldXNow) > 1.0e-4f || std::abs (fy - fieldYNow) > 1.0e-4f
                         || std::abs (mo - motionNow) > 1.0e-3f || std::abs (sm - smearNow) > 1.0e-3f;
    fieldXNow = fx; fieldYNow = fy; motionNow = mo; smearNow = sm;

    // telemetría honesta: refresca strings cada ~0.2 s (6 frames a 30 fps)
    if (--teleCountdown <= 0)
    {
        teleCountdown = 6;
        refreshTelemetry();
    }

    // Repintar mientras haya movimiento, cambie campo/distancia, morphee SHAPE, o corra un reset suave.
    const bool moved = trajMoved || std::abs (dist - lastDist) > 1.0e-4f || fieldMoved
                    || dShape > 1.0e-4f || rebuildFrames > 0;
    lastX = x; lastY = y; lastDist = dist;
    return moved;
}

} // namespace pulsar::ui
