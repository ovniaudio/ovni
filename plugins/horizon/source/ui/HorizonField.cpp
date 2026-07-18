#include "ui/HorizonField.h"
#include "ui/HorizonFieldStatic.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace horizon::ui
{

namespace th = ovni::ui::theme;

// ---- constantes del dibujo (anti-magic-number) ------------------------------------------------
namespace
{
    constexpr float kHorizonYFrac = 0.52f;     // altura del horizonte de eventos (algo bajo el centro)
    constexpr float kEdgePad      = 26.0f;     // margen L/R del eje de frecuencia (px lógicos)
    constexpr float kMaxHalfFrac  = 0.40f;     // media-altura máxima de una lámina (fracción del alto)
    constexpr float kSmoothK      = 0.18f;     // one-pole visual de macros (igual que AuroraField)
    constexpr float kEnergyK      = 0.32f;     // el espectro responde algo más rápido (cristaliza)
    constexpr float kGateK        = 0.42f;     // el gate es el latido: responde rápido o no se VE
    constexpr float kIdleFloor    = 0.16f;     // piso de luz del nivel (legible a 320 px, nunca apagado)
    constexpr float kFreezeFloor  = 0.22f;     // presencia mínima SIN freeze (fantasma del cristal)

    inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }
}

HorizonField::HorizonField (std::atomic<float>& whisper,
                            std::atomic<float>& spread,
                            std::atomic<float>& duck,
                            std::atomic<float>& mix,
                            std::atomic<float>& freeze,
                            std::atomic<float>& gateAmp,
                            std::atomic<float>& gatePhase,
                            std::atomic<float>& wetEnergy,
                            std::atomic<float>& duckEnv,
                            std::atomic<float>& rateNorm,
                            std::array<std::atomic<float>, kBands>& spectrum)
    : ovni::ui::VisualizerBase (30),
      whisperA (whisper), spreadA (spread), duckA (duck), mixA (mix), freezeA (freeze),
      gateAmpA (gateAmp), gatePhaseA (gatePhase), wetEnergyA (wetEnergy),
      duckEnvA (duckEnv), rateNormA (rateNorm), spectrumA (spectrum)
{
    setSettleHold (60);   // tras quietud, el latido termina de asentarse y la base pausa (CPU ~0)
    setOpaque (false);

    // Estado inicial desde los atomics (primer frame coherente — clave para reduced-motion
    // y para el snapshot, que no corren el timer).
    whisperSm = whisperA.load  (std::memory_order_relaxed);
    spreadSm  = spreadA.load   (std::memory_order_relaxed);
    duckSm    = duckA.load     (std::memory_order_relaxed);
    mixSm     = mixA.load      (std::memory_order_relaxed);
    freezeSm  = freezeA.load   (std::memory_order_relaxed);
    gateAmpSm = gateAmpA.load  (std::memory_order_relaxed);
    duckEnvSm = duckEnvA.load  (std::memory_order_relaxed);
    rateSm    = rateNormA.load (std::memory_order_relaxed);
    gatePhaseSm = gatePhaseA.load (std::memory_order_relaxed);
    for (int b = 0; b < kBands; ++b)
        eSm[(size_t) b] = spectrumA[(size_t) b].load (std::memory_order_relaxed);

    // Semillas de tiembleo FIJAS → snapshot/test reproducibles (patrón AuroraField).
    juce::Random rng (0x4202);
    for (int b = 0; b < kBands; ++b)
        bandSeed[(size_t) b] = rng.nextFloat() * juce::MathConstants<float>::twoPi;

    ensureSprites();
    advanceFrame();   // computa drawLvl YA (el primer paint muestra el espectro suspendido)
}

// ------------------------------------------------------------------------------ sprites
// Patrón HaloRings/AuroraField: blancos con el alpha embebido, horneados UNA vez; el blit
// los tiñe (setColour + fillAlphaChannel) y escala → floración real sin gradientes por frame.
void HorizonField::ensureSprites()
{
    // Disco radial (cabeza de la lámina / barrido de luz del gate): núcleo lleno, cola suave.
    {
        constexpr int S = 128;
        glowSprite = juce::Image (juce::Image::ARGB, S, S, true);
        const float c = S * 0.5f;
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
            {
                const float d = juce::jmin (1.0f, std::hypot ((float) x + 0.5f - c, (float) y + 0.5f - c) / c);
                const float f = 1.0f - d;
                const float a = f * f * (0.6f + 0.4f * f);   // núcleo lleno, cola suave (bloom real)
                glowSprite.setPixelAt (x, y, juce::Colours::white.withAlpha (a));
            }
    }
    // Tira vertical (LA LÁMINA DE CRISTAL): perfil horizontal con núcleo brillante y borde
    // suave × perfil vertical que brilla en la PUNTA (lejos del horizonte) y se funde hacia la
    // base (donde toca el horizonte de eventos) — la lámina "nace" del horizonte y se suspende.
    {
        constexpr int W = 24, H = 256;
        shardSprite = juce::Image (juce::Image::ARGB, W, H, true);
        for (int y = 0; y < H; ++y)
        {
            const float v  = ((float) y + 0.5f) / (float) H;             // 0 = punta, 1 = base (horizonte)
            const float vp = (0.40f + 0.60f * std::pow (1.0f - v, 1.2f)) // más luz en la punta…
                           * (v < 0.06f ? v / 0.06f : 1.0f);             // …con el extremo fundido (sin tope duro)
            for (int x = 0; x < W; ++x)
            {
                const float hN = std::abs (((float) x + 0.5f) / (float) W - 0.5f) * 2.0f;   // 0 centro → 1 borde
                const float hp = std::pow (1.0f - hN, 2.0f);                          // núcleo fino, velo ancho
                shardSprite.setPixelAt (x, y, juce::Colours::white.withAlpha (clamp01 (vp * hp)));
            }
        }
    }
}

void HorizonField::blitGlow (juce::Graphics& g, float cx, float cy, float radius, juce::Colour tint) const
{
    if (glowSprite.isNull() || radius <= 0.5f) return;
    const float s = radius * 2.0f / (float) glowSprite.getWidth();
    g.setColour (tint);
    g.drawImageTransformed (glowSprite,
        juce::AffineTransform::scale (s).translated (cx - radius, cy - radius), /*fillAlphaChannel*/ true);
}

void HorizonField::blitShard (juce::Graphics& g, float cx, float yTop, float width, float height, juce::Colour tint) const
{
    if (shardSprite.isNull() || width <= 0.5f || height <= 0.5f) return;
    const float sx = width  / (float) shardSprite.getWidth();
    const float sy = height / (float) shardSprite.getHeight();
    g.setColour (tint);
    g.drawImageTransformed (shardSprite,
        juce::AffineTransform::scale (sx, sy).translated (cx - width * 0.5f, yTop), /*fillAlphaChannel*/ true);
}

// Tinte del cristal: verde del sello (Espectral) SIEMPRE. Graves = verde profundo,
// aire = menta más clara/brillante (el degradé natural del espectro). Familia fija.
juce::Colour HorizonField::bandTint (float u, float alpha) const noexcept
{
    const juce::Colour deep   = th::greenD.darker (0.15f);
    const juce::Colour bright = th::green.brighter (0.10f);
    return deep.interpolatedWith (bright, clamp01 (u)).withAlpha (clamp01 (alpha));
}

// --------------------------------------------------- geometría de una lámina (pura, DRY)
// Alpha efectivo: presencia(Mix) × dim(Duck·duckEnv) × cristalización(Freeze) × parpadeo(Whisper)
// × nivel(espectro·gate) + un toque del barrido del gate. Es la MISMA fórmula que paintLive.
float HorizonField::bandAlpha01 (int band) const noexcept
{
    const int b = juce::jlimit (0, kBands - 1, band);
    const float u   = ((float) b + 0.5f) / (float) kBands;
    const float lvl = drawLvl[(size_t) b];

    const float wetPresence = juce::jmap (mixSm, 0.0f, 1.0f, 0.42f, 1.0f);
    const float duckDim     = 1.0f - 0.34f * duckSm * duckEnvSm;
    const float crystal     = kFreezeFloor + (1.0f - kFreezeFloor) * freezeSm;

    const float trem    = std::sin (twinklePhase * (0.8f + 0.6f * u) + bandSeed[(size_t) b]);
    const float flicker = 1.0f - whisperSm * 0.30f * (0.5f + 0.5f * trem);

    float du = std::abs (u - gatePhaseSm);
    du = juce::jmin (du, 1.0f - du);
    const float sweep = rateSm * (1.0f - clamp01 (du * 4.0f)) * gateAmpSm;

    return clamp01 (wetPresence * duckDim * crystal * flicker * (0.26f + 0.66f * lvl) + sweep * 0.18f);
}

// X dibujado (fracción 0..1 del ancho útil): u (log-f) + abanico del SPREAD hacia los bordes +
// jitter del WHISPER. La banda central (u=0.5) no se abre; las extremas se separan con SPREAD.
float HorizonField::bandXFrac (int band) const noexcept
{
    const int b = juce::jlimit (0, kBands - 1, band);
    const float u   = ((float) b + 0.5f) / (float) kBands;
    const float fan = (u - 0.5f) * 2.0f * (0.55f * spreadSm);    // −fanK..+fanK
    const float trem = std::sin (twinklePhase * (0.8f + 0.6f * u) + bandSeed[(size_t) b]);
    const float jitterFrac = trem * whisperSm * 0.012f;          // ±~1.2% del ancho a whisper 100
    return clamp01 (u + fan * 0.5f + jitterFrac);
}

float HorizonField::dbgBandAlpha (int band) const noexcept { return bandAlpha01 (band); }
float HorizonField::dbgBandX     (int band) const noexcept { return bandXFrac   (band); }

// ------------------------------------------------------------------------- renderStatic
// La capa baked (UNA vez) vive en HorizonFieldStatic.h: pozo/horizonte de alto contraste,
// ambiente verde, banda luminosa, polvo estelar (LCG), línea de horizonte + marcas L/R + eje,
// scanlines + viñeta + rim-shadow que hunde el campo en el chasis.
void HorizonField::renderStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    detail::paintFieldStatic   (g, w, h);
    detail::paintFieldOverlays (g, w, h);
}

// ---------------------------------------------------------------------------- paintLive
void HorizonField::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    const float horizon = (float) h * kHorizonYFrac;
    const float x0      = kEdgePad, x1 = (float) w - kEdgePad;
    const float span    = x1 - x0;
    const float cx      = (float) w * 0.5f;
    const float maxHalf = (float) h * kMaxHalfFrac;

    // Presencia/cristalización para el ancla central (los helpers re-derivan lo per-banda).
    const float wetPresence = juce::jmap (mixSm, 0.0f, 1.0f, 0.42f, 1.0f);
    const float crystal     = kFreezeFloor + (1.0f - kFreezeFloor) * freezeSm;

    for (int b = 0; b < kBands; ++b)
    {
        const float u   = ((float) b + 0.5f) / (float) kBands;        // 0 = graves, 1 = aire
        const float lvl = drawLvl[(size_t) b];

        // Posición X (fracción 0..1) y alpha vía los MISMOS helpers que los tests (DRY): captura
        // SPREAD (abanico) + WHISPER (jitter) en X; freeze·gate·whisper·duck·mix en alpha.
        const float xj = juce::jlimit (x0, x1, x0 + bandXFrac (b) * span);
        const float a  = bandAlpha01 (b);
        if (a < 0.004f) continue;

        // Barrido del gate: la lámina más cerca de la cresta del latido brilla más (el latido
        // "recorre" el espectro). Sólo cuando el RATE está vivo (rateSm > 0).
        float du = std::abs (u - gatePhaseSm);
        du = juce::jmin (du, 1.0f - du);                              // distancia circular
        const float sweep = rateSm * (1.0f - clamp01 (du * 4.0f)) * gateAmpSm;

        const float colW = juce::jmax (3.0f, span / (float) kBands * 0.46f);
        const float halfH = maxHalf * (0.14f + 0.80f * lvl);

        // ESPEJO arriba/abajo del horizonte = el espectro SUSPENDIDO (cristal flotando).
        const juce::Colour tint = bandTint (u, a);
        // Velo ancho (irradia) + núcleo fino brillante = bloom real con contraste.
        blitShard (g, xj, horizon - halfH, colW * 2.8f, halfH, bandTint (u, a * 0.28f));   // arriba (velo)
        blitShard (g, xj, horizon - halfH, colW,        halfH, tint);                       // arriba (núcleo)
        // abajo = la lámina espejada (cristal completo). El sprite se voltea con escala −Y.
        {
            const float sx = colW / (float) shardSprite.getWidth();
            const float sy = halfH / (float) shardSprite.getHeight();
            g.setColour (bandTint (u, a * 0.28f));
            g.drawImageTransformed (shardSprite,
                juce::AffineTransform::scale (colW * 2.8f / (float) shardSprite.getWidth(), -sy)
                    .translated (xj - colW * 2.8f * 0.5f, horizon + halfH), true);
            g.setColour (tint);
            g.drawImageTransformed (shardSprite,
                juce::AffineTransform::scale (sx, -sy).translated (xj - colW * 0.5f, horizon + halfH), true);
        }

        // Cabeza de la lámina: floración (glow) + chispa caliente arriba (el ojo la agarra).
        const float headGlow = colW * (1.4f + 1.4f * lvl) * (1.0f + 0.5f * sweep);
        blitGlow (g, xj, horizon - halfH * 0.96f, headGlow, bandTint (u, a * (0.45f + 0.40f * sweep)).brighter (0.2f));
        g.setColour (juce::Colours::white.withAlpha (clamp01 (a * (0.22f + 0.50f * lvl + 0.4f * sweep))));
        g.fillEllipse (xj - 1.3f, horizon - halfH * 0.96f - 1.3f, 2.6f, 2.6f);

        // Brillo donde la lámina toca el horizonte (la banda "ancla" ahí).
        blitGlow (g, xj, horizon, colW * 0.8f, bandTint (u, a * 0.30f));
    }

    // Ancla central del horizonte: un faro tenue que late con el gate (el corazón del freeze).
    if (freezeSm > 0.02f)
    {
        const float coreA = wetPresence * crystal * (0.06f + 0.16f * gateAmpSm);
        blitGlow (g, cx, horizon, 22.0f + 26.0f * gateAmpSm, th::greenD.withAlpha (coreA));
    }

    // Velo de presencia global ∝ Mix (el wet se siente en todo el campo, sutil).
    if (mixSm > 0.02f)
    {
        g.setColour (th::greenD.withAlpha (0.04f * wetPresence));
        g.fillRect (0.0f, 0.0f, (float) w, (float) h);
    }

    // Telemetría honesta en las 4 esquinas (valores REALES de los atomics).
    paintTelemetry (g, w, h);
}

// ------------------------------------------------------------------- telemetría honesta
// Las 4 esquinas del mockup (§tele): estado FREEZE/LIVE, SPREAD, RATE, láminas, DUCK, balance
// L/R. Strings cacheadas (refrescadas ~cada 0.2 s en advanceFrame) con valores del DSP real.
void HorizonField::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = ovni::ui::fonts::mono (8.0f).withExtraKerningFactor (0.12f);
    const int lh = 12;
    const int colW = 150;
    auto line = [&g, &lab] (const juce::String& s, int x, int y, int wd, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, wd, 11, right ? juce::Justification::centredRight
                                            : juce::Justification::centredLeft);
    };

    // TL: cabecera + estado (FROZEN colorea — es el gesto central).
    line (juce::String::fromUTF8 ("HORIZON // STFT-\xce\x94"), 12, 10, colW, false, th::fnt);
    line (teleState, 12, 10 + lh, colW, false, teleFrozen ? th::greenD : th::mut);
    // TR: SPREAD + RATE.
    line ("SPREAD " + teleSpread, w - colW - 12, 10,      colW, true, th::mut);
    line ("RATE "   + teleRate,   w - colW - 12, 10 + lh, colW, true, th::mut);
    // BL: PDC real + DUCK (el duck COLOREA cuando reacciona). ("LAYERS 24" era el conteo de bandas
    // del VISUALIZADOR disfrazado de telemetría — un número muerto; la latencia es el dato honesto
    // que el Manifiesto #3 pide publicar.)
    line (telePdc, 12, h - 8 - 2 * lh, colW, false, th::mut);
    g.setColour (duckEnvSm > 0.02f ? th::greenD : th::mut);
    g.setFont (lab);
    g.drawText ("DUCK " + teleDuck, 12, h - 8 - lh, colW, 11, juce::Justification::centredLeft);
    // BR: balance L/R (derivado del abanico REAL de las láminas).
    line (teleBalance, w - colW - 12, h - 8 - lh, colW, true, th::mut);
}

// Refresca las strings de telemetría (valores REALES de los atomics suavizados). Llamado cada
// ~0.2 s desde advanceFrame (mockup §tele). Balance L/R derivado del abanico REAL de las láminas.
void HorizonField::refreshTelemetry()
{
    teleFrozen = freezeSm > 0.5f;
    teleState  = teleFrozen ? "FROZEN" : "LIVE";
    teleSpread = juce::String (juce::roundToInt (spreadSm * 100.0f)).paddedLeft ('0', 3) + "%";
    // RATE: 0 = OFF; si no, su valor en Hz (rateSm es 0..1 sobre 0–8 Hz → des-normalizo).
    const float rateHz = rateSm * 8.0f;
    teleRate = rateHz < 0.05f ? juce::String ("OFF")
             : rateHz < 1.0f  ? juce::String (rateHz, 2) + " Hz"
                              : juce::String (rateHz, 1) + " Hz";
    teleDuck = juce::String (duckEnvSm, 2);
    telePdc  = "PDC " + juce::String (pdcProvider ? pdcProvider() : 0) + " SMP";

    // Balance L/R: energía dibujada a cada lado del eje central, según el abanico REAL del SPREAD.
    float eL = 0.0f, eR = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const float x = bandXFrac (b);            // 0..1 del ancho útil (captura SPREAD + jitter)
        if (x < 0.5f) eL += drawLvl[(size_t) b];
        else          eR += drawLvl[(size_t) b];
    }
    const float tot = juce::jmax (1.0e-4f, eL + eR);
    teleBalance = "L " + juce::String (eL / tot, 2) + juce::String::fromUTF8 (" \xc2\xb7 R ")
                + juce::String (eR / tot, 2);
}

// -------------------------------------------------------------------------- advanceFrame
bool HorizonField::advanceFrame()
{
    // --- leer telemetría ------------------------------------------------------------------------
    const float whisperNow = whisperA.load  (std::memory_order_relaxed);
    const float spreadNow  = spreadA.load   (std::memory_order_relaxed);
    const float duckNow    = duckA.load     (std::memory_order_relaxed);
    const float mixNow     = mixA.load      (std::memory_order_relaxed);
    const float freezeNow  = freezeA.load   (std::memory_order_relaxed);
    const float gateNow    = gateAmpA.load  (std::memory_order_relaxed);
    const float phaseNow   = gatePhaseA.load (std::memory_order_relaxed);
    const float duckEnvNow = duckEnvA.load  (std::memory_order_relaxed);
    const float rateNow    = rateNormA.load (std::memory_order_relaxed);

    const float gatePrev = gateAmpSm;

    // De-zipper visual (one-pole, POST-mapeo). El gate responde más rápido (es el latido —
    // si se alisa demasiado, no se VE pulsar).
    whisperSm += (whisperNow - whisperSm) * kSmoothK;
    spreadSm  += (spreadNow  - spreadSm)  * kSmoothK;
    duckSm    += (duckNow    - duckSm)    * kSmoothK;
    mixSm     += (mixNow     - mixSm)     * kSmoothK;
    freezeSm  += (freezeNow  - freezeSm)  * kSmoothK;
    duckEnvSm += (duckEnvNow - duckEnvSm) * 0.42f;
    rateSm    += (rateNow    - rateSm)    * kSmoothK;
    gateAmpSm += (gateNow    - gateAmpSm) * kGateK;

    // La fase del gate puede dar la vuelta (0→1); seguirla por el camino corto (sin saltos).
    {
        float d = phaseNow - gatePhaseSm;
        if (d >  0.5f) d -= 1.0f;
        if (d < -0.5f) d += 1.0f;
        gatePhaseSm += d * 0.5f;
        if (gatePhaseSm < 0.0f) gatePhaseSm += 1.0f;
        if (gatePhaseSm > 1.0f) gatePhaseSm -= 1.0f;
    }

    // Tiembleo del cristal: avanza despacio; con WHISPER y/o RATE se acelera (el cristal
    // "vibra"). Sólo modula alpha/jitter (compositor-friendly).
    twinklePhase += 0.04f + 0.20f * whisperSm + 0.14f * rateSm;
    if (twinklePhase > 1.0e6f)
        twinklePhase = std::fmod (twinklePhase, juce::MathConstants<float>::twoPi);

    // --- nivel por banda: espectro × gate(latido) × cristalización(freeze), con piso de luz ----
    // gateVisible: el latido SIEMPRE deja un piso bajo (la lámina no desaparece del todo al cerrar
    // si está congelada — el pad no parpadea a negro), pero cerrado se AGACHA fuerte (se VE pulsar).
    const float gateVisible = 0.10f + 0.90f * clamp01 (gateAmpSm);
    const float crystalLvl  = kFreezeFloor + (1.0f - kFreezeFloor) * freezeSm;   // freeze sube el cristal

    float maxE = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const size_t i = (size_t) b;
        const float eNow = spectrumA[i].load (std::memory_order_relaxed);
        eSm[i] += (eNow - eSm[i]) * kEnergyK;
        maxE = juce::jmax (maxE, eSm[i]);

        // Nivel perceptual del espectro (√ para que las bandas chicas se LEAN) con piso de luz
        // en arco suave (legible a 320 px), todo escalado por gate × cristalización.
        const float sig = clamp01 (std::sqrt (juce::jmax (0.0f, eSm[i])));
        const float arc = kIdleFloor + 0.10f * std::sin (((float) b + 0.5f) / (float) kBands
                                                          * juce::MathConstants<float>::pi);
        const float base = juce::jmax (arc, sig);
        drawLvl[i] = clamp01 (base * gateVisible * crystalLvl);
    }

    // --- telemetría honesta: refresca strings cada ~0.2 s (6 frames a 30 fps), como el mockup ---
    if (--teleCountdown <= 0)
    {
        teleCountdown = 6;
        refreshTelemetry();
    }

    // --- ¿repintar? macros moviéndose, el gate latiendo, energía viva o el cristal vibrando. ----
    const bool macroMoved = std::abs (whisperNow - whisperSm) > 5.0e-4f || std::abs (spreadNow - spreadSm) > 5.0e-4f
                         || std::abs (duckNow - duckSm) > 5.0e-4f || std::abs (mixNow - mixSm) > 5.0e-4f
                         || std::abs (freezeNow - freezeSm) > 5.0e-4f;
    const bool beating  = std::abs (gateNow - gatePrev) > 2.0e-4f || duckEnvSm > 0.01f
                       || (rateSm > 0.01f && freezeSm > 0.02f);   // con RATE+freeze el latido recorre el espectro
    const bool living   = maxE > 1.0e-4f || (mixSm > 0.02f && whisperSm > 0.02f);  // el cristal vibra con whisper
    return macroMoved || beating || living;
}

} // namespace horizon::ui
