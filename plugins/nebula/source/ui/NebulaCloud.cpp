#include "NebulaCloud.h"
#include "NebulaCloudStatic.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace nebula::ui
{

namespace th = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// ---- constantes del dibujo (anti-magic-number; espejo del mockup nebula-a §draw) -----------------
// Radio del disco de la nube como fracción de R (radio del campo), en los extremos de Size.
static constexpr float kRadMinFrac = 0.42f;   // Size=0  → nube apretada
static constexpr float kRadMaxFrac = 0.96f;   // Size=1  → llena el campo
// Profundidad de la respiración (cuánto modula el radio en ±) a Breath máximo (mockup: *0.34).
static constexpr float kBreathDepth = 0.34f;
// Densidad de motas visibles en los extremos de Decay (mockup: 0.30 + decay*0.70).
static constexpr float kMoteVisMin = 0.30f;
static constexpr float kMoteVisMax = 1.00f;
// Piso de luminosidad del bloom: NUNCA se apaga del todo (mockup: 0.6 + decay*0.4).
static constexpr float kBloomFloor = 0.60f;

NebulaCloud::NebulaCloud (std::atomic<float>& size, std::atomic<float>& decay,
                          std::atomic<float>& tone, std::atomic<float>& breath,
                          juce::RangedAudioParameter* sizeParam,
                          juce::RangedAudioParameter* decayParam)
    : ovni::ui::VisualizerBase (30),
      sizeSrc (size), decaySrc (decay), toneSrc (tone), breathSrc (breath),
      sizeP (sizeParam), decayP (decayParam)
{
    setSettleHold (45);
    setInterceptsMouseClicks (true, false);              // la nube ES el control: captura el drag
    setMouseCursor (juce::MouseCursor::CrosshairCursor);

    buildMotes();

    // Estado inicial desde los atomics (para que el primer frame no parta de un default raro).
    sizeNow = sizeSm = sizeSrc.load (std::memory_order_relaxed);
    decayNow = decaySm = decaySrc.load (std::memory_order_relaxed);
    toneNow = toneSm = toneSrc.load (std::memory_order_relaxed);
    breathNow = breathSm = breathSrc.load (std::memory_order_relaxed);
    densitySm = 0.18f + decaySm * 0.82f;

    // Fases de los 2 LFOs incoherentes desfasadas (que el primer frame ya respire, no parta plano).
    lfoA = 1.3f; lfoB = 4.1f;
    for (int i = 0; i < 40; ++i) advanceFrame();   // precalentar la respiración (mockup §boot)
    refreshTelemetry();
}

// Sembrar las motas de polvo: distribución hacia el centro + fases propias de respiración/titileo.
// LCG determinístico (mismo patrón que el mockup §buildDustGeometry) → snapshot/test reproducibles.
void NebulaCloud::buildMotes()
{
    detail::Lcg rnd (90125);
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    for (auto& m : motes)
    {
        m.ang    = rnd() * twoPi;
        m.rad    = std::pow (rnd(), 0.78f);          // densidad suave hacia el centro, llena el disco
        m.base   = 0.5f + rnd() * 0.5f;              // radio base relativo
        m.sz     = rnd() < 0.16f ? 1.5f : 0.8f + rnd() * 0.5f;
        m.ph1    = rnd() * twoPi;
        m.ph2    = rnd() * twoPi;
        m.tw     = rnd() * twoPi;                    // fase de titileo
        m.twRate = 0.6f + rnd() * 1.8f;
    }
}

// Tone → tinte de la nube: 0 = claro/lila brillante · 1 = magenta profundo/apagado (mockup §tintColor:
// lerp de (230,206,255) a (157,99,214) = theme::magenta → theme::magD). Damping alto = nube más apagada.
juce::Colour NebulaCloud::cloudTint (float tone01, float alpha) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, tone01);
    const juce::Colour bright (0xffe6cdff);                   // claro/lila (mockup r230 g206 b255)
    const juce::Colour deep   = th::magD;                     // magenta profundo (mockup r157 g99 b214)
    return bright.interpolatedWith (deep, t).withAlpha (alpha);
}

// =================================================================================================
// Esculpir: NO hay cursor — la nube ES el control. X = Size (radio), Y = Decay (densidad). Drag 1:1:
// la X del cursor mapea linealmente a [0..1] del campo, e idéntico para Y invertida (arriba = más cola).
void NebulaCloud::mouseDown (const juce::MouseEvent& e)
{
    dragging = true;
    if (sizeP)  sizeP->beginChangeGesture();
    if (decayP) decayP->beginChangeGesture();
    mouseDrag (e);
}

void NebulaCloud::mouseDrag (const juce::MouseEvent& e)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    if (w <= 0.0f || h <= 0.0f) return;

    // Drag 1:1 con el dibujo: arrastrar hacia un borde agranda (Size), hacia arriba espesa (Decay).
    // Un arrastre de borde a borde barre 0..100% (natural para "abrir/cerrar" y "poblar/vaciar").
    const float nx = juce::jlimit (0.0f, 1.0f, e.position.x / w);          // X → Size 0..1
    const float ny = juce::jlimit (0.0f, 1.0f, 1.0f - e.position.y / h);   // Y(arriba=1) → Decay 0..1

    if (sizeP)  sizeP->setValueNotifyingHost  (sizeP->convertTo0to1  (sizeP->getNormalisableRange().convertFrom0to1 (nx)));
    if (decayP) decayP->setValueNotifyingHost (decayP->convertTo0to1 (decayP->getNormalisableRange().convertFrom0to1 (ny)));
}

void NebulaCloud::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        if (sizeP)  sizeP->endChangeGesture();
        if (decayP) decayP->endChangeGesture();
        dragging = false;
    }
}

// =================================================================================================
void NebulaCloud::renderStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    detail::paintFieldStatic   (g, w, h);
    detail::paintFieldOverlays (g, w, h);
}

// ---- bloom: sprite de glow horneado por bucket de TINTE (4 capas apiladas, mockup §draw bloom) ----
// El sprite se hornea con tinte plano (luminosidad lineal); el piso de luminosidad y la respiración se
// aplican al BLITEAR (opacidad/escala) → nada de createRadialGradient por frame.
const juce::Image& NebulaCloud::bloomSpriteFor (float tone01)
{
    const int bucket = juce::jlimit (0, kTintBuckets,
                                     juce::roundToInt (juce::jlimit (0.0f, 1.0f, tone01) * (float) kTintBuckets));
    auto& img = bloomSprites[(size_t) bucket];
    if (img.isValid()) return img;

    constexpr int   kSize = 200;                 // diámetro del sprite (se escala al dibujar)
    constexpr float c     = kSize * 0.5f;
    const float     tRef  = (float) bucket / (float) kTintBuckets;
    img = juce::Image (juce::Image::ARGB, kSize, kSize, true);
    juce::Graphics sg (img);

    // 4 capas apiladas (mockup: exterior amplia · media · núcleo denso · chispa central), additivas.
    struct Layer { float r; float a0, aMid, midPos; float tintScale; };
    const Layer layers[] = {
        { c,         0.17f, 0.075f, 0.45f, 1.00f },   // capa exterior amplia
        { c * 0.54f, 0.28f, 0.10f,  0.50f, 1.00f },   // capa media
        { c * 0.25f, 0.40f, 0.14f,  0.60f, 0.55f },   // núcleo denso (un toque más claro)
        { c * 0.10f, 0.45f, 0.0f,   1.00f, 0.40f },   // chispa central brillante
    };
    for (const auto& L : layers)
    {
        const float tShade = tRef * L.tintScale;
        juce::ColourGradient gl (cloudTint (tShade, L.a0), c, c,
                                 cloudTint (tShade, 0.0f), c, c - L.r, true);
        if (L.aMid > 0.0f) gl.addColour (L.midPos, cloudTint (tShade, L.aMid));
        sg.setGradientFill (gl);
        sg.fillEllipse (c - L.r, c - L.r, L.r * 2.0f, L.r * 2.0f);
    }
    return img;
}

// BLOOM volumétrico de la nube: el sprite cacheado escalado al radio vivo, opacidad = piso de luminosidad.
void NebulaCloud::paintBloom (juce::Graphics& g, int w, int h)
{
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float R  = detail::fieldRadius (w, h);

    // radio base de la nube ∝ SIZE; la respiración modula el radio (amplitud ∝ BREATH).
    const float cloudR   = R * juce::jmap (sizeSm, 0.0f, 1.0f, kRadMinFrac, kRadMaxFrac);
    const float breathMod = 1.0f + (breathPhase01 - 0.5f) * 2.0f * breathSm * kBreathDepth;
    const float liveR    = juce::jmax (4.0f, cloudR * breathMod);

    // piso de luminosidad: nunca se apaga del todo (∝ DECAY).
    const float bloomBri = kBloomFloor + decaySm * (1.0f - kBloomFloor);

    const juce::Graphics::ScopedSaveState save (g);
    g.setOpacity (1.0f);

    const auto& sprite = bloomSpriteFor (toneSm);
    // El sprite cubre liveR*1.22 (capa exterior del mockup); se blitea additivo con la opacidad del piso.
    const float outR  = liveR * 1.22f;
    const float scale = (outR * 2.0f) / (float) sprite.getWidth();
    g.setOpacity (bloomBri);
    g.drawImageTransformed (sprite, juce::AffineTransform::scale (scale)
                                        .translated (cx - outR, cy - outR));
    g.setOpacity (1.0f);
}

// MOTAS de polvo: ~240, respiran y titilan; cantidad visible ∝ DECAY (mockup §draw motas).
void NebulaCloud::paintMotes (juce::Graphics& g, int w, int h)
{
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float R  = detail::fieldRadius (w, h);

    const float cloudR   = R * juce::jmap (sizeSm, 0.0f, 1.0f, kRadMinFrac, kRadMaxFrac);
    const float breathMod = 1.0f + (breathPhase01 - 0.5f) * 2.0f * breathSm * kBreathDepth;
    const float liveR    = juce::jmax (4.0f, cloudR * breathMod);
    const float bloomBri = kBloomFloor + decaySm * (1.0f - kBloomFloor);
    const float sizeScale = juce::jmap (sizeSm, 0.0f, 1.0f, 0.7f, 1.3f);

    const int visN = juce::jlimit (1, kMotes,
                                   juce::roundToInt (kMotes * (kMoteVisMin + decaySm * (kMoteVisMax - kMoteVisMin))));
    for (int i = 0; i < visN; ++i)
    {
        const auto& m = motes[i];
        // micro-respiración propia (2 LFOs) → cada mota inhala/exhala desfasada.
        const float rBreath = 1.0f + std::sin (lfoA + m.ph1) * 0.05f * breathSm
                                   + std::sin (lfoB + m.ph2) * 0.04f * breathSm;
        const float rr = m.rad * liveR * rBreath * (0.85f + m.base * 0.30f);
        const float px = cx + std::cos (m.ang) * rr;
        const float py = cy + std::sin (m.ang) * rr;

        // titileo ∝ DECAY (más decay = más vida) — su ritmo propio avanza con la fase de respiración.
        // flick = lerp(1, twk, 0.4 + decay*0.5): a poco decay casi no titila; a full decay titila pleno.
        const float twk     = 0.5f + 0.5f * std::sin (breathPhase01 * juce::MathConstants<float>::twoPi * m.twRate + m.tw);
        const float twkAmt  = juce::jlimit (0.0f, 1.0f, 0.4f + decaySm * 0.5f);
        const float flick   = 1.0f + (twk - 1.0f) * twkAmt;
        // desvanecimiento radial hacia el borde, suave (la nube no tiene borde duro).
        const float edge  = 1.0f - juce::jlimit (0.0f, 1.0f, rr / (liveR * 1.1f)) * 0.5f;
        const float a     = juce::jlimit (0.0f, 1.0f, (0.30f + decaySm * 0.55f) * flick * edge * bloomBri);
        const float sz    = m.sz * sizeScale;

        g.setColour (cloudTint (toneSm, a));
        g.fillEllipse (px - sz, py - sz, sz * 2.0f, sz * 2.0f);
    }

    // halo de respiración: un anillo tenue que pulsa con la cola (mockup §draw ring).
    const float ringA = juce::jlimit (0.0f, 1.0f, 0.06f + breathSm * 0.10f * (0.4f + 0.6f * breathPhase01));
    g.setColour (cloudTint (toneSm, ringA));
    g.drawEllipse (cx - liveR * 0.98f, cy - liveR * 0.98f, liveR * 1.96f, liveR * 1.96f, 1.2f);
}

// =================================================================================================
void NebulaCloud::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;

    // Capas de gas/partículas: el mockup usa composite 'lighter'; JUCE no lo expone por estado global, así
    // que emulamos el glow apilando capas translúcidas (alpha bajo) sobre el campo OSCURO de la estática —
    // sumar luz sobre negro casi-puro da el mismo resultado visual sin necesidad del blend aditivo.
    paintBloom (g, w, h);
    paintMotes (g, w, h);

    paintTelemetry (g, w, h);
}

// =================================================================================================
void NebulaCloud::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = fonts::mono (8.0f).withExtraKerningFactor (0.12f);
    const int lh = 13, colW = 150;
    auto line = [&g, &lab] (const juce::String& s, int x, int y, int wd, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, wd, 11, right ? juce::Justification::centredRight
                                           : juce::Justification::centredLeft);
    };

    // TL: identidad del motor + densidad observada (mockup #teleTL)
    line (juce::String::fromUTF8 ("FDN // CLOUD"), 12, 10,      colW, false, th::fnt);
    line (teleDens,                               12, 10 + lh, colW, false, th::mut);
    // TR: RT60 + SIZE (mockup #teleTR)
    line ("RT60 " + teleRt,  w - colW - 12, 10,      colW, true, th::mut);
    line ("SIZE " + teleSize, w - colW - 12, 10 + lh, colW, true, th::mut);
    // BL: BREATH + TINTE (el TINTE COLOREA su valor con el tinte real, mockup #teleBL .hot)
    line ("BREATH " + teleBreath, 12, h - 8 - 2 * lh, colW, false, th::mut);
    g.setColour (cloudTint (teleToneVal, 1.0f));
    g.setFont (lab);
    g.drawText ("TINT " + teleTone, 12, h - 8 - lh, colW, 11, juce::Justification::centredLeft);
    // BR: MOTAS visibles (mockup #teleBR)
    line ("MOTES " + teleMotes, w - colW - 12, h - 8 - lh, colW, true, th::mut);
}

void NebulaCloud::refreshTelemetry()
{
    // DENS: densidad observada (∝ Decay, suavizada) — espejo del mockup §tele.
    teleDens = "DENS " + juce::String (densitySm, 2);
    // RT60 derivado de DECAY+SIZE, honesto: rango 0.2–12 s (mockup §tele).
    const float rt = 0.2f + decaySm * juce::jmap (sizeSm, 0.0f, 1.0f, 4.0f, 11.8f);
    teleRt   = juce::String (rt, 1) + " s";
    teleSize = juce::String (juce::jlimit (0.0f, 1.0f, sizeSm), 2);
    teleBreath = juce::String (juce::jlimit (0.0f, 1.0f, breathPhase01), 2);
    teleTone   = juce::String (juce::jlimit (0.0f, 1.0f, toneSm), 2);
    teleToneVal = toneSm;
    const int visN = juce::jlimit (1, kMotes,
                                   juce::roundToInt (kMotes * (kMoteVisMin + decaySm * (kMoteVisMax - kMoteVisMin))));
    teleMotes = juce::String (visN).paddedLeft ('0', 3);
}

// =================================================================================================
bool NebulaCloud::advanceFrame()
{
    // --- leer telemetría (lo que dibuja la nube), lock-free ---------------------------------------
    const float sz = sizeSrc.load   (std::memory_order_relaxed);
    const float dc = decaySrc.load  (std::memory_order_relaxed);
    const float tn = toneSrc.load   (std::memory_order_relaxed);
    const float br = breathSrc.load (std::memory_order_relaxed);

    sizeNow = sz; decayNow = dc; toneNow = tn; breathNow = br;

    // De-zipper visual: suavizar para que knob/drag muevan la nube sin saltos (~one-pole).
    const float k = 0.18f;
    sizeSm   += (sizeNow   - sizeSm)   * k;
    decaySm  += (decayNow  - decaySm)  * k;
    toneSm   += (toneNow   - toneSm)   * k;
    breathSm += (breathNow - breathSm) * k;

    // densidad observada ∝ DECAY (mockup §step: dTarget = 0.18 + decay*0.82, suavizado).
    const float dTarget = 0.18f + decaySm * 0.82f;
    densitySm += (dTarget - densitySm) * 0.08f;

    // --- respiración: 2 LFOs incoherentes → inhala/exhala orgánico (mockup §step) -------------------
    // Avanza SIEMPRE (mientras Breath>0 la nube respira aunque las perillas no se toquen).
    const float breathAmt = breathSm;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    const float dt = 1.0f / 30.0f;
    const float bw = 0.10f + breathAmt * 0.22f;   // orgánica: lenta (rapidez ∝ Breath)
    lfoA += dt * bw * twoPi * 0.5f;
    lfoB += dt * bw * twoPi * 0.5f * 0.61803f;    // incoherente (razón áurea)
    if (lfoA > 1.0e6f) lfoA = std::fmod (lfoA, twoPi);
    if (lfoB > 1.0e6f) lfoB = std::fmod (lfoB, twoPi);
    const float raw = 0.5f + 0.5f * (0.62f * std::sin (lfoA) + 0.38f * std::sin (lfoB));
    breathPhase01 += (raw - breathPhase01) * 0.10f;

    // telemetría honesta: refresca strings cada ~0.2 s (6 frames a 30 fps), como el mockup.
    if (--teleCountdown <= 0)
    {
        teleCountdown = 6;
        refreshTelemetry();
    }

    // --- ¿hay que repintar? -----------------------------------------------------------------------
    // SÍ si algo del set cambió (knob/drag) o si la respiración sigue moviendo la nube (breath>0).
    const bool macroMoved = std::abs (sizeNow - sizeSm) > 5.0e-4f || std::abs (decayNow - decaySm) > 5.0e-4f
                         || std::abs (toneNow - toneSm) > 5.0e-4f || std::abs (breathNow - breathSm) > 5.0e-4f
                         || std::abs (dTarget - densitySm) > 5.0e-4f;
    const bool breathing = breathSm > 0.01f;   // mientras haya respiración, la nube se mueve sola
    return macroMoved || breathing;
}

} // namespace nebula::ui
