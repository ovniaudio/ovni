#pragma once
// VideoExporter (Phase C) — frames RGBA → MP4 (H.264) con AVAssetWriter. Fachada mac-only con pimpl (como
// VideoSource/SystemAudioSource): el header es C++ PURO (sin AVFoundation) → compila en tests/Windows; el .mm
// tiene el encoder. En Windows la fachada existe pero begin() devuelve false (Media Foundation = M5).
//
// Uso: begin(cfg) → pushFrame(rgba,…) por cada cuadro (en orden) → finish(). El adaptador maneja el pool de
// CVPixelBuffers; pushFrame convierte RGBA8→BGRA. La PTS avanza a 1/fps por cuadro.
#include <cstdint>
#include <memory>
#include <string>

namespace supernova
{
class VideoExporter
{
public:
    struct Config
    {
        int         width  = 1920;
        int         height = 1080;
        int         fps    = 60;
        int         bitsPerSecond = 12'000'000;
        std::string path;                 // destino .mp4 (absoluto)
        // Export CON SONIDO (opcional): agrega una pista AAC 256k; alimentar con pushAudio().
        bool        withAudio       = false;
        int         audioSampleRate = 48000;
        int         audioChannels   = 2;
    };

    VideoExporter();
    ~VideoExporter();

    bool begin (const Config& cfg);                                  // crea el writer; false si falla
    bool pushFrame (const uint8_t* rgba, int w, int h) noexcept;      // un cuadro RGBA8 (w·h·4); false si falla
    bool pushAudio (const float* interleaved, int numFrames) noexcept; // PCM float32 interleaved (cfg.withAudio)
    bool finish() noexcept;                                          // finaliza y cierra el archivo (bloquea)
    bool isActive() const noexcept;
    const std::string& lastError() const noexcept { return error; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::string error;
};
}
