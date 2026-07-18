#pragma once
// ExportPreset — formatos de EXPORT a video (Phase C · la palanca de adopción masiva: convertir tu track en un
// loop para release/Reels/TikTok). Puro (sin JUCE/AVFoundation) → testeable. El VideoExporter (mac) hace el
// encode real con AVAssetWriter.
#include <cstdint>
#include <cmath>

namespace supernova
{
enum class ExportFormat : int
{
    HD1080 = 0,      // 1920×1080 — YouTube / landscape
    UHD4K,           // 3840×2160 — 4K master
    Square1080,      // 1080×1080 — Instagram feed 1:1
    Vertical1080     // 1080×1920 — Reels / TikTok / Shorts 9:16
};

struct ExportDims { int width = 1920, height = 1080; };

inline ExportDims exportDims (ExportFormat f) noexcept
{
    switch (f)
    {
        case ExportFormat::HD1080:       return { 1920, 1080 };
        case ExportFormat::UHD4K:        return { 3840, 2160 };
        case ExportFormat::Square1080:   return { 1080, 1080 };
        case ExportFormat::Vertical1080: return { 1080, 1920 };
    }
    return { 1920, 1080 };
}

inline const char* exportFormatName (ExportFormat f) noexcept
{
    switch (f)
    {
        case ExportFormat::HD1080:       return "1080p (16:9)";
        case ExportFormat::UHD4K:        return "4K (16:9)";
        case ExportFormat::Square1080:   return "Square (1:1)";
        case ExportFormat::Vertical1080: return "Vertical (9:16)";
    }
    return "1080p";
}

// Bitrate H.264 recomendado (bits/seg): ~0.10 bits por (pixel·frame) → nítido sin archivos gigantes.
// 1080p60 ≈ 12 Mbps, 4K60 ≈ 50 Mbps (se clampa a un piso/techo sano).
inline int recommendedBitrate (ExportDims d, int fps) noexcept
{
    const long long px = (long long) d.width * d.height * (fps < 1 ? 1 : fps);
    long long bps = (long long) (px * 0.10);
    if (bps < 4'000'000)   bps = 4'000'000;      // piso 4 Mbps
    if (bps > 120'000'000) bps = 120'000'000;    // techo 120 Mbps
    return (int) bps;
}

// PHOTO-SEQUENCE CYCLING (fix · export cicla las fotos). Puro → testeable; usado por SupernovaEditor::exportVideo.
// Cuántos FRAMES dura cada foto en el clip: intervalo (seg) × fps, mínimo 1 (una foto nunca dura 0 frames).
inline int framesPerPhoto (double intervalSeconds, int fps) noexcept
{
    const int f = fps < 1 ? 1 : fps;
    const long long n = std::llround (intervalSeconds * (double) f);
    return n < 1 ? 1 : (int) n;
}

// Qué foto (0..numPhotos-1) muestra el frame `frame`: cada foto su ventana contigua, en loop sobre el clip.
inline int photoSlotForFrame (int frame, int perPhoto, int numPhotos) noexcept
{
    if (numPhotos <= 0) return 0;
    const int pp = perPhoto < 1 ? 1 : perPhoto;
    const int f  = frame < 0 ? 0 : frame;
    return (f / pp) % numPhotos;
}
}
