#pragma once
// ParticleParams — struct designated-init (estilo ovni::engines) que el editor arma desde el APVTS y
// pasa al renderer por frame. M0 usa intensity/chaos; el resto se activa en M2.
namespace supernova
{
struct ParticleParams
{
    float intensity    = 0.5f;   // 0..1 — energía global del sistema
    float chaos        = 0.3f;   // 0..1 — turbulencia / peso del curl noise
    float particleSize = 1.0f;   // px   — tamaño de cada partícula
    float glow         = 0.5f;   // 0..1 — intensidad del bloom (M2)
    float explode      = 0.0f;   // 0..1 — impulso radial one-shot (M2/M3)

    // Física por-preset (M3). Defaults = los magic numbers de M2 → un patch sin preset renderiza igual que M2.
    float curlScale    = 1.5f;   // frecuencia espacial del curl (0.3..2.7)
    float homeStrength = 1.4f;   // atracción al hogar / velocidad de re-armado (0.2..2.6)
    float gravity      = 0.0f;   // tiro constante en Y, +abajo/−arriba (−0.6..0.6)
    float momentum     = 0.90f;  // retención de velocidad (0.80..0.99)
    float radialGain   = 1.2f;   // fuerza del impulso radial de la explosión (0.2..2.2)
    float jitterGain   = 0.16f;  // agitación por agudos (0..0.32)
    float breatheGain  = 0.28f;  // respiración por RMS (0..0.56)

    // Rayo direccional MIDI (M3, RF5): one-shot. El renderer late en rayTrigger → pulso que decae.
    bool  rayTrigger   = false;  // dispara el rayo este frame
    float rayAngle     = 0.0f;   // rad CCW desde +X — dirección del haz

    // Filosofía de movimiento (ver ShaderSource). ELEGIDA por Joaquín (2026-07-11): 3 = FLUJO POR CONTORNOS
    // ("la imagen se recorre a sí misma") — el movimiento nace del contenido, sin epicentro fijo. Los otros
    // modos (0 radial, 1 materia, 2 onda, 4 vórtices, 5 natural) quedan para presets/poda futura.
    int   motionMode   = 3;

    // CUTOUT gradual (knob del usuario, RF8+): 0 = imagen completa, 1 = fondo borrado (escultura del sujeto).
    float cutoutAmt    = 0.0f;

    // VOCABULARIO VISUAL (diseño con Joaquín): forma del glifo (0=dot 1=disc 2=ring 3=dash 4=tri 5=quad
    // 6=spark) + estela (0 = sin memoria, →1 = los glifos dibujan caminos que se desvanecen lento).
    int   shapeMode    = 0;
    float trailAmt     = 0.0f;

    // COLOR GRADING (capa de paleta, post-ACES): saturación 0=B/N · 1=neutro · 2=vívido; hue en radianes
    // (rotación Rodrigues alrededor del eje gris). Defaults = identidad byte-exacta.
    float satAmt       = 1.0f;
    float hueShift     = 0.0f;

    // CAPA 3 · PLEXUS: conexiones entre elementos cercanos (constelaciones/redes vivas). 0 = ninguna;
    // →1 = malla densa (más radio de vecindad + más alpha de línea).
    float linksAmt     = 0.0f;

    // TIER 1 PRO (pedido "más controles, más técnicas"): todos con default = identidad byte-exacta.
    float densityAmt   = 1.0f;   // fracción de partículas dibujadas (prefijo permutado): mosaico real/puntillismo
    float scatterAmt   = 0.0f;   // dispersión de hogares 0..1: imagen ↔ nube (hash estable, re-armable)
    float speedMul     = 1.0f;   // timewarp 0.25×..4× (escala dt de la física; las estelas no cambian su decay)
    float rotateRate   = 0.0f;   // giro de VISTA en rad/s (la física queda en el espacio original)
    float pumpAmt      = 0.6f;   // sidechain visual del kick (flash de tamaño; 0.6 = comportamiento histórico)
    float hueCycleRate = 0.0f;   // deriva de tono en rad/s (se acumula y suma al HUE)
    int   kaleidoSeg   = 0;      // espejos del caleidoscopio (0/1 = off; 2/4/6/8)

    // 3D DE PRESENTACIÓN (el pedido "darlo vuelta"): la física sigue en el plano 2D probado; la profundidad
    // (almohada-SDF/luma o CoreML) y la cámara orbital entran en los VERTEX shaders. Defaults = identidad
    // byte-exacta (depth 0 + ángulos 0 → camino legacy exacto, branch por uniform).
    float depthAmt     = 0.0f;   // 0..1 — cuánto volumen (escala de z + fog + atenuación de tamaño)
    float rotXRad      = 0.0f;   // pitch de cámara (rad, ±π) — "tumbarlo"
    float rotYRad      = 0.0f;   // yaw de cámara  (rad, ±π) — "darlo vuelta"
    float orbitRate    = 0.0f;   // auto-órbita en rad/s sobre el yaw (se acumula con el dt escalado)

    // MOTOR GEOMÉTRICO (FIGURA + FORM): los hogares abandonan la imagen y se re-arman en una figura.
    // 0=Imagen (identidad) · 1=Esfera · 2=Espiral · 3=Anillos · 4=Grilla · 5=Hélice. formAmt = fader
    // imagen↔figura (morphable). Compone con TODO: los 5 motores de movimiento actúan SOBRE la figura.
    int   formMode     = 0;
    float formAmt      = 1.0f;   // el stepper decide SI hay figura; el fader decide CUÁNTO tira

    // COLOR LAB (gradient map post-ACES + papel): paletteIdx = look del banco (0 = Original, off exacto);
    // rampAmt = mix del map (el stepper decide SI, el fader CUÁNTO — mismo patrón que FIGURA/FORM);
    // bgAmt = lienzo de papel del look (screen blend; 0 = negro clásico exacto).
    int   paletteIdx   = 0;
    float rampAmt      = 1.0f;
    float bgAmt        = 0.0f;

    // ÓPTICA DE CINE (Phase A) — el "tell" de lo caro sobre el composite. Defaults 0 = IDENTIDAD byte-exacta
    // (los goldens usan ParticleParams por defecto → estas ramas del shader nunca se encienden). El producto
    // en vivo (PluginEditor) las sube a un valor sutil; la aberración pulsa con el kick en el renderer.
    float grainAmt     = 0.0f;   // 0..1 — grano animado modulado por luma
    float ditherAmt    = 0.0f;   // 0..1 — dither anti-banding (±1 LSB, casi invisible)
    float aberrationAmt= 0.0f;   // 0..1 — aberración cromática radial (se escala con el kick)
    float bloomWideAmt = 0.0f;   // 0..1 — halo envolvente ancho sobre el bloom base (multi-escala)

    // GRADE DE COLORISTA (Phase A6): lift/contraste + split-tone. Defaults NEUTROS = identidad byte-exacta.
    float gradeLift     = 0.0f;   // 0 = neutro; >0 levanta sombras
    float gradeContrast = 1.0f;   // 1 = neutro; >1 = más contraste (pivote 0.5)
    float splitAmt      = 0.0f;   // 0 = sin split-tone; →1 = tinte sombras/luces
    float splitShadowHue= 0.55f;  // vueltas 0..1 — tinte de sombras (0.55 ≈ cyan/teal)
    float splitHighHue  = 0.08f;  // vueltas 0..1 — tinte de luces (0.08 ≈ ámbar/naranja)
};
}
