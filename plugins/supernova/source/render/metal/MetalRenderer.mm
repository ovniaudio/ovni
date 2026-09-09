#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>
#include <atomic>
#include "render/metal/MetalRenderer.h"
#include "render/Dissolve.h"
#include "render/metal/ShaderSource.h"
#include "render/metal/MetalSyphonServer.h"
#include "analysis/AnalysisFrame.h"
#include "image/ImageField.h"
#include "color/LookRamps.h"

// M2: pipeline de color PRO (RNF6). Partículas acumulan en HDR lineal (additive) → bloom (umbral en lineal +
// blur separable) → composite con tone-mapping fílmico (ACES) → sRGB. On-screen (CAMetalLayer) y offscreen
// (readback→RGBA8) comparten el mismo camino.
namespace supernova
{
namespace
{
struct Uniforms { float dt, time, chaos, intensity, pointSize, aspect,
                        bass, rms, treble, explodePulse,
                        curlScale, homeStrength, gravity, momentum,
                        radialGain, jitterGain, breatheGain,
                        rayPulse, rayDirX, rayDirY,
                        reformPulse, centerX, centerY;
                  unsigned count, motionMode;
                  float    cutoutAmt;
                  unsigned shapeMode;
                  float    trailAmt;
                  float    linksAmt;
                  float    trailFeed;
                  float    scatterAmt, pumpAmt, viewCos, viewSin;
                  float    depthAmt, yawCos, yawSin, pitCos, pitSin;   // cámara 3D (precomputada por tick)
                  float    formAmt;
                  unsigned formMode;                                   // FIGURA (motor geométrico)
                  float    fitX, fitY, resScale;
                  // FUNDIDO entre fotos: mezcla A→B por atributo. −1 = reposo (branch, sin operación nueva).
                  float    xfade; };   // fit + resScale + xfade; 41 f + 4 u = 180 B, espejo del MSL
struct PostU { float threshold, bloomGain, exposure, satAmt; float dirx, diry, hueShift; unsigned kaleidoSeg;
               float rampAmt, bgAmt, paperR, paperG, paperB;
               float grainAmt, ditherAmt, aberrationAmt, grainTime, bloomWide,
               gradeLift, gradeContrast, splitAmt, splitShadowHue, splitHighHue, q3; };   // == MSL (96 B)
struct LinkU { float radius, alphaGain, cutoutAmt, minDist2; unsigned nodeCount, activeCount, maxLines, p1;
               float depthAmt, formAmt; unsigned formMode; float xfade; };   // == MSL
struct F2 { float x, y; };
struct F4 { float r, g, b, a; };

// PLEXUS (capa 3): dimensiones del grid hash y presupuestos. Espejo de los `constant constexpr` del MSL.
constexpr int      kLinkGridC     = 16;
constexpr int      kSlotsPerCellC = 40;
constexpr unsigned kMaxLinkNodes  = 8192;    // subconjunto de nodos (uniforme, sesgado al sujeto con máscara)
constexpr unsigned kMaxLinkLines  = 40960;   // 8192 nodos × K=5

constexpr MTLPixelFormat kOutFormat = MTLPixelFormatBGRA8Unorm;
constexpr MTLPixelFormat kHdrFormat = MTLPixelFormatRGBA16Float;

inline float srgbToLinear (float c) { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
}

// Un destino de presentación: una CAMetalLayer con su PROPIO bundle de post (bloom nativo por tamaño) + una
// shareTex final legible (BGRA8). Habilita fullscreen a 2º monitor (N targets) y Syphon (textura legible), con
// UNA sola simulación por tick. [0] = preview del editor.
struct PresentTarget
{
    __weak CAMetalLayer* layer = nil;
    id<MTLTexture> hdr = nil, bloomA = nil, bloomB = nil;
    id<MTLTexture> shareTex = nil;   // BGRA8, RenderTarget|ShaderRead: la salida final legible (blit + Syphon)
    int  w = 0, h = 0;
    bool primary = false;            // el que publica Syphon (la salida grande/limpia)
    bool hdrInitialized = false;     // trails: Load sólo tras el primer Clear (textura nueva = undefined)
};

struct MetalRenderer::Impl
{
    id<MTLDevice>               device = nil;
    id<MTLCommandQueue>         queue  = nil;

    id<MTLComputePipelineState> simPSO       = nil;
    id<MTLRenderPipelineState>  drawPSO      = nil;   // partículas → HDR (additive)
    id<MTLRenderPipelineState>  fadePSO      = nil;   // estela: dst *= k (Zero/SourceColor), mismo pass
    id<MTLRenderPipelineState>  thresholdPSO = nil;
    id<MTLRenderPipelineState>  blurPSO      = nil;
    id<MTLRenderPipelineState>  compositePSO = nil;
    id<MTLSamplerState>         sampler      = nil;

    // PLEXUS (capa 3): kernels de vecindad + draw de líneas. Si algo falla en el compile, links queda
    // deshabilitado (linkReady=false) y el resto del motor sigue — nunca rompe el render.
    id<MTLComputePipelineState> linkClearPSO = nil, linkBinPSO = nil, linkEmitPSO = nil, linkArgsPSO = nil;
    id<MTLRenderPipelineState>  linkDrawPSO  = nil;
    id<MTLBuffer> linkNodes = nil, linkNodesB = nil, linkCellCount = nil, linkCellSlots = nil,
                  linkLineCount = nil, linkVerts = nil, linkArgs = nil;
    unsigned linkNodeCount = 0, linkNodeCountB = 0;   // B = la selección de la foto entrante (FUNDIDO)
    bool     linksEncoded  = false;   // los kernels corrieron este tick → el draw es válido
    bool linkReady() const { return linkClearPSO != nil && linkBinPSO != nil && linkEmitPSO != nil
                                 && linkArgsPSO != nil && linkDrawPSO != nil && linkNodes != nil; }

    id<MTLBuffer> positions = nil, velocities = nil, homes = nil, colors = nil;
    // Campos de CONTENIDO (ImageField/Vision, por celda de grilla): saliencia (float), flow tangente a bordes
    // (float2), y content2 = (burstDirX, burstDirY, signedDist, salVision) para el kick-desde-la-silueta.
    // Recomputados en uploadImage; el kernel los usa según el modo de movimiento.
    id<MTLBuffer> weights = nil, flow = nil, content2 = nil;
    // extra = (depth, formT, formTheta, libre) por partícula: profundidad (almohada/luma/CoreML) + rango y
    // ángulo áureo POR CLASE para la FIGURA (motor geométrico). Estático: se llena en uploadImage.
    id<MTLBuffer> extra = nil;
    // FUNDIDO — el juego B: los MISMOS cinco campos por partícula para la foto que ENTRA. La única
    // diferencia real entre dos fotos, para el motor, son estos cinco buffers y el aspecto (los `homes` son
    // la misma grilla para toda imagen). +15 MB a 262k partículas. Al terminar el fundido: swap de punteros
    // A↔B, sin copia.
    id<MTLBuffer> colorsB = nil, weightsB = nil, flowB = nil, content2B = nil, extraB = nil;
    Dissolve dissolve;                 // la curva (pura, avanzada con el dt del render)
    double   dissolveSeconds = 0.0;    // 0 = CORTE, el default: el renderer nace como hoy
    bool     haveImage       = false;  // la primera imagen nunca funde (no hay desde qué)
    float    imgAspectB = 1.0f, contentCXB = 0.5f, contentCYB = 0.5f;   // destino del fundido
    float    imgAspectEff = 1.0f;      // aspecto del TICK (== imgAspect en reposo: asignación, byte-exacto)
    // Nodos del plexus de la foto entrante: se recomputan con la máscara nueva y se aplican AL TERMINAR
    // (por swap de punteros, sin escribir memoria que la GPU podría estar leyendo). Recomputarlos en pleno
    // fundido haría titilar la constelación.
    // COLOR LAB: rampa del look activo (256×1 RGBA32F display-lineal, bake CPU LookRamps) — se re-hornea
    // SOLO al cambiar de paleta (lastPalette). Siempre bindeada al composite (el branch decide si se lee).
    id<MTLTexture> rampTex = nil;
    int lastPalette = -1;
    float contentCX = 0.5f, contentCY = 0.5f;   // centroide de luminancia de la imagen cargada
    float imgAspect = 1.0f;                      // w/h de la imagen cargada → FIT del aspecto por target
    int   fitMode   = 0;                         // 0 = FIT (contain) · 1 = FILL (cover) — MEDIA SESSION PRO

    std::vector<PresentTarget> targets;   // present N pantallas con 1 sim (preview + fullscreen)
    Uniforms curU {};                     // uniforms del tick (encodeSimulate → lo reusa el draw por target)
    float curDtFrames = 1.0f;             // duración del tick en cuadros del ancla de 120 Hz (ver frameSteps)

    PresentTarget offTarget;              // camino offscreen (determinista, golden frames)
    id<MTLBuffer>  readback = nil;
    int offW = 0, offH = 0;
    double offDt = 1.0 / 60.0;            // paso de tiempo del offscreen (default = el histórico)
    bool offSizeInvariance = false;       // resScale también offscreen (default off = legacy byte-exacto)

    std::unique_ptr<MetalSyphonServer> syphon;
    bool syphonEnabled = false;

    int gridW = 0, gridH = 0;
    unsigned count = 0;
    bool  ready = false;
    float time  = 0.0f;
    float explodePulse = 0.0f;
    float reformPulse  = 0.0f;   // sombra lenta del explodePulse: re-armado enérgico post-explosión
    float viewAngle = 0.0f;      // ROTATE: ángulo de vista acumulado (rad, envuelto)
    float huePhase  = 0.0f;      // HUE CYC: deriva de tono acumulada (rad, envuelta)
    float orbitPhase = 0.0f;     // ORBIT: yaw auto-acumulado (rad, envuelto) — se suma al ROT Y del knob
    float rayPulse = 0.0f, rayDirX = 1.0f, rayDirY = 0.0f;   // rayo direccional MIDI (M3), decae como explodePulse
    unsigned lastOnsetCount = 0;   // edge-detect del contador monotónico (el bool de onset puede pisarse)
    bool     onsetSeeded = false;  // el PRIMER cuadro siembra el contador; no golpea (ver encodeSimulate)

    // Densidad adaptativa (RNF2): se dibuja un PREFIJO de los buffers; `order` permuta las celdas de la grilla
    // (semilla fija → determinista) para que cualquier prefijo sea un submuestreo espacialmente uniforme, no un
    // recorte. `activeCount` sube/baja según el COSTO GPU real (no el dt de vblank, que está capado por el vsync
    // y haría inalcanzable la recuperación a 60 Hz). `lastGpuMs` lo publica el completion handler (hilo de Metal).
    std::vector<uint32_t> order;
    unsigned activeCount = 0;
    float    emaFrameMs  = 8.0f;
    std::atomic<float> lastGpuMs { 6.0f };
    id<MTLCommandBuffer> lastCb = nil;   // último cb on-screen — para drenar antes de reescribir buffers (upload)

    // Diagnóstico del camino EN VIVO (el que los goldens no cubren): una línea a stderr cada ~120 frames.
    int  diagFrames = 0, diagNilDrawables = 0;

    id<MTLRenderPipelineState> makePostPSO (id<MTLLibrary> lib, NSString* frag, MTLPixelFormat fmt, NSError** err);
    void ensureTargetTex (PresentTarget& t, int w, int h);
    void adapt();
    // Split sim/present: la simulación avanza UNA vez por tick (encodeSimulate); el composite corre por target
    // (encodeComposite) → sin doble física con fullscreen abierto.
    void encodeSimulate (id<MTLCommandBuffer> cb, const AnalysisFrame& frame, const ParticleParams& p,
                         float dt, unsigned nActive);
    void encodeComposite (id<MTLCommandBuffer> cb, PresentTarget& t, const ParticleParams& p, unsigned nActive);
    PresentTarget* findTarget (CAMetalLayer* l);

    // FUNDIDO — los tres momentos del ciclo de vida (ver uploadImage / encodeSimulate / snapToHome).
    void fillSlot (bool intoB, const SourceImage& img);   // llena los 5 campos + aspecto/centroide/nodos
    void bakeDissolve();                                  // A ← mix(A, B, m): retarget sin salto
    void finishDissolve();                                // swap A↔B (sin copia) + nodos pendientes
};

MetalRenderer::MetalRenderer() : impl (std::make_unique<Impl>())
{
    impl->device = MTLCreateSystemDefaultDevice();
    if (impl->device != nil)
        impl->queue = [impl->device newCommandQueue];
}

MetalRenderer::~MetalRenderer()
{
    // Drenar el último frame en vuelo antes de destruir Impl: su completion handler captura &lastGpuMs, así que
    // el cb NO debe sobrevivir a Impl (sería un write a memoria liberada desde el hilo de Metal).
    if (impl && impl->lastCb != nil) [impl->lastCb waitUntilCompleted];
}

bool  MetalRenderer::isAvailable() const { return impl->device != nil && impl->queue != nil; }
void* MetalRenderer::deviceHandle() const { return (__bridge void*) impl->device; }

PresentTarget* MetalRenderer::Impl::findTarget (CAMetalLayer* l)
{
    for (auto& t : targets) if (t.layer == l) return &t;
    return nullptr;
}

// Config común de una CAMetalLayer que vamos a presentar: mismo device/formato + framebufferOnly=NO (el drawable
// es DESTINO de blit shareTex→drawable).
static void configureLayer (CAMetalLayer* layer, id<MTLDevice> dev)
{
    if (layer == nil || dev == nil) return;
    layer.device = dev;
    layer.pixelFormat = kOutFormat;
    layer.framebufferOnly = NO;
}

void MetalRenderer::setLayer (void* l)   // compat: el preview del editor = target[0], primary
{
    addPresentTarget (l, /*primary*/ true);
}

void MetalRenderer::addPresentTarget (void* caMetalLayer, bool primary)
{
    CAMetalLayer* layer = (__bridge CAMetalLayer*) caMetalLayer;
    if (layer == nil || impl->device == nil) return;
    configureLayer (layer, impl->device);
    if (impl->findTarget (layer) == nullptr)
    {
        PresentTarget t; t.layer = layer; t.primary = primary;
        impl->targets.push_back (t);
    }
    if (primary)                                   // sólo uno primary (el que publica Syphon)
        for (auto& t : impl->targets) t.primary = (t.layer == layer);
}

void MetalRenderer::removePresentTarget (void* caMetalLayer)
{
    CAMetalLayer* layer = (__bridge CAMetalLayer*) caMetalLayer;
    const bool wasPrimary = [&] { auto* t = impl->findTarget (layer); return t && t->primary; }();
    impl->targets.erase (std::remove_if (impl->targets.begin(), impl->targets.end(),
                                         [layer] (const PresentTarget& t) { return t.layer == layer; }),
                         impl->targets.end());
    if (wasPrimary && ! impl->targets.empty())     // re-promover el preview
        impl->targets.front().primary = true;
}

id<MTLRenderPipelineState> MetalRenderer::Impl::makePostPSO (id<MTLLibrary> lib, NSString* frag,
                                                            MTLPixelFormat fmt, NSError** err)
{
    MTLRenderPipelineDescriptor* d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction   = [lib newFunctionWithName:@"v_fullscreen"];
    d.fragmentFunction = [lib newFunctionWithName:frag];
    d.colorAttachments[0].pixelFormat = fmt;
    return [device newRenderPipelineStateWithDescriptor:d error:err];
}

void MetalRenderer::prepare (int gridW, int gridH)
{
    if (! isAvailable()) return;
    impl->gridW = gridW; impl->gridH = gridH;
    impl->count = (unsigned) (gridW * gridH);

    NSError* err = nil;
    id<MTLLibrary> lib = [impl->device newLibraryWithSource:@(kParticleShaderSource) options:nil error:&err];
    if (lib == nil)
    {
        std::fprintf (stderr, "[supernova] shader compile FAILED: %s\n",
                      err ? err.localizedDescription.UTF8String : "?");
        impl->ready = false; return;
    }

    impl->simPSO = [impl->device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k_simulate"]
                                                               error:&err];

    // Partículas → HDR lineal, blending ADDITIVE (acumulación de luz).
    MTLRenderPipelineDescriptor* rpd = [[MTLRenderPipelineDescriptor alloc] init];
    rpd.vertexFunction   = [lib newFunctionWithName:@"v_particle"];
    rpd.fragmentFunction = [lib newFunctionWithName:@"f_particle"];
    rpd.colorAttachments[0].pixelFormat                 = kHdrFormat;
    rpd.colorAttachments[0].blendingEnabled             = YES;
    rpd.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
    rpd.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
    rpd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorOne;
    rpd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
    rpd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
    rpd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    impl->drawPSO = [impl->device newRenderPipelineStateWithDescriptor:rpd error:&err];

    // Estela (trails): quad multiplicativo en el pass HDR.
    {
        MTLRenderPipelineDescriptor* fd = [[MTLRenderPipelineDescriptor alloc] init];
        fd.vertexFunction   = [lib newFunctionWithName:@"v_fullscreen"];
        fd.fragmentFunction = [lib newFunctionWithName:@"f_fade"];
        fd.colorAttachments[0].pixelFormat                 = kHdrFormat;
        fd.colorAttachments[0].blendingEnabled             = YES;
        fd.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
        fd.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
        fd.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorZero;
        fd.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorZero;
        fd.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorSourceColor;
        fd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        impl->fadePSO = [impl->device newRenderPipelineStateWithDescriptor:fd error:&err];
    }
    impl->thresholdPSO = impl->makePostPSO (lib, @"f_threshold", kHdrFormat, &err);
    impl->blurPSO      = impl->makePostPSO (lib, @"f_blur",      kHdrFormat, &err);
    impl->compositePSO = impl->makePostPSO (lib, @"f_composite", kOutFormat, &err);

    // PLEXUS (capa 3): kernels de vecindad + PSO de líneas (additive al MISMO HDR que las partículas).
    {
        auto makeCompute = [&] (NSString* name) -> id<MTLComputePipelineState>
        {
            id<MTLFunction> fn = [lib newFunctionWithName:name];
            return fn != nil ? [impl->device newComputePipelineStateWithFunction:fn error:&err] : nil;
        };
        impl->linkClearPSO = makeCompute (@"k_linkClear");
        impl->linkBinPSO   = makeCompute (@"k_linkBin");
        impl->linkEmitPSO  = makeCompute (@"k_linkEmit");
        impl->linkArgsPSO  = makeCompute (@"k_linkArgs");

        MTLRenderPipelineDescriptor* ld = [[MTLRenderPipelineDescriptor alloc] init];
        ld.vertexFunction   = [lib newFunctionWithName:@"v_link"];
        ld.fragmentFunction = [lib newFunctionWithName:@"f_link"];
        ld.colorAttachments[0].pixelFormat                 = kHdrFormat;
        ld.colorAttachments[0].blendingEnabled             = YES;
        ld.colorAttachments[0].rgbBlendOperation           = MTLBlendOperationAdd;
        ld.colorAttachments[0].alphaBlendOperation         = MTLBlendOperationAdd;
        ld.colorAttachments[0].sourceRGBBlendFactor        = MTLBlendFactorOne;
        ld.colorAttachments[0].sourceAlphaBlendFactor      = MTLBlendFactorOne;
        ld.colorAttachments[0].destinationRGBBlendFactor   = MTLBlendFactorOne;
        ld.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        impl->linkDrawPSO = [impl->device newRenderPipelineStateWithDescriptor:ld error:&err];

        const unsigned cells = (unsigned) (kLinkGridC * kLinkGridC);
        impl->linkNodes     = [impl->device newBufferWithLength:kMaxLinkNodes * sizeof (uint32_t)
                                                        options:MTLResourceStorageModeShared];
        impl->linkNodesB    = [impl->device newBufferWithLength:kMaxLinkNodes * sizeof (uint32_t)
                                                        options:MTLResourceStorageModeShared];
        impl->linkCellCount = [impl->device newBufferWithLength:cells * sizeof (uint32_t)
                                                        options:MTLResourceStorageModePrivate];
        impl->linkCellSlots = [impl->device newBufferWithLength:cells * kSlotsPerCellC * sizeof (uint32_t)
                                                        options:MTLResourceStorageModePrivate];
        impl->linkLineCount = [impl->device newBufferWithLength:sizeof (uint32_t)
                                                        options:MTLResourceStorageModePrivate];
        impl->linkVerts     = [impl->device newBufferWithLength:kMaxLinkLines * 2 * 32   // sizeof(LinkVert) MSL
                                                        options:MTLResourceStorageModePrivate];
        impl->linkArgs      = [impl->device newBufferWithLength:4 * sizeof (uint32_t)
                                                        options:MTLResourceStorageModePrivate];
        if (! impl->linkReady())
            std::fprintf (stderr, "[supernova] plexus deshabilitado (pipeline links no compiló): %s\n",
                          err ? err.localizedDescription.UTF8String : "?");
    }

    MTLSamplerDescriptor* sd = [[MTLSamplerDescriptor alloc] init];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    impl->sampler = [impl->device newSamplerStateWithDescriptor:sd];

    // COLOR LAB: textura de rampa (256×1 RGBA32F). Shared en Apple Silicon / Managed en Intel — replaceRegion
    // sincroniza en ambos. Arranca horneada con el look 0 (inocua: el branch rampAmt=0 nunca la lee).
    {
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                         width:(NSUInteger) color::kRampSize height:1 mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = impl->device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
        impl->rampTex = [impl->device newTextureWithDescriptor:td];
        if (impl->rampTex != nil)
        {
            const auto texels = color::bakeRamp (0);
            [impl->rampTex replaceRegion:MTLRegionMake2D (0, 0, (NSUInteger) color::kRampSize, 1)
                             mipmapLevel:0 withBytes:texels.data()
                             bytesPerRow:(NSUInteger) color::kRampSize * 16];
            impl->lastPalette = 0;
        }
    }

    if (impl->simPSO == nil || impl->drawPSO == nil || impl->fadePSO == nil || impl->thresholdPSO == nil
        || impl->blurPSO == nil || impl->compositePSO == nil)
    {
        std::fprintf (stderr, "[supernova] pipeline FAILED: %s\n", err ? err.localizedDescription.UTF8String : "?");
        impl->ready = false; return;
    }

    const unsigned n = impl->count;
    impl->positions  = [impl->device newBufferWithLength:n * sizeof (F2) options:MTLResourceStorageModeShared];
    impl->velocities = [impl->device newBufferWithLength:n * sizeof (F2) options:MTLResourceStorageModeShared];
    impl->homes      = [impl->device newBufferWithLength:n * sizeof (F2) options:MTLResourceStorageModeShared];
    impl->colors     = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    impl->weights    = [impl->device newBufferWithLength:n * sizeof (float) options:MTLResourceStorageModeShared];
    impl->flow       = [impl->device newBufferWithLength:n * sizeof (F2) options:MTLResourceStorageModeShared];
    impl->content2   = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    impl->extra      = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    // FUNDIDO — juego B (la foto que entra). Siempre bindeados: el shader los declara aunque en reposo
    // (xfade < 0) ni los lea. Arrancan como copia de A tras el uploadImage de fábrica de más abajo.
    impl->colorsB    = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    impl->weightsB   = [impl->device newBufferWithLength:n * sizeof (float) options:MTLResourceStorageModeShared];
    impl->flowB      = [impl->device newBufferWithLength:n * sizeof (F2) options:MTLResourceStorageModeShared];
    impl->content2B  = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    impl->extraB     = [impl->device newBufferWithLength:n * sizeof (F4) options:MTLResourceStorageModeShared];
    std::memset (impl->velocities.contents, 0, n * sizeof (F2));

    // Permutación de celdas (Fisher-Yates, xorshift semilla fija). El slot k del buffer toma la celda order[k];
    // así un prefijo [0,activeCount) cubre celdas aleatorias = densidad uniforme al bajar partículas (RNF2).
    impl->order.resize (n);
    for (unsigned i = 0; i < n; ++i) impl->order[i] = i;
    uint32_t s = 0x9E3779B9u;
    for (unsigned i = n; i > 1; --i)
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        const unsigned j = s % i;
        std::swap (impl->order[i - 1], impl->order[j]);
    }
    impl->activeCount = n;
    impl->emaFrameMs  = 8.0f;
    impl->haveImage   = false;   // prepare() re-arma: la imagen de fábrica entra por CORTE, sin fundido
    impl->dissolve    = {};
    impl->ready = true;

    uploadImage ({});
}

// FUNDIDO — llena UN juego de campos por partícula desde una imagen: colores, pesos, flow, content2
// (dirección de estallido / distancia con signo / máscara) y extra (depth + figura), más el aspecto, el
// centroide y la selección de nodos del plexus. `intoB` elige el juego: A = lo que está en pantalla (el
// camino de siempre), B = la foto que ENTRA durante un fundido.
//
// NO toca positions / velocities / homes: los `homes` son la MISMA grilla para toda imagen (entre dos fotos
// no hay ningún "viaje" que hacer), y teletransportar las partículas es justamente el golpe que se saca.
void MetalRenderer::Impl::fillSlot (bool intoB, const SourceImage& img)
{
    id<MTLBuffer> bCol = intoB ? colorsB   : colors;
    id<MTLBuffer> bWgt = intoB ? weightsB  : weights;
    id<MTLBuffer> bFlw = intoB ? flowB     : flow;
    id<MTLBuffer> bCn2 = intoB ? content2B : content2;
    id<MTLBuffer> bExt = intoB ? extraB    : extra;
    auto* col = (F4*)    bCol.contents;
    auto* wgt = (float*) bWgt.contents;
    auto* flw = (F2*)    bFlw.contents;
    auto* cn2 = (F4*)    bCn2.contents;
    auto* ext = (F4*)    bExt.contents;

    // Aspecto de la imagen para el FIT (w/h). Sin imagen (fallback procedural) → 1 (cuadrado, sin fit).
    const float aspect = (img.width > 0 && img.height > 0) ? (float) img.width / (float) img.height : 1.0f;

    // Análisis de CONTENIDO (ImageField, puro, testeado): saliencia + flow por celda + centroide. Con imagen
    // nula devuelve el campo neutro (pesos 1, flow 0, centro 0.5) → fábrica/fallback idénticos a siempre.
    const auto field = ImageField::compute (img.rgba, img.width, img.height, gridW, gridH);
    if (intoB) { imgAspectB = aspect; contentCXB = field.centroidX; contentCYB = field.centroidY; }
    else       { imgAspect  = aspect; contentCX  = field.centroidX; contentCY  = field.centroidY; }

    // Sujeto para el modo NATURAL: saliencia REAL si vino (Vision, computada por el caller en su hilo de
    // decode), si no una derivada de los pesos de luma (fallback: sigue habiendo silueta con contraste).
    const size_t cellsN = (size_t) gridW * (size_t) gridH;
    std::vector<float> sal;
    if (img.saliency != nullptr)
        sal.assign (img.saliency, img.saliency + cellsN);
    else
    {
        sal.resize (cellsN);
        for (size_t i = 0; i < cellsN; ++i)
            sal[i] = (field.weight[i] - 0.25f) / 0.75f;   // deshacer el piso → [0..1] por luma
    }
    const auto subject = ImageField::computeSubject (sal, gridW, gridH);

    // DEPTH por celda (3D de presentación): override del caller (CoreML, cuando llegue) o el mapa procedural
    // (almohada desde la máscara + detalle por luma; modo foto sin máscara). Determinista → goldens estables.
    std::vector<float> depthMap;
    if (img.depth != nullptr)
        depthMap.assign (img.depth, img.depth + cellsN);
    else
    {
        std::vector<float> maskVec;
        if (img.subjectMask != nullptr) maskVec.assign (img.subjectMask, img.subjectMask + cellsN);
        depthMap = ImageField::computeDepth (img.rgba, img.width, img.height, maskVec,
                                             (img.saliency != nullptr) ? sal : std::vector<float> {},
                                             gridW, gridH);
    }

    // FIGURA: rango + ángulo áureo POR CLASE (sujeto/fondo por separado, en orden de slot — determinista).
    // Por clase para que la escultura recortada forme la figura DENSA, sin los huecos del fondo invisible.
    const bool haveMaskF = (img.subjectMask != nullptr);
    unsigned nSub = 0, nBg = 0;
    for (unsigned k = 0; k < count; ++k)
    {
        if (! haveMaskF || img.subjectMask[order[k]] >= 0.5f) ++nSub;
        else                                                  ++nBg;
    }
    unsigned subRank = 0, bgRank = 0;

    // Slot k del buffer ← celda order[k] de la grilla (ver permutación en prepare). Un prefijo de slots =
    // submuestreo espacialmente uniforme (densidad adaptativa, RNF2).
    for (unsigned k = 0; k < count; ++k)
    {
        const unsigned cell = order[k];
        const int gx = (int) (cell % (unsigned) gridW);
        const int gy = (int) (cell / (unsigned) gridW);
        const float u = (gx + 0.5f) / (float) gridW;
        const float v = (gy + 0.5f) / (float) gridH;
        wgt[k] = (img.saliency != nullptr) ? 0.25f + 0.75f * sal[cell]   // saliencia REAL (Vision)
                                           : field.weight[cell];          // fallback por luma
        flw[k] = { field.flowXY[(size_t) cell * 2], field.flowXY[(size_t) cell * 2 + 1] };
        // content2.w = MÁSCARA del sujeto (1 sin máscara): el CUTOUT gradual se aplica EN VIVO en el shader
        // (uniform cutoutAmt sobre alpha + física) — el knob no requiere re-upload.
        cn2[k] = { subject.burstXY[(size_t) cell * 2], subject.burstXY[(size_t) cell * 2 + 1],
                   subject.signedDist[cell],
                   (img.subjectMask != nullptr) ? img.subjectMask[cell] : 1.0f };

        // extra = (depth, formT, formTheta, libre): profundidad + FIGURA (rango/θ áureo por clase).
        {
            const bool sub = ! haveMaskF || img.subjectMask[cell] >= 0.5f;
            const unsigned rank   = sub ? subRank++ : bgRank++;
            const unsigned classN = std::max (1u, sub ? nSub : nBg);
            const double  theta   = std::fmod ((double) rank * 2.39996322972865332, 6.28318530717958648);
            ext[k] = { depthMap[cell], ((float) rank + 0.5f) / (float) classN, (float) theta, 0.0f };
        }

        if (img.rgba != nullptr && img.width > 0 && img.height > 0)
        {
            int ix = (int) (u * img.width);  ix = ix < 0 ? 0 : (ix >= img.width  ? img.width  - 1 : ix);
            int iy = (int) (v * img.height); iy = iy < 0 ? 0 : (iy >= img.height ? img.height - 1 : iy);
            const uint8_t* px = img.rgba + ((size_t) iy * img.width + ix) * 4;
            // sRGB → LINEAL (acumulamos en lineal, RNF6).
            col[k] = { srgbToLinear (px[0] / 255.0f), srgbToLinear (px[1] / 255.0f),
                       srgbToLinear (px[2] / 255.0f), px[3] / 255.0f };
        }
        else
        {
            col[k] = { srgbToLinear (u), srgbToLinear (v), srgbToLinear (0.6f), 1.0f };
        }
    }

    // Nodos del PLEXUS (capa 3): subconjunto de slots para las conexiones. SUJETO PRIMERO (con máscara, la
    // constelación vive en la figura), después el fondo hasta llenar el presupuesto. El orden permutado de los
    // slots ya es espacialmente uniforme → cualquier prefijo cubre parejo.
    id<MTLBuffer> bNodes = intoB ? linkNodesB : linkNodes;
    if (bNodes != nil)
    {
        auto* nodes = (uint32_t*) bNodes.contents;
        unsigned filled = 0;
        const bool haveMask = (img.subjectMask != nullptr);
        for (int pass = 0; pass < 2 && filled < kMaxLinkNodes; ++pass)
            for (unsigned k = 0; k < count && filled < kMaxLinkNodes; ++k)
            {
                const bool subj = ! haveMask || img.subjectMask[order[k]] >= 0.35f;
                if (subj == (pass == 0))
                    nodes[filled++] = k;
            }
        if (intoB) linkNodeCountB = filled;
        else       linkNodeCount  = filled;
    }
}

// RETARGET (llega otra foto con el fundido en vuelo): se hornea UNA vez en CPU A ← mix(A, B, m) y el fundido
// re-arranca de 0 sobre esa base. Sin esto, una ráfaga de KICK saltaría en cada cambio. 262k × 15 floats,
// una pasada: se paga sólo cuando dos cambios se pisan.
void MetalRenderer::Impl::bakeDissolve()
{
    const float m = dissolve.mix01();
    auto lerp = [m] (float a, float b) { return a + (b - a) * m; };
    auto* cA = (F4*)    colors.contents;    auto* cB = (const F4*)    colorsB.contents;
    auto* wA = (float*) weights.contents;   auto* wB = (const float*) weightsB.contents;
    auto* fA = (F2*)    flow.contents;      auto* fB = (const F2*)    flowB.contents;
    auto* nA = (F4*)    content2.contents;  auto* nB = (const F4*)    content2B.contents;
    auto* eA = (F4*)    extra.contents;     auto* eB = (const F4*)    extraB.contents;
    for (unsigned k = 0; k < count; ++k)
    {
        cA[k] = { lerp (cA[k].r, cB[k].r), lerp (cA[k].g, cB[k].g),
                  lerp (cA[k].b, cB[k].b), lerp (cA[k].a, cB[k].a) };
        wA[k] = lerp (wA[k], wB[k]);
        fA[k] = { lerp (fA[k].x, fB[k].x), lerp (fA[k].y, fB[k].y) };
        nA[k] = { lerp (nA[k].r, nB[k].r), lerp (nA[k].g, nB[k].g),
                  lerp (nA[k].b, nB[k].b), lerp (nA[k].a, nB[k].a) };
        eA[k] = { lerp (eA[k].r, eB[k].r), lerp (eA[k].g, eB[k].g),
                  lerp (eA[k].b, eB[k].b), lerp (eA[k].a, eB[k].a) };
    }
    imgAspect = lerp (imgAspect, imgAspectB);
    contentCX = lerp (contentCX, contentCXB);
    contentCY = lerp (contentCY, contentCYB);
}

// El fundido llegó a destino: B pasa a ser el juego ACTUAL por swap de punteros (sin copia, sin escribir
// memoria que la GPU pueda estar leyendo) y xfade vuelve a −1. Deliberadamente NO se rinde un mix(A, B, 1.0):
// a + (b − a) no es `b` en punto flotante, y ese último cuadro se apartaría del camino byte-exacto.
void MetalRenderer::Impl::finishDissolve()
{
    // __strong explícito: bajo ARC una referencia a `id` se asume __autoreleasing y no ata a un ivar.
    auto swapBuf = [] (__strong id<MTLBuffer>& a, __strong id<MTLBuffer>& b)
                   { id<MTLBuffer> t = a; a = b; b = t; };
    swapBuf (colors, colorsB);
    swapBuf (weights, weightsB);
    swapBuf (flow, flowB);
    swapBuf (content2, content2B);
    swapBuf (extra, extraB);
    swapBuf (linkNodes, linkNodesB);
    linkNodeCount = linkNodeCountB;   // la constelación se re-selecciona con la máscara nueva, de una vez
    imgAspect = imgAspectB;
    contentCX = contentCXB;
    contentCY = contentCYB;
    dissolve = {};
}

// FUNDIDO — cuánto dura la disolución entre fotos. 0 = CORTE, exactamente como hoy (y el default: el
// renderer nace en el comportamiento histórico y es el editor/el export el que sube el valor, mismo patrón
// que los defaults de ParticleParams).
void MetalRenderer::setDissolveSeconds (double s) { impl->dissolveSeconds = s > 0.0 ? s : 0.0; }
double MetalRenderer::dissolveSeconds() const     { return impl->dissolveSeconds; }

void MetalRenderer::uploadImage (const SourceImage& img)
{
    if (! impl->ready) return;
    // Los buffers son Shared (CPU+GPU). Si un frame previo sigue en vuelo leyéndolos, esperar a que complete
    // antes de reescribirlos (evita la data race CPU↔GPU). Barato: uploadImage sólo corre en drag&drop/prepare,
    // no en el steady-state. (renderOffscreen ya hace waitUntilCompleted; ahí lastCb queda nil.)
    if (impl->lastCb != nil) { [impl->lastCb waitUntilCompleted]; impl->lastCb = nil; }

    // ¿Funde o corta? Funde si hay duración pedida Y ya hay una foto en pantalla desde la cual disolver
    // (la primera imagen —fábrica, restore de sesión— no tiene desde qué: es un corte, como siempre).
    const bool fade = impl->dissolveSeconds > 0.0 && impl->haveImage;
    if (fade && impl->dissolve.active()) impl->bakeDissolve();   // retarget: A ← mix(A, B, m)

    impl->fillSlot (fade, img);

    if (fade)
    {
        impl->dissolve.start (impl->dissolveSeconds);
    }
    else
    {
        // CORTE — el camino de siempre, byte por byte: cada partícula al centro de su celda, velocidades a
        // cero. Los `homes` se reescriben con los mismos valores (son la misma grilla para toda imagen).
        auto* pos = (F2*) impl->positions.contents;
        auto* hom = (F2*) impl->homes.contents;
        for (unsigned k = 0; k < impl->count; ++k)
        {
            const unsigned cell = impl->order[k];
            const int gx = (int) (cell % (unsigned) impl->gridW);
            const int gy = (int) (cell / (unsigned) impl->gridW);
            const float u = (gx + 0.5f) / (float) impl->gridW;
            const float v = (gy + 0.5f) / (float) impl->gridH;
            hom[k] = { u, v };
            pos[k] = { u, v };
        }
        std::memset (impl->velocities.contents, 0, impl->count * sizeof (F2));
        impl->dissolve = {};   // un corte cancela cualquier fundido en vuelo
    }
    impl->haveImage = true;
}

// CLEAR es un CORTE (pedido de campo: "que al toque lo ponga limpio — el viaje al hogar tarda mucho"):
// posiciones ← hogar, velocidades ← 0, y el estado ACUMULADO del mundo a cero (sin esto el lienzo saldría
// girado por ROTATE/ORBIT viejos, con el tono derivado por HUE CYC, o en plena explosión). Mismo patrón
// anti-race que uploadImage: los buffers son Shared → drenar el frame en vuelo antes de escribirlos.
void MetalRenderer::snapToHome()
{
    if (! impl->ready) return;
    if (impl->lastCb != nil) { [impl->lastCb waitUntilCompleted]; impl->lastCb = nil; }
    // CLEAR corta también el FUNDIDO en vuelo: la foto que estaba entrando pasa a ser la actual de una vez
    // (decisión de campo — CLEAR es "limpio YA", no "limpio dentro de medio segundo").
    if (impl->dissolve.active()) impl->finishDissolve();
    std::memcpy (impl->positions.contents, impl->homes.contents, impl->count * sizeof (F2));
    std::memset (impl->velocities.contents, 0, impl->count * sizeof (F2));
    impl->viewAngle = 0.0f; impl->huePhase = 0.0f; impl->orbitPhase = 0.0f;
    impl->explodePulse = 0.0f; impl->reformPulse = 0.0f; impl->rayPulse = 0.0f;
    // La siembra del contador de onsets NO se toca (LOW-2 del revisor del 45). `lastOnsetCount` ya vale el
    // del último cuadro rendido, así que el flanco del cuadro siguiente sigue siendo el correcto: sólo
    // dispara si entró un golpe NUEVO. Ponerlo en false hacía que ese cuadro re-sembrara sin disparar, y un
    // kick real detectable únicamente por el contador (el camino de respaldo, cuando el pipeline
    // latest-wins pisa el bool) se perdía. Si el CLEAR llega antes del primer cuadro, `onsetSeeded` ya es
    // false y sigue siéndolo: la protección contra la explosión falsa del cuadro 0 queda intacta.
}

// VIDEO: recolorea el lattice desde un frame RGBA — SOLO el buffer de colores (mismo muestreo por celda que
// uploadImage: cell = order[k], u,v del centro de celda, sRGB→lineal). No toca homes/flow/máscara/depth →
// barato (~262k samples). Mismo drain anti-race que uploadImage (buffer Shared). imgAspect ya lo fijó el
// uploadImage del primer frame; acá no cambia (un video tiene aspecto constante).
void MetalRenderer::updateColors (const uint8_t* rgba, int w, int h)
{
    if (! impl->ready || rgba == nullptr || w <= 0 || h <= 0) return;
    if (impl->lastCb != nil) { [impl->lastCb waitUntilCompleted]; impl->lastCb = nil; }
    // FUNDIDO en vuelo (el primer frame del video entró disolviéndose): el color nuevo va al juego ENTRANTE.
    // Si escribiera en A, el video terminaría fundiendo hacia la foto vieja en vez de hacia sí mismo.
    auto* col = (F4*) (impl->dissolve.active() ? impl->colorsB : impl->colors).contents;
    for (unsigned k = 0; k < impl->count; ++k)
    {
        const unsigned cell = impl->order[k];
        const int gx = (int) (cell % (unsigned) impl->gridW);
        const int gy = (int) (cell / (unsigned) impl->gridW);
        const float u = (gx + 0.5f) / (float) impl->gridW;
        const float v = (gy + 0.5f) / (float) impl->gridH;
        int ix = (int) (u * w);  ix = ix < 0 ? 0 : (ix >= w ? w - 1 : ix);
        int iy = (int) (v * h);  iy = iy < 0 ? 0 : (iy >= h ? h - 1 : iy);
        const uint8_t* px = rgba + ((size_t) iy * w + ix) * 4;
        col[k] = { srgbToLinear (px[0] / 255.0f), srgbToLinear (px[1] / 255.0f),
                   srgbToLinear (px[2] / 255.0f), px[3] / 255.0f };
    }
}

void MetalRenderer::resize (int, int, float) {}

void MetalRenderer::Impl::ensureTargetTex (PresentTarget& t, int w, int h)
{
    if (t.hdr != nil && t.w == w && t.h == h) return;
    t.w = w; t.h = h;
    const int bw = std::max (1, w / 2), bh = std::max (1, h / 2);

    auto make = [this] (MTLPixelFormat fmt, int tw, int th)
    {
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                     width:(NSUInteger) tw
                                                                                    height:(NSUInteger) th
                                                                                 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModePrivate;
        return [device newTextureWithDescriptor:td];
    };
    t.hdr      = make (kHdrFormat, w, h);
    t.bloomA   = make (kHdrFormat, bw, bh);
    t.bloomB   = make (kHdrFormat, bw, bh);
    t.shareTex = make (kOutFormat, w, h);   // BGRA8 legible: salida final (blit al drawable + Syphon)
    t.hdrInitialized = false;
}

// INVARIANCIA AL REFRESH (bug de campo: "el MP4 no se ve como la ventana"). Los decaimientos del motor eran
// históricamente POR CUADRO (explodePulse *= 0.90, reformPulse, rayPulse, v = v·momentum, la estela): a 60
// pasos por segundo cada golpe duraba el DOBLE de segundos que a 120 y el freno tardaba el doble → la misma
// música movía las partículas ~1,5× más lejos. La ventana de una Mac ProMotion corre a 120 y el export
// simula a 60: dos mundos con el mismo patch.
//
// `frameSteps` mide el cuadro en "cuadros del ancla" y `decayOver` eleva cada k a esa potencia → el
// decaimiento pasa a ser por SEGUNDO. EL ANCLA ES 120 Hz (D-43, decidida por Joaquín el 7-sep): los 50
// mundos se afinaron y se aprobaron mirando una pantalla ProMotion, así que ESE es el look canónico. A
// dt = 1/120 la potencia es 1 y `decayOver` devuelve k SIN pasar por pow() → esa ventana queda bit a bit
// como estaba, y las pantallas de 60 Hz y el MP4 exportado (que simula a 1/60, o sea DOS pasos de ancla por
// cuadro) pasan a verse como ella. El gate por épsilon es DEFENSIVO: en float32 (float)(1.0/120.0)·120.0f
// da 1.0f exacto (igual que daba (1/60)·60 con el ancla vieja), pero el atajo no depende de esa suerte.
namespace
{
constexpr float kPhysicsAnchorHz = 120.0f;   // el refresh en que se afinaron los mundos (D-43)

inline float frameSteps (float dtSeconds)
{
    float n = dtSeconds * kPhysicsAnchorHz;
    if (std::fabs (n - 1.0f) < 1.0e-6f) n = 1.0f;
    // El vivo ya capa dt a 0,1 s y el techo es ese mismo tope real medido en cuadros del ancla (0,1·120 = 12)
    // más margen para un dt patológico. Con el ancla en 60 el mismo tope real eran 8.
    return std::clamp (n, 0.0f, 16.0f);
}
inline float decayOver (float k, float n) { return (n == 1.0f) ? k : std::pow (k, n); }
}

// La simulación avanza UNA vez por tick (evita doble física con fullscreen). Guarda `curU` para el draw.
void MetalRenderer::Impl::encodeSimulate (id<MTLCommandBuffer> cb, const AnalysisFrame& frame,
                                          const ParticleParams& p, float dt, unsigned nActive)
{
    // DENSITY: fracción de partículas dibujadas (prefijo permutado = submuestreo uniforme, la maquinaria de
    // RNF2). Con densidad baja el mosaico/constelación respira de verdad. Se compone con el adaptativo.
    nActive = std::min (nActive, std::max (1u, (unsigned) ((float) count * std::clamp (p.densityAmt, 0.01f, 1.0f))));
    if (nActive > count) nActive = count;
    if (nActive == 0)    nActive = 1;

    // FUNDIDO entre fotos: avanza con el dt DEL CUADRO — antes del SPEED, porque el fundido lo manda el
    // reloj de la SECUENCIA (segundos / compases / kick), no el timewarp de la física. Al llegar a 1 se hace
    // el swap A↔B acá mismo (antes de bindear) y xfade vuelve a −1: nunca se rinde un mix(a, b, 1.0).
    // En el export el paso es el mismo 1/60 fijo que come toda la física del clip → el fundido se ve igual
    // de rápido, relativo al resto del movimiento, que en pantalla.
    if (dissolve.active())
    {
        dissolve.tick ((double) dt);
        if (! dissolve.active()) finishDissolve();
    }
    const float xfade = dissolve.active() ? dissolve.mix01() : -1.0f;
    // El encuadre también se desliza: aspecto (FIT/FILL por target) y centroide del contenido. Con dos fotos
    // del mismo aspecto no cambia nada; con distinto, el letterbox se desliza en vez de saltar. En reposo son
    // asignaciones directas → byte-exacto.
    imgAspectEff = imgAspect;
    float cX = contentCX, cY = contentCY;
    if (xfade >= 0.0f)
    {
        imgAspectEff = imgAspect + (imgAspectB - imgAspect) * xfade;
        cX = contentCX + (contentCXB - contentCX) * xfade;
        cY = contentCY + (contentCYB - contentCY) * xfade;
    }

    // Cuántos cuadros del ancla (120 Hz) dura ESTE cuadro. Se toma del dt CRUDO, antes del SPEED: lo que se
    // corrige acá es la dependencia del REFRESH, no el timewarp — SPEED es una decisión del usuario y su
    // carácter (empujar más fuerte sin cambiar el freno) queda exactamente como estaba afinado.
    const float dtFrames = frameSteps (dt);
    curDtFrames = dtFrames;                 // encodeComposite lo necesita para el fade de la estela

    // SPEED (timewarp): escala el dt de la FÍSICA (0.25×..4×). speedMul=1 → dt intacto (goldens byte-idénticos).
    dt *= std::clamp (p.speedMul, 0.25f, 4.0f);

    // ROTATE + HUE CYC + ORBIT: fases acumuladas con el dt (escalado: SPEED frena/acelera TODO, es musical).
    viewAngle += p.rotateRate * dt;
    if (viewAngle >  3.14159265f) viewAngle -= 6.2831853f;
    if (viewAngle < -3.14159265f) viewAngle += 6.2831853f;
    huePhase += p.hueCycleRate * dt;
    if (huePhase >  3.14159265f) huePhase -= 6.2831853f;
    if (huePhase < -3.14159265f) huePhase += 6.2831853f;
    orbitPhase += p.orbitRate * dt;
    if (orbitPhase >  3.14159265f) orbitPhase -= 6.2831853f;
    if (orbitPhase < -3.14159265f) orbitPhase += 6.2831853f;

    // Cámara 3D del tick: yaw = knob ROT Y + órbita acumulada; pitch = ROT X. Identidad EXACTA en reposo
    // (cos=1/sin=0 literales → el vertex toma el camino legacy byte-exacto, mismo patrón que ROTATE).
    const float camYaw = p.rotYRad + orbitPhase;
    const float camPit = p.rotXRad;

    time += dt;
    // Onset: por el bool (frame directo, escenarios/goldens) O por el edge del contador monotónico (en vivo,
    // donde el bool puede pisarse en el pipeline latest-wins: thread batch → triple buffer → lectura a 60Hz).
    //
    // Dos golpes que NO estaban en la música y ahora no existen:
    //   · El PRIMER cuadro siembra el contador y no dispara. El renderer nacía en 0 y el primer frame real
    //     trae un contador de miles (el analizador viene contando desde que abrió el plugin, y el export
    //     arranca en medio de la ventana del anillo) → explosión en el cuadro 0, siempre.
    //   · El flanco sólo cuenta cuando el contador SUBE. Al dar la vuelta al anillo el clip vuelve al
    //     principio de la ventana y el contador BAJA: eso no es un golpe, es el loop (una explosión a los
    //     12 s y otra a los 24 s en cada clip). El onset que de verdad cae en ese cuadro sigue entrando por
    //     el bool `frame.onset`, que viaja en el frame de análisis.
    const bool onsetEdge = onsetSeeded && frame.onsetCount > lastOnsetCount;
    lastOnsetCount = frame.onsetCount;
    onsetSeeded    = true;
    if (frame.onset || onsetEdge || p.explode > 0.5f) explodePulse = 1.0f;
    else                                              explodePulse *= decayOver (0.90f, dtFrames);

    // Sombra lenta del pulso (re-armado): sigue al explodePulse hacia arriba y decae LENTO (τ≈45 frames ≈ lo
    // que dura la vuelta). Gateada por (1−explodePulse): mientras la explosión vive (o EXPLODE está sostenido)
    // no tira de vuelta; al morir el pulso, el resorte se multiplica y la imagen se re-arma en <1 s (criterio #2).
    reformPulse = std::max (reformPulse * decayOver (0.990f, dtFrames), explodePulse);
    const float reformGated = reformPulse * (1.0f - explodePulse);

    if (p.rayTrigger) { rayPulse = 1.0f; rayDirX = std::cos (p.rayAngle); rayDirY = std::sin (p.rayAngle); }
    else                rayPulse *= decayOver (0.90f, dtFrames);

    // CONSERVACIÓN DE ENERGÍA de la estela: el fade multiplicativo acumula 1/(1−k) — con k=0.975 son 40× y una
    // imagen clara REVIENTA a blanco. feed = M_target·(1−k) acota el estado estacionario a ~1.3× (glow
    // controlado, la estela es memoria, no exposición). trails=0 → feed=1.0 exacto (goldens byte-idénticos).
    // TRADE-OFF aceptado: cruzar el gate 0→0.001 salta feed 1.0→0.52 en un frame (estado estacionario 1.0→1.3);
    // suavizarlo reintroduce la ganancia 2.5× sin conservar en la zona baja (el quemado original). El morph
    // cruza el borde una sola vez y la acumulación amortigua el pop en ~3 frames.
    float trailFeed = 1.0f;
    if (p.trailAmt > 0.001f)
    {
        const float tA = std::min (1.0f, p.trailAmt);
        // El MISMO k efectivo que el fade quad (encodeComposite): con la estela por segundo, el estado
        // estacionario feed/(1−k) sigue acotado a 1,3× en cualquier refresh.
        const float k  = decayOver (0.60f + 0.375f * tA, dtFrames);
        trailFeed = 1.30f * (1.0f - k);                    // estado estacionario de lo estático ≈ 1.3×
    }

    curU = Uniforms { dt, time, p.chaos, p.intensity,
                      (p.particleSize < 1.0f ? 1.0f : p.particleSize) * 2.0f,
                      1.0f /*aspect: se setea POR TARGET en encodeComposite*/,
                      frame.bass, frame.energy, frame.treble, explodePulse,
                      p.curlScale, p.homeStrength, p.gravity, decayOver (p.momentum, dtFrames),
                      p.radialGain, p.jitterGain, p.breatheGain,
                      rayPulse, rayDirX, rayDirY,
                      reformGated, cX, cY,
                      nActive, (unsigned) p.motionMode, p.cutoutAmt,
                      (unsigned) p.shapeMode, p.trailAmt,
                      p.linksAmt, trailFeed,
                      p.scatterAmt, p.pumpAmt,
                      (viewAngle == 0.0f) ? 1.0f : std::cos (viewAngle),   // identidad EXACTA sin rotación
                      (viewAngle == 0.0f) ? 0.0f : std::sin (viewAngle),
                      std::clamp (p.depthAmt, 0.0f, 1.0f),
                      (camYaw == 0.0f) ? 1.0f : std::cos (camYaw),         // idem para la cámara 3D
                      (camYaw == 0.0f) ? 0.0f : std::sin (camYaw),
                      (camPit == 0.0f) ? 1.0f : std::cos (camPit),
                      (camPit == 0.0f) ? 0.0f : std::sin (camPit),
                      std::clamp (p.formAmt, 0.0f, 1.0f),
                      (unsigned) std::clamp (p.formMode, 0, 5),
                      1.0f, 1.0f, 1.0f,     // fitX, fitY, resScale (se setean POR TARGET en encodeComposite)
                      xfade };              // FUNDIDO (−1 = reposo → los kernels toman el camino de hoy)

    id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
    [ce setComputePipelineState:simPSO];
    [ce setBuffer:positions offset:0 atIndex:0];
    [ce setBuffer:velocities offset:0 atIndex:1];
    [ce setBuffer:homes offset:0 atIndex:2];
    [ce setBytes:&curU length:sizeof (curU) atIndex:3];
    [ce setBuffer:weights offset:0 atIndex:4];
    [ce setBuffer:flow offset:0 atIndex:5];
    [ce setBuffer:content2 offset:0 atIndex:6];
    [ce setBuffer:extra offset:0 atIndex:7];
    [ce setBuffer:weightsB offset:0 atIndex:8];      // FUNDIDO: el juego de la foto que entra. Siempre
    [ce setBuffer:flowB offset:0 atIndex:9];         // bindeado (el kernel lo declara); en reposo ni se lee.
    [ce setBuffer:content2B offset:0 atIndex:10];
    [ce setBuffer:extraB offset:0 atIndex:11];
    NSUInteger tpt = simPSO.maxTotalThreadsPerThreadgroup; if (tpt > 256) tpt = 256;
    [ce dispatchThreads:MTLSizeMake (curU.count, 1, 1) threadsPerThreadgroup:MTLSizeMake (tpt, 1, 1)];
    [ce endEncoding];

    // PLEXUS (capa 3): vecindad + emisión de líneas — DESPUÉS de la sim (encoder aparte = la posición nueva ya
    // está visible; barriers entre dispatches = clear→bin→emit→args en orden). La CPU nunca lee el conteo: el
    // draw es indirecto. alphaGain respira con la energía y FLASHEA con el kick (las conexiones viven).
    linksEncoded = false;
    if (p.linksAmt > 0.001f && linkReady() && linkNodeCount > 0)
    {
        const float links  = std::min (1.0f, p.linksAmt);
        const float radius = 0.014f + 0.044f * links;                 // más alcance = constelaciones que se leen
        const float minD   = std::max (0.005f, 0.22f * radius);       // piso: sin líneas invisibles entre pegados
        LinkU lu { radius,
                   links * (0.38f + 0.55f * frame.energy + 0.35f * explodePulse) * trailFeed,
                   p.cutoutAmt, minD * minD,
                   linkNodeCount, nActive, kMaxLinkLines, 0,
                   curU.depthAmt, curU.formAmt, curU.formMode, curU.xfade };

        id<MTLComputeCommandEncoder> le = [cb computeCommandEncoder];
        [le setComputePipelineState:linkClearPSO];
        [le setBuffer:linkCellCount offset:0 atIndex:0];
        [le setBuffer:linkLineCount offset:0 atIndex:1];
        [le dispatchThreads:MTLSizeMake ((NSUInteger) (kLinkGridC * kLinkGridC + 1), 1, 1)
              threadsPerThreadgroup:MTLSizeMake (64, 1, 1)];
        [le memoryBarrierWithScope:MTLBarrierScopeBuffers];

        [le setComputePipelineState:linkBinPSO];
        [le setBuffer:positions offset:0 atIndex:0];
        [le setBuffer:linkNodes offset:0 atIndex:1];
        [le setBuffer:linkCellCount offset:0 atIndex:2];
        [le setBuffer:linkCellSlots offset:0 atIndex:3];
        [le setBytes:&lu length:sizeof (lu) atIndex:4];
        [le dispatchThreads:MTLSizeMake (linkNodeCount, 1, 1) threadsPerThreadgroup:MTLSizeMake (64, 1, 1)];
        [le memoryBarrierWithScope:MTLBarrierScopeBuffers];

        [le setComputePipelineState:linkEmitPSO];
        [le setBuffer:positions offset:0 atIndex:0];
        [le setBuffer:colors offset:0 atIndex:1];
        [le setBuffer:linkNodes offset:0 atIndex:2];
        [le setBuffer:linkCellCount offset:0 atIndex:3];
        [le setBuffer:linkCellSlots offset:0 atIndex:4];
        [le setBuffer:linkLineCount offset:0 atIndex:5];
        [le setBuffer:linkVerts offset:0 atIndex:6];
        [le setBuffer:content2 offset:0 atIndex:7];
        [le setBytes:&lu length:sizeof (lu) atIndex:8];
        [le setBuffer:extra offset:0 atIndex:9];
        [le setBuffer:homes offset:0 atIndex:10];
        [le setBuffer:colorsB offset:0 atIndex:11];     // FUNDIDO: la constelación funde con las partículas
        [le setBuffer:content2B offset:0 atIndex:12];
        [le setBuffer:extraB offset:0 atIndex:13];
        [le dispatchThreads:MTLSizeMake (linkNodeCount, 1, 1) threadsPerThreadgroup:MTLSizeMake (64, 1, 1)];
        [le memoryBarrierWithScope:MTLBarrierScopeBuffers];

        [le setComputePipelineState:linkArgsPSO];
        [le setBuffer:linkLineCount offset:0 atIndex:0];
        [le setBuffer:linkArgs offset:0 atIndex:1];
        [le setBytes:&lu length:sizeof (lu) atIndex:2];
        [le dispatchThreads:MTLSizeMake (1, 1, 1) threadsPerThreadgroup:MTLSizeMake (1, 1, 1)];
        [le endEncoding];
        linksEncoded = true;
    }
}

// Draw partículas → t.hdr → bloom → composite → t.shareTex (tone-map+sRGB). Por target (bloom nativo por tamaño).
void MetalRenderer::Impl::encodeComposite (id<MTLCommandBuffer> cb, PresentTarget& t,
                                           const ParticleParams& p, unsigned nActive)
{
    auto pass = [] (id<MTLTexture> tex, MTLClearColor clear) -> MTLRenderPassDescriptor*
    {
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = tex;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = clear;
        return rp;
    };

    // ---- Partículas → HDR lineal ----
    // ESTELA (trails): con trailAmt>0 el HDR NO se limpia (Load) y un quad multiplicativo atenúa el frame
    // anterior (dst *= k) ANTES de dibujar — los glifos dibujan CAMINOS. El primer pass de una textura nueva
    // SIEMPRE limpia (contenido undefined). Con trailAmt=0: Clear clásico (camino idéntico, goldens intactos).
    {
        const bool trails = (curU.trailAmt > 0.001f) && t.hdrInitialized;
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture     = t.hdr;
        rp.colorAttachments[0].loadAction  = trails ? MTLLoadActionLoad : MTLLoadActionClear;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor  = MTLClearColorMake (0.006, 0.007, 0.011, 1.0);
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rp];
        if (trails)
        {
            // decay: trailAmt 0→estela corta (k≈0.60), 1→caminos largos (k≈0.975). Elevado a los cuadros
            // de 60 Hz que duró el tick → la estela mide lo mismo en SEGUNDOS a 60 y a 120 (dtFrames=1 → k).
            float k = decayOver (0.60f + 0.375f * std::min (1.0f, curU.trailAmt), curDtFrames);
            [re setRenderPipelineState:fadePSO];
            [re setFragmentBytes:&k length:sizeof (k) atIndex:0];
            [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        // Uniforms del TARGET: el aspect vuelve isotrópica la cámara 3D en drawables no cuadrados (la
        // rotación orbital dibuja círculos, no elipses). Con la cámara inactiva el vertex NI LEE aspect →
        // el camino legacy sigue byte-exacto aunque el valor cambie.
        Uniforms tu = curU;
        tu.aspect = (t.h > 0) ? (float) t.w / (float) t.h : 1.0f;
        // FIT del aspecto: encoge el cuadrado unidad para que el CONTENIDO conserve el aspecto de la imagen
        // dentro del viewport (letterbox/pillarbox centrado) — la foto vertical se ve vertical. Cuando la
        // imagen y el target tienen el mismo aspecto (p.ej. cuadrado↔cuadrado, los goldens) → 1,1 = sin fit.
        {
            const float vpAspect  = tu.aspect;                       // target w/h
            const float ratio     = (vpAspect > 0.0f) ? imgAspectEff / vpAspect : 1.0f;
            if (fitMode == 1)   // FILL (cover): el lado que sobra se AGRANDA (>1) y el target lo recorta
            {
                tu.fitX = (ratio >= 1.0f) ? ratio : 1.0f;
                tu.fitY = (ratio >= 1.0f) ? 1.0f : 1.0f / ratio;
            }
            else                // FIT (contain): el lado que sobra se ENCOGE (<1) → aire centrado
            {
                tu.fitX = (ratio >= 1.0f) ? 1.0f : ratio;
                tu.fitY = (ratio >= 1.0f) ? 1.0f / ratio : 1.0f;
            }
        }
        // INVARIANCIA AL TAMAÑO (bug de campo "al ampliar la vista se ve más suave/menos intenso"): el
        // glifo escala con min(w,h)/1024 → la figura cubre la MISMA fracción de pantalla con knobs, en
        // inmersivo y en fullscreen (exposición aditiva constante: d² crece como los píxeles del target).
        // Targets de PANTALLA (t.layer) siempre; el offscreen SOLO si el caller lo pide (el export lo pide;
        // los goldens y --render-frames no) → el camino histórico queda en 1.0, byte-exacto.
        if ((t.layer != nil || (offSizeInvariance && &t == &offTarget)) && t.w > 0 && t.h > 0)
            tu.resScale = std::max (0.5f, (float) std::min (t.w, t.h) / 1024.0f);

        [re setRenderPipelineState:drawPSO];
        [re setVertexBuffer:positions offset:0 atIndex:0];
        [re setVertexBuffer:colors offset:0 atIndex:1];
        [re setVertexBytes:&tu length:sizeof (tu) atIndex:2];
        [re setVertexBuffer:content2 offset:0 atIndex:3];
        [re setVertexBuffer:velocities offset:0 atIndex:4];
        [re setVertexBuffer:extra offset:0 atIndex:5];
        [re setVertexBuffer:homes offset:0 atIndex:6];
        [re setVertexBuffer:colorsB offset:0 atIndex:7];     // FUNDIDO: color/máscara/depth de la que entra
        [re setVertexBuffer:content2B offset:0 atIndex:8];
        [re setVertexBuffer:extraB offset:0 atIndex:9];
        [re setFragmentBytes:&tu length:sizeof (tu) atIndex:0];
        // curU.count = lo que la sim REALMENTE avanzó este tick (con DENSITY aplicada) — dibujar más sería
        // mostrar partículas congeladas fuera del prefijo.
        [re drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:curU.count];

        // PLEXUS: las líneas se suman al MISMO HDR (mismo pass) → estelas y bloom las abrazan igual que a los
        // glifos. Draw INDIRECTO: el conteo lo escribió k_linkArgs en GPU, la CPU nunca sincroniza.
        if (linksEncoded && linkDrawPSO != nil)
        {
            [re setRenderPipelineState:linkDrawPSO];
            [re setVertexBuffer:linkVerts offset:0 atIndex:0];
            [re setVertexBytes:&tu length:sizeof (tu) atIndex:1];   // ROTATE/3D: las líneas giran con la vista
            [re drawPrimitives:MTLPrimitiveTypeLine indirectBuffer:linkArgs indirectBufferOffset:0];
        }
        [re endEncoding];
        t.hdrInitialized = true;
    }

    // COLOR LAB: re-hornear la rampa SOLO al cambiar de look (user-rate). Drenar el frame en vuelo antes de
    // escribir la textura (mismo cuidado CPU↔GPU que uploadImage — el anterior puede estar leyéndola).
    const int palette = std::clamp (p.paletteIdx, 0, color::kNumLooks - 1);
    if (palette != lastPalette && rampTex != nil)
    {
        if (lastCb != nil) { [lastCb waitUntilCompleted]; lastCb = nil; }
        const auto texels = color::bakeRamp (palette);
        [rampTex replaceRegion:MTLRegionMake2D (0, 0, (NSUInteger) color::kRampSize, 1)
                   mipmapLevel:0 withBytes:texels.data()
                   bytesPerRow:(NSUInteger) color::kRampSize * 16];
        lastPalette = palette;
    }
    const auto paper = color::paperOf (palette);

    // PUMP también flashea el bloom, pero SOLO por encima del default (0.6): en default el glow no cambia
    // (goldens byte-idénticos); subir PUMP = el kick infla tamaño Y resplandor (sidechain visual completo).
    const float bloomPump = 1.0f + std::max (0.0f, p.pumpAmt - 0.6f) * 0.7f * explodePulse;
    PostU post { 0.55f, (0.5f + p.glow * 1.5f) * bloomPump, 1.0f + p.intensity * 0.3f, p.satAmt,
                 0.0f, 0.0f, p.hueShift + huePhase, (unsigned) std::max (0, p.kaleidoSeg),
                 // PALETTE Original (0) apaga el map aunque AMOUNT esté a fondo (el stepper decide SI).
                 (palette > 0) ? std::clamp (p.rampAmt, 0.0f, 1.0f) : 0.0f,
                 std::clamp (p.bgAmt, 0.0f, 1.0f),
                 paper[0], paper[1], paper[2],
                 // OPTICS PACK (Phase A): p.*=0 → 0 → byte-exacto (el golden test usa ParticleParams default=0).
                 // La aberración PULSA con el kick (explodePulse); grainTime=time anima el grano frame a frame.
                 std::clamp (p.grainAmt, 0.0f, 1.0f),
                 std::clamp (p.ditherAmt, 0.0f, 1.0f),
                 std::clamp (p.aberrationAmt, 0.0f, 1.0f) * (0.35f + 0.65f * explodePulse),
                 time,
                 std::clamp (p.bloomWideAmt, 0.0f, 1.0f),   // bloom envolvente (0 = byte-exacto)
                 // GRADE DE COLORISTA (Phase A6): neutro (0,1,0) = byte-exacto (goldens: pp default neutro).
                 p.gradeLift, p.gradeContrast, std::clamp (p.splitAmt, 0.0f, 1.0f),
                 p.splitShadowHue, p.splitHighHue,
                 0.0f };

    // ---- Bloom: threshold (t.hdr → t.bloomA) ----
    {
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:
            pass (t.bloomA, MTLClearColorMake (0, 0, 0, 1))];
        [re setRenderPipelineState:thresholdPSO];
        [re setFragmentTexture:t.hdr atIndex:0];
        [re setFragmentSamplerState:sampler atIndex:0];
        [re setFragmentBytes:&post length:sizeof (post) atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    }
    // ---- Blur H (bloomA → bloomB), Blur V (bloomB → bloomA) ----
    const float bw = (float) std::max (1, t.w / 2), bh = (float) std::max (1, t.h / 2);
    auto blur = [&] (id<MTLTexture> src, id<MTLTexture> dst, float dx, float dy)
    {
        PostU b = post; b.dirx = dx; b.diry = dy;
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:
            pass (dst, MTLClearColorMake (0, 0, 0, 1))];
        [re setRenderPipelineState:blurPSO];
        [re setFragmentTexture:src atIndex:0];
        [re setFragmentSamplerState:sampler atIndex:0];
        [re setFragmentBytes:&b length:sizeof (b) atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    };
    blur (t.bloomA, t.bloomB, 1.0f / bw, 0.0f);
    blur (t.bloomB, t.bloomA, 0.0f, 1.0f / bh);

    // ---- Composite → t.shareTex (tone-map + COLOR LAB + sRGB) ----
    {
        id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:
            pass (t.shareTex, MTLClearColorMake (0, 0, 0, 1))];
        [re setRenderPipelineState:compositePSO];
        [re setFragmentTexture:t.hdr atIndex:0];
        [re setFragmentTexture:t.bloomA atIndex:1];
        [re setFragmentTexture:rampTex atIndex:2];   // la rampa del look (el branch decide si se lee)
        [re setFragmentSamplerState:sampler atIndex:0];
        [re setFragmentBytes:&post length:sizeof (post) atIndex:0];
        [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [re endEncoding];
    }
}

// Densidad adaptativa (RNF2): baja partículas ANTES que fps. Métrica = COSTO GPU real del frame (independiente
// del refresh: a 60 y 120 Hz mide lo mismo). Presupuesto ~16.7ms (60fps) con headroom para post/present. Con
// histéresis: si el costo GPU suavizado se acerca al presupuesto, recorta; si sobra holgura, recupera. Piso del
// 25% del total (no vaciar la imagen). El offscreen NO adapta (determinismo QA).
void MetalRenderer::Impl::adapt()
{
    const float ms = std::clamp (lastGpuMs.load (std::memory_order_relaxed), 0.1f, 100.0f);
    emaFrameMs += (ms - emaFrameMs) * 0.1f;   // EMA suave

    const unsigned floorN = std::max (1u, count / 4);
    if (emaFrameMs > 14.0f && activeCount > floorN)            // GPU cerca del presupuesto → recortá 4%
        activeCount = std::max (floorN, (unsigned) (activeCount * 0.96f));
    else if (emaFrameMs < 9.0f && activeCount < count)         // GPU con holgura → recuperá densidad
        activeCount = std::min (count, activeCount + std::max (1u, count / 50u));
}

void MetalRenderer::render (const AnalysisFrame& frame, const ParticleParams& params, double dtSeconds)
{
    if (! impl->ready || impl->targets.empty()) return;
    @autoreleasepool
    {
        impl->adapt();
        id<MTLCommandBuffer> cb = [impl->queue commandBuffer];
        impl->encodeSimulate (cb, frame, params, (float) dtSeconds, impl->activeCount);   // UNA sim por tick

        std::vector<id<CAMetalDrawable>> presented;
        presented.reserve (impl->targets.size());
        for (auto& t : impl->targets)
        {
            if (t.layer == nil) continue;
            id<CAMetalDrawable> d = [t.layer nextDrawable];
            if (d == nil) { ++impl->diagNilDrawables; continue; }   // pantalla en transición → saltar, no abortar
            const int tw = (int) d.texture.width, th = (int) d.texture.height;
            impl->ensureTargetTex (t, tw, th);
            impl->encodeComposite (cb, t, params, impl->activeCount);   // → t.shareTex (bloom nativo por tamaño)

            id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
            [be copyFromTexture:t.shareTex sourceSlice:0 sourceLevel:0
                   sourceOrigin:MTLOriginMake (0, 0, 0) sourceSize:MTLSizeMake ((NSUInteger) tw, (NSUInteger) th, 1)
                      toTexture:d.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake (0, 0, 0)];
            [be endEncoding];

            // Syphon publica el target PRIMARY (la salida grande/limpia). Best-effort (RNF3).
            if (t.primary && impl->syphonEnabled && impl->syphon && impl->syphon->isValid())
                impl->syphon->publish ((__bridge void*) t.shareTex, (__bridge void*) cb, tw, th, false);

            presented.push_back (d);
        }

        std::atomic<float>* gpuMs = &impl->lastGpuMs;
        [cb addCompletedHandler:^(id<MTLCommandBuffer> b) {
            const float dt = (float) ((b.GPUEndTime - b.GPUStartTime) * 1000.0);
            if (dt > 0.0f) gpuMs->store (dt, std::memory_order_relaxed);
        }];

        impl->lastCb = cb;               // se drena en uploadImage()/dtor para no reescribir buffers en vuelo
        [cb commit];
        [cb waitUntilScheduled];
        for (id<CAMetalDrawable> d : presented) [d present];   // presentar tras schedule

        // Diagnóstico en vivo (~cada 2s a 60fps): targets, tamaño del primary, drawables nil, y el audio que
        // LLEGA al renderer (si bass/rms son 0.000 acá, el problema está antes del render).
        if (++impl->diagFrames >= 120)
        {
            char sizes[128] = ""; size_t off = 0;
            for (const auto& t : impl->targets)
                off += (size_t) snprintf (sizes + off, sizeof (sizes) - off, " %dx%d%s",
                                          t.w, t.h, t.primary ? "*" : "");
            std::fprintf (stderr, "[supernova] live: targets=%d[%s ] nilDrawables=%d "
                                  "gpuMs=%.1f active=%u bass=%.2f rms=%.2f%s\n",
                          (int) impl->targets.size(), sizes,
                          impl->diagNilDrawables, impl->lastGpuMs.load (std::memory_order_relaxed),
                          impl->activeCount, frame.bass, frame.rms, frame.onset ? " ONSET" : "");
            impl->diagFrames = 0; impl->diagNilDrawables = 0;
        }
    }
}

void MetalRenderer::setSyphonEnabled (bool on)
{
    if (! isAvailable()) return;
    if (on && impl->syphon == nullptr)
        impl->syphon = std::make_unique<MetalSyphonServer> ((__bridge void*) impl->device, "SUPERNOVA");
    impl->syphonEnabled = on && impl->syphon != nullptr && impl->syphon->isValid();
    if (! on)
    {
        // DRENAR antes de soltar el server: publish() le pasó el cb a Syphon, que registró un completion
        // handler (publishNewFrame). Si liberamos el server con ese frame en vuelo, el handler pega en
        // memoria liberada → SIGSEGV (crash al apagar Syphon / al cerrar con Syphon activo).
        if (impl->lastCb != nil) { [impl->lastCb waitUntilCompleted]; impl->lastCb = nil; }
        impl->syphon.reset();            // stop + release (ya sin frames en vuelo)
    }
}

bool MetalRenderer::isSyphonActive() const
{
    return impl->syphonEnabled && impl->syphon != nullptr && impl->syphon->isValid();
}

unsigned MetalRenderer::activeParticles() const { return impl->activeCount; }
unsigned MetalRenderer::totalParticles()  const { return impl->count; }

bool MetalRenderer::debugReadPositions (float* outXY, unsigned countPairs) const
{
    if (! impl->ready || outXY == nullptr || impl->positions == nil) return false;
    const unsigned n = std::min (countPairs, impl->count);
    std::memcpy (outXY, impl->positions.contents, (size_t) n * sizeof (F2));   // Shared → legible post-complete
    return true;
}

bool MetalRenderer::renderOffscreen (const AnalysisFrame& frame, const ParticleParams& params,
                                     int pxW, int pxH, uint8_t* outRgba)
{
    if (! impl->ready || pxW <= 0 || pxH <= 0 || outRgba == nullptr) return false;
    @autoreleasepool
    {
        impl->ensureTargetTex (impl->offTarget, pxW, pxH);   // mismo bundle de post que on-screen → golden intactos
        if (impl->readback == nil || impl->offW != pxW || impl->offH != pxH)
        {
            impl->readback = [impl->device newBufferWithLength:(NSUInteger) (pxW * pxH * 4)
                                                       options:MTLResourceStorageModeShared];
            impl->offW = pxW; impl->offH = pxH;
        }

        id<MTLCommandBuffer> cb = [impl->queue commandBuffer];
        impl->encodeSimulate  (cb, frame, params, (float) impl->offDt, impl->count);   // total, paso fijo: determinista
        impl->encodeComposite (cb, impl->offTarget, params, impl->count);       // → offTarget.shareTex

        id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
        [be copyFromTexture:impl->offTarget.shareTex sourceSlice:0 sourceLevel:0
               sourceOrigin:MTLOriginMake (0, 0, 0) sourceSize:MTLSizeMake ((NSUInteger) pxW, (NSUInteger) pxH, 1)
                   toBuffer:impl->readback destinationOffset:0
          destinationBytesPerRow:(NSUInteger) (pxW * 4)
        destinationBytesPerImage:(NSUInteger) (pxW * pxH * 4)];
        [be endEncoding];
        [cb commit];
        [cb waitUntilCompleted];

        const uint8_t* src = (const uint8_t*) impl->readback.contents;   // BGRA
        for (int i = 0; i < pxW * pxH; ++i)
        {
            outRgba[i * 4 + 0] = src[i * 4 + 2];
            outRgba[i * 4 + 1] = src[i * 4 + 1];
            outRgba[i * 4 + 2] = src[i * 4 + 0];
            outRgba[i * 4 + 3] = src[i * 4 + 3];
        }
        return true;
    }
}
void MetalRenderer::setOffscreenDt (double seconds)
{
    impl->offDt = (seconds > 0.0) ? seconds : 1.0 / 60.0;
}
double MetalRenderer::offscreenDt() const { return impl->offDt; }

ViewPhases MetalRenderer::viewPhases() const
{
    return { impl->viewAngle, impl->orbitPhase, impl->huePhase };
}
void MetalRenderer::setViewPhases (const ViewPhases& p)
{
    impl->viewAngle  = p.rotate;
    impl->orbitPhase = p.orbit;
    impl->huePhase   = p.hue;
}

void MetalRenderer::setOffscreenSizeInvariance (bool on) { impl->offSizeInvariance = on; }
bool MetalRenderer::offscreenSizeInvariance() const      { return impl->offSizeInvariance; }

void MetalRenderer::setFitMode (int mode) { impl->fitMode = (mode == 1) ? 1 : 0; }
int  MetalRenderer::fitMode() const       { return impl->fitMode; }
}
