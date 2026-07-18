#include "video/VideoSource.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <atomic>

namespace supernova
{
namespace
{
// Cuartos de vuelta CW a partir del preferredTransform del track (videos verticales de celular = 90°).
int quarterTurnsFromTransform (CGAffineTransform t)
{
    // Ejes: identidad→0, (0,1,-1,0)→90CW, (-1,0,0,-1)→180, (0,-1,1,0)→90CCW (=270CW).
    const double a = t.a, b = t.b, c = t.c, d = t.d;
    auto near = [] (double x, double y) { return std::abs (x - y) < 0.01; };
    if (near (a, 1) && near (b, 0) && near (c, 0) && near (d, 1))   return 0;
    if (near (a, 0) && near (b, 1) && near (c, -1) && near (d, 0))  return 1;
    if (near (a, -1) && near (b, 0) && near (c, 0) && near (d, -1)) return 2;
    if (near (a, 0) && near (b, -1) && near (c, 1) && near (d, 0))  return 3;
    return 0;
}

// Rota un RGBA (w×h) `turns` cuartos CW → out (dims intercambiadas en 1/3). Mismo mapeo que ImageLoader.
void rotateRgba (const std::vector<uint8_t>& in, int w, int h, int turns,
                 std::vector<uint8_t>& out, int& ow, int& oh)
{
    turns = ((turns % 4) + 4) % 4;
    if (turns == 0) { out = in; ow = w; oh = h; return; }
    const bool swap = (turns == 1 || turns == 3);
    ow = swap ? h : w;
    oh = swap ? w : h;
    out.resize ((size_t) ow * oh * 4);
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox)
        {
            int sx = 0, sy = 0;
            switch (turns)
            {
                case 1: sx = oy;         sy = h - 1 - ox; break;   // 90 CW
                case 2: sx = w - 1 - ox; sy = h - 1 - oy; break;   // 180
                case 3: sx = w - 1 - oy; sy = ox;         break;   // 270 CW (=90 CCW)
                default: break;
            }
            const uint8_t* s = in.data() + ((size_t) sy * w + sx) * 4;
            uint8_t* dpx = out.data() + ((size_t) oy * ow + ox) * 4;
            dpx[0] = s[0]; dpx[1] = s[1]; dpx[2] = s[2]; dpx[3] = s[3];
        }
}
}

struct VideoSource::Impl : private juce::Thread
{
    Impl() : juce::Thread ("supernova-video") {}
    ~Impl() override { stop(); }

    AVAssetReader*            reader = nil;
    AVAssetReaderTrackOutput* output = nil;
    AVURLAsset*               asset  = nil;
    int   turns = 0;                       // rotación del preferredTransform
    std::atomic<int> extraTurns { 0 };     // rotación manual del usuario (⟳), encima del transform
    std::atomic<int> naturalW { 0 }, naturalH { 0 };   // tamaño (post-transform, pre-manual) para el aspecto
    std::atomic<float> aspectV { 1.0f };
    std::atomic<bool>  playing { true };
    std::atomic<bool>  opened  { false };

    juce::CriticalSection frontLock;
    std::vector<uint8_t>  front;           // último frame publicado (RGBA), protegido por frontLock
    int frontW = 0, frontH = 0;
    std::atomic<bool> hasNew { false };

    double startWallMs = 0.0;
    double firstPtsSec = -1.0;

    bool openFile (const juce::File& file)
    {
        close();
        NSString* path = [NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()];
        NSURL* url = [NSURL fileURLWithPath:path];
        asset = [AVURLAsset URLAssetWithURL:url options:nil];
        NSArray<AVAssetTrack*>* vtracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        if (vtracks.count == 0) { asset = nil; return false; }
        AVAssetTrack* track = vtracks.firstObject;

        const CGSize sz = track.naturalSize;
        turns = quarterTurnsFromTransform (track.preferredTransform);
        const bool swap = (turns == 1 || turns == 3);
        naturalW.store ((int) (swap ? sz.height : sz.width));
        naturalH.store ((int) (swap ? sz.width  : sz.height));
        extraTurns.store (0);
        recomputeAspect();

        if (! createReader (track)) { asset = nil; return false; }
        opened.store (true);
        firstPtsSec = -1.0;
        startWallMs = juce::Time::getMillisecondCounterHiRes();
        startThread (juce::Thread::Priority::normal);
        return true;
    }

    void recomputeAspect()
    {
        const int w = naturalW.load(), h = naturalH.load();
        const bool swap = (extraTurns.load() % 2) != 0;
        const float aw = (float) (swap ? h : w), ah = (float) (swap ? w : h);
        aspectV.store (ah > 0.0f ? aw / ah : 1.0f);
    }

    bool createReader (AVAssetTrack* track)
    {
        NSError* err = nil;
        reader = [[AVAssetReader alloc] initWithAsset:asset error:&err];
        if (reader == nil) return false;
        NSDictionary* settings = @{ (id) kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:settings];
        output.alwaysCopiesSampleData = NO;
        if (! [reader canAddOutput:output]) { reader = nil; output = nil; return false; }
        [reader addOutput:output];
        return [reader startReading];
    }

    void run() override
    {
        std::vector<uint8_t> bgra, rgba, rot;
        while (! threadShouldExit())
        {
            if (! playing.load()) { wait (30); continue; }

            CMSampleBufferRef sample = (reader.status == AVAssetReaderStatusReading)
                                           ? [output copyNextSampleBuffer] : nullptr;
            if (sample == nullptr)
            {
                // Fin (o error recuperable): re-crear el reader y loopear desde el principio.
                AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
                reader = nil; output = nil;
                if (track == nil || ! createReader (track)) { wait (100); continue; }
                firstPtsSec = -1.0;
                startWallMs = juce::Time::getMillisecondCounterHiRes();
                continue;
            }

            const double pts = CMTimeGetSeconds (CMSampleBufferGetPresentationTimeStamp (sample));
            CVImageBufferRef pix = CMSampleBufferGetImageBuffer (sample);
            if (pix != nullptr)
            {
                CVPixelBufferLockBaseAddress (pix, kCVPixelBufferLock_ReadOnly);
                const int w = (int) CVPixelBufferGetWidth (pix);
                const int h = (int) CVPixelBufferGetHeight (pix);
                const size_t stride = CVPixelBufferGetBytesPerRow (pix);
                const uint8_t* base = (const uint8_t*) CVPixelBufferGetBaseAddress (pix);
                if (base != nullptr && w > 0 && h > 0)
                {
                    rgba.resize ((size_t) w * h * 4);
                    for (int y = 0; y < h; ++y)
                    {
                        const uint8_t* srow = base + (size_t) y * stride;
                        uint8_t* drow = rgba.data() + (size_t) y * w * 4;
                        for (int x = 0; x < w; ++x)               // BGRA → RGBA
                        {
                            drow[x * 4 + 0] = srow[x * 4 + 2];
                            drow[x * 4 + 1] = srow[x * 4 + 1];
                            drow[x * 4 + 2] = srow[x * 4 + 0];
                            drow[x * 4 + 3] = srow[x * 4 + 3];
                        }
                    }
                    int ow = w, oh = h;
                    rotateRgba (rgba, w, h, turns + extraTurns.load(), rot, ow, oh);
                    {
                        const juce::ScopedLock sl (frontLock);
                        front.swap (rot);
                        frontW = ow; frontH = oh;
                    }
                    hasNew.store (true);
                }
                CVPixelBufferUnlockBaseAddress (pix, kCVPixelBufferLock_ReadOnly);
            }
            CFRelease (sample);

            // Ritmo: esperar a que el reloj de pared alcance el PTS (reproducción a velocidad real, cap 60fps).
            if (firstPtsSec < 0.0) firstPtsSec = pts;
            const double targetMs = startWallMs + (pts - firstPtsSec) * 1000.0;
            const double nowMs = juce::Time::getMillisecondCounterHiRes();
            int waitMs = (int) juce::jlimit (0.0, 250.0, targetMs - nowMs);
            if (waitMs < 16) waitMs = 16;                         // cap ~60 fps de publicación
            wait (waitMs);
        }
    }

    void stop()
    {
        signalThreadShouldExit();
        stopThread (1000);
        reader = nil; output = nil; asset = nil;
        opened.store (false);
    }

    void close()
    {
        if (isThreadRunning()) stop();
        else { reader = nil; output = nil; asset = nil; opened.store (false); }
        const juce::ScopedLock sl (frontLock);
        front.clear(); frontW = frontH = 0; hasNew.store (false);
    }
};

VideoSource::VideoSource() : impl (std::make_unique<Impl>()) {}
VideoSource::~VideoSource() { impl->close(); }

bool VideoSource::open (const juce::File& file)  { return impl->openFile (file); }
void VideoSource::close()                        { impl->close(); }
bool VideoSource::isOpen() const noexcept        { return impl->opened.load(); }
void VideoSource::setPlaying (bool p) noexcept   { impl->playing.store (p); }
float VideoSource::aspect() const noexcept       { return impl->aspectV.load(); }
void VideoSource::rotate() noexcept              { impl->extraTurns.store ((impl->extraTurns.load() + 1) % 4); impl->recomputeAspect(); }

bool VideoSource::latestFrame (std::vector<uint8_t>& outRgba, int& outW, int& outH)
{
    if (! impl->hasNew.load()) return false;
    const juce::ScopedLock sl (impl->frontLock);
    if (impl->front.empty()) return false;
    outRgba = impl->front;
    outW = impl->frontW;
    outH = impl->frontH;
    impl->hasNew.store (false);
    return true;
}
}
