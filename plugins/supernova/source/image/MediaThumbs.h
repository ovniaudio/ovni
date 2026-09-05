#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "image/ImageLoader.h"

// MediaThumbCache (MEDIA SESSION PRO, 2026-09-02) — miniaturas de los items de la sesión para la tira.
// Un hilo de fondo decodifica DE A UNA (cancelable), con los MISMOS bytes y la MISMA corrección de
// orientación que el motor (EXIF/HEIF + rotación manual del usuario) → lo que ves en la tira es lo que
// carga el lattice. Cache por `path#rot`; el message thread pide con get() (no bloquea: si no está, encola
// y devuelve nullptr) y se entera por onThumbReady (callAsync). Video: primer frame por AVFoundation.
namespace supernova
{
struct MediaThumb
{
    juce::Image image;          // ARGB opaca, lado mayor <= kThumbSide (o nula si falló)
    int  srcW = 0, srcH = 0;    // dims de la FUENTE ya enderezada+rotada (PNG/JPEG: reales; HEIC/WebP/TIFF: reducidas
                                // por el decoder, mismo aspecto) — sólo el ASPECTO se usa (AUTO canvas)
    bool failed = false;        // no decodifica (borrada/corrupta/formato ajeno)

    float aspect() const noexcept { return (srcW > 0 && srcH > 0) ? (float) srcW / (float) srcH : 0.0f; }
};

class MediaThumbCache : private juce::Thread
{
public:
    static constexpr int kThumbSide = 116;   // 2× de un tile de 58 px (retina)
    static constexpr int kMaxEntries = 256;  // ~54 KB c/u → tope ~14 MB; al pasarlo se vacía

    MediaThumbCache();
    ~MediaThumbCache() override;

    // Message thread. Devuelve la miniatura si ya está; si no, la encola (una sola vez) y devuelve nullptr.
    std::shared_ptr<const MediaThumb> get (const juce::String& path, int rot, bool isVideo);
    // ¿Está (lista o fallida) sin encolar nada?
    std::shared_ptr<const MediaThumb> peek (const juce::String& path, int rot) const;
    void clear();

    std::function<void()> onThumbReady;   // message thread: llegó una nueva → la tira repinta

    // ---- PURO (testeable sin hilos) ----
    // Reduce un RGBA8 a lado mayor `maxSide` con filtro de caja (promedio por bloque). Alpha = 255.
    static juce::Image makeThumbnail (const uint8_t* rgba, int w, int h, int maxSide);
    static juce::Image makeThumbnail (const LoadedImage& li, int maxSide)
    {
        return makeThumbnail (li.rgba.data(), li.width, li.height, maxSide);
    }
    // Decodifica + endereza (EXIF/HEIF) + rotación manual → miniatura. BLOQUEANTE (hilo de fondo).
    static MediaThumb loadThumb (const juce::File& file, int rot, bool isVideo, int maxSide);

    static juce::String keyFor (const juce::String& path, int rot) { return path + "#" + juce::String (((rot % 4) + 4) % 4); }

private:
    void run() override;

    struct Job { std::string key; juce::String path; int rot = 0; bool isVideo = false; };

    mutable juce::CriticalSection lock;
    std::vector<Job>                                          queue;    // FIFO (bajo lock)
    std::set<std::string>                                     pending;  // claves encoladas (bajo lock)
    std::map<std::string, std::shared_ptr<const MediaThumb>>  map;      // resultados (bajo lock)
    std::shared_ptr<std::atomic<bool>>                        alive;    // guarda del callAsync tras el dtor

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MediaThumbCache)
};
}
