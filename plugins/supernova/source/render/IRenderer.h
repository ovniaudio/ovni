#pragma once
#include <cstdint>
#include "render/ParticleParams.h"

// IRenderer — frontera dura entre la simulación/estética (especificada una vez) y cada backend nativo
// (Metal en macOS, D3D11 en Windows). Metal es la referencia dorada; D3D11 se porta contra sus frames
// (spec §9.4, riesgo R2). C++ puro: sin JUCE ni GPU en el header. CONGELADO en M0.
namespace supernova
{
struct AnalysisFrame;  // analysis/AnalysisFrame.h

// Densidad default de la grilla de partículas (512² ≈ 262k, spec RF2). También es la resolución de los
// campos de contenido (saliencia/flow) que los callers precomputan.
inline constexpr int kParticleGrid = 512;

// Textura fuente muestreada a grilla: cada partícula toma su posición "hogar" y su color del píxel.
// `saliency` (opcional, extensión M-natural): heatmap gridW×gridH [0..1] de "qué es lo importante"
// (Apple Vision en macOS; spectral residual como fallback futuro en Windows). nullptr = sin saliencia →
// el renderer deriva pesos de la luma (camino clásico). El caller la computa FUERA del render loop.
struct SourceImage
{
    const uint8_t* rgba = nullptr;  // RGBA8, row-major
    int width  = 0;
    int height = 0;
    const float* saliency    = nullptr;   // gridW×gridH (la grilla de prepare()), row 0 = fila superior
    // Máscara SUAVE del sujeto [0..1] (CUTOUT: "borrar el fondo, que quede la forma"). Si viene, el fondo
    // (mask≈0) se vuelve invisible y su física se apaga: queda la escultura de partículas del sujeto.
    const float* subjectMask = nullptr;   // gridW×gridH
    // Depth por celda [0..1] (0.5 = plano de la pantalla, 1 = hacia la cámara) para el 3D de presentación.
    // nullptr = el renderer lo deriva solo (almohada desde la silueta + luma, ImageField::computeDepth).
    // Este es el enchufe del modelo CoreML (Depth-Anything, hilo de decode) cuando llegue.
    const float* depth = nullptr;         // gridW×gridH
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    // ---- Disponibilidad (GPU-opcional: sin GPU → false → el editor muestra el panel fallback) ----
    virtual bool isAvailable() const = 0;

    // ---- Ciclo de vida (todo por-editor; nada en estáticos) ----
    virtual void prepare (int gridW, int gridH) = 0;          // dimensiona buffers (gridW*gridH partículas)
    virtual void uploadImage (const SourceImage& img) = 0;    // sube hogar+color (swap fuera del render loop)
    virtual void resize (int pxW, int pxH, float contentsScale) = 0;  // drawableSize = bounds * scale

    // ---- Un frame on-screen: avanza la simulación con el análisis y dibuja al drawable ----
    virtual void render (const AnalysisFrame& frame, const ParticleParams& params, double dtSeconds) = 0;

    // ---- Camino offscreen para --render-frames (M4) y paridad (§9.4): rinde a textura y lee a RGBA8 CPU ----
    virtual bool renderOffscreen (const AnalysisFrame& frame, const ParticleParams& params,
                                  int pxW, int pxH, uint8_t* outRgba) = 0;
};
}
