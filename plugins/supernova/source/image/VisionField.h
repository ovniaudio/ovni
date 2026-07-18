#pragma once
#include <vector>
#include <cstdint>

// VisionField — saliencia REAL de Apple Vision para SUPERNOVA ("reconocer qué es la imagen"). API C++ pura;
// la implementación Obj-C++ vive en VisionField.mm (solo APPLE). Verificado headless en thread de fondo:
// sin main thread, sin entitlements, sin prompt (framework público, modelos del sistema, ~5 ms @1MP, corre
// en ANE → no compite con el render Metal). En plataformas sin Vision (Windows M5) o si Vision falla,
// devuelve vector VACÍO → el caller se queda con los pesos CPU de ImageField (fallback ya existente).
namespace supernova
{
enum class SaliencyMode { attention, objectness };

// gridW*gridH floats [0..1] (row 0 = fila SUPERIOR, misma convención que el RGBA de entrada), normalizado
// al pico. VACÍO = sin Vision / falló / heatmap plano → usar el fallback.
std::vector<float> visionSaliency (const uint8_t* rgba, int w, int h,
                                   int gridW, int gridH,
                                   SaliencyMode mode = SaliencyMode::attention);

// Máscara SUAVE del SUJETO [0..1] por celda (para el modo CUTOUT: "borrar el fondo, que quede la forma").
// Cascada: ForegroundInstanceMask (macOS 14+, el modelo de "Quitar fondo" de Finder/Fotos) → Person
// Segmentation accurate (macOS 12+) → nada (vector VACÍO → el caller deriva una máscara de la saliencia).
std::vector<float> visionSubjectMask (const uint8_t* rgba, int w, int h, int gridW, int gridH);
}
