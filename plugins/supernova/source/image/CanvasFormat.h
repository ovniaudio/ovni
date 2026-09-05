#pragma once
// CanvasFormat — el FORMATO del lienzo (MEDIA SESSION PRO, 2026-09-02). Puro (sin JUCE ni GPU) → testeable.
//
// Hoy el visual tiene la forma de la VENTANA; el software pro (Resolume/TouchDesigner: composición; Premiere/
// FCP/CapCut: proyecto 16:9 · 9:16 · 1:1) trabaja con un lienzo de aspecto FIJO que el monitor muestra
// letterboxeado. AUTO sigue la orientación del PRIMER media (FCP: "set based on first clip"): una foto
// vertical → 9:16 (Reels/TikTok), cuadrada → 1:1, horizontal → 16:9; sin media → FREE (la ventana entera,
// como siempre). Es LAYOUT, no motor: la vista Metal recibe bounds con este aspecto y el FIT por target del
// renderer hace el resto → goldens intactos.
#include <cmath>
#include "video/ExportPreset.h"

namespace supernova
{
enum class CanvasFormat : int { Auto = 0, Free, Wide16x9, Tall9x16, Square1x1, Portrait4x5, Classic4x3, Count };

// Cómo entra la imagen en el lienzo: FIT = contain (letterbox, hoy) · FILL = cover (llena, recortada).
enum class FitMode : int { Fit = 0, Fill = 1 };

inline constexpr float kPortraitMaxAspect  = 0.9f;   // w/h < 0.9 → vertical
inline constexpr float kLandscapeMinAspect = 1.1f;   // w/h > 1.1 → horizontal; entre medio = cuadrado

inline CanvasFormat canvasFormatFromInt (int v) noexcept
{
    return (v >= 0 && v < (int) CanvasFormat::Count) ? (CanvasFormat) v : CanvasFormat::Auto;
}

// Aspecto (w/h) de un preset fijo; 0 = libre (Auto y Free no tienen aspecto propio).
inline float presetAspect (CanvasFormat f) noexcept
{
    switch (f)
    {
        case CanvasFormat::Wide16x9:    return 16.0f / 9.0f;
        case CanvasFormat::Tall9x16:    return 9.0f / 16.0f;
        case CanvasFormat::Square1x1:   return 1.0f;
        case CanvasFormat::Portrait4x5: return 4.0f / 5.0f;
        case CanvasFormat::Classic4x3:  return 4.0f / 3.0f;
        case CanvasFormat::Auto:
        case CanvasFormat::Free:
        case CanvasFormat::Count:       return 0.0f;
    }
    return 0.0f;
}

// AUTO: la orientación del media manda. sourceAspect <= 0 = desconocido/sin media → 0 (FREE).
inline float autoAspectFor (float sourceAspect) noexcept
{
    if (! (sourceAspect > 0.0f)) return 0.0f;
    if (sourceAspect < kPortraitMaxAspect)  return 9.0f / 16.0f;
    if (sourceAspect <= kLandscapeMinAspect) return 1.0f;
    return 16.0f / 9.0f;
}

// El aspecto RESUELTO del lienzo para un formato + el aspecto del media. 0 = libre (llena el área).
inline float canvasAspectFor (CanvasFormat f, float sourceAspect) noexcept
{
    if (f == CanvasFormat::Auto) return autoAspectFor (sourceAspect);
    if (f == CanvasFormat::Free) return 0.0f;
    return presetAspect (f);
}

inline const char* canvasFormatName (CanvasFormat f) noexcept
{
    switch (f)
    {
        case CanvasFormat::Auto:        return "AUTO";
        case CanvasFormat::Free:        return "FREE";
        case CanvasFormat::Wide16x9:    return "16:9";
        case CanvasFormat::Tall9x16:    return "9:16";
        case CanvasFormat::Square1x1:   return "1:1";
        case CanvasFormat::Portrait4x5: return "4:5";
        case CanvasFormat::Classic4x3:  return "4:3";
        case CanvasFormat::Count:       return "AUTO";
    }
    return "AUTO";
}

// Etiqueta corta de un aspecto resuelto (el chip de la barra): el preset más cercano, o FREE.
inline const char* aspectLabel (float aspect) noexcept
{
    if (! (aspect > 0.0f)) return "FREE";
    const CanvasFormat presets[] = { CanvasFormat::Wide16x9, CanvasFormat::Tall9x16, CanvasFormat::Square1x1,
                                     CanvasFormat::Portrait4x5, CanvasFormat::Classic4x3 };
    CanvasFormat best = CanvasFormat::Wide16x9;
    float bestD = 1.0e9f;
    for (auto p : presets)
    {
        const float d = std::fabs (std::log (aspect / presetAspect (p)));   // distancia en log (simétrica w/h)
        if (d < bestD) { bestD = d; best = p; }
    }
    return canvasFormatName (best);
}

// El rectángulo MÁS GRANDE con `aspect` centrado en el área (letterbox/pillarbox). aspect <= 0 → el área.
struct CanvasRect { int x = 0, y = 0, w = 0, h = 0; };

inline CanvasRect fitRect (int ax, int ay, int aw, int ah, float aspect) noexcept
{
    if (! (aspect > 0.0f) || aw <= 0 || ah <= 0) return { ax, ay, aw, ah };
    int w = aw, h = ah;
    if ((float) aw / (float) ah > aspect) w = (int) std::floor ((float) ah * aspect + 1.0e-4f);   // área más ancha → pillarbox
    else                                  h = (int) std::floor ((float) aw / aspect + 1.0e-4f);   // área más alta → letterbox
    w = w < 1 ? 1 : w;
    h = h < 1 ? 1 : h;
    return { ax + (aw - w) / 2, ay + (ah - h) / 2, w, h };
}

// El preset de EXPORT que coincide con el lienzo (va primero en el menú, marcado "canvas").
inline ExportFormat defaultExportFormat (float aspect) noexcept
{
    if (! (aspect > 0.0f)) return ExportFormat::HD1080;
    if (aspect < kPortraitMaxAspect)   return ExportFormat::Vertical1080;
    if (aspect <= kLandscapeMinAspect) return ExportFormat::Square1080;
    return ExportFormat::HD1080;
}
}
