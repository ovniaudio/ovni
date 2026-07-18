// VideoExporter.mm — encoder RGBA→MP4 (H.264) con AVAssetWriter (mac). Ver VideoExporter.h.
#include "VideoExporter.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

namespace supernova
{
struct VideoExporter::Impl
{
    AVAssetWriter*                          writer  = nil;
    AVAssetWriterInput*                     input   = nil;
    AVAssetWriterInputPixelBufferAdaptor*   adaptor = nil;
    AVAssetWriterInput*                     audioInput = nil;   // pista AAC (cfg.withAudio)
    int   width = 0, height = 0, fps = 60;
    long long frameIndex = 0;
    long long audioPts   = 0;    // PTS de audio en SAMPLES (timescale = aSr)
    int   aSr = 48000, aCh = 2;
    bool active = false;
};

VideoExporter::VideoExporter() : impl (std::make_unique<Impl>()) {}
VideoExporter::~VideoExporter() { if (impl && impl->active) finish(); }

bool VideoExporter::begin (const Config& cfg)
{
    @autoreleasepool
    {
        error.clear();
        if (cfg.width <= 0 || cfg.height <= 0 || cfg.fps <= 0 || cfg.path.empty())
        { error = "invalid export config"; return false; }

        NSString* nsPath = [NSString stringWithUTF8String:cfg.path.c_str()];
        NSURL* url = [NSURL fileURLWithPath:nsPath];
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];   // sobrescribir

        NSError* err = nil;
        impl->writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&err];
        if (impl->writer == nil) { error = err ? err.localizedDescription.UTF8String : "writer init failed"; return false; }

        NSDictionary* compression = @{
            AVVideoAverageBitRateKey        : @(cfg.bitsPerSecond),
            AVVideoProfileLevelKey          : AVVideoProfileLevelH264HighAutoLevel,
            AVVideoAllowFrameReorderingKey  : @NO,
        };
        NSDictionary* settings = @{
            AVVideoCodecKey                 : AVVideoCodecTypeH264,
            AVVideoWidthKey                 : @(cfg.width),
            AVVideoHeightKey                : @(cfg.height),
            AVVideoCompressionPropertiesKey : compression,
        };
        impl->input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
        // Mudo: batch (NO). Con audio: realtime=YES en AMBOS inputs — el muxer multi-input con push ciego
        // batchea en ventanas (~1s) y isReady del video queda en NO para siempre (visto: se traba en f≈35).
        impl->input.expectsMediaDataInRealTime = cfg.withAudio ? YES : NO;

        NSDictionary* pbAttrs = @{
            (NSString*) kCVPixelBufferPixelFormatTypeKey  : @(kCVPixelFormatType_32BGRA),
            (NSString*) kCVPixelBufferWidthKey            : @(cfg.width),
            (NSString*) kCVPixelBufferHeightKey           : @(cfg.height),
            (NSString*) kCVPixelBufferIOSurfacePropertiesKey : @{},   // sin esto el pool del adaptor puede quedar nil
        };
        impl->adaptor = [AVAssetWriterInputPixelBufferAdaptor
            assetWriterInputPixelBufferAdaptorWithAssetWriterInput:impl->input
                                       sourcePixelBufferAttributes:pbAttrs];

        if (! [impl->writer canAddInput:impl->input]) { error = "cannot add video input"; return false; }
        [impl->writer addInput:impl->input];

        if (cfg.withAudio)   // pista AAC 256k (el writer comprime; nosotros empujamos PCM float32)
        {
            AudioChannelLayout acl {};
            acl.mChannelLayoutTag = cfg.audioChannels == 1 ? kAudioChannelLayoutTag_Mono
                                                           : kAudioChannelLayoutTag_Stereo;
            NSDictionary* aset = @{
                AVFormatIDKey         : @(kAudioFormatMPEG4AAC),
                AVSampleRateKey       : @(cfg.audioSampleRate),
                AVNumberOfChannelsKey : @(cfg.audioChannels),
                AVEncoderBitRateKey   : @(256000),
                AVChannelLayoutKey    : [NSData dataWithBytes:&acl length:sizeof (acl)],
            };
            impl->audioInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:aset];
            impl->audioInput.expectsMediaDataInRealTime = YES;   // ver nota del input de video
            if (! [impl->writer canAddInput:impl->audioInput]) { error = "cannot add audio input"; return false; }
            [impl->writer addInput:impl->audioInput];
            impl->aSr = cfg.audioSampleRate; impl->aCh = cfg.audioChannels; impl->audioPts = 0;
        }
        else impl->audioInput = nil;

        if (! [impl->writer startWriting]) { error = "startWriting failed"; return false; }
        [impl->writer startSessionAtSourceTime:kCMTimeZero];

        impl->width = cfg.width; impl->height = cfg.height; impl->fps = cfg.fps;
        impl->frameIndex = 0; impl->active = true;
        return true;
    }
}

bool VideoExporter::pushFrame (const uint8_t* rgba, int w, int h) noexcept
{
    if (! impl->active || rgba == nullptr || w != impl->width || h != impl->height) { error = "bad frame"; return false; }
    @autoreleasepool
    {
        // Espera activa breve a que el input acepte datos (writer offline, no realtime → casi nunca espera).
        for (int spins = 0; ! impl->input.isReadyForMoreMediaData && spins < 5000; ++spins)
            [NSThread sleepForTimeInterval:0.0005];
        if (! impl->input.isReadyForMoreMediaData) { error = "input not ready"; return false; }

        // El pool del adaptor puede ser nil hasta el 1er append (o si faltan attrs) → pasar un pool nil a
        // CVPixelBufferPoolCreatePixelBuffer CRASHEA. Guarda + fallback a CVPixelBufferCreate directo (robusto).
        CVPixelBufferRef pb = nullptr;
        CVReturn cvr;
        if (impl->adaptor.pixelBufferPool != nullptr)
            cvr = CVPixelBufferPoolCreatePixelBuffer (kCFAllocatorDefault, impl->adaptor.pixelBufferPool, &pb);
        else
        {
            NSDictionary* attrs = @{ (NSString*) kCVPixelBufferIOSurfacePropertiesKey : @{} };
            cvr = CVPixelBufferCreate (kCFAllocatorDefault, (size_t) impl->width, (size_t) impl->height,
                                       kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef) attrs, &pb);
        }
        if (cvr != kCVReturnSuccess || pb == nullptr) { error = "pixel buffer alloc failed"; return false; }

        CVPixelBufferLockBaseAddress (pb, 0);
        uint8_t*     dst    = (uint8_t*) CVPixelBufferGetBaseAddress (pb);
        const size_t stride = CVPixelBufferGetBytesPerRow (pb);
        if (dst == nullptr) { CVPixelBufferUnlockBaseAddress (pb, 0); CVPixelBufferRelease (pb); error = "nil base addr"; return false; }
        for (int y = 0; y < h; ++y)
        {
            const uint8_t* srow = rgba + (size_t) y * (size_t) w * 4;
            uint8_t*       drow = dst  + (size_t) y * stride;
            for (int x = 0; x < w; ++x)     // RGBA → BGRA
            {
                drow[x * 4 + 0] = srow[x * 4 + 2];   // B
                drow[x * 4 + 1] = srow[x * 4 + 1];   // G
                drow[x * 4 + 2] = srow[x * 4 + 0];   // R
                drow[x * 4 + 3] = srow[x * 4 + 3];   // A
            }
        }
        CVPixelBufferUnlockBaseAddress (pb, 0);

        const CMTime pts = CMTimeMake (impl->frameIndex, impl->fps);
        const bool ok = [impl->adaptor appendPixelBuffer:pb withPresentationTime:pts];
        CVPixelBufferRelease (pb);
        if (! ok) { error = impl->writer.error ? impl->writer.error.localizedDescription.UTF8String : "append failed"; return false; }
        ++impl->frameIndex;
        return true;
    }
}

// PCM float32 interleaved → CMSampleBuffer → pista AAC. PTS continuo en samples (sin drift).
bool VideoExporter::pushAudio (const float* interleaved, int numFrames) noexcept
{
    if (! impl->active || impl->audioInput == nil || interleaved == nullptr || numFrames <= 0)
    { error = "bad audio push"; return false; }
    @autoreleasepool
    {
        for (int spins = 0; ! impl->audioInput.isReadyForMoreMediaData && spins < 5000; ++spins)
            [NSThread sleepForTimeInterval:0.0005];
        if (! impl->audioInput.isReadyForMoreMediaData) { error = "audio input not ready"; return false; }

        AudioStreamBasicDescription asbd {};
        asbd.mSampleRate       = (Float64) impl->aSr;
        asbd.mFormatID         = kAudioFormatLinearPCM;
        asbd.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mChannelsPerFrame = (UInt32) impl->aCh;
        asbd.mBitsPerChannel   = 32;
        asbd.mBytesPerFrame    = (UInt32) (4 * impl->aCh);
        asbd.mFramesPerPacket  = 1;
        asbd.mBytesPerPacket   = (UInt32) (4 * impl->aCh);

        CMAudioFormatDescriptionRef fmt = nullptr;
        if (CMAudioFormatDescriptionCreate (kCFAllocatorDefault, &asbd, 0, nullptr, 0, nullptr, nullptr, &fmt) != noErr
            || fmt == nullptr) { error = "audio fmt failed"; return false; }

        const size_t bytes = (size_t) numFrames * 4 * (size_t) impl->aCh;
        CMBlockBufferRef block = nullptr;
        if (CMBlockBufferCreateWithMemoryBlock (kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault,
                                                nullptr, 0, bytes, 0, &block) != kCMBlockBufferNoErr || block == nullptr)
        { CFRelease (fmt); error = "audio block failed"; return false; }
        CMBlockBufferReplaceDataBytes (interleaved, block, 0, bytes);

        CMSampleBufferRef sb = nullptr;
        CMSampleTimingInfo timing { CMTimeMake (1, impl->aSr), CMTimeMake (impl->audioPts, impl->aSr), kCMTimeInvalid };
        const OSStatus st = CMSampleBufferCreate (kCFAllocatorDefault, block, true, nullptr, nullptr, fmt,
                                                  (CMItemCount) numFrames, 1, &timing, 0, nullptr, &sb);
        CFRelease (block); CFRelease (fmt);
        if (st != noErr || sb == nullptr) { error = "audio sample buffer failed"; return false; }

        const bool ok = [impl->audioInput appendSampleBuffer:sb];
        CFRelease (sb);
        if (! ok)
        { error = impl->writer.error ? impl->writer.error.localizedDescription.UTF8String : "audio append failed"; return false; }
        impl->audioPts += numFrames;
        return true;
    }
}

bool VideoExporter::finish() noexcept
{
    if (! impl->active) return false;
    @autoreleasepool
    {
        [impl->input markAsFinished];
        if (impl->audioInput != nil) [impl->audioInput markAsFinished];
        dispatch_semaphore_t sem = dispatch_semaphore_create (0);
        [impl->writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal (sem); }];
        dispatch_semaphore_wait (sem, dispatch_time (DISPATCH_TIME_NOW, (int64_t) 30 * NSEC_PER_SEC));
        const bool ok = (impl->writer.status == AVAssetWriterStatusCompleted);
        if (! ok && impl->writer.error) error = impl->writer.error.localizedDescription.UTF8String;
        impl->active = false;
        impl->writer = nil; impl->input = nil; impl->adaptor = nil; impl->audioInput = nil;
        return ok;
    }
}

bool VideoExporter::isActive() const noexcept { return impl && impl->active; }
}
