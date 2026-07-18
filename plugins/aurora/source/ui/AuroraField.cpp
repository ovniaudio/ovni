#include "AuroraField.h"
#include "AuroraFieldStatic.h"
#include "ui-kit/Fonts.h"
#include <cmath>

namespace aurora::ui
{

namespace th = ovni::ui::theme;
namespace fonts = ovni::ui::fonts;

// ---- constantes del dibujo (anti-magic-number) ------------------------------------------------
namespace
{
    // Réplica UI de la ley del motor (AuroraEngine.cpp): MISMOS números, comentados allá.
    // (NO TOCAR sin re-correr DiccionarioTest: el idle-law se prueba por bounds.)
    constexpr float kFLoHz        = 40.0f;      // rango log-f del despliegue (40 Hz → 16 kHz)
    constexpr float kFHiHz        = 16000.0f;
    constexpr float kWeaveCycles  = 2.75f;      // alternancias L↔R del serpenteo
    constexpr float kMonoSafeLoHz = 60.0f;      // corte de colapso-al-centro 60→700 Hz log
    constexpr float kMonoSafeHiHz = 700.0f;
    constexpr float kOpenKnee     = 0.55f;      // knee de la envolvente de apertura e(u,s)
    constexpr float kOpenKneeDrop = 0.45f;      // cuánto BAJA el knee con tilt>0 (abre antes)
    constexpr float kOpenPowDrop  = 0.4f;       // convexidad extra con tilt>0
    constexpr float kTiltDrive     = 0.5f;      // tilt>0 también EMPUJA a los bordes (re-curva QA, = motor)
    constexpr float kTiltDriveLoHz = 350.0f;    // rampa log del drive: 0 bajo 350 Hz → 1 sobre 1.4 kHz (= motor)
    constexpr float kTiltDriveHiHz = 1400.0f;

    // Mapeo energía→nivel/posición.
    constexpr float kSmoothK     = 0.18f;       // one-pole visual de macros (igual que HaloRings)
    constexpr float kEnergyK     = 0.35f;       // la energía responde más rápido (la aurora "enciende")
    constexpr float kAudioWeight = 200.0f;      // amp ~0.005 (−46 dBFS) → posición 100% del motor
    constexpr float kLvlGain     = 8.0f;        // amp → nivel perceptual: lvl = √(amp·kLvlGain)
    constexpr float kIdleFloor   = 0.22f;       // piso de luz del nivel (legible a 320 px, nunca apagado)

    inline float clamp01 (float v) noexcept { return juce::jlimit (0.0f, 1.0f, v); }

    // Saturador suave del ángulo (C1, identidad ≤0.75, codo tanh) — idéntico al motor.
    inline float softLimitTheta (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= 0.75f) return x;
        const float y = 0.75f + 0.25f * std::tanh ((a - 0.75f) / 0.25f);
        return x < 0.0f ? -y : y;
    }

    // e(u, s): envolvente de apertura (cuánto se va AL BORDE la posición espectral u con
    // fuerza s). Idéntica a AuroraEngine::openShape.
    inline float openShape (float u, float s) noexcept
    {
        const float knee = kOpenKnee - kOpenKneeDrop * s;
        const float v    = clamp01 (u / knee);
        return std::pow (v, 1.0f - kOpenPowDrop * s);
    }
}

AuroraField::AuroraField (std::atomic<float>& spread, std::atomic<float>& tilt,
                          std::atomic<float>& motion, std::atomic<float>& monoSafe,
                          std::atomic<float>& duck, std::atomic<float>& mix,
                          std::atomic<float>& gamma, std::atomic<float>& duckEnv,
                          std::atomic<float>& rateNorm,
                          std::array<std::atomic<float>, kBands>& bandEnergy,
                          std::array<std::atomic<float>, kBands>& bandPos)
    : ovni::ui::VisualizerBase (30),
      spreadSrc (spread), tiltSrc (tilt), motionSrc (motion), monoSafeSrc (monoSafe),
      duckSrc (duck), mixSrc (mix), gammaSrc (gamma), duckEnvSrc (duckEnv),
      rateNormSrc (rateNorm), bandEnergySrc (bandEnergy), bandPosSrc (bandPos)
{
    setSettleHold (60);   // tras quietud, el latido termina de asentarse y la base pausa (CPU ~0)

    // Estado inicial desde los atomics (primer frame coherente — clave para reduced-motion
    // y para el snapshot, que no corren el timer).
    spreadSm  = spreadSrc.load   (std::memory_order_relaxed);
    tiltSm    = tiltSrc.load     (std::memory_order_relaxed);
    motionSm  = motionSrc.load   (std::memory_order_relaxed);
    msSm      = monoSafeSrc.load (std::memory_order_relaxed);
    duckSm    = duckSrc.load     (std::memory_order_relaxed);
    mixSm     = mixSrc.load      (std::memory_order_relaxed);
    gammaSm   = gammaSrc.load    (std::memory_order_relaxed);
    duckEnvSm = duckEnvSrc.load  (std::memory_order_relaxed);
    rateSm    = rateNormSrc.load (std::memory_order_relaxed);

    // Semillas de titileo/serpenteo FIJAS → snapshot/test reproducibles (patrón HaloRings).
    juce::Random rng (0x4a55);
    for (int b = 0; b < kBands; ++b)
        bandSeed[(size_t) b] = rng.nextFloat() * juce::MathConstants<float>::twoPi;

    ensureSprites();
    advanceFrame();        // computa drawPos/drawLvl YA (el primer paint muestra la aurora desplegada)
    refreshTelemetry();
}

// ------------------------------------------------------------------- ley del motor (idle)
// (NO TOCAR — DiccionarioTest prueba que SPREAD/γ abre, TILT reordena, MONO SAFE amarra por bounds.)
float AuroraField::idlePosFor (float u, float gamma, float tilt, float monoSafe01) const noexcept
{
    // Envolvente de apertura con morph continuo al espejo (tilt<0 = agudos al centro).
    float e;
    if (tilt >= 0.0f)
    {
        // Knee + drive hacia los bordes band-limitado (rampa log 350 Hz→1.4 kHz, = motor).
        const float fU    = kFLoHz * std::pow (kFHiHz / kFLoHz, u);
        const float ramp  = clamp01 (std::log2 (fU / kTiltDriveLoHz)
                                     / std::log2 (kTiltDriveHiHz / kTiltDriveLoHz));
        e = openShape (u, tilt) * (1.0f + kTiltDrive * tilt * ramp);
    }
    else
        e = (1.0f + tilt) * openShape (u, 0.0f) + (-tilt) * openShape (1.0f - u, 0.0f);

    // Serpenteo nominal (−sin → la octava más alta cae al borde DERECHO, como el motor).
    const float weave = -std::sin (juce::MathConstants<float>::twoPi * kWeaveCycles * u);

    float pos = gamma * e * weave;

    // MONO SAFE: colapso al centro bajo el corte (60→700 Hz log), transición de UNA octava.
    const float ms = clamp01 (monoSafe01);
    if (ms > 0.0f)
    {
        const float cutHz    = kMonoSafeLoHz * std::pow (kMonoSafeHiHz / kMonoSafeLoHz, ms);
        const float fBand    = kFLoHz * std::pow (kFHiHz / kFLoHz, u);
        const float m        = clamp01 (std::log2 (fBand) - (std::log2 (cutHz) - 0.5f));
        const float strength = clamp01 (ms * 50.0f);   // fade-in en el primer 2% del knob (como el motor)
        pos *= 1.0f - strength * (1.0f - m);
    }
    return softLimitTheta (pos);   // saturación suave (= motor)
}

// ------------------------------------------------------------------------------ sprites
// Patrón HaloRings/StellarPad: blancos con el alpha embebido, horneados UNA vez; el blit los
// tiñe (setColour + fillAlphaChannel) y escala → floración real sin gradientes por frame.
void AuroraField::ensureSprites()
{
    // Disco radial (cabeza de cortina / pie / faros): núcleo lleno, cola suave.
    {
        constexpr int S = 128;
        glowSprite = juce::Image (juce::Image::ARGB, S, S, true);
        const float c = S * 0.5f;
        for (int y = 0; y < S; ++y)
            for (int x = 0; x < S; ++x)
            {
                const float d = juce::jmin (1.0f, std::hypot ((float) x + 0.5f - c, (float) y + 0.5f - c) / c);
                const float f = 1.0f - d;
                const float a = f * f * (0.6f + 0.4f * f);
                glowSprite.setPixelAt (x, y, juce::Colours::white.withAlpha (a));
            }
    }
    // Tira vertical (LA CORTINA): perfil horizontal con núcleo brillante y borde suave ×
    // perfil vertical que se FUNDE en la cabeza (arriba), brilla en el cuerpo y vuelve a
    // encenderse en el PIE (donde toca el horizonte). = mockup grad de la cortina.
    {
        constexpr int W = 32, H = 256;
        curtainSprite = juce::Image (juce::Image::ARGB, W, H, true);
        for (int y = 0; y < H; ++y)
        {
            const float v  = ((float) y + 0.5f) / (float) H;             // 0 = cabeza, 1 = pie
            // mockup stops: 0→0, 0.18→0.04, 0.55→0.12, 0.88→0.34, 1→0.06 (cuerpo translúcido, pie brillante)
            float vp;
            if      (v < 0.18f) vp = juce::jmap (v, 0.0f,  0.18f, 0.00f, 0.18f);
            else if (v < 0.55f) vp = juce::jmap (v, 0.18f, 0.55f, 0.18f, 0.42f);
            else if (v < 0.88f) vp = juce::jmap (v, 0.55f, 0.88f, 0.42f, 1.00f);
            else                vp = juce::jmap (v, 0.88f, 1.0f,  1.00f, 0.30f);
            for (int x = 0; x < W; ++x)
            {
                const float hN = std::abs (((float) x + 0.5f) / (float) W - 0.5f) * 2.0f;   // 0 centro → 1 borde
                const float hp = std::pow (1.0f - hN, 2.2f);                          // núcleo fino, velo ancho
                curtainSprite.setPixelAt (x, y, juce::Colours::white.withAlpha (clamp01 (vp * hp)));
            }
        }
    }
}

void AuroraField::blitGlow (juce::Graphics& g, float cx, float cy, float radius, juce::Colour tint) const
{
    if (glowSprite.isNull() || radius <= 0.5f) return;
    const float s = radius * 2.0f / (float) glowSprite.getWidth();
    g.setColour (tint);
    g.drawImageTransformed (glowSprite,
        juce::AffineTransform::scale (s).translated (cx - radius, cy - radius), /*fillAlphaChannel*/ true);
}

void AuroraField::blitCurtain (juce::Graphics& g, float cx, float yTop, float width, float height, juce::Colour tint) const
{
    if (curtainSprite.isNull() || width <= 0.5f || height <= 0.5f) return;
    const float sx = width  / (float) curtainSprite.getWidth();
    const float sy = height / (float) curtainSprite.getHeight();
    g.setColour (tint);
    g.drawImageTransformed (curtainSprite,
        juce::AffineTransform::scale (sx, sy).translated (cx - width * 0.5f, yTop), /*fillAlphaChannel*/ true);
}

// ------------------------------------------------------------------------- renderStatic
void AuroraField::renderStatic (juce::Graphics& g, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    detail::paintFieldStatic   (g, w, h);
    detail::paintFieldOverlays (g, w, h);
}

// --------------------------------------------------------------------------- una cortina
// Cuerpo translúcido (velo ancho + núcleo) que nace del horizonte, con leve serpenteo
// horizontal (sólo transform: compositor-friendly), filamento central y pie con bloom.
void AuroraField::paintCurtain (juce::Graphics& g, float x, float baseY, float colH,
                                float colW, float lvl, float hue, float alpha, float phaseB) const
{
    const float yTop = baseY - colH;
    const juce::Colour tint = detail::curtainColour (hue, alpha);

    // Serpenteo: la cabeza se mece, el pie queda anclado al horizonte (sway ∝ nivel).
    // Sólo transform (cizalla) → compositor-friendly, sin recomputar geometría por píxel.
    const float sway  = (4.0f + lvl * 8.0f) * std::sin (phaseB);
    const float xHead = x + sway;

    // Velo ancho + núcleo fino, inclinados cabeza→pie. La transform se arma con la matriz 2×3
    // EXPLÍCITA (constructor de 6 floats — API universal, sin depender de helpers de cizalla):
    // mapea el sprite recto (W×H) de modo que el PIE (py=H) caiga centrado en (x, baseY) y la
    // CABEZA (py=0) se vaya a xHead. Derivación en el comentario de cada coeficiente.
    const float spW  = (float) curtainSprite.getWidth();
    const float spH  = (float) curtainSprite.getHeight();
    const float lean = xHead - x;                       // desplazamiento horizontal de la cabeza
    auto blitLeaning = [&] (float width, juce::Colour c)
    {
        if (curtainSprite.isNull() || width <= 0.5f || colH <= 0.5f || spH <= 0.0f) return;
        const float sxw = width / spW;                  // escala horizontal
        g.setColour (c);
        const juce::AffineTransform t (
            /*mat00*/ sxw,            /*mat01*/ -lean / spH, /*mat02*/ x + lean - width * 0.5f,
            /*mat10*/ 0.0f,          /*mat11*/ colH / spH,  /*mat12*/ yTop);
        g.drawImageTransformed (curtainSprite, t, /*fillAlphaChannel*/ true);
    };

    blitLeaning (colW * 3.0f, tint.withMultipliedAlpha (0.32f));   // velo
    blitLeaning (colW,        tint);                                // núcleo

    // Filamento central brillante (la línea viva de la cortina).
    g.setColour (detail::curtainColour (hue, clamp01 (alpha * 0.55f)).brighter (0.10f));
    g.drawLine (xHead, yTop + colH * 0.08f, x, baseY, 1.4f);

    // Cabeza: floración + chispa caliente (el ojo la agarra).
    blitGlow (g, xHead, yTop + colH * 0.10f, colW * (1.4f + 1.2f * lvl),
              detail::curtainColour (hue, clamp01 (alpha * 0.5f)).brighter (0.2f));
    g.setColour (juce::Colours::white.withAlpha (clamp01 (alpha * (0.22f + 0.5f * lvl))));
    g.fillEllipse (xHead - 1.4f, yTop + colH * 0.10f - 1.4f, 2.8f, 2.8f);

    // Pie: bloom donde la cortina toca el horizonte (la banda "aterriza" ahí, mockup §pie).
    blitGlow (g, x, baseY, colW * (0.9f + 0.8f * lvl), detail::curtainColour (hue, clamp01 (alpha * 0.45f)));
}

// ------------------------------------------------------------------------- reflejo (charco)
// La presencia (Mix) entra en el ALPHA del gradiente: un gradient-fill ignora g.setOpacity(),
// así que el atenuado por Mix se aplica acá directo (no con setOpacity, que sería inerte).
void AuroraField::paintReflection (juce::Graphics& g, int w, int h, float baseY, float presence) const
{
    const float cx    = (float) w * 0.5f;
    const float span  = (float) w * detail::kAxisHalfFr;
    const float poolH = ((float) h - baseY);
    for (int b = 0; b < kBands; ++b)
    {
        const float pos = drawPos[(size_t) b];
        const float lvl = drawLvl[(size_t) b];
        const float u   = ((float) b + 0.5f) / (float) kBands;
        const float x   = cx + pos * span;
        const float refH = poolH * 0.5f * lvl;
        if (refH < 1.0f) continue;
        juce::ColourGradient rg (detail::curtainColour (u, 0.10f * lvl * presence), 0.0f, baseY,
                                 detail::curtainColour (u, 0.0f),                   0.0f, baseY + refH, false);
        g.setGradientFill (rg);
        const float wRef = 4.0f + lvl * 10.0f;
        g.fillRect (x - wRef * 0.5f, baseY, wRef, refH);
    }
}

// ---------------------------------------------------------------------------- paintLive
void AuroraField::paintLive (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    const float cx       = (float) w * 0.5f;
    const float baseY    = detail::horizonY (h);                    // horizonte (y≈0.70)
    const float halfSpan = (float) w * detail::kAxisHalfFr;         // px de azimut ±1
    const float usableH  = baseY - (float) h * 0.12f;               // alto máximo de una cortina (top≈0.12)

    // Presencia (Mix) con PISO DE LUZ (a Mix 0 la aurora se atenúa pero se LEE — gancho del
    // Reel a 320 px) + cierre del DUCK (la pegada del dry atenúa el brillo: se VE el campo
    // apartarse, no sólo cerrarse).
    const float wetPresence = juce::jmap (mixSm, 0.0f, 1.0f, 0.45f, 1.0f);
    const float duckDim     = 1.0f - 0.30f * duckSm * duckEnvSm;

    // El charco-reflejo bajo el horizonte (sutil; primero, queda detrás de las cortinas).
    paintReflection (g, w, h, baseY, 0.85f * wetPresence);

    // Amarre del MONO SAFE: un faro central tenue (el "ancla" de los graves). Crece con el
    // knob → el control SE VE aunque no haya graves sonando en ese instante.
    if (msSm > 0.01f)
    {
        const float tetherH = usableH * (0.25f + 0.30f * msSm);
        const float tetherA = wetPresence * (0.05f + 0.12f * msSm);
        blitCurtain (g, cx, baseY - tetherH, 6.0f, tetherH, detail::curtainColour (0.0f, tetherA));
        blitGlow (g, cx, baseY, 10.0f + 8.0f * msSm, detail::curtainColour (0.0f, tetherA * 1.4f));
    }

    // LAS CORTINAS: una por banda, en SU posición de azimut (el espectro desplegado). Los velos
    // translúcidos (sprite con núcleo brillante + velo ancho) se apilan con alpha-over → donde se
    // cruzan suman luz (la "tela" de la aurora), sin recomputar gradientes por frame.
    const float colW = juce::jmax (3.0f, (float) w / (float) kBands * 0.42f);
    for (int b = 0; b < kBands; ++b)
    {
        const float u    = ((float) b + 0.5f) / (float) kBands;
        const float pos  = drawPos[(size_t) b];
        const float lvl  = drawLvl[(size_t) b];
        const float x    = cx + pos * halfSpan;

        const float phaseB  = twinklePhase * (0.7f + 0.5f * u) + bandSeed[(size_t) b];
        const float twinkle = 0.88f + 0.12f * std::sin (phaseB);
        const float a       = clamp01 (wetPresence * duckDim * (0.30f + 0.62f * lvl) * twinkle);
        if (a < 0.004f) continue;

        const float colH = usableH * (0.16f + 0.78f * lvl);
        paintCurtain (g, x, baseY, colH, colW, lvl, u, a, phaseB);
    }

    // Velo de presencia global ∝ Mix (el wet se siente en todo el campo, sutil).
    if (mixSm > 0.02f)
    {
        g.setColour (th::greenD.withAlpha (0.04f * wetPresence));
        g.fillRect (0.0f, 0.0f, (float) w, (float) h);
    }

    // Telemetría honesta en las 4 esquinas (sobre TODO; valores reales de los atomics).
    paintTelemetry (g, w, h);
}

// --------------------------------------------------------------------------- telemetría
void AuroraField::paintTelemetry (juce::Graphics& g, int w, int h) const
{
    const juce::Font lab = fonts::mono (8.0f).withExtraKerningFactor (0.12f);
    const int lh = 12;
    const int colW = 150;
    auto line = [&g, &lab] (const juce::String& s, int x, int y, int wd, bool right, juce::Colour c)
    {
        g.setColour (c);
        g.setFont (lab);
        g.drawText (s, x, y, wd, 11, right ? juce::Justification::topRight : juce::Justification::topLeft);
    };

    // TL: identidad + γ (la apertura efectiva del abanico, late con MOTION).
    line (juce::String::fromUTF8 ("AURORA // STFT-\xce\x94"), 13, 10,      colW, false, th::fnt);
    line (teleGamma,                                          13, 10 + lh, colW, false, th::mut);
    // TR: SPREAD + TILT (con signo).
    line ("SPREAD " + teleSpread, w - colW - 13, 10,      colW, true, th::mut);
    line ("TILT "   + teleTilt,   w - colW - 13, 10 + lh, colW, true, th::mut);
    // BL (sobre el rail de macros, mockup bottom:122): PDC real + DUCK (el duck colorea su valor).
    // ("BANDS 24" era el conteo del VISUALIZADOR disfrazado de telemetría — el motor panea POR BIN;
    // la latencia es el dato honesto que el Manifiesto #3 pide publicar.)
    const int by = h - 122;
    line (telePdc, 13, by, colW, false, th::mut);
    g.setColour (teleDuckVal > 0.02f ? th::greenD : th::fnt);
    g.setFont (lab);
    g.drawText ("DUCK " + teleDuck, 13, by + lh, colW, 11, juce::Justification::topLeft);
    // BR: balance de energía L · R real (suma de bandas a cada lado).
    line (teleLR, w - colW - 13, by + lh, colW, true, th::mut);
}

void AuroraField::refreshTelemetry()
{
    auto pad3 = [] (int v) { return juce::String (juce::jlimit (0, 999, std::abs (v))).paddedLeft ('0', 3); };

    teleGamma  = juce::String::fromUTF8 ("\xce\xb3 ") + juce::String (clamp01 (gammaSm), 2);
    telePdc    = "PDC " + juce::String (pdcProvider ? pdcProvider() : 0) + " SMP";
    teleSpread = pad3 (juce::roundToInt (clamp01 (spreadSm) * 100.0f)) + "%";
    teleTilt   = juce::String::fromUTF8 (tiltSm >= 0.0f ? "+" : "\xe2\x88\x92")
               + pad3 (juce::roundToInt (std::abs (tiltSm) * 100.0f));
    teleDuckVal = duckEnvSm;
    teleDuck    = juce::String (clamp01 (duckEnvSm), 2);
    teleLR      = "L " + juce::String (clamp01 (teleEnergyL), 2)
                + juce::String::fromUTF8 (" \xc2\xb7 R ") + juce::String (clamp01 (teleEnergyR), 2);
}

// -------------------------------------------------------------------------- advanceFrame
bool AuroraField::advanceFrame()
{
    // --- leer telemetría ------------------------------------------------------------------------
    const float spreadNow  = spreadSrc.load   (std::memory_order_relaxed);
    const float tiltNow    = tiltSrc.load     (std::memory_order_relaxed);
    const float motionNow  = motionSrc.load   (std::memory_order_relaxed);
    const float msNow      = monoSafeSrc.load (std::memory_order_relaxed);
    const float duckNow    = duckSrc.load     (std::memory_order_relaxed);
    const float mixNow     = mixSrc.load      (std::memory_order_relaxed);
    const float gammaNow   = gammaSrc.load    (std::memory_order_relaxed);
    const float duckEnvNow = duckEnvSrc.load  (std::memory_order_relaxed);
    const float rateNow    = rateNormSrc.load (std::memory_order_relaxed);

    const float gammaPrev = gammaSm;

    // De-zipper visual (one-pole, POST-mapeo). γ y la envolvente del duck responden más
    // rápido (son el latido/la pegada — si se alisan demasiado, no se VEN).
    spreadSm  += (spreadNow  - spreadSm)  * kSmoothK;
    tiltSm    += (tiltNow    - tiltSm)    * kSmoothK;
    motionSm  += (motionNow  - motionSm)  * kSmoothK;
    msSm      += (msNow      - msSm)      * kSmoothK;
    duckSm    += (duckNow    - duckSm)    * kSmoothK;
    mixSm     += (mixNow     - mixSm)     * kSmoothK;
    rateSm    += (rateNow    - rateSm)    * kSmoothK;
    gammaSm   += (gammaNow   - gammaSm)   * 0.45f;
    duckEnvSm += (duckEnvNow - duckEnvSm) * 0.45f;

    // Titileo/serpenteo: avanza siempre despacio; con MOTION activo se acelera ∝ RATE (el
    // campo "ondula" al ritmo del abanico). Sólo modula alpha + transform (compositor-friendly).
    twinklePhase += 0.045f + 0.16f * motionSm * rateSm;
    if (twinklePhase > 1.0e6f)
        twinklePhase = std::fmod (twinklePhase, juce::MathConstants<float>::twoPi);

    // --- posiciones + niveles por banda + balance L/R --------------------------------------------
    float maxE = 0.0f;
    bool  posMoved = false;
    float eL = 0.0f, eR = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        const size_t i = (size_t) b;
        const float u  = ((float) b + 0.5f) / (float) kBands;

        const float eNow = bandEnergySrc[i].load (std::memory_order_relaxed);
        const float pNow = bandPosSrc[i].load    (std::memory_order_relaxed);
        eSm[i] += (eNow - eSm[i]) * kEnergyK;
        pSm[i] += (pNow - pSm[i]) * kEnergyK;
        maxE = juce::jmax (maxE, eSm[i]);

        // Posición: la del MOTOR cuando la banda tiene energía (honestidad: la cortina
        // está donde el bin suena); cae a la ley replicada (idle) cuando no suena nada.
        const float audioW = clamp01 (eSm[i] * kAudioWeight);
        const float idle   = idlePosFor (u, gammaSm, tiltSm, msSm);
        const float pos    = idle + (pSm[i] - idle) * audioW;
        if (std::abs (pos - drawPos[i]) > 1.0e-3f) posMoved = true;
        drawPos[i] = pos;

        // Nivel: energía perceptual (√) con PISO DE LUZ en arco suave (legible a 320 px).
        const float sig  = clamp01 (std::sqrt (juce::jmax (0.0f, eSm[i]) * kLvlGain));
        const float arc  = kIdleFloor + 0.14f * std::sin (u * juce::MathConstants<float>::pi);
        drawLvl[i] = juce::jmax (arc, sig);

        // Balance de energía honesto L vs R (ponderado por nivel dibujado).
        if (pos < 0.0f) eL += drawLvl[i]; else eR += drawLvl[i];
    }
    const float tot = juce::jmax (1.0e-4f, eL + eR);
    teleEnergyL = eL / tot;
    teleEnergyR = eR / tot;

    // telemetría honesta: refresca strings cada ~0.2 s (6 frames a 30 fps), como el mockup.
    if (--teleCountdown <= 0)
    {
        teleCountdown = 6;
        refreshTelemetry();
    }

    // --- ¿repintar? macros moviéndose, γ latiendo, posiciones migrando o energía viva. ----------
    const bool macroMoved = std::abs (spreadNow - spreadSm) > 5.0e-4f || std::abs (tiltNow - tiltSm) > 5.0e-4f
                         || std::abs (motionNow - motionSm) > 5.0e-4f || std::abs (msNow - msSm) > 5.0e-4f
                         || std::abs (duckNow - duckSm) > 5.0e-4f || std::abs (mixNow - mixSm) > 5.0e-4f;
    const bool breathing  = std::abs (gammaNow - gammaPrev) > 2.0e-4f || duckEnvSm > 0.01f;
    const bool energetic  = maxE > 1.0e-4f || mixSm > 0.02f;   // con Mix arriba el titileo vive (la aurora nunca es una foto)
    return macroMoved || breathing || posMoved || energetic;
}

} // namespace aurora::ui
