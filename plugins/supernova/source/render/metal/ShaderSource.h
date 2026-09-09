#pragma once
// MSL de SUPERNOVA — compilado en runtime (solo CLT, sin `metal` offline).
//   Sim: física AUDIO-REACTIVA (graves→turbulencia, onset→explosión radial, agudos→jitter, RMS→respiración).
//   Color (RNF6): las partículas ACUMULAN en una textura HDR LINEAL (additive) → bloom con umbral en lineal →
//   tone-mapping fílmico (ACES) → sRGB correcto. Es la diferencia entre glow PRO y "neón lavado" amateur.
namespace supernova
{
inline constexpr const char* kParticleShaderSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float dt, time, chaos, intensity;
    float pointSize, aspect;
    float bass, rms, treble, explodePulse;
    // Física por-preset (M3): promovidos de magic numbers a uniforms. 50% del param = el valor M2 (sin regresión).
    float curlScale, homeStrength, gravity, momentum;
    float radialGain, jitterGain, breatheGain;
    // Rayo direccional MIDI (M3): pulso que decae + dirección del haz (cos/sin del ángulo).
    float rayPulse, rayDirX, rayDirY;
    // Pulso de RE-ARMADO (bug de campo): sombra lenta del explodePulse — boostea el resorte cuando la explosión
    // muere → la vuelta tarda <1 s en vez de ~3 s (drag alto), sin cambiar el atractor ni el reposo.
    float reformPulse;
    // Centroide de luminancia de la imagen cargada (ImageField) — disponible para los modos que lo quieran.
    float centerX, centerY;
    uint  count;
    // PROTOTIPO de diseño (mockups para Joaquín): filosofía de movimiento. 0=radial clásico (actual),
    // 1=MATERIA (hierve, sin epicentro), 2=ONDA (frente que barre), 3=FLOW (recorre los contornos de la
    // imagen), 4=VÓRTICES (epicentros vivos que migran). El elegido queda; el resto se poda.
    uint  motionMode;
    // CUTOUT gradual (knob automatizable, control del usuario): 0 = imagen completa, 1 = fondo borrado.
    // Se aplica EN VIVO (alpha del draw + peso físico) sobre la máscara de sujeto (content2.w) — sin re-upload.
    float cutoutAmt;
    // VOCABULARIO VISUAL (capa 1+2 del diseño): forma del glifo + estela.
    // shapeMode: 0=SOFT DOT (clásico) 1=DISC 2=RING 3=DASH (trazo orientado al movimiento) 4=TRI 5=QUAD 6=SPARK.
    // trailAmt: 0 = sin estela (clear por frame); →1 = los glifos DIBUJAN CAMINOS que se desvanecen lento.
    uint  shapeMode;
    float trailAmt;
    // CAPA 3 · PLEXUS: 0 = sin conexiones; →1 = malla densa (más radio + más alpha de línea).
    float linksAmt;
    // CONSERVACIÓN DE ENERGÍA de la estela: escala del input al HDR cuando hay trails. El fade multiplicativo
    // acumula 1/(1−k) (hasta 40× con k=0.975) → una imagen clara se REVIENTA a blanco. El C++ calcula
    // feed = M_target·(1−k) para que el estado estacionario quede acotado (~1.8..2.4×, glow controlado).
    // Con trails=0 → 1.0 exacto (byte-idéntico, goldens intactos).
    float trailFeed;
    // TIER 1 PRO: SCATTER dispersa los hogares (imagen ↔ nube, hash estable por partícula); PUMP es el
    // sidechain VISUAL del kick (flash de tamaño; default 0.6 = el comportamiento histórico exacto);
    // viewCos/viewSin = rotación de VISTA acumulada (ROTATE, la física queda en el espacio original).
    float scatterAmt;
    float pumpAmt;
    float viewCos, viewSin;
    // 3D DE PRESENTACIÓN ("darlo vuelta"): cámara orbital precomputada en CPU (yaw = ROT Y + órbita
    // acumulada; pitch = ROT X). La física sigue 2D: el volumen entra SOLO en los vertex (proyección).
    // depthAmt=0 y ángulos=0 → camino legacy EXACTO (branch → goldens intactos). `aspect` (arriba) vuelve
    // isotrópica la rotación y se setea POR TARGET en encodeComposite (el drawable puede no ser cuadrado).
    float depthAmt;
    float yawCos, yawSin;
    float pitCos, pitSin;
    // MOTOR GEOMÉTRICO (FIGURA + FORM): remap del HOGAR en k_simulate (xy) + z de figura en los vertex.
    // La figura se computa de (formT, formTheta, home, depth) — todo estático → determinista y re-armable.
    float formAmt;
    uint  formMode;       // 0=Imagen 1=Esfera 2=Espiral 3=Anillos 4=Grilla 5=Hélice
    // FIT del aspecto (POR TARGET): escala el contenido para preservar el aspecto de la IMAGEN dentro del
    // viewport (letterbox/pillarbox). 1,1 = sin fit → camino legacy byte-exacto (gate en el vertex).
    // resScale (POR TARGET): invariancia al TAMAÑO de la vista — el glifo escala con min(w,h)/1024 y la
    // figura cubre la MISMA fracción de pantalla en inmersivo/fullscreen que con los knobs (bug de campo
    // "al ampliar se ve más suave/menos intenso"). Exposición aditiva constante: d² crece como los píxeles.
    // 1.0 = camino legacy byte-exacto (offscreen/goldens; ×1.0 es IEEE-exacto).
    float fitX, fitY, resScale;
    // FUNDIDO entre fotos: mezcla [0..1] entre el juego de buffers A (la foto que estaba) y el B (la que
    // entra). −1 = REPOSO: los kernels toman el camino de siempre sin una sola operación nueva (branch por
    // uniform). Nunca llega a 1: al completarse, el C++ hace swap A↔B y vuelve a −1 — mix(a,b,1.0) redondea
    // distinto que `b` y eso movería los goldens.
    float xfade;                  // 41 floats + 4 uint = 180 B, idéntico C++/MSL
};

struct PostU {
    // satAmt: 0 = B/N, 1 = neutro (identidad exacta), 2 = vívido. hueShift: rotación de tono en radianes
    // (incluye la deriva de HUE CYC acumulada en C++). kaleidoSeg: espejos del caleidoscopio (0/1 = off).
    float threshold, bloomGain, exposure, satAmt;
    float2 dir; float hueShift; uint kaleidoSeg;
    // COLOR LAB: rampAmt = mix del gradient map (0 = off exacto, branch); bgAmt = lienzo de papel (screen
    // blend, 0 = off exacto); paper = color de papel del look en display-LINEAL (LookRamps::paperOf).
    float rampAmt, bgAmt;
    float paperR, paperG, paperB;
    // OPTICS PACK (Phase A · "óptica de cine"): grano animado modulado por luma + dither anti-banding +
    // aberración cromática radial (pulsa con el kick desde C++). Los tres default 0 = IDENTIDAD byte-exacta
    // (branch por uniform). grainTime anima el grano (solo se lee si grainAmt>0 → nunca afecta goldens).
    float grainAmt, ditherAmt, aberrationAmt, grainTime;
    // BLOOM ENVOLVENTE (Phase A · multi-escala): halo ancho y suave sobre el bloom base (anillo multi-tap del
    // bloom ya difuminado → aproxima la pirámide de mips a bajo costo). 0 = identidad byte-exacta (branch).
    float bloomWide;
    // GRADE DE COLORISTA (Phase A6): lift (levanta sombras) + contraste (pivote 0.5) + SPLIT-TONE (tiñe sombras
    // hacia splitShadowHue y luces hacia splitHighHue — el teal-orange de cine). Neutro (lift 0, contrast 1,
    // splitAmt 0) = IDENTIDAD byte-exacta (branch). splitShadowHue/splitHighHue en vueltas 0..1.
    float gradeLift, gradeContrast, splitAmt, splitShadowHue, splitHighHue;
    float _q3;   // 96 B (múltiplo de 16), idéntico C++/MSL
};

// HSV → RGB (para el split-tone: hue en vueltas 0..1). Devuelve un tinte lineal.
static inline float3 hsv2rgb(float h, float s, float v)
{
    float3 k = fract(float3(h) + float3(1.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0;
    return v * mix(float3(1.0), clamp(fabs(k) - 1.0, 0.0, 1.0), s);
}

static inline float hash1(float n) { return fract(sin(n) * 43758.5453123); }

// CUTOUT nivel ultra ("al palo = SOLO la figura"): las máscaras reales NO son binarias (saliencia: fondo
// 0.25..0.4; Vision: bordes suaves) y el blending ADITIVO re-satura cualquier alpha residual (N partículas
// apiladas) → con el mix lineal el fondo sobrevivía casi PLENO a cutout=1 (luma medida 214/255) y el knob
// era casi inerte en todo el recorrido. Curva de contraste que sube con el knob: al 100% mask<0.5 muere
// EXACTO (0, tan muerto como el lienzo vacío), mask>0.72 queda a brillo PLENO (1), borde con banda AA.
// Contra la saturación aditiva: t con ease-out (bite temprano del knob) y resultado al CUADRADO.
// t=0 → te=0 → mix(1,·,0)=1 → 1² = 1: identidad byte-exacta (goldens intactos).
static inline float cutoutMask(float m, float t)
{
    const float te = 1.0 - (1.0 - t) * (1.0 - t);
    const float lo = 0.50 * te;
    const float hi = mix(1.0, 0.72, te);
    const float f  = mix(1.0, smoothstep(lo, max(hi, lo + 1e-4), m), te);
    return f * f;
}

// ===================== MOTOR GEOMÉTRICO (FIGURA) =====================
// Target 3D estático por partícula. ft/theta = rango y ángulo áureo precomputados POR CLASE (sujeto/fondo
// por separado en el upload) → con CUTOUT la escultura forma la figura DENSA, sin los huecos del fondo
// invisible. h0 = hogar ORIGINAL (pre-scatter). depth = canal de profundidad (la figura hereda el relieve).
static inline float3 figureTarget(uint mode, float ft, float theta, float2 h0, float depth)
{
    if (mode == 1u) {   // ESFERA fibonacci: la imagen (orden scanline por clase) envuelta en bandas
        float zf  = 1.0 - 2.0 * ft;
        float rad = sqrt(max(0.0, 1.0 - zf * zf));
        return float3(0.5 + cos(theta) * rad * 0.34, 0.5 + zf * 0.38, sin(theta) * rad * 0.34);
    }
    if (mode == 2u) {   // ESPIRAL arquimediana: la imagen se desenrolla del centro hacia afuera
        float th = ft * 20.420352;   // 6.5π
        float r  = 0.055 + ft * 0.40;
        return float3(0.5 + r * cos(th), 0.5 + r * sin(th), (depth - 0.5) * 0.55);
    }
    if (mode == 3u) {   // ANILLOS: ángulo propio conservado, radio cuantizado (la imagen comprimida en vinilo)
        float2 d   = h0 - 0.5;
        float  r0  = length(d);
        float2 dir = (r0 > 1e-4) ? d / r0 : float2(1.0, 0.0);
        float  rq  = min(0.46, (floor(r0 * 10.0) + 0.5) * 0.09);
        return float3(0.5 + rq * dir.x, 0.5 + rq * dir.y, (depth - 0.5) * 0.55);
    }
    if (mode == 4u) {   // GRILLA: mosaico magnético 13×13 con parallax por bloque (hash estable)
        float2 b  = floor(h0 * 13.0);
        float  bh = hash1(b.x * 12.9898 + b.y * 78.233);
        return float3((b.x + 0.5) / 13.0, (b.y + 0.5) / 13.0, (bh - 0.5) * 0.34);
    }
    // 5 · HÉLICE: doble vuelta vertical — el ADN de tus píxeles
    float th = ft * 31.415927;       // 10π
    return float3(0.5 + 0.30 * cos(th), 0.5 + (ft - 0.5) * 0.85, 0.30 * sin(th));
}

// ===================== CÁMARA 3D (proyección compartida partículas/links) =====================
// Yaw (Y) → pitch (X) → perspectiva D=1.9 (FOV≈30°, look "product turntable"). Coordenadas isotrópicas por
// `aspect`; guard de plano cercano (w mínimo) para que el kick-z/dolly no explote detrás de cámara.
// outPersp = D/w (atenuación de tamaño y cue de brillo). SOLO se llama con la cámara activa (p3d).
static inline float4 projectP3(float2 p, float z, constant Uniforms& u, thread float& outPersp)
{
    float2 c  = float2((p.x - 0.5) * u.aspect, p.y - 0.5);
    float  x1 =  c.x * u.yawCos + z * u.yawSin;
    float  z1 = -c.x * u.yawSin + z * u.yawCos;
    float  y1 =  c.y * u.pitCos - z1 * u.pitSin;
    float  z2 =  c.y * u.pitSin + z1 * u.pitCos;
    const float D = 1.9;
    float  w  = max(0.35, D - z2);
    float  s  = D / w;
    outPersp  = s;
    float2 q  = float2(x1 * s / u.aspect + 0.5, y1 * s + 0.5);
    return float4(q.x * 2.0 - 1.0, 1.0 - q.y * 2.0, 0.0, 1.0);
}

// ¿Cámara 3D activa? (depth o ángulos fuera de identidad — mismo patrón de branch que ROTATE/SAT/HUE.)
// FIT del aspecto: encoge el cuadrado unidad para que el CONTENIDO conserve el aspecto de la imagen dentro
// del viewport (centrado). Gate: 1,1 = sin transformar → posición byte-idéntica al camino legacy (goldens).
static inline float2 applyFit(float2 p, constant Uniforms& u)
{
    if (u.fitX != 1.0 || u.fitY != 1.0)
        return (p - float2(0.5)) * float2(u.fitX, u.fitY) + float2(0.5);
    return p;
}

static inline bool p3dActive(constant Uniforms& u)
{
    return u.depthAmt != 0.0 || u.yawSin != 0.0 || u.yawCos != 1.0 || u.pitSin != 0.0 || u.pitCos != 1.0;
}

// ===================== SIMULACIÓN (compute) =====================
kernel void k_simulate(device float2*        positions  [[buffer(0)]],
                       device float2*        velocities [[buffer(1)]],
                       const device float2*  homes      [[buffer(2)]],
                       constant Uniforms&    u          [[buffer(3)]],
                       const device float*   weights    [[buffer(4)]],
                       const device float2*  flowField  [[buffer(5)]],
                       const device float4*  content2   [[buffer(6)]],   // (burstX, burstY, signedDist, salW)
                       const device float4*  extra      [[buffer(7)]],   // (depth, formT, formTheta, libre)
                       // FUNDIDO: el mismo juego de campos para la foto que ENTRA (ver u.xfade).
                       const device float*   weightsB   [[buffer(8)]],
                       const device float2*  flowB      [[buffer(9)]],
                       const device float4*  content2B  [[buffer(10)]],
                       const device float4*  extraB     [[buffer(11)]],
                       uint gid [[thread_position_in_grid]])
{
    if (gid >= u.count) return;
    // FUNDIDO: los cuatro campos por partícula se mezclan UNA vez acá y el resto del kernel los usa igual
    // que siempre. En reposo (xfade < 0) el branch ni entra: son los mismos loads de hoy → byte-exacto.
    float  wRaw = weights[gid];
    float2 fRaw = flowField[gid];
    float4 c2v  = content2[gid];
    float4 exv  = extra[gid];
    if (u.xfade >= 0.0)
    {
        wRaw = mix(wRaw, weightsB[gid],   u.xfade);
        fRaw = mix(fRaw, flowB[gid],      u.xfade);
        c2v  = mix(c2v,  content2B[gid],  u.xfade);
        exv  = mix(exv,  extraB[gid],     u.xfade);
    }
    float2 p = positions[gid];
    float2 v = velocities[gid];
    float2 h = homes[gid];
    // SCATTER: dispersa el HOGAR con un offset hasheado ESTABLE por partícula (imagen ↔ nube, gradual y
    // automatizable; el resorte tira hacia el hogar disperso → la imagen se disuelve y se re-arma donde digas).
    if (u.scatterAmt > 0.0)
    {
        float2 off = float2(hash1(float(gid) * 1.31 + 7.0), hash1(float(gid) * 2.71 + 3.0)) - 0.5;
        h = clamp(h + off * (u.scatterAmt * 1.2), 0.02, 0.98);
    }
    // FIGURA (motor geométrico): el hogar abandona la imagen y tira hacia la figura — el resorte, el kick y
    // el re-armado existentes hacen TODO el resto (mismo patrón que SCATTER; se componen: figura difusa).
    if (u.formMode != 0u && u.formAmt > 0.001)
    {
        float3 ftgt = figureTarget(u.formMode, exv.y, exv.z, homes[gid], exv.x);
        h = mix(h, ftgt.xy, u.formAmt);
    }
    // Saliencia del contenido (0.25 fondo .. 1 sujeto), atenuada por el CUTOUT gradual: al borrar el fondo,
    // su física también se apaga (queda la escultura del sujeto, el resto no molesta).
    const float mask = c2v.w;
    const float w = wRaw * mix(1.0, 0.15 + 0.85 * mask, u.cutoutAmt);

    const float t = u.time;
    const float s = 6.2831853 * u.curlScale;
    float2 curl = float2( sin(p.y * s + t)        - cos(p.x * s * 0.7 - t * 0.6),
                          cos(p.x * s + t)        - sin(p.y * s * 0.7 + t * 0.6) );
    const float curlGain = u.chaos * 0.25 + u.bass * 0.55;

    float2 toC   = p - float2(0.5, 0.5);
    float  rad   = length(toC);
    // Singularidad del curl/radial en r=0: dirección hasheada ESTABLE cuando el radio es diminuto.
    float2 dHash = float2(cos(6.2831853 * hash1(float(gid) + 0.5)),
                          sin(6.2831853 * hash1(float(gid) + 0.5)));
    float2 outward = (rad > 2e-3) ? toC / rad : dHash;

    // Resorte de hogar + RE-ARMADO GARANTIZADO (común a todos los modos).
    float2 home = (h - p) * (u.homeStrength + u.explodePulse * 0.6 + u.reformPulse * 10.0);

    float  ja     = hash1(float(gid) * 0.017 + t * 0.13) * 6.2831853;
    float2 jitter = float2(cos(ja), sin(ja)) * u.treble * u.jitterGain;

    // Rayo direccional (MIDI, M3) — común.
    float2 rayDir = float2(u.rayDirX, u.rayDirY);
    float  cone   = smoothstep(0.35, 1.0, dot(outward, rayDir));
    float2 ray    = outward * u.rayPulse * cone * (4.0 + u.intensity * 2.0);

    // ============ FILOSOFÍA DE MOVIMIENTO (prototipos de diseño; el elegido queda, el resto se poda) ========
    const float kickAmp = u.explodePulse * (u.radialGain + u.intensity);
    float2 react = float2(0.0);
    if (u.motionMode == 0)
    {
        // 0 · RADIAL CLÁSICO (referencia): late/explota desde el centro geométrico. El "hueco que late".
        float2 radial  = outward * kickAmp * smoothstep(0.0, 0.02, rad);
        float2 breathe = toC * min(u.rms * u.breatheGain, u.homeStrength * 0.35);
        react = radial + breathe;
    }
    else if (u.motionMode == 1)
    {
        // 1 · MATERIA VIVA: SIN epicentro. Cada partícula respira alrededor de SU casa con fase propia (la
        // imagen "hierve" con el nivel) y el kick es un shock granular: cada una salta en su dirección.
        float  phase  = hash1(float(gid) * 0.031) * 6.2831853;
        float2 dirOsc = float2(cos(phase + t * 1.7), sin(phase + t * 1.7));
        float2 seethe = dirOsc * u.rms * u.breatheGain * 1.6;
        float2 shock  = dHash * kickAmp * 0.9;
        react = (seethe + shock) * (0.3 + 0.7 * w);
    }
    else if (u.motionMode == 2)
    {
        // 2 · ONDA VIAJERA: un frente recorre la imagen (bandera/shockwave), nada emana de un punto. La fase
        // viaja con el tiempo; el kick excita la amplitud del frente.
        float2 wdir  = normalize(float2(1.0, 0.35));
        float  phase = dot(p, wdir) * 9.0 - t * 3.2;
        float2 swellD = wdir * sin(phase);
        float2 wave  = swellD * (u.rms * u.breatheGain * 1.3 + kickAmp * 1.1);
        react = wave * (0.3 + 0.7 * w);
    }
    else if (u.motionMode == 3)
    {
        // 3 · FLUJO POR CONTORNOS: las partículas RECORREN los bordes de la propia imagen (ImageField). El
        // kick es un golpe de caudal: todo el contorno avanza de golpe. Zonas planas: hervor mínimo (no mueren).
        // COHESIÓN del sujeto (bug de campo: "el logo lo rompe al toque"): con CUTOUT activo la escultura
        // recibe el kick ATENUADO (~55%) y un resorte extra → la forma se mantiene legible y se re-arma rápido.
        const float subj = mask * u.cutoutAmt;   // 0 sin cutout / fondo; →1 en el sujeto recortado
        float2 f = fRaw;
        float  drive = 0.35 + u.bass * 1.6 + u.rms * 0.5;
        float2 flowForce = f * drive * 1.7 + f * kickAmp * 2.4 * (1.0 - 0.45 * subj);
        float  phase  = hash1(float(gid) * 0.031) * 6.2831853;
        float2 simmer = float2(cos(phase + t * 1.9), sin(phase + t * 1.9)) * u.rms * 0.18;
        // BREATHE vive en Contours (hallazgo de la medición 2026-07-12: el knob era inerte en el modo
        // default). Branch: en 50% (breatheGain == 0.28, mismo bit que produce el mapeo 0.5·0.56f) corre
        // la línea original → byte-exacto en defaults/goldens; girar el knob escala el hervor 0..2×.
        if (u.breatheGain != 0.28)
            simmer *= u.breatheGain / 0.28;
        react = (flowForce + simmer) * (0.25 + 0.75 * w) + (h - p) * (subj * 1.6);
    }
    else if (u.motionMode == 5)
    {
        // 5 · NATURAL (la receta de la investigación): el motor RECONOCE la imagen —
        //   · el flujo tangente (ETF) recorre los contornos, modulado por medios/graves;
        //   · el ambiente (curl) se APAGA cerca del sujeto (rampa por distancia con signo): el aire fluye
        //     alrededor de la figura en vez de atravesarla;
        //   · el KICK estalla DESDE LA SILUETA del sujeto (burstDir = normal exterior, envolvente exp(−|sd|/σ))
        //     y el reform lo re-arma: la FIGURA respira/exhala, no un punto;
        //   · la saliencia (Vision) escala la energía: el sujeto vive, el fondo acompaña.
        float4 c2   = c2v;
        float2 f    = fRaw;
        float  sd   = c2.z;
        float  salW = c2.w;   // = máscara del sujeto (mismo espíritu: el sujeto vive, el fondo acompaña)
        float  drive = 0.30 + u.bass * 1.3 + u.rms * 0.4;
        float2 flowForce = f * drive * 1.5;

        float  ramp   = smoothstep(0.02, 0.16, fabs(sd));    // 0 en la silueta, 1 lejos
        float2 ambient = curl * (0.10 + u.rms * 0.25) * ramp; // hervor ambiente que RESPETA la figura

        float2 burst = float2(c2.x, c2.y) * kickAmp * 1.9 * exp(-fabs(sd) * 14.0);   // σ≈0.07 (~36 celdas)

        float  phase  = hash1(float(gid) * 0.031) * 6.2831853;
        float2 simmer = float2(cos(phase + t * 1.9), sin(phase + t * 1.9)) * u.rms * 0.14;

        react = (flowForce + ambient + burst + simmer) * (0.30 + 0.70 * salW);
    }
    else
    {
        // 4 · VÓRTICES MIGRANTES: 3 epicentros INVISIBLES que pasean lento por el cuadro; la energía orbita
        // el más cercano y el kick estalla DESDE ese vórtice (el origen nunca es el mismo, migra siempre).
        float2 c0 = float2(0.5 + 0.33 * sin(t * 0.23),        0.5 + 0.33 * cos(t * 0.171));
        float2 c1 = float2(0.5 + 0.33 * sin(t * 0.147 + 2.1), 0.5 + 0.33 * cos(t * 0.256 + 4.4));
        float2 c2 = float2(0.5 + 0.33 * sin(t * 0.302 + 4.0), 0.5 + 0.33 * cos(t * 0.121 + 1.3));
        float2 d0 = p - c0, d1 = p - c1, d2 = p - c2;
        float  r0 = length(d0), r1 = length(d1), r2 = length(d2);
        float2 dm = d0; float rm = r0;
        if (r1 < rm) { dm = d1; rm = r1; }
        if (r2 < rm) { dm = d2; rm = r2; }
        float2 dir  = (rm > 2e-3) ? dm / rm : dHash;
        float2 tang = float2(-dir.y, dir.x);
        float2 orbitF = tang * (0.35 + u.bass * 0.9) / (rm + 0.35);
        float2 pulse  = dir * kickAmp * smoothstep(0.0, 0.03, rm) * exp(-rm * 2.2);
        float2 breatheV = dm * u.rms * u.breatheGain * 0.9;
        react = (orbitF + pulse * 1.6 + breatheV) * (0.3 + 0.7 * w);
    }

    // Gravedad = tiro constante en Y. 0 = ninguna.
    float2 force = curl * curlGain + home + react + jitter + ray + float2(0.0, u.gravity);

    // CAMINO DE QUIETUD (CLEAR / estado cero): con INTENSITY < 4% las fuerzas autónomas y de audio se
    // apagan en rampa — a 0 solo queda el resorte de hogar → la foto asienta NÍTIDA y QUIETA aunque
    // suene música. BRANCH deliberado: a intensity ≥ 0.04 corre la expresión ORIGINAL de arriba (misma
    // suma, mismo orden → byte-exacto; multiplicar todo por alive==1.0 reasociaría la suma y rompería
    // los goldens). El resorte/reform/paredes NUNCA se gatean: el re-armado está garantizado.
    if (u.intensity < 0.04)
    {
        float aliveT = u.intensity * 25.0;                       // 0..1 dentro de la rampa
        float alive  = aliveT * aliveT * (3.0 - 2.0 * aliveT);   // smoothstep(0, 0.04, intensity)
        force = (curl * curlGain + react + jitter + ray + float2(0.0, u.gravity)) * alive + home;
    }

    // PAREDES elásticas suaves (bug de campo: "se van a cualquier lado fuera del recuadro y queda negro").
    // Cero efecto dentro de [−0.08, 1.08] (la explosión puede asomar del cuadro, dramático); fuera, resorte
    // fuerte de vuelta. Nada se pierde jamás: el re-armado está garantizado desde cualquier estado.
    force += (clamp(p, -0.08, 1.08) - p) * 42.0;

    v = v * u.momentum + force * u.dt;

    // Techo de velocidad: acota el viaje balístico post-kick (con radialGain alto la velocidad se iba sin
    // límite → segundos de pantalla vacía). 2.2 unidades/s = cruzar la pantalla en ~0.45 s, expresivo y acotado.
    const float vMax = 2.2;
    float sp = length(v);
    if (sp > vMax) v *= vMax / sp;

    p = p + v * u.dt * (0.3 + 0.7 * u.intensity);

    // GARANTÍA dura de contención (el resorte de pared es el pre-freno estético; esto es el contrato): tras
    // integrar, la posición se clampea al recuadro extendido y SOLO se mata la componente de velocidad que
    // apunta hacia afuera (la tangencial vive → nada de partículas "pegadas"). Sin esto, con momentum→0.99 la
    // pared blanda se penetra ~0.3 (verificado por el test [bounds] con bombardeo extremo).
    if (p.x < -0.08) { p.x = -0.08; v.x = max(v.x, 0.0); }
    if (p.x >  1.08) { p.x =  1.08; v.x = min(v.x, 0.0); }
    if (p.y < -0.08) { p.y = -0.08; v.y = max(v.y, 0.0); }
    if (p.y >  1.08) { p.y =  1.08; v.y = min(v.y, 0.0); }

    // Auto-sanado: si un estado viejo/corrupto dejó NaN/Inf (no se repara con clamp), la partícula renace en
    // su hogar. Un check por partícula, gratis al lado del resto del kernel.
    if (!isfinite(p.x + p.y + v.x + v.y)) { p = h; v = float2(0.0); }

    positions[gid]  = p;
    velocities[gid] = v;
}

// ===================== PARTÍCULAS (draw → HDR lineal) =====================
// dir = dirección de movimiento (cos,sin) para orientar el glifo (DASH/TRI siguen la velocidad → caligrafía).
struct VOut { float4 position [[position]]; float pointSize [[point_size]]; float4 color; float2 dir; };

vertex VOut v_particle(uint vid [[vertex_id]],
                       const device float2* positions [[buffer(0)]],
                       const device float4* colors    [[buffer(1)]],
                       constant Uniforms&   u          [[buffer(2)]],
                       const device float4* content2  [[buffer(3)]],
                       const device float2* velocities [[buffer(4)]],
                       const device float4* extra      [[buffer(5)]],   // (depth, formT, formTheta, libre)
                       const device float2* homes      [[buffer(6)]],
                       // FUNDIDO: color, máscara y depth de la foto que ENTRA (ver u.xfade).
                       const device float4* colorsB    [[buffer(7)]],
                       const device float4* content2B  [[buffer(8)]],
                       const device float4* extraB     [[buffer(9)]])
{
    // FUNDIDO: la máscara del sujeto se mezcla una vez (la usan la cáscara 3D y el CUTOUT). En reposo
    // (xfade < 0) es el mismo load de siempre → byte-exacto. `extra` y `colors` se mezclan donde se leen,
    // para no agregar un load de 16 B por vértice en el camino plano.
    float mask2 = content2[vid].w;
    if (u.xfade >= 0.0) mask2 = mix(mask2, content2B[vid].w, u.xfade);

    float2 p = applyFit(positions[vid], u);   // FIT del aspecto (gate: 1,1 = camino legacy byte-exacto)
    // ROTATE: giro de VISTA alrededor del centro (la física vive en el espacio original — las paredes no
    // giran). viewCos=1/viewSin=0 → identidad byte-exacta. Con 3D activo actúa como ROLL de cámara.
    if (u.viewSin != 0.0 || u.viewCos != 1.0)
    {
        float2 c = p - float2(0.5);
        p = float2(c.x * u.viewCos - c.y * u.viewSin, c.x * u.viewSin + c.y * u.viewCos) + float2(0.5);
    }
    VOut o;
    float persp = 1.0;
    if (!p3dActive(u))
        o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);   // camino legacy EXACTO (goldens)
    else
    {
        float4 ex = extra[vid];
        if (u.xfade >= 0.0) ex = mix(ex, extraB[vid], u.xfade);
        const float  mk   = mask2;
        // z de imagen (almohada/luma/CoreML, 0.5 = plano). CÁSCARA por paridad (Photo Wake-Up): la mitad de
        // las partículas del sujeto viven en el DORSO espejado — de frente el aditivo suma igual (cada una
        // dibuja una vez), al girar 180° hay materia, no un hueco. Solo en modo imagen (las figuras ya son
        // volumétricas y cerradas).
        float zImg = ex.x - 0.5;
        if (((vid & 1u) != 0u) && mk > 0.5) zImg = -zImg;
        float z = zImg;
        if (u.formMode != 0u && u.formAmt > 0.001)
        {
            float3 ftgt = figureTarget(u.formMode, ex.y, ex.z, homes[vid], ex.x);
            z = mix(zImg, ftgt.z, u.formAmt);
        }
        z *= u.depthAmt * 1.05;
        // KICK VOLUMÉTRICO: el estallido también empuja en z (dirección hasheada estable, decae con el
        // pulso — el resorte 2D y este envelope llegan juntos: explosión esférica que se re-arma).
        z += (hash1(float(vid) * 3.7 + 11.0) - 0.5) * u.explodePulse * u.depthAmt * 0.55;
        o.position = projectP3(p, z, u, persp);
    }
    // PUMP: el sidechain VISUAL — el kick infla el glifo (default 0.6 = el flash histórico exacto).
    // Los glifos con contorno fino (RING/DASH/SPARK) necesitan más px para leerse: piso de tamaño más alto.
    float basePx = u.pointSize * (1.0 + u.explodePulse * u.pumpAmt);
    if (u.shapeMode >= 2) basePx = max(basePx, 4.0);
    // Perspectiva: atenuación de tamaño por 1/w (clamp sano contra overdraw) + cue de brillo (cerca un
    // toque más caliente, lejos se apaga — el fog barato que vende el volumen en aditivo).
    if (persp != 1.0) basePx *= clamp(persp, 0.55, 2.4);
    o.pointSize = basePx * u.resScale;   // invariancia al tamaño de la vista (1.0 = legacy byte-exacto)
    o.color     = colors[vid];   // ya en LINEAL (uploadImage lo convierte)
    if (u.xfade >= 0.0) o.color = mix(o.color, colorsB[vid], u.xfade);   // FUNDIDO: la foto que entra
    if (persp != 1.0) o.color.rgb *= mix(1.0, persp, 0.6);
    // CONSERVACIÓN DE ENERGÍA del SIZE (bug de campo: "subir SIZE solo suma brillo y pierde definición").
    // Blending aditivo: un punto más grande solapa más → la suma crece ~size². Normalizamos la intensidad por
    // el ÁREA relativa al mínimo (pointSize base = 2.0): SIZE ahora cambia la TEXTURA (puntillismo ↔ manchas)
    // con la exposición constante. El boost del explodePulse NO se normaliza (el flash del kick es deseado).
    {
        float rel = max(1.0, u.pointSize / 2.0);
        o.color.rgb /= rel * rel;
    }
    // Orientación del glifo = dirección de la velocidad (o del hogar si está quieta → estable, sin flicker).
    // Con ROTATE activo, la dirección gira con la vista (los trazos siguen acompañando lo que ves).
    float2 vel = velocities[vid];
    float  sp  = length(vel);
    float2 dirW = (sp > 1e-4) ? vel / sp : float2(1.0, 0.0);
    o.dir = (u.viewSin != 0.0 || u.viewCos != 1.0)
              ? float2(dirW.x * u.viewCos - dirW.y * u.viewSin, dirW.x * u.viewSin + dirW.y * u.viewCos)
              : dirW;
    // CONSERVACIÓN DE ENERGÍA de la estela (mismo principio que SIZE): el fade multiplicativo acumula 1/(1−k)
    // (hasta 40×) → imagen clara = blanco quemado. feed acota lo ESTÁTICO a ~1.3× … y el brillo vuelve a lo
    // que SE MUEVE (boost por velocidad): las cabezas pintan caminos vivos, el cuerpo quieto conserva su
    // exposición. Con trails=0 → feed=1 y boost=0 → byte-idéntico (goldens intactos).
    {
        // El umbral de velocidad deja FUERA el hervor idle (~0.1 u/s): solo el gesto rápido (kick, caudal)
        // pinta brillante — si no, el boost se filtra a toda la imagen y vuelve el quemado.
        float headBoost = (u.trailAmt > 0.001) ? 2.2 * smoothstep(0.28, 0.90, sp) : 0.0;
        o.color.rgb *= u.trailFeed * (1.0 + headBoost);
    }
    // CUTOUT gradual EN VIVO: el fondo (mask→0) se desvanece según el knob. mask=1 sin máscara → sin efecto.
    // cutoutMask (curva de contraste²): al 100% el fondo muere EXACTO y el sujeto queda a brillo pleno.
    o.color.a  *= cutoutMask(mask2, u.cutoutAmt);
    return o;
}

// SDFs de glifos (en el espacio del point sprite, pc ∈ [0,1]²). Devuelven "coverage" [0..1] con AA suave.
// La ENERGÍA de cada glifo se normaliza a ojo contra el SOFT DOT (área efectiva similar) para que cambiar
// de forma no cambie la exposición (mismo principio que la conservación del SIZE).
static inline float glyphCoverage(uint mode, float2 pc, float2 dir)
{
    float2 q = pc - float2(0.5);
    // rotar al marco del glifo (x = a lo largo del movimiento)
    float2 r = float2( q.x * dir.x + q.y * dir.y,
                      -q.x * dir.y + q.y * dir.x );
    const float aa = 0.08;
    if (mode == 1)   // DISC duro
        return (1.0 - smoothstep(0.40 - aa, 0.40 + aa, length(q))) * 0.85;
    if (mode == 2)   // RING (anillo)
    {
        float d = fabs(length(q) - 0.34);
        return (1.0 - smoothstep(0.10 - aa, 0.10 + aa, d)) * 1.05;
    }
    if (mode == 3)   // DASH: trazo orientado al movimiento (caligrafía/pinceladas)
    {
        float2 d = fabs(r) - float2(0.42, 0.10);
        float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
        return (1.0 - smoothstep(-aa, aa, sd)) * 1.10;
    }
    if (mode == 4)   // TRI: flecha/delta apuntando al movimiento
    {
        float2 t = r;
        float sd = max(max(-t.x - 0.30, t.x - 0.42), fabs(t.y) - (0.42 - t.x) * 0.55);
        return (1.0 - smoothstep(-aa, aa, sd)) * 1.00;
    }
    if (mode == 5)   // QUAD: cuadrado axis-aligned (mosaico/píxel)
    {
        float2 d = fabs(q) - float2(0.36);
        float sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
        return (1.0 - smoothstep(-aa, aa, sd)) * 0.80;
    }
    if (mode == 6)   // SPARK: cruz fina (destello)
    {
        float a1 = 1.0 - smoothstep(0.05 - aa, 0.05 + aa, fabs(q.x));
        float a2 = 1.0 - smoothstep(0.05 - aa, 0.05 + aa, fabs(q.y));
        float lim = 1.0 - smoothstep(0.45 - aa, 0.45 + aa, length(q));
        return min(1.0, (a1 + a2)) * lim * 1.35;
    }
    // 0 · SOFT DOT (el clásico)
    return smoothstep(0.5, 0.0, length(q));
}

fragment float4 f_particle(VOut in [[stage_in]],
                           float2 pc [[point_coord]],
                           constant Uniforms& u [[buffer(0)]])
{
    float a = glyphCoverage(u.shapeMode, pc, in.dir) * in.color.a;
    return float4(in.color.rgb * a, a);   // premultiplicado; additive acumula en HDR lineal
}

// ===================== PLEXUS (capa 3 · conexión) =====================
// Líneas finas entre elementos CERCANOS de un subconjunto de nodos (~8k, sesgado al sujeto): constelaciones,
// redes vivas. Vecindad por grid hash en GPU (24×24 celdas) + K vecinos MÁS CERCANOS por nodo (determinista
// dado el mismo estado → sin popping entre frames). Cada par (i<j) se emite UNA vez a un buffer de vértices
// que se dibuja con draw INDIRECTO (la CPU nunca lee el conteo). Additive al mismo HDR → bloom y estelas
// también abrazan las líneas.
struct LinkU {
    float radius;      // radio de vecindad (UV)
    float alphaGain;   // ganancia global de línea (linksAmt × respiración por energía)
    float cutoutAmt;   // el fondo pierde sus líneas al borrarse (mismo fade que las partículas)
    float minDist2;    // distancia MÍNIMA de enlace al cuadrado — en contenido DENSO los vecinos pegados
                       // (~2px) son invisibles bajo las partículas: sin piso, K-nearest elige SIEMPRE esos
                       // y la constelación no se lee (hallazgo de la QA visual). Con piso, las líneas nacen
                       // donde hay separación que el ojo ve.
    uint  nodeCount;   // nodos válidos en linkNodes
    uint  activeCount; // prefijo de densidad adaptativa del frame (nodos fuera → se saltean)
    uint  maxLines;    // capacidad del buffer de líneas
    uint  _p1;
    // 3D: los extremos de cada línea llevan su z (misma fórmula que las partículas, sin kick-z) para que la
    // constelación viva DENTRO del volumen y gire con él. Con depthAmt=0 → z=0 (camino plano intacto).
    float depthAmt;
    float formAmt;
    uint  formMode;
    float xfade;       // FUNDIDO: mismo contrato que Uniforms.xfade (−1 = reposo, sin operación nueva)
};
struct LinkVert { float2 pos; float2 _pad; float4 col; };
struct DrawArgs { uint vertexCount, instanceCount, vertexStart, baseInstance; };

constant constexpr int  kLinkGrid     = 16;    // 16×16 celdas (celda 0.0625 > radio máx 0.058 → 3×3 completo)
constant constexpr int  kSlotsPerCell = 40;    // ~32 nodos/celda promedio con 8k nodos; clumps extremos se ignoran
constant constexpr int  kLinkK        = 5;     // vecinos más cercanos retenidos por nodo (sobre el piso)

kernel void k_linkClear(device atomic_uint* cellCount [[buffer(0)]],
                        device atomic_uint* lineCount [[buffer(1)]],
                        uint gid [[thread_position_in_grid]])
{
    if (gid < (uint)(kLinkGrid * kLinkGrid))
        atomic_store_explicit(&cellCount[gid], 0u, memory_order_relaxed);
    if (gid == 0)
        atomic_store_explicit(&lineCount[0], 0u, memory_order_relaxed);
}

kernel void k_linkBin(const device float2* positions  [[buffer(0)]],
                      const device uint*   linkNodes  [[buffer(1)]],
                      device atomic_uint*  cellCount  [[buffer(2)]],
                      device uint*         cellSlots  [[buffer(3)]],
                      constant LinkU&      u          [[buffer(4)]],
                      uint gid [[thread_position_in_grid]])
{
    if (gid >= u.nodeCount) return;
    const uint slot = linkNodes[gid];
    if (slot >= u.activeCount) return;            // fuera del prefijo adaptativo → este frame no existe
    float2 p = positions[slot];
    const int cx = clamp((int)(p.x * kLinkGrid), 0, kLinkGrid - 1);
    const int cy = clamp((int)(p.y * kLinkGrid), 0, kLinkGrid - 1);
    const uint cell = (uint)(cy * kLinkGrid + cx);
    const uint idx = atomic_fetch_add_explicit(&cellCount[cell], 1u, memory_order_relaxed);
    if (idx < (uint)kSlotsPerCell)
        cellSlots[cell * kSlotsPerCell + idx] = gid;
}

// z de un nodo para las líneas: la MISMA geometría que v_particle (cáscara por paridad + figura), sin el
// kick-z (el envelope del pulso no viaja en LinkU; la diferencia es invisible en líneas de 1px).
static inline float linkNodeZ(uint slot, const device float4* extra, const device float2* homes,
                              const device float4* content2, constant LinkU& u,
                              const device float4* extraB, const device float4* content2B)
{
    if (u.depthAmt == 0.0) return 0.0;
    float4 ex = extra[slot];
    float  mk = content2[slot].w;
    if (u.xfade >= 0.0) { ex = mix(ex, extraB[slot], u.xfade); mk = mix(mk, content2B[slot].w, u.xfade); }
    float zImg = ex.x - 0.5;
    if (((slot & 1u) != 0u) && mk > 0.5) zImg = -zImg;
    float z = zImg;
    if (u.formMode != 0u && u.formAmt > 0.001)
        z = mix(zImg, figureTarget(u.formMode, ex.y, ex.z, homes[slot], ex.x).z, u.formAmt);
    return z * u.depthAmt * 1.05;
}

kernel void k_linkEmit(const device float2* positions  [[buffer(0)]],
                       const device float4* colors     [[buffer(1)]],
                       const device uint*   linkNodes  [[buffer(2)]],
                       const device atomic_uint* cellCount [[buffer(3)]],
                       const device uint*   cellSlots  [[buffer(4)]],
                       device atomic_uint*  lineCount  [[buffer(5)]],
                       device LinkVert*     verts      [[buffer(6)]],
                       const device float4* content2   [[buffer(7)]],
                       constant LinkU&      u          [[buffer(8)]],
                       const device float4* extra      [[buffer(9)]],
                       const device float2* homes      [[buffer(10)]],
                       // FUNDIDO: color, máscara y depth de la foto que ENTRA (ver u.xfade).
                       const device float4* colorsB    [[buffer(11)]],
                       const device float4* content2B  [[buffer(12)]],
                       const device float4* extraB     [[buffer(13)]],
                       uint gid [[thread_position_in_grid]])
{
    if (gid >= u.nodeCount) return;
    const uint slot = linkNodes[gid];
    if (slot >= u.activeCount) return;
    const float2 p = positions[slot];
    const float r2 = u.radius * u.radius;

    // K más cercanos entre los candidatos de las 3×3 celdas (solo j>gid: cada par se emite UNA vez).
    float bd[kLinkK]; uint bj[kLinkK];
    for (int i = 0; i < kLinkK; ++i) { bd[i] = 1e9; bj[i] = 0xFFFFFFFFu; }

    const int cx = clamp((int)(p.x * kLinkGrid), 0, kLinkGrid - 1);
    const int cy = clamp((int)(p.y * kLinkGrid), 0, kLinkGrid - 1);
    for (int oy = -1; oy <= 1; ++oy)
    for (int ox = -1; ox <= 1; ++ox)
    {
        const int nx = cx + ox, ny = cy + oy;
        if (nx < 0 || nx >= kLinkGrid || ny < 0 || ny >= kLinkGrid) continue;
        const uint cell = (uint)(ny * kLinkGrid + nx);
        const uint n = min(atomic_load_explicit(&cellCount[cell], memory_order_relaxed), (uint)kSlotsPerCell);
        for (uint i = 0; i < n; ++i)
        {
            const uint j = cellSlots[cell * kSlotsPerCell + i];
            if (j <= gid) continue;
            const float2 q = positions[linkNodes[j]];
            const float2 d = q - p;
            const float d2 = dot(d, d);
            if (d2 >= r2 || d2 < u.minDist2) continue;
            if (d2 < bd[kLinkK - 1])
            {
                int at = kLinkK - 1;
                while (at > 0 && bd[at - 1] > d2) { bd[at] = bd[at - 1]; bj[at] = bj[at - 1]; --at; }
                bd[at] = d2; bj[at] = j;
            }
        }
    }

    // FUNDIDO: color y máscara del nodo salen del mismo mix que las partículas (xfade < 0 = los de hoy).
    float4 myCol = colors[slot];
    float  myMk  = content2[slot].w;
    if (u.xfade >= 0.0) { myCol = mix(myCol, colorsB[slot], u.xfade);
                          myMk  = mix(myMk,  content2B[slot].w, u.xfade); }
    const float myCut = cutoutMask(myMk, u.cutoutAmt);
    const float myZ   = linkNodeZ(slot, extra, homes, content2, u, extraB, content2B);
    for (int i = 0; i < kLinkK; ++i)
    {
        if (bj[i] == 0xFFFFFFFFu) break;
        const uint js = linkNodes[bj[i]];
        const float fade = 1.0 - sqrt(bd[i]) / u.radius;      // 1 pegados → 0 al borde del radio
        const float a = fade * fade * u.alphaGain;
        const uint li = atomic_fetch_add_explicit(&lineCount[0], 1u, memory_order_relaxed);
        if (li >= u.maxLines) break;
        float4 jsCol = colors[js];
        float  jsMk  = content2[js].w;
        if (u.xfade >= 0.0) { jsCol = mix(jsCol, colorsB[js], u.xfade);
                              jsMk  = mix(jsMk,  content2B[js].w, u.xfade); }
        const float aA = a * myCol.a * myCut;
        const float aB = a * jsCol.a * cutoutMask(jsMk, u.cutoutAmt);
        // _pad.x = z del extremo (3D): la constelación vive dentro del volumen y gira con él.
        LinkVert v0; v0.pos = p;             v0._pad = float2(myZ, 0.0);
        v0.col = float4(myCol.rgb * aA, aA);
        LinkVert v1; v1.pos = positions[js];
        v1._pad = float2(linkNodeZ(js, extra, homes, content2, u, extraB, content2B), 0.0);
        v1.col = float4(jsCol.rgb  * aB, aB);
        verts[li * 2]     = v0;
        verts[li * 2 + 1] = v1;
    }
}

kernel void k_linkArgs(const device atomic_uint* lineCount [[buffer(0)]],
                       device DrawArgs&          args      [[buffer(1)]],
                       constant LinkU&           u         [[buffer(2)]],
                       uint gid [[thread_position_in_grid]])
{
    if (gid != 0) return;
    const uint n = min(atomic_load_explicit(&lineCount[0], memory_order_relaxed), u.maxLines);
    args.vertexCount = n * 2; args.instanceCount = 1; args.vertexStart = 0; args.baseInstance = 0;
}

struct LinkVOut { float4 position [[position]]; float4 col; };

vertex LinkVOut v_link(uint vid [[vertex_id]],
                       const device LinkVert* verts [[buffer(0)]],
                       constant Uniforms& u [[buffer(1)]])
{
    LinkVert v = verts[vid];
    float2 p = applyFit(v.pos, u);              // FIT del aspecto (mismo que las partículas → alineadas)
    if (u.viewSin != 0.0 || u.viewCos != 1.0)   // ROTATE: las líneas giran con las partículas
    {
        float2 c = p - float2(0.5);
        p = float2(c.x * u.viewCos - c.y * u.viewSin, c.x * u.viewSin + c.y * u.viewCos) + float2(0.5);
    }
    LinkVOut o;
    if (!p3dActive(u))
        o.position = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);   // camino legacy exacto
    else
    {
        float persp = 1.0;
        o.position = projectP3(p, v._pad.x, u, persp);   // z del extremo (k_linkEmit) → misma cámara
    }
    o.col = v.col;   // premultiplicado (gradiente entre los colores de ambos extremos)
    return o;
}

fragment float4 f_link(LinkVOut in [[stage_in]])
{
    return in.col;
}

// ===================== POST (fullscreen) =====================
struct FSOut { float4 pos [[position]]; float2 uv; };

// ESTELA (capa 2 del vocabulario): quad de fade multiplicativo que corre ANTES de las partículas en el mismo
// pass (blending Zero/SourceColor → dst *= k). El frame anterior persiste atenuado → los glifos DIBUJAN CAMINOS.
fragment float4 f_fade(FSOut in [[stage_in]], constant float& k [[buffer(0)]])
{
    return float4(k, k, k, k);
}

vertex FSOut v_fullscreen(uint vid [[vertex_id]])
{
    float2 p = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2) → triángulo que cubre la pantalla
    FSOut o;
    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);
    o.uv  = float2(p.x, 1.0 - p.y);
    return o;
}

// Umbral de brillo EN LINEAL (knee suave) → bloom. Esto es lo que separa glow pro de neón lavado.
fragment float4 f_threshold(FSOut in [[stage_in]],
                            texture2d<float> src [[texture(0)]],
                            sampler smp [[sampler(0)]],
                            constant PostU& u [[buffer(0)]])
{
    float3 c = src.sample(smp, in.uv).rgb;
    float  lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    float  k = max(0.0, lum - u.threshold);
    float3 bright = c * (k / max(lum, 1e-4));
    return float4(bright, 1.0);
}

// Blur separable (gaussiano de 5 taps). dir = (texel,0) o (0,texel).
fragment float4 f_blur(FSOut in [[stage_in]],
                       texture2d<float> src [[texture(0)]],
                       sampler smp [[sampler(0)]],
                       constant PostU& u [[buffer(0)]])
{
    float2 d = u.dir;
    float3 sum  = src.sample(smp, in.uv).rgb * 0.2270270270;
    sum += src.sample(smp, in.uv + d * 1.3846153846).rgb * 0.3162162162;
    sum += src.sample(smp, in.uv - d * 1.3846153846).rgb * 0.3162162162;
    sum += src.sample(smp, in.uv + d * 3.2307692308).rgb * 0.0702702703;
    sum += src.sample(smp, in.uv - d * 3.2307692308).rgb * 0.0702702703;
    return float4(sum, 1.0);
}

static inline float3 aces(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
static inline float3 linToSrgb(float3 c)
{
    c = clamp(c, 0.0, 1.0);
    return mix(12.92 * c, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}

// Composite HDR + bloom → tone-map fílmico → COLOR LAB (gradient map) → SAT/HUE → papel (BG) → sRGB.
fragment float4 f_composite(FSOut in [[stage_in]],
                            texture2d<float> hdr   [[texture(0)]],
                            texture2d<float> bloom [[texture(1)]],
                            texture2d<float> ramp  [[texture(2)]],
                            sampler smp [[sampler(0)]],
                            constant PostU& u [[buffer(0)]])
{
    float2 uv = in.uv;
    float  vig = 1.0;
    // KALEIDO: pliegue angular en N espejos alrededor del centro (muestrea una cuña espejada del HDR — el
    // clásico caleidoscopio VJ). Viñeta circular suave solo con kaleido activo (evita el smear del clamp).
    if (u.kaleidoSeg > 1u)
    {
        float2 d = uv - float2(0.5);
        float  r = length(d);
        float  seg = 6.2831853 / float(u.kaleidoSeg);
        float  ang = atan2(d.y, d.x);
        ang = ang - seg * floor(ang / seg);
        ang = min(ang, seg - ang);
        uv  = float2(0.5) + r * float2(cos(ang), sin(ang));
        vig = 1.0 - 0.85 * smoothstep(0.60, 0.72, r);
    }
    // ABERRACIÓN CROMÁTICA RADIAL: R hacia afuera, B hacia adentro sobre el vector al centro (lente real).
    // Escala con la distancia al centro (0 en el eje, máx en los bordes) y con aberrationAmt (C++ la pulsa con
    // el kick). aberrationAmt=0 → camino ORIGINAL byte-idéntico (else). El split abraza el bloom también.
    float3 c;
    if (u.aberrationAmt > 1e-5)
    {
        float2 rad2 = uv - float2(0.5);
        float2 off  = rad2 * (u.aberrationAmt * 0.030);
        float3 cr = float3(hdr.sample(smp, uv + off).r, hdr.sample(smp, uv).g, hdr.sample(smp, uv - off).b);
        float3 cb = float3(bloom.sample(smp, uv + off).r, bloom.sample(smp, uv).g, bloom.sample(smp, uv - off).b);
        c = cr + cb * u.bloomGain;
    }
    else
    {
        c = hdr.sample(smp, uv).rgb + bloom.sample(smp, uv).rgb * u.bloomGain;
    }
    // BLOOM ENVOLVENTE: anillo de taps a dos radios sobre el bloom ya difuminado → halo ancho y suave que
    // ABRAZA la luz (el look "caro" de glow multi-escala). bloomWide=0 → branch salteado → byte-exacto.
    if (u.bloomWide > 1e-5)
    {
        float3 wide = float3(0.0);
        for (int i = 0; i < 8; ++i)
        {
            float  a = 0.7853981634 * float(i);            // 8 direcciones (π/4)
            float2 o = float2(cos(a), sin(a));
            wide += bloom.sample(smp, uv + o * 0.010).rgb;
            wide += bloom.sample(smp, uv + o * 0.024).rgb;
        }
        c += wide * (u.bloomWide * u.bloomGain * 0.0625);   // /16 taps
    }
    c *= u.exposure * vig;
    c = aces(c);
    // GRADE DE COLORISTA (Phase A6, post-ACES pre-rampa): lift + contraste + split-tone. Neutro = byte-exacto.
    if (u.gradeLift != 0.0 || u.gradeContrast != 1.0 || u.splitAmt > 1e-5)
    {
        c = c + u.gradeLift * (1.0 - c);                 // LIFT: levanta sombras (más efecto abajo)
        c = clamp((c - 0.5) * u.gradeContrast + 0.5, 0.0, 1.0);   // CONTRASTE con pivote en 0.5
        if (u.splitAmt > 1e-5)                           // SPLIT-TONE: sombras vs luces con distinto tinte
        {
            float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
            float3 tint = mix(hsv2rgb(u.splitShadowHue, 0.6, 1.0), hsv2rgb(u.splitHighHue, 0.6, 1.0), lum);
            c = clamp(mix(c, c * (0.5 + tint), u.splitAmt), 0.0, 1.0);
        }
    }
    // COLOR LAB · GRADIENT MAP (post-ACES, pre-SAT/HUE): la luminancia se re-mapea a la rampa curada
    // (textura 1D horneada en CPU vía OKLab — LookRamps). El negro del fondo se vuelve ramp(0): el lienzo
    // deja de ser negro gratis. SAT/HUE corren DESPUÉS → SAT satura la paleta y HUE CYC la hace derivar.
    // Branch por uniform → rampAmt=0 = identidad byte-exacta (goldens intactos).
    if (u.rampAmt > 0.0001)
    {
        float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
        float3 rc = ramp.sample(smp, float2(clamp(lum, 0.0, 1.0), 0.5)).rgb;
        c = mix(c, rc, u.rampAmt);
    }
    // SAT/HUE post-ACES pre-sRGB (incluye el bloom → un mundo B/N es B/N completo). El branch por uniform
    // garantiza IDENTIDAD BYTE-EXACTA en defaults (sat=1, hue=0): mix/rotación con float no redondean igual.
    if (fabs(u.satAmt - 1.0) > 1e-4 || fabs(u.hueShift) > 1e-4)
    {
        // Saturación con pivote en luma Rec.709: 0 = monocromo editorial, 2 = vívido.
        float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
        c = max(float3(0.0), mix(float3(luma), c, u.satAmt));
        // Rotación de tono: Rodrigues alrededor del eje gris (1,1,1)/√3 — la luminancia queda ~intacta.
        float ca = cos(u.hueShift), sa = sin(u.hueShift);
        const float3 k = float3(0.5773502692);
        c = clamp(c * ca + cross(k, c) * sa + k * dot(k, c) * (1.0 - ca), 0.0, 1.0);
    }
    // PAPEL (BG, hallazgo riso): screen-blend del color de papel del look — levanta el lienzo (navy profundo,
    // crema editorial…) sin quemar las partículas. bgAmt=0 = identidad byte-exacta.
    if (u.bgAmt > 0.0001)
    {
        float3 paper = float3(u.paperR, u.paperG, u.paperB) * u.bgAmt;
        c = 1.0 - (1.0 - c) * (1.0 - paper);
    }
    c = linToSrgb(c);
    // GRANO DE PELÍCULA (espacio display, post-sRGB): hash animado por frame, MODULADO por luma (pico en los
    // medios, ~0 en negros/blancos puros — el grano "caro" no ensucia sombras ni quema luces). default 0 = id.
    if (u.grainAmt > 1e-5)
    {
        float2 gp = in.uv * 1024.0 + float2(u.grainTime * 71.0, u.grainTime * 113.0);
        float  n  = fract(sin(dot(gp, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
        float  lum = dot(c, float3(0.299, 0.587, 0.114));
        float  m   = 4.0 * lum * (1.0 - lum);      // 0 en 0/1, 1 en 0.5 — luma-modulado
        c += n * (u.grainAmt * 0.14) * m;
    }
    // DITHER (rompe el banding de 8 bits): ruido triangular ±1 LSB (diferencia de dos hashes → TPDF). Casi
    // invisible, mata los escalones de gradiente. default 0 = identidad byte-exacta.
    if (u.ditherAmt > 1e-5)
    {
        float2 dp = in.uv * 2048.0 + float2(u.grainTime, -u.grainTime);
        float  a  = fract(sin(dot(dp, float2(41.317, 289.11))) * 24634.6345);
        float  b  = fract(sin(dot(dp + 17.0, float2(97.113, 13.977))) * 51287.1913);
        c += (a - b) * (u.ditherAmt / 255.0);
    }
    c = clamp(c, 0.0, 1.0);
    return float4(c, 1.0);
}
)METAL";
}
