// VisionField.mm — saliencia Apple Vision (ver VisionField.h). Adaptado del esqueleto BENCHMARKEADO en esta
// máquina (vision_bench.mm de la investigación): compiló y corrió headless en std::thread sin main thread ni
// entitlements; attention 4.8 ms @1MP (M4). Reglas duras verificadas: (1) el heatmap es OneComponent32Float
// con STRIDE (bytesPerRow ≠ w*4 → jamás indexar y*w+x), (2) row 0 = fila SUPERIOR (alinea con nuestro RGBA
// sin flip), (3) normalizar al pico (el máximo real ronda 0.5–0.8), (4) @autoreleasepool obligatorio (el
// juce::Thread no tiene pool). Cualquier falla → vector vacío → el caller usa el fallback CPU (ImageField).
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <CoreGraphics/CoreGraphics.h>
#include <algorithm>
#include <cmath>
#include "image/VisionField.h"

namespace supernova
{
namespace
{
// CGImage desde nuestros bytes RGBA8 — sin copia y sin disco (alpha ignorado para análisis).
CGImageRef makeCGImage (const uint8_t* rgba, int w, int h)
{
    CGDataProviderRef provider = CGDataProviderCreateWithData (nullptr, rgba,
                                                               (size_t) w * (size_t) h * 4, nullptr);
    if (provider == nullptr) return nullptr;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGImageRef img = CGImageCreate ((size_t) w, (size_t) h, 8, 32, (size_t) w * 4, cs,
                                    (CGBitmapInfo) kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big,
                                    provider, nullptr, false, kCGRenderingIntentDefault);
    CGColorSpaceRelease (cs);
    CGDataProviderRelease (provider);   // img retiene al provider
    return img;
}

// Remuestreo bilineal de un CVPixelBuffer OneComponent32Float → grilla (respeta el STRIDE; row 0 = arriba).
// maxOut (opcional) devuelve el pico. Vector vacío si el formato/dims no sirven.
std::vector<float> resampleToGrid (CVPixelBufferRef buf, int gridW, int gridH, float* maxOut = nullptr)
{
    if (buf == nullptr || CVPixelBufferGetPixelFormatType (buf) != kCVPixelFormatType_OneComponent32Float)
        return {};
    CVPixelBufferLockBaseAddress (buf, kCVPixelBufferLock_ReadOnly);
    const int    hw   = (int) CVPixelBufferGetWidth (buf);
    const int    hh   = (int) CVPixelBufferGetHeight (buf);
    const size_t rowB = CVPixelBufferGetBytesPerRow (buf);
    const auto*  base = (const uint8_t*) CVPixelBufferGetBaseAddress (buf);
    if (base == nullptr || hw < 2 || hh < 2)
    {
        CVPixelBufferUnlockBaseAddress (buf, kCVPixelBufferLock_ReadOnly);
        return {};
    }
    std::vector<float> out ((size_t) gridW * (size_t) gridH);
    float maxV = 0.0f;
    for (int gy = 0; gy < gridH; ++gy)
    {
        const float fy = ((gy + 0.5f) / (float) gridH) * hh - 0.5f;
        const int   y0 = std::clamp ((int) std::floor (fy), 0, hh - 1);
        const int   y1 = std::min (y0 + 1, hh - 1);
        const float ty = std::clamp (fy - (float) y0, 0.0f, 1.0f);
        const auto* r0 = (const float*) (base + (size_t) y0 * rowB);
        const auto* r1 = (const float*) (base + (size_t) y1 * rowB);
        for (int gx = 0; gx < gridW; ++gx)
        {
            const float fx = ((gx + 0.5f) / (float) gridW) * hw - 0.5f;
            const int   x0 = std::clamp ((int) std::floor (fx), 0, hw - 1);
            const int   x1 = std::min (x0 + 1, hw - 1);
            const float tx = std::clamp (fx - (float) x0, 0.0f, 1.0f);
            const float top = r0[x0] + (r0[x1] - r0[x0]) * tx;
            const float bot = r1[x0] + (r1[x1] - r1[x0]) * tx;
            const float v   = top + (bot - top) * ty;
            out[(size_t) gy * gridW + gx] = v;
            maxV = std::max (maxV, v);
        }
    }
    CVPixelBufferUnlockBaseAddress (buf, kCVPixelBufferLock_ReadOnly);
    if (maxOut != nullptr) *maxOut = maxV;
    return out;
}
}

std::vector<float> visionSaliency (const uint8_t* rgba, int w, int h,
                                   int gridW, int gridH, SaliencyMode mode)
{
    if (rgba == nullptr || w <= 0 || h <= 0 || gridW <= 0 || gridH <= 0)
        return {};

    std::vector<float> out;
    @autoreleasepool
    {
        CGImageRef cgImage = makeCGImage (rgba, w, h);
        if (cgImage == nullptr) return {};

        // performRequests: es SINCRÓNICO → correcto en el thread de carga (fondo), jamás en el de audio.
        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCGImage: cgImage options: @{}];
        VNRequest* request = (mode == SaliencyMode::attention)
            ? (VNRequest*) [VNGenerateAttentionBasedSaliencyImageRequest new]
            : (VNRequest*) [VNGenerateObjectnessBasedSaliencyImageRequest new];

        NSError* error = nil;
        const bool ok = [handler performRequests: @[request] error: &error];
        CGImageRelease (cgImage);
        if (! ok || request.results.count == 0)
            return {};

        VNSaliencyImageObservation* obs = (VNSaliencyImageObservation*) request.results.firstObject;
        float maxV = 0.0f;
        out = resampleToGrid (obs.pixelBuffer, gridW, gridH, &maxV);

        // Normalizar al pico; heatmap plano = sin señal → fallback.
        if (out.empty() || maxV <= 1.0e-6f)
            return {};
        for (auto& v : out) v /= maxV;
    }
    return out;
}

std::vector<float> visionSubjectMask (const uint8_t* rgba, int w, int h, int gridW, int gridH)
{
    if (rgba == nullptr || w <= 0 || h <= 0 || gridW <= 0 || gridH <= 0)
        return {};

    std::vector<float> out;
    @autoreleasepool
    {
        CGImageRef cgImage = makeCGImage (rgba, w, h);
        if (cgImage == nullptr) return {};
        VNImageRequestHandler* handler =
            [[VNImageRequestHandler alloc] initWithCGImage: cgImage options: @{}];
        CGImageRelease (cgImage);

        // 1) ForegroundInstanceMask (macOS 14+): el modelo de "Quitar fondo" de Finder/Fotos — sujeto
        //    genérico (objetos, personas, mascotas). La máscara escalada a la imagen viene suave (soft edges).
        if (@available (macOS 14.0, *))
        {
            NSError* error = nil;
            VNGenerateForegroundInstanceMaskRequest* req = [VNGenerateForegroundInstanceMaskRequest new];
            if ([handler performRequests: @[req] error: &error] && req.results.count > 0)
            {
                VNInstanceMaskObservation* obs = (VNInstanceMaskObservation*) req.results.firstObject;
                CVPixelBufferRef mask = [obs generateScaledMaskForImageForInstances: obs.allInstances
                                                                fromRequestHandler: handler
                                                                             error: &error];
                if (mask != nullptr)
                {
                    float maxV = 0.0f;
                    out = resampleToGrid (mask, gridW, gridH, &maxV);
                    CVPixelBufferRelease (mask);
                    if (! out.empty() && maxV > 0.1f)
                        return out;
                    out.clear();
                }
            }
        }

        // 2) PersonSegmentation accurate (macOS 12+): si hay una persona, su máscara es excelente.
        //    Sin persona el modelo devuelve ~0 en todos lados (medido: max≈0.05) → umbral 0.15 y seguimos.
        if (@available (macOS 12.0, *))
        {
            NSError* error = nil;
            VNGeneratePersonSegmentationRequest* req = [VNGeneratePersonSegmentationRequest new];
            req.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelAccurate;
            req.outputPixelFormat = kCVPixelFormatType_OneComponent32Float;
            if ([handler performRequests: @[req] error: &error] && req.results.count > 0)
            {
                VNPixelBufferObservation* obs = (VNPixelBufferObservation*) req.results.firstObject;
                float maxV = 0.0f;
                out = resampleToGrid (obs.pixelBuffer, gridW, gridH, &maxV);
                if (! out.empty() && maxV > 0.15f)
                    return out;
                out.clear();
            }
        }
    }
    return out;   // vacío → el caller deriva la máscara de la saliencia (fallback universal)
}
}
