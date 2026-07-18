// SystemAudioSource — captura del audio del SISTEMA en macOS 13+ vía ScreenCaptureKit (SCStream),
// sin drivers. La app reacciona a CUALQUIER cosa que suene en la Mac (Spotify/YouTube/Serato/Ableton).
//
// Diseño:
//  - start() es NO-bloqueante: dispara el setup (fetch de contenido compartible + arranque del stream)
//    en una cola de fondo, así el prompt de Screen Recording (TCC) NO congela la UI. El resultado
//    (capturing / permissionDenied / error) se refleja en status() de forma asincrónica; el caller lo
//    pollea (AppTopBar ya poll-ea a 30Hz para el medidor).
//  - El callback de samples llega en la cola propia del stream (sampleHandlerQueue). Entrega TODOS los
//    frames Float32 planar (NO latest-wins: dropear audio = cortes). El FIFO del processor absorbe.
//  - @autoreleasepool en cada cuerpo Obj-C (colas sin pool). STRIDE respetado (mDataByteSize, no w*4).
//  - Guardas: generación (switch rápido de fuente no deja un stream huérfano) + flag 'live' atómico en
//    el tap (no se corre la std::function del callback contra la cola de samples).
#include "SystemAudioSource.h"

#if JUCE_MAC

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreGraphics/CoreGraphics.h>   // CGPreflight/CGRequestScreenCaptureAccess (permiso en vivo)
#include <atomic>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdarg>
#include <cstdio>

namespace { constexpr int kMaxCh = 8; }

// Diagnóstico de captura a stderr (visible corriendo la app desde Terminal).
static void snvLog (const char* fmt, ...)
{
    char buf[400];
    va_list ap; va_start (ap, fmt);
    std::vsnprintf (buf, sizeof buf, fmt, ap);
    va_end (ap);
    std::fprintf (stderr, "%s", buf);
}

using SnvSampleCb = supernova::SystemAudioSource::SampleCallback;

// ---- Delegate Obj-C: recibe los CMSampleBuffer de audio y llama al callback C++ ----
API_AVAILABLE(macos(13.0))
@interface SnvAudioTap : NSObject <SCStreamOutput, SCStreamDelegate>
- (void)setCallback:(SnvSampleCb)c requestedSr:(double)sr;
- (void)disable;
@end

@implementation SnvAudioTap
{
    SnvSampleCb cb;
    double requestedSr;
    std::atomic<bool> live;
    std::atomic<int> frameLogs;   // logea los primeros callbacks de audio (diagnóstico)
    std::vector<float> planar;    // scratch para el caso interleaved → planar
    std::vector<uint8_t> ablBuf;  // storage reusable del AudioBufferList (tamaño exacto que pide CoreMedia)
}

- (instancetype)init
{
    if ((self = [super init])) { live.store (true); frameLogs.store (0); requestedSr = 48000.0; }
    return self;
}

- (void)setCallback:(SnvSampleCb)c requestedSr:(double)sr { cb = std::move (c); requestedSr = sr; }
- (void)disable { live.store (false); }

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio) return;
    if (! live.load() || ! cb) return;

    @autoreleasepool
    {
        if (! CMSampleBufferIsValid (sampleBuffer)) return;
        const CMItemCount frames = CMSampleBufferGetNumSamples (sampleBuffer);
        if (frames <= 0) return;

        const CMAudioFormatDescriptionRef fmt = (CMAudioFormatDescriptionRef) CMSampleBufferGetFormatDescription (sampleBuffer);
        if (! fmt) return;
        const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (fmt);
        if (! asbd) return;
        const bool isFloat = (asbd->mFormatFlags & kAudioFormatFlagIsFloat) != 0;
        if (! isFloat || asbd->mBitsPerChannel != 32) return;              // esperamos Float32
        const bool nonInterleaved = (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
        const int numChFmt = (int) asbd->mChannelsPerFrame;
        const double sr = asbd->mSampleRate > 0 ? asbd->mSampleRate : requestedSr;

        // AudioBufferList: patrón de DOS llamadas de CoreMedia — 1º pedir el tamaño EXACTO, 2º llenarlo.
        // (Un buffer de tamaño fijo daba kCMSampleBufferError_ArrayTooSmall −12737 → 0 frames → sin audio.)
        size_t ablNeeded = 0;
        CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (
            sampleBuffer, &ablNeeded, nullptr, 0, kCFAllocatorDefault, kCFAllocatorDefault, 0, nullptr);
        if (ablNeeded == 0) return;
        if (ablBuf.size() < ablNeeded) ablBuf.resize (ablNeeded);
        AudioBufferList* abl = reinterpret_cast<AudioBufferList*> (ablBuf.data());
        CMBlockBufferRef block = nullptr;
        const OSStatus st = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer (
            sampleBuffer, nullptr, abl, ablNeeded,
            kCFAllocatorDefault, kCFAllocatorDefault, 0, &block);
        if (st != noErr || block == nullptr) { if (block) CFRelease (block); return; }

        const float* chans[kMaxCh] = {};
        int nCh = 0, nFrames = (int) frames;

        if (nonInterleaved)
        {
            nCh = juce::jmin ((int) abl->mNumberBuffers, kMaxCh);
            for (int c = 0; c < nCh; ++c)
            {
                chans[c] = reinterpret_cast<const float*> (abl->mBuffers[c].mData);
                nFrames  = juce::jmin (nFrames, (int) (abl->mBuffers[c].mDataByteSize / sizeof (float)));
            }
        }
        else
        {
            const float* inter = reinterpret_cast<const float*> (abl->mBuffers[0].mData);
            const int total    = (int) (abl->mBuffers[0].mDataByteSize / sizeof (float));
            nCh     = juce::jmin (numChFmt, kMaxCh);
            nFrames = juce::jmin (nFrames, nCh > 0 ? total / nCh : 0);
            planar.resize ((size_t) nCh * (size_t) juce::jmax (0, nFrames));
            for (int c = 0; c < nCh; ++c)
            {
                float* d = planar.data() + (size_t) c * (size_t) nFrames;
                for (int f = 0; f < nFrames; ++f) d[f] = inter[f * nCh + c];
                chans[c] = d;
            }
        }

        if (nCh > 0 && nFrames > 0 && cb)
        {
            if (frameLogs.load() < 1) { frameLogs.fetch_add (1); snvLog ("[sysaudio] audio fluyendo (%dch @%.0fHz)\n", nCh, sr); }
            cb (chans, nCh, nFrames, sr);
        }

        CFRelease (block);
    }
}

// SCStreamDelegate: si el stream muere solo (p.ej. permiso revocado), no colgamos.
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error { (void) stream; (void) error; }
@end

// ------------------------------------------------------------------------------------
namespace supernova {

struct SystemAudioSource::Impl
{
    std::atomic<Status> status { Status::idle };
    std::atomic<int>    generation { 0 };
    id  tap    = nil;   // SnvAudioTap* (API 13.0) guardado como id → struct sin restricción de versión
    id  stream = nil;   // SCStream*    (API 12.3)
    dispatch_queue_t setupQ  = nil;
    dispatch_queue_t sampleQ = nil;
    std::atomic<bool> setupInFlight { false };   // evita apilar setups en el retry
    std::atomic<bool> prompted { false };        // el prompt de TCC se dispara UNA vez
    int sr = 48000, ch = 2;
};

SystemAudioSource::SystemAudioSource() : impl (std::make_unique<Impl>()) {}

SystemAudioSource::~SystemAudioSource() { stop(); }

bool SystemAudioSource::isSupported() noexcept
{
    if (@available (macOS 13.0, *)) return true;
    return false;
}

bool SystemAudioSource::hasPermission() noexcept
{
    return CGPreflightScreenCaptureAccess();   // estado TCC EN VIVO (no depende del relanzamiento)
}

bool SystemAudioSource::start (int sampleRate, int channels, SampleCallback onSamples) noexcept
{
    if (! isSupported()) { impl->status = Status::unsupported; return false; }
    if (impl->status.load() == Status::capturing) return true;

    bool expected = false;
    if (! impl->setupInFlight.compare_exchange_strong (expected, true))
        return true;   // ya hay un setup en vuelo (retry): no apilar

    impl->sr = sampleRate > 0 ? sampleRate : 48000;
    impl->ch = juce::jlimit (1, kMaxCh, channels);
    if (impl->setupQ  == nil) impl->setupQ  = dispatch_queue_create ("com.ovni.supernova.sysaudio.setup",   DISPATCH_QUEUE_SERIAL);
    if (impl->sampleQ == nil) impl->sampleQ = dispatch_queue_create ("com.ovni.supernova.sysaudio.samples", DISPATCH_QUEUE_SERIAL);

    Impl* I = impl.get();
    const int gen = ++I->generation;   // esta puesta en marcha
    SnvSampleCb cb = std::move (onSamples);

    dispatch_async (I->setupQ, ^{
        @autoreleasepool
        {
            struct InFlightGuard { Impl* i; ~InFlightGuard() { i->setupInFlight.store (false); } } guard { I };

            if (@available (macOS 13.0, *))
            {
                if (I->generation.load() != gen) return;   // cancelado antes de arrancar

                // El prompt de Screen Recording lo dispara SCK NATIVAMENTE en el primer getShareableContent
                // (async, cualquier thread). NO usamos CGRequestScreenCaptureAccess: en una cola de fondo
                // devuelve 0 sin mostrar diálogo, y bailábamos antes de pedir el contenido (bug que TAPABA
                // el prompt). CGPreflight queda solo como dato para el log/banner.
                snvLog ("[sysaudio] preflight=%d → pido shareableContent (dispara el prompt si falta)\n",
                              (int) CGPreflightScreenCaptureAccess());

                __block SCShareableContent* content = nil;
                __block NSError* contentErr = nil;
                dispatch_semaphore_t sem = dispatch_semaphore_create (0);
                [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* c, NSError* e)
                {
                    content = c; contentErr = e;
                    dispatch_semaphore_signal (sem);
                }];
                dispatch_semaphore_wait (sem, dispatch_time (DISPATCH_TIME_NOW, (int64_t) (12 * NSEC_PER_SEC)));

                snvLog ("[sysaudio] shareableContent: content=%d displays=%lu err=%s\n",
                              (int) (content != nil), (unsigned long) (content ? content.displays.count : 0),
                              contentErr ? contentErr.localizedDescription.UTF8String : "nil");

                if (content == nil || content.displays.count == 0)
                {
                    I->status = Status::permissionDenied;   // aún no propagó / sin displays
                    return;
                }
                if (I->generation.load() != gen) return;

                SCDisplay* display = content.displays.firstObject;
                SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];

                SCStreamConfiguration* cfg = [[SCStreamConfiguration alloc] init];
                cfg.capturesAudio               = YES;
                cfg.sampleRate                  = I->sr;
                cfg.channelCount                = I->ch;
                cfg.excludesCurrentProcessAudio = YES;      // no realimentar nuestra propia salida
                cfg.width  = (NSUInteger) juce::jmax (2, (int) display.width);   // no consumimos video, pero
                cfg.height = (NSUInteger) juce::jmax (2, (int) display.height);  // tamaño VÁLIDO del display
                cfg.minimumFrameInterval = CMTimeMake (1, 30);

                SnvAudioTap* tap = [[SnvAudioTap alloc] init];
                [tap setCallback:cb requestedSr:(double) I->sr];

                SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:cfg delegate:tap];
                NSError* addErr = nil;
                if (! [stream addStreamOutput:tap type:SCStreamOutputTypeAudio sampleHandlerQueue:I->sampleQ error:&addErr])
                {
                    snvLog ("[sysaudio] addStreamOutput(audio) FAILED: %s\n",
                                  addErr ? addErr.localizedDescription.UTF8String : "?");
                    I->status = Status::error;
                    return;
                }
                // Salida de PANTALLA además del audio: en macOS 15+/26 el motor de captura no arranca el
                // audio si el stream no tiene un consumidor de video. El tap descarta los frames de pantalla
                // (solo procesa Audio). Best-effort: si falla, seguimos con audio-only.
                NSError* scrErr = nil;
                const BOOL scrOk = [stream addStreamOutput:tap type:SCStreamOutputTypeScreen sampleHandlerQueue:I->sampleQ error:&scrErr];
                snvLog ("[sysaudio] addStreamOutput(screen) ok=%d err=%s\n", (int) scrOk,
                        scrErr ? scrErr.localizedDescription.UTF8String : "nil");

                __block bool ok = false;
                __block NSError* startErr = nil;
                dispatch_semaphore_t startSem = dispatch_semaphore_create (0);
                [stream startCaptureWithCompletionHandler:^(NSError* e)
                {
                    ok = (e == nil); startErr = e;
                    dispatch_semaphore_signal (startSem);
                }];
                dispatch_semaphore_wait (startSem, dispatch_time (DISPATCH_TIME_NOW, (int64_t) (8 * NSEC_PER_SEC)));
                snvLog ("[sysaudio] startCapture ok=%d err=%s\n", (int) ok,
                              startErr ? startErr.localizedDescription.UTF8String : "nil");

                if (! ok || I->generation.load() != gen)
                {
                    [stream stopCaptureWithCompletionHandler:^(NSError*){}];
                    if (! ok) I->status = Status::error;
                    return;
                }

                I->tap    = tap;
                I->stream = stream;
                I->status = Status::capturing;
                snvLog ("[sysaudio] CAPTURANDO (sr=%d ch=%d) — esperando frames de audio…\n",
                              I->sr, I->ch);
            }
            else
            {
                I->status = Status::unsupported;
            }
        }
    });

    return true;
}

void SystemAudioSource::stop() noexcept
{
    if (! impl) return;
    ++impl->generation;   // invalida cualquier setup en vuelo

    id t = impl->tap;    impl->tap = nil;
    id s = impl->stream; impl->stream = nil;
    if (@available (macOS 13.0, *))
    {
        if (t) [(SnvAudioTap*) t disable];   // corta entregas sin racear la std::function
        if (s) [(SCStream*)    s stopCaptureWithCompletionHandler:^(NSError*){}];
    }
    impl->status = Status::idle;
}

SystemAudioSource::Status SystemAudioSource::status() const noexcept { return impl->status.load(); }

} // namespace supernova

#endif // JUCE_MAC
