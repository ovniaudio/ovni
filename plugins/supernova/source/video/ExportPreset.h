#pragma once
// ExportPreset — formatos de EXPORT a video (Phase C · la palanca de adopción masiva: convertir tu track en un
// loop para release/Reels/TikTok). Puro (sin JUCE/AVFoundation) → testeable. El VideoExporter (mac) hace el
// encode real con AVAssetWriter.
#include <cstdint>
#include <cmath>
#include <vector>

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

// ================= MEDIA SESSION PRO (2026-09-02) · el export sigue el RELOJ, el ORDEN y la TRANSICIÓN =================
// Hasta acá el export ciclaba SIEMPRE por segundos, en orden lineal y sin BURST: un loop armado en Beats o
// Kick, en Shuffle o con explosión, se exportaba distinto de como se veía. Estas funciones (puras) arman el
// PLAN del clip ANTES de lanzar el hilo de export (que sólo lo lee, por valor).

// BEATS: cuántos SEGUNDOS dura cada foto a N beats del BPM del host. Lo usan el export y el menú
// "Full photo loop" (una vuelta entera de la secuencia al compás, no a los segundos del otro reloj).
inline double secondsPerPhotoBeats (double intervalBeats, double bpm) noexcept
{
    const double safeBpm = bpm > 1.0 ? bpm : 120.0;      // un host sin tempo (o parado) → 120 BPM
    return intervalBeats * 60.0 / safeBpm;
}

// BEATS: los mismos segundos, en cuadros.
inline int framesPerPhotoBeats (double intervalBeats, double bpm, int fps) noexcept
{
    return framesPerPhoto (secondsPerPhotoBeats (intervalBeats, bpm), fps);
}

// ---- La TASA del anillo de análisis (ronda 2b) ----------------------------------------------------------
// El editor publica UN frame de análisis por tick de su timer (30 Hz), no uno por cuadro de video: un clip a
// 60 fps que consuma uno por cuadro reproduce la reactividad al DOBLE de velocidad y desincroniza el audio
// muxeado. Estas tres funciones son el mapeo correcto entre el anillo y los cuadros del clip.

// Qué frame de análisis le toca al cuadro `frame` del clip: el anillo corre a su propia tasa y se repite en
// loop (a 60 fps / 30 Hz, los cuadros 0-1 → 0, 2-3 → 1, …).
inline int analysisIndexForFrame (int frame, int fps, int ringHz, int count) noexcept
{
    if (count <= 0) return 0;
    const int f = fps < 1 ? 1 : fps, hz = ringHz < 1 ? 1 : ringHz;
    const long long k = ((long long) (frame < 0 ? 0 : frame) * hz) / f;
    return (int) (k % (long long) count);
}

// Cuántos CUADROS del clip dura una vuelta entera de `count` frames de análisis.
inline int exportLoopFrames (int count, int fps, int ringHz) noexcept
{
    if (count <= 0) return 0;
    const int f = fps < 1 ? 1 : fps, hz = ringHz < 1 ? 1 : ringHz;
    const long long v = ((long long) count * f) / hz;
    return (int) (v < 1 ? 1 : v);
}

// Qué tramo del anillo de análisis entra en el loop. CON sonido, el análisis y el audio tienen que cubrir el
// MISMO tramo: el anillo de audio guarda `audioSeconds`, así que se usan los ÚLTIMOS frames que cubren ese
// tiempo (los más recientes, que es lo que el ring de audio tiene). Sin sonido, el anillo entero.
struct ExportWindow { int first = 0, count = 0; };

inline ExportWindow exportLoopWindow (int ringSize, int ringHz, double audioSeconds, bool wantAudio) noexcept
{
    if (ringSize <= 0) return {};
    if (! wantAudio || audioSeconds <= 0.0) return { 0, ringSize };
    const int hz  = ringHz < 1 ? 1 : ringHz;
    const int cap = (int) std::llround (audioSeconds * (double) hz);
    if (cap >= ringSize) return { 0, ringSize };
    const int count = cap < 1 ? 1 : cap;
    return { ringSize - count, count };
}

// KICK · paso 1 — los cuadros del CLIP donde el contador de onsets cambia. `windowCounts` es la ventana del
// anillo que reproduce el clip (a su tasa `ringHz`), así que el índice k del anillo cae en el PRIMER cuadro
// que lo muestra (el inverso exacto de analysisIndexForFrame) y la vuelta se repite cada exportLoopFrames.
// El cruce de la vuelta (posición 0) NO cuenta: el contador es monótono adentro del anillo y al volver al
// principio "baja" — sería un edge falso.
inline std::vector<int> onsetFramesFromCounts (const std::vector<unsigned>& windowCounts, int totalFrames,
                                               int fps, int ringHz)
{
    std::vector<int> out;
    const int n = (int) windowCounts.size();
    if (n < 2 || totalFrames <= 0) return out;
    const int f = fps < 1 ? 1 : fps, hz = ringHz < 1 ? 1 : ringHz;
    const int loopV = exportLoopFrames (n, f, hz);
    for (int base = 0; base < totalFrames; base += loopV)
        for (int k = 1; k < n; ++k)
        {
            if (windowCounts[(size_t) k] == windowCounts[(size_t) (k - 1)]) continue;
            const int frame = base + (int) (((long long) k * f + hz - 1) / hz);   // primer cuadro que muestra k
            // Con fps < ringHz (24 fps sobre un anillo de 30 Hz) el clip tiene MENOS cuadros que frames de
            // análisis: hay índices del anillo que no se muestran nunca y dos onsets seguidos pueden caer en
            // el mismo cuadro. El techo de arriba sigue dando "el primer cuadro que muestra un índice ≥ k";
            // acá sacamos el duplicado (si no, dos cambios de foto en el mismo cuadro) y el onset que se iría
            // PASADO su propia vuelta — ahí el anillo ya volvió a empezar y ese cuadro muestra otra cosa.
            if (frame < totalFrames && frame < base + loopV && (out.empty() || out.back() != frame))
                out.push_back (frame);
        }
    return out;
}

// KICK · paso 2 — de esos onsets sobreviven sólo los que respetan el GAP mínimo, igual que en vivo: el reloj
// se arma en el cuadro 0 (la foto actual ya está en pantalla), así que el primer cambio pide un gap entero.
inline std::vector<int> kickSwitchFrames (const std::vector<int>& onsetFrames, double gapSeconds, int fps)
{
    std::vector<int> out;
    const int f = fps < 1 ? 1 : fps;
    const int gapFrames = (int) std::llround ((gapSeconds < 0.0 ? 0.0 : gapSeconds) * (double) f);
    int last = 0;
    for (int fr : onsetFrames)
    {
        if (fr <= 0) continue;                           // el cuadro 0 no es un cambio
        if (fr - last >= gapFrames) { out.push_back (fr); last = fr; }
    }
    return out;
}

// Un cuadro del plan: qué foto muestra y si ESE cuadro es el del cambio (el BURST dura un solo cuadro).
struct ExportSlot { int slot = 0; bool changed = false; };

// El plan del clip entero. `playOrder` es el orden de reproducción ya resuelto (LOOP = el natural desde la
// foto actual; SHUFFLE = el que produce la propia secuencia) y se recorre en loop. `switchFrames` (KICK)
// manda sobre el reloj periódico: si viene vacío, se cambia cada `perPhoto` cuadros. El cuadro 0 nunca es
// cambio: la primera foto ya está puesta cuando arranca el clip.
inline std::vector<ExportSlot> exportSlotPlan (int totalFrames, int perPhoto, const std::vector<int>& playOrder,
                                               const std::vector<int>& switchFrames = {})
{
    std::vector<ExportSlot> plan;
    const int tf = totalFrames < 0 ? 0 : totalFrames;
    plan.reserve ((size_t) tf);
    const int n = (int) playOrder.size();
    if (n <= 0) { plan.assign ((size_t) tf, ExportSlot {}); return plan; }

    const int pp = perPhoto < 1 ? 1 : perPhoto;
    size_t nextSwitch = 0;
    int    pos = 0;                                      // posición en el orden de reproducción
    for (int f = 0; f < tf; ++f)
    {
        bool changed = false;
        if (! switchFrames.empty())
        {
            while (nextSwitch < switchFrames.size() && switchFrames[nextSwitch] <= f)
            {
                ++nextSwitch;
                if (f > 0) { changed = true; ++pos; }
            }
        }
        else if (f > 0 && f % pp == 0) { changed = true; ++pos; }
        plan.push_back ({ playOrder[(size_t) (pos % n)], changed });
    }
    return plan;
}
}
