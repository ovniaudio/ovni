#pragma once
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <cstdint>
#include "render/IRenderer.h"   // SourceImage

// ImageLoader — decodifica una imagen del usuario (RF1: drag & drop) a RGBA8 contiguo, lista para muestrear
// a la grilla de partículas (MetalRenderer::uploadImage). Nunca crashea (spec §8): entrada corrupta/ilegible →
// LoadedImage inválida. Downsample de lado mayor > 4096 px antes de tocar la GPU (RNF7: no subir texturas
// gigantes ni reventar el presupuesto de memoria). Depende sólo de juce_graphics (testeable sin ensamblar el
// plugin ni GPU).
namespace supernova
{
struct LoadedImage
{
    std::vector<uint8_t> rgba;      // RGBA8 row-major (width*height*4)
    int width  = 0;
    int height = 0;
    std::vector<float> saliency;     // opcional (VisionField, hilo de decode); vacía = sin señal
    std::vector<float> subjectMask;  // opcional (cutout): máscara suave del sujeto; vacía = no disponible

    bool valid() const noexcept { return ! rgba.empty() && width > 0 && height > 0; }

    // withCutout: incluir la máscara del sujeto (si existe) → el renderer borra el fondo (modo CUTOUT).
    SourceImage source (bool withCutout = false) const noexcept
    {
        return { rgba.data(), width, height,
                 saliency.empty() ? nullptr : saliency.data(),
                 (withCutout && ! subjectMask.empty()) ? subjectMask.data() : nullptr };
    }
};

class ImageLoader
{
public:
    static constexpr int kMaxSide = 4096;   // RNF7: lado mayor máximo que subimos a GPU

    // Decodifica desde archivo (PNG/JPEG/GIF… lo que decodifica JUCE). Inválida si no existe/no decodifica.
    static LoadedImage fromFile (const juce::File& file);

    // Decodifica desde bytes crudos (algunos drops entregan datos, no una ruta). maxSide = tope del lado mayor
    // para los formatos que van por ImageIO (HEIC/WebP/TIFF, mac): el motor pide kMaxSide; las miniaturas
    // piden mucho menos y así un HEIC de 48 MP no pasa entero por memoria para un tile de 58 px.
    static LoadedImage fromEncodedData (const void* data, size_t numBytes, int maxSide = kMaxSide);

    // Convierte una juce::Image ya decodificada → RGBA8 (aplica downsample RNF7). Inválida si la imagen es nula.
    static LoadedImage fromImage (const juce::Image& image);

    // Factor entero de downsample para que max(w,h) <= maxSide (>=1). Puro (testeable sin decodificar).
    static int downsampleFactor (int w, int h, int maxSide = kMaxSide) noexcept;

    // ORIENTACIÓN. JUCE NO aplica el tag EXIF → las fotos de celular llegan de costado (el bug de "foto
    // vertical en lienzo horizontal"). Parseamos el APP1 del JPEG y enderezamos en el decode.
    // exifOrientation: 1..8 (1 = normal), 0/1 si no hay tag o no es JPEG. Puro (testeable con blobs).
    static int  exifOrientation (const void* data, size_t numBytes) noexcept;
    // applyOrientation: rota/espeja el RGBA in-place según la orientación EXIF (1..8). 1 = no-op.
    static void applyOrientation (LoadedImage& im, int orientation);
    // orientationOf: la orientación (1..8) de CUALQUIER formato que decodificamos: JPEG por el APP1 (parser
    // propio), HEIC/TIFF/WebP por ImageIO en macOS (el `irot` del HEIF no vive en APP1). 1 si no hay dato.
    static int  orientationOf (const void* data, size_t numBytes) noexcept;
    // rotate90: gira el RGBA in-place `turns` cuartos de vuelta CW (el botón manual ⟳). turns se toma mod 4.
    static void rotate90 (LoadedImage& im, int turns);

    // ¿Esta ruta parece una imagen que sabemos decodificar? (para filtrar el drop antes de leer el archivo).
    static bool looksLikeImage (const juce::File& file);
    // Las extensiones aceptadas ("png;jpg;…"; en macOS también heic/heif/webp/tif/tiff/bmp) — drop y FileChooser.
    static const char* imageExtensions() noexcept;
};
}
