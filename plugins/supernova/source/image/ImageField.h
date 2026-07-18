#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

// ImageField — análisis de CONTENIDO de la imagen fuente (C++ puro, sin GPU ni JUCE → testeable directo).
// Responde al "que reconozca qué es y lo mueva así" (feedback de Joaquín): en vez de latir desde el centro
// geométrico de la pantalla (un "hueco" arbitrario sobre una foto), el movimiento nace del CONTENIDO:
//   · centroid  — centro de masa de luminancia: el latido/explosión emana del SUJETO de la imagen.
//   · weights   — saliencia por celda (luma con piso): el sujeto brillante vive, el fondo oscuro acompaña.
//   · flow      — tangente de los bordes (Sobel ⟂ gradiente): las partículas fluyen SIGUIENDO las formas.
// Todo se computa UNA vez al cargar (fuera del render loop, RNF4) sobre la grilla de partículas.
namespace supernova
{
struct ImageFieldResult
{
    float centroidX = 0.5f, centroidY = 0.5f;   // centro de masa de luma, coords normalizadas [0..1]
    std::vector<float> weight;                  // por celda de grilla: 0.25..1 (piso: el fondo no muere)
    std::vector<float> flowXY;                  // por celda: dirección tangente al borde × fuerza (x,y)
};

// Campo del SUJETO: para el kick "la silueta estalla" (modo NATURAL). Derivado de una saliencia [0..1]:
// máscara por percentil → signed distance transform (8SSEDT) → dirección del estallido = normal exterior.
struct SubjectField
{
    std::vector<uint8_t> mask;      // 1 = sujeto
    std::vector<float>   signedDist; // por celda, NORMALIZADO (unidades de ancho de grilla); negativo adentro
    std::vector<float>   burstXY;    // por celda: normal EXTERIOR unitaria (= ∇sd) — la dirección del estallido
};

class ImageField
{
public:
    // Muestrea la imagen RGBA8 a la grilla (gridW×gridH, igual que uploadImage) y computa los tres campos.
    // rgba nullptr/vacío → resultado neutro (centroide 0.5, pesos 1, flow 0): la fábrica y el fallback siguen
    // comportándose como siempre.
    static ImageFieldResult compute (const uint8_t* rgba, int imgW, int imgH, int gridW, int gridH);

    // Sujeto desde una saliencia por celda [0..1] (Vision o fallback): máscara = saliencia > percentil
    // `maskPercentile` (0..1, típ. 0.75), luego 8SSEDT con dirección. Saliencia vacía/plana → campo neutro
    // (sin máscara, sd=+1, burst=0) → el modo NATURAL degrada a flujo+hervor sin kick de silueta.
    static SubjectField computeSubject (const std::vector<float>& saliency01,
                                        int gridW, int gridH, float maskPercentile = 0.75f);

    // Máscara SUAVE del sujeto [0..1] derivada de la saliencia (fallback del CUTOUT cuando Vision no da
    // máscara: Windows, macOS <14 sin persona, o imagen sin sujeto claro). smoothstep alrededor del umbral
    // por percentil → bordes graduales, no recorte duro. Saliencia vacía/plana → vector vacío (sin cutout).
    static std::vector<float> maskFromSaliency (const std::vector<float>& saliency01,
                                                int gridW, int gridH, float maskPercentile = 0.70f);

    // Máscara por FONDO PLANO (bug de campo: los LOGOS/gráficos confunden a la IA fotográfica). Si el marco
    // exterior de la imagen es de color ~uniforme (logo sobre blanco/negro/color liso), la máscara es la
    // distancia de color a ese fondo — recorta gráficos PERFECTO. Vector vacío si el fondo no es plano
    // (foto normal → que decida Vision). Va PRIMERO en la cascada del cutout.
    static std::vector<float> maskFromFlatBackground (const uint8_t* rgba, int imgW, int imgH,
                                                      int gridW, int gridH);

    // DEPTH por celda [0..1] para el 3D de presentación (0.5 = plano neutro, >0.5 = hacia la cámara).
    // Con máscara: "almohada" dentro del sujeto (perfil sqrt de la distancia a la silueta — Teddy'99 /
    // Monster Mash: el sqrt evita la meseta chata) + detalle por luma; el fondo queda apenas atrás (parallax
    // de losa). Sin máscara: relieve suave por luma+saliencia (modo foto). Determinista; se computa UNA vez
    // al cargar. El modelo CoreML (cuando llegue) REEMPLAZA este mapa por SourceImage::depth.
    static std::vector<float> computeDepth (const uint8_t* rgba, int imgW, int imgH,
                                            const std::vector<float>& mask01,
                                            const std::vector<float>& saliency01,
                                            int gridW, int gridH);

    // Luma perceptual [0..1] de un píxel RGBA8 (Rec.709).
    static float lumaOf (const uint8_t* px) noexcept
    {
        return (0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2]) / 255.0f;
    }
};
}
