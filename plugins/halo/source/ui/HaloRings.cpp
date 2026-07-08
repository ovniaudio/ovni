#include "HaloRings.h"
#include "HaloRingsStatic.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace halo::ui
{

namespace th    = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

// ---- constantes del funnel (anti-magic-number) ------------------------------------------------
static constexpr float kRadMinFrac = 0.34f;   // Size=0 → coronas chicas (fracción de R)
static constexpr float kRadMaxFrac = 0.86f;   // Size=1 → coronas amplias (mockup baseR = R·lerp(0.34,0.86))
static constexpr float kBaseRiseV  = 0.014f;  // velocidad de ascenso base por frame (life/frame), ×Decay ×RATE
static constexpr float kEllH       = 0.30f;   // aplanado vertical de la corona (perspectiva), mockup ellH = rad·0.30

HaloRings::HaloRings (std::atomic<float>& shimmer, std::atomic<float>& decay, std::atomic<float>& size,
                      std::atomic<float>& tone, std::atomic<float>& orbit, std::atomic<float>& mix,
                      std::atomic<float>& freeze, std::atomic<float>& loopRms, std::atomic<float>& rateNorm)
    : ovni::ui::VisualizerBase (30),
      shimmerSrc (shimmer), decaySrc (decay), sizeSrc (size), toneSrc (tone), orbitSrc (orbit),
      mixSrc (mix), freezeSrc (freeze), loopRmsSrc (loopRms), rateNormSrc (rateNorm)
{
    setSettleHold (60);   // tras quietud, sigue un toque (las coronas en vuelo terminan de subir) y pausa.

    // Estado inicial desde los atomics (primer frame coherente).
    shimmerNow = shimmerSm = shimmerSrc.load (std::memory_order_relaxed);
    decayNow   = decaySm   = decaySrc.load   (std::memory_order_relaxed);
    sizeNow    = sizeSm    = sizeSrc.load    (std::memory_order_relaxed);
    toneNow    = toneSm    = toneSrc.load    (std::memory_order_relaxed);
    orbitNow   = orbitSm   = orbitSrc.load   (std::memory_order_relaxed);
    mixNow     = mixSm     = mixSrc.load     (std::memory_order_relaxed);
    freezeNow  = freezeSm  = freezeSrc.load  (std::memory_order_relaxed);
    loopNow    = loopSm    = loopRmsSrc.load (std::memory_order_relaxed);
    rateNow    = rateSm    = rateNormSrc.load (std::memory_order_relaxed);

    // Sembrar coronas YA en vuelo (a distintas etapas de vida) → el primer frame (y un snapshot estático, que
    // no corre el timer) muestra el FUNNEL ya formado, no un campo vacío. Live, el flujo sigue por advanceFrame().
    // Semilla fija → snapshot/test reproducibles.
    juce::Random rng (0x4a10);
    const int seedCount = juce::jmin (kRings, 12);
    for (int i = 0; i < seedCount; ++i)
    {
        Ring& r = rings[(size_t) i];
        r.life   = (float) i / (float) seedCount;                           // repartidos a lo largo del ascenso
        r.seed   = rng.nextFloat() * kTwoPi;                                // ángulo orbital propio
        r.active = true;
    }
    nextRing  = seedCount % kRings;
    liveCount = seedCount;

    refreshTelemetry();
}

// Tinte: claro → magenta profundo del sello según TONE (mockup §tintColor: lerp 232,206,255 → 157,99,214).
juce::Colour HaloRings::tintColour (float tone01, float alpha) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, tone01);
    const auto  r = (juce::uint8) juce::roundToInt (juce::jmap (t, 232.0f, 157.0f));
    const auto  gg= (juce::uint8) juce::roundToInt (juce::jmap (t, 206.0f,  99.0f));
    const auto  b = (juce::uint8) juce::roundToInt (juce::jmap (t, 255.0f, 214.0f));
    return juce::Colour (r, gg, b).withAlpha (juce::jlimit (0.0f, 1.0f, alpha));
}

void HaloRings::spawnRing() noexcept
{
    Ring& r = rings[(size_t) nextRing];
    r.life   = 0.0f;
    r.seed   = juce::Random::getSystemRandom().nextFloat() * kTwoPi;
    r.active = true;
    nextRing = (nextRing + 1) % kRings;
    coreGlow = 1.0f;
    ++dbgSpawns;
}

int HaloRings::dbgSpawnsOverFrames (int frames) noexcept
{
    dbgSpawns = 0;
    for (int i = 0; i < frames; ++i) advanceFrame();
    return (int) dbgSpawns;
}

void HaloRings::renderStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    detail::paintFieldStatic   (g, w, h);
    detail::paintFieldOverlays (g, w, h);
}

// ── UNA corona: elipse en perspectiva (yScale ∝ aplanado), bloom apilado (2 capas additivas) + trazo de 3
//    grosores (halo difuso → medio → núcleo casi blanco) + 2 chispas de flanco. mockup §drawRing (lighter).
//    Todo se SUMA a la capa de luz `bl` → solapamientos revientan hacia el blanco como en el mockup. ────────
void HaloRings::drawCorona (ovni::ui::Bloom& bl, const Ring& r, float cx, float baseR,
                            float toneAmt, float orbAmt, float decayAmt, float wetPresence, float flare,
                            float srcY, float topYpos) const
{
    const float life = juce::jlimit (0.0f, 1.0f, r.life);

    // ascenso vertical: de la FUENTE (srcY) hacia el TOPE (topYpos).
    const float ease = std::pow (life, 0.82f);
    const float cy   = juce::jmap (ease, srcY, topYpos);
    // radio crece con la vida y con SIZE.
    const float rad  = baseR * juce::jmap (std::pow (life, 0.7f), 0.12f, 1.0f);

    // desvanecimiento: fade-in rápido al nacer, fade-out al final; piso de luminosidad alto (Decay) + presencia.
    const float fadeIn  = juce::jlimit (0.0f, 1.0f, life / 0.10f);
    const float fadeOut = juce::jlimit (0.0f, 1.0f, (1.0f - life) / 0.42f);
    float alpha = fadeIn * fadeOut * (0.60f + decayAmt * 0.40f) * wetPresence * flare;
    if (alpha <= 0.004f) return;

    // órbita: la corona rota alrededor del eje → el ancho elíptico oscila (compresión horizontal) + deriva.
    const float orbAng = orbitPhase + r.seed;
    const float squashX = juce::jmap (orbAmt, 1.0f, 0.40f + 0.6f * std::abs (std::cos (orbAng)));
    const float drift   = std::sin (orbAng) * rad * 0.12f * orbAmt;   // abraza el eje (no lo cruza)
    const float ringCx  = cx + drift;
    const float rx      = juce::jmax (1.0f, rad * squashX);
    const float ry      = juce::jmax (0.6f, rad * kEllH);             // aplanado de perspectiva

    // 1) NUBE DE BLOOM apilada (2 discos additivos achatados a la elipse): halo amplio + halo medio denso.
    bl.addSprite (ringCx, cy, rx * 1.55f, ry * 1.55f, tintColour (toneAmt, 0.22f * alpha));
    bl.addSprite (ringCx, cy, rx * 1.05f, ry * 1.05f, tintColour (toneAmt, 0.40f * alpha));

    // 2) TRAZO de corona (elipse), dibujado dentro de la capa de luz (alpha-over que sobre negro lee additivo):
    //    halo gordo difuso → difuso medio → núcleo casi blanco al centro (la corona BRILLANTE del mockup). El
    //    trazo medio se queda en MAGENTA SATURADO (toneAmt sin lavar) → el funnel no se vuelve pálido.
    auto& g = bl.gfx();
    const auto ell = juce::Rectangle<float> (ringCx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
    g.setColour (tintColour (toneAmt, 0.30f * alpha));
    g.drawEllipse (ell, juce::jmap (life, 14.0f, 5.0f) * 0.5f);
    g.setColour (tintColour (toneAmt, 0.80f * alpha));            // anillo magenta vivo (saturado)
    g.drawEllipse (ell, juce::jmap (life, 7.0f, 2.6f) * 0.5f);
    g.setColour (tintColour (toneAmt * 0.40f, 1.0f * alpha));     // filo casi blanco al centro
    g.drawEllipse (ell, juce::jmax (0.6f, juce::jmap (life, 2.6f, 1.0f) * 0.5f));

    // 3) dos chispas brillantes additivas en los flancos del anillo (lectura de coronas apiladas).
    const float sparkA = 0.85f * alpha * (0.5f + 0.5f * std::abs (std::sin (orbAng)));
    if (sparkA > 0.01f)
    {
        const float sr = juce::jmap (life, 11.0f, 5.0f);
        for (const float sgn : { -1.0f, 1.0f })
            bl.addSprite (ringCx + sgn * rx, cy, sr, sr, tintColour (toneAmt * 0.30f, 0.95f * sparkA));
    }
}

void HaloRings::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float R  = detail::haloR (w, h);
    const float srcY = detail::sourceY (h, cy, R);
    const float topYpos = detail::topY (h, cy, R);

    const float toneAmt  = juce::jlimit (0.0f, 1.0f, toneSm);
    const float decayAmt = juce::jlimit (0.0f, 1.0f, decaySm);
    const float orbAmt   = juce::jlimit (0.0f, 1.0f, orbitSm);
    const float sizeAmt  = juce::jlimit (0.0f, 1.0f, sizeSm);

    // Presencia (Mix) + floración (loopRms): pisos ALTOS → aun en reposo el funnel se LEE (gancho del Reel a
    // 320 px). flare arranca de 0.6 (nada de "apagado"); el bloom brilla con Decay.
    const float wetPresence = juce::jmap (mixSm, 0.0f, 1.0f, 0.55f, 1.0f);   // piso 0.55
    const float flare       = juce::jlimit (0.0f, 1.2f, 0.60f + 1.0f * loopSm);
    const float bloomBri    = 0.6f + decayAmt * 0.4f;

    // radio máximo de corona ∝ SIZE, con un leve latido global (vivo aunque las macros estén quietas).
    const float pulse = 1.0f + 0.04f * std::sin (pulsePhase);
    const float baseR = R * juce::jmap (sizeAmt, kRadMinFrac, kRadMaxFrac) * pulse;

    // ── TODA la luz del funnel se acumula en una capa ADDITIVA (mockup §draw: globalCompositeOperation
    //    = 'lighter') y se vuelca de una sola vez → los anillos REVIENTAN hacia el blanco al solaparse. ───────
    bloom.begin (w, h, currentScale());

    // COLUMNA de luz magenta apilada (el gancho del sello): disco alto y angosto + corazón concentrado abajo.
    {
        const float colCy = cy - R * 0.14f;
        bloom.addSprite (cx, colCy, R * 0.66f, R * 1.28f, tintColour (toneAmt, 0.24f * bloomBri * wetPresence));
        const float heartY = cy + R * 0.08f;
        bloom.addSprite (cx, heartY, R * 0.62f, R * 0.46f, tintColour (toneAmt * 0.7f, 0.32f * bloomBri * wetPresence));
    }

    // las coronas: del más viejo (arriba, tenue) al más nuevo (abajo, brillante) → los nuevos quedan encima.
    int order[kRings];
    int nOrder = 0;
    for (int i = 0; i < kRings; ++i)
        if (rings[(size_t) i].active) order[nOrder++] = i;
    for (int i = 1; i < nOrder; ++i)   // insertion sort por life DESC (n pequeño → barato, sin heap)
    {
        const int key = order[i];
        int j = i - 1;
        while (j >= 0 && rings[(size_t) order[j]].life < rings[(size_t) key].life) { order[j + 1] = order[j]; --j; }
        order[j + 1] = key;
    }
    for (int i = 0; i < nOrder; ++i)
        drawCorona (bloom, rings[(size_t) order[i]], cx, baseR,
                    toneAmt, orbAmt, decayAmt, wetPresence, flare, srcY, topYpos);

    // FUENTE: núcleo brillante donde nacen las coronas, con pulso (coreGlow) — additivo + chispa blanca.
    {
        const float cPulse = 0.6f + coreGlow * 0.4f;
        const float fr = 36.0f * bloomBri;
        bloom.addSprite (cx, srcY, fr, fr * 0.7f, tintColour (toneAmt, 0.52f * bloomBri * cPulse * wetPresence));
        const float sr = 2.8f + coreGlow * 2.4f;
        bloom.addSprite (cx, srcY, sr, sr, juce::Colours::white.withAlpha (juce::jlimit (0.0f, 0.95f, 0.85f * cPulse)));
    }

    bloom.compositeOnto (g);   // vuelca la capa de luz acumulada sobre el chasis

    // ── velo de presencia: tinte global sutil ∝ Mix (el wet "se siente" en todo el campo). En FREEZE el velo se
    //    intensifica un poco (la nube capturada llena más). ──────────────────────────────────────────────────
    if (mixSm > 0.02f)
    {
        const float veil = (0.04f + 0.05f * freezeSm) * wetPresence;
        g.setColour (tintColour (toneAmt, veil));
        g.fillRect (0.0f, 0.0f, (float) w, (float) h);
    }

    // ── telemetría honesta en las 4 esquinas (valores REALES de los atomics). ────────────────────────────────
    paintTelemetry (g, w, h);
}

void HaloRings::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = fonts::mono (8.0f).withExtraKerningFactor (0.12f);
    const int colW = 150, lh = 13;
    auto line = [&g, &lab] (const juce::String& s, int x, int y, int wd, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, wd, 11, right ? juce::Justification::centredRight : juce::Justification::centredLeft);
    };

    // TL: estructura del motor + cantidad real de coronas en vuelo (mockup teleTL "FDN // CORONAS" + "ANILLOS NN").
    line ("FDN // HALOS", 12, 10,      colW, false, th::fnt);
    line (teleRings,                                 12, 10 + lh, colW, false, th::mut);
    // TR: RT60 (de Decay+Size) + SIZE (mockup teleTR).
    line ("RT60 " + teleRt,  w - colW - 12, 10,      colW, true, th::mut);
    line ("SIZE " + teleSize, w - colW - 12, 10 + lh, colW, true, th::mut);
    // BL: ÓRBITA + TINTE (el TINTE COLOREA su valor, mockup §blTone).
    line ("ORBIT " + teleOrbit, 12, h - 8 - 2 * lh, colW, false, th::mut);
    g.setColour (tintColour (teleToneVal, 1.0f));
    g.setFont (lab);
    g.drawText ("TINT " + teleTone, 12, h - 8 - lh, colW, 11,
                juce::Justification::centredLeft);
    // BR: SHIMMER = las voces FIJAS (octava + quinta = +12 / +7 st), honestidad del feedback (mockup teleBR).
    line (juce::String::fromUTF8 ("SHIMMER +12 / +7"), w - colW - 12, h - 8 - lh, colW, true, th::mut);
}

void HaloRings::refreshTelemetry()
{
    teleRings = "RINGS " + juce::String (liveCount).paddedLeft ('0', 2);
    // RT60 derivado de DECAY+SIZE (mockup §tele: rango 0.3–14 s). Honesto: la cola del FDN crece con ambos.
    const float rt = 0.3f + decaySm * juce::jmap (sizeSm, 4.0f, 13.7f);
    teleRt    = juce::String (rt, 1) + " s";
    teleSize  = juce::String (sizeSm, 2);
    teleOrbit = juce::String (orbitSm, 2);
    teleTone  = juce::String (toneSm, 2);
    teleToneVal = toneSm;
}

bool HaloRings::advanceFrame()
{
    // --- leer telemetría ------------------------------------------------------------------------
    shimmerNow = shimmerSrc.load (std::memory_order_relaxed);
    decayNow   = decaySrc.load   (std::memory_order_relaxed);
    sizeNow    = sizeSrc.load    (std::memory_order_relaxed);
    toneNow    = toneSrc.load    (std::memory_order_relaxed);
    orbitNow   = orbitSrc.load   (std::memory_order_relaxed);
    mixNow     = mixSrc.load     (std::memory_order_relaxed);
    freezeNow  = freezeSrc.load  (std::memory_order_relaxed);
    loopNow    = loopRmsSrc.load (std::memory_order_relaxed);
    rateNow    = rateNormSrc.load (std::memory_order_relaxed);

    // De-zipper visual (one-pole).
    const float k = 0.18f;
    shimmerSm += (shimmerNow - shimmerSm) * k;
    decaySm   += (decayNow   - decaySm)   * k;
    sizeSm    += (sizeNow    - sizeSm)    * k;
    toneSm    += (toneNow    - toneSm)    * k;
    orbitSm   += (orbitNow   - orbitSm)   * k;
    mixSm     += (mixNow     - mixSm)     * k;
    freezeSm  += (freezeNow  - freezeSm)  * k;
    loopSm    += (loopNow    - loopSm)    * 0.25f;   // la energía responde un poco más rápido (floración)
    rateSm    += (rateNow    - rateSm)    * k;

    // VELOCIDAD GLOBAL del funnel ∝ RATE/órbita: a RATE lento las coronas NACEN, SUBEN y ORBITAN lento (los
    // "circulitos lentos" que pidió Joaquín); a RATE rápido, rápido. El RATE es el amo del reloj (no Shimmer).
    const float rateScale = juce::jmap (juce::jlimit (0.0f, 1.0f, rateSm), 0.12f, 1.7f);

    // Latido global lento (el funnel respira aunque nada se toque).
    pulsePhase += 0.05f;
    if (pulsePhase > 1.0e6f) pulsePhase = std::fmod (pulsePhase, kTwoPi);

    // Fase orbital: avanza ∝ ORBIT, escalada por la VELOCIDAD del RATE (con ORBIT≈0 casi no rota).
    orbitPhase += (0.010f + 0.07f * orbitSm) * rateScale;
    if (orbitPhase > 1.0e6f) orbitPhase = std::fmod (orbitPhase, kTwoPi);

    coreGlow *= 0.90f;   // el pulso de la FUENTE decae

    const bool frozen = freezeSm > 0.5f;   // FREEZE: la nube queda capturada (no nacen nuevas, holdean en sitio)

    // --- nacimiento de coronas: densidad ∝ Shimmer, pero la VELOCIDAD la pone el RATE (rateScale). A RATE lento
    //     nacen MUY de a poco; a RATE rápido, en chorro. En FREEZE no nacen nuevas. ----------------------------
    if (! frozen)
    {
        const float rate = (0.05f + 0.55f * shimmerSm) * rateScale;   // floraciones/frame
        const int   want = juce::roundToInt (juce::jmap (shimmerSm, 3.0f, (float) kRings));   // objetivo ∝ Shimmer
        spawnAccum += rate;
        while (spawnAccum >= 1.0f)
        {
            spawnAccum -= 1.0f;
            if (liveCount < want) spawnRing();
        }
        if (liveCount >= want) spawnAccum = juce::jmin (spawnAccum, 1.0f);
    }

    // --- avanzar las coronas en vuelo: suben/decaen a velocidad ∝ Decay (más feedback = suben más lento y
    //     llegan más alto = duran más). En FREEZE no avanzan (holdean). ---------------------------------------
    const float riseV = kBaseRiseV * juce::jmap (decaySm, 1.8f, 0.7f) * rateScale;
    int active = 0;
    for (auto& r : rings)
    {
        if (! r.active) continue;
        if (! frozen)
        {
            r.life += riseV;
            if (r.life >= 1.0f) { r.active = false; continue; }
        }
        ++active;
    }
    liveCount = active;

    // telemetría honesta: refresca strings cada ~0.2 s (6 frames a 30 fps), como el mockup.
    if (--teleCountdown <= 0)
    {
        teleCountdown = 6;
        refreshTelemetry();
    }

    // --- ¿repintar? sí si hay coronas vivas, o si una macro se mueve, o si hay energía/órbita/freeze/glow. ----
    const bool macroMoved = std::abs (shimmerNow - shimmerSm) > 5.0e-4f || std::abs (decayNow - decaySm) > 5.0e-4f
                         || std::abs (sizeNow - sizeSm) > 5.0e-4f || std::abs (toneNow - toneSm) > 5.0e-4f
                         || std::abs (mixNow - mixSm) > 5.0e-4f || std::abs (orbitNow - orbitSm) > 5.0e-4f
                         || std::abs (freezeNow - freezeSm) > 5.0e-4f || std::abs (rateNow - rateSm) > 5.0e-4f;
    const bool energetic = loopSm > 0.005f || shimmerSm > 0.02f || coreGlow > 0.01f || (orbitSm > 0.02f && active > 0);
    return active > 0 || macroMoved || energetic;
}

} // namespace halo::ui
