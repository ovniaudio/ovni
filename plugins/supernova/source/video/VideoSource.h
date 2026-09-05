#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>   // juce::Image (miniatura)
#include <memory>
#include <vector>

// VideoSource (spec §C) — reproduce un archivo de video como FUENTE de color para las partículas (la
// pantalla LED viva). Decodifica en un HILO propio a RGBA, publica el último frame (latest-wins) y loopea.
// mac-only (AVFoundation); en Windows la fachada existe pero open() devuelve false (Media Foundation = M5).
// El renderer consume el frame por updateColors (camino rápido: reescribe SOLO los colores, sin re-análisis).
namespace supernova
{
class VideoSource
{
public:
    VideoSource();
    ~VideoSource();

    bool open (const juce::File& file);          // false si no es un video legible
    void close();
    bool isOpen() const noexcept;
    void setPlaying (bool p) noexcept;

    float aspect() const noexcept;               // w/h del video (ya rotado); 1 si no hay video
    void  rotate() noexcept;                      // ⟳: gira 90° CW la salida (encima del EXIF/transform)

    // Toma el último frame si hay uno NUEVO desde la última llamada (message thread). true → llena out*.
    bool latestFrame (std::vector<uint8_t>& outRgba, int& outW, int& outH);

    // ¿parece un archivo de video que sabemos abrir? (filtra el drop antes de tocar AVFoundation).
    static bool looksLikeVideo (const juce::File& file)
    {
        return file.hasFileExtension ("mp4;mov;m4v");
    }

    // MINIATURA (MEDIA SESSION PRO): un frame temprano del video, ya enderezado por su preferredTransform,
    // con el lado mayor <= maxSide. Bloqueante (llamar desde el hilo de miniaturas). Imagen nula si falla
    // o en plataformas sin AVFoundation.
    static juce::Image firstFrameThumbnail (const juce::File& file, int maxSide);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
