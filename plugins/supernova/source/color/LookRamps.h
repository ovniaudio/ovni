#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

// LookRamps — el banco de PALETAS del COLOR LAB (C++ puro, header-only, testeable sin GPU).
// Cada look es un gradient map multi-stop curado (la investigación: FLIR/Aerochrome/duotono/cianotipo/riso…).
// El "ramp compiler" hornea los stops a una rampa de 256 texels en DISPLAY-LINEAL (el map corre post-ACES,
// pre-encode-sRGB), interpolando en OKLab (Björn Ottosson) — la diferencia entre una rampa "cara" y una con
// zonas muertas grises. Determinista y byte-idéntico entre backends (crítico para el port D3D11: cos/pow
// inline en shader difiere en ULPs entre GPUs; el bake CPU no).
// paper = el color de "papel" del look (fondo BG, hallazgo riso): en looks editoriales es el extremo CLARO.
namespace supernova::color
{
struct LookStop { float pos; uint32_t rgb; };   // pos 0..1 creciente; rgb = sRGB 0xRRGGBB (como se diseñó)

struct LookRamp
{
    const char* name;       // nombre de fábrica (stepper / host)
    const LookStop* stops;
    int numStops;
    uint32_t paper;         // color de papel del look (BG) — sRGB
};

namespace detail
{
inline constexpr LookStop kOriginal[]    = { { 0.0f, 0x000000 }, { 1.0f, 0xffffff } };   // placeholder (off)
inline constexpr LookStop kTermica[]     = { { 0.00f, 0x02000d }, { 0.22f, 0x3b0f70 }, { 0.45f, 0xc43c4e },
                                             { 0.70f, 0xf8961e }, { 0.88f, 0xffd166 }, { 1.00f, 0xffffff } };
inline constexpr LookStop kInfrarrojo[]  = { { 0.00f, 0x0a0011 }, { 0.25f, 0x57003f }, { 0.50f, 0xd81b60 },
                                             { 0.78f, 0xff8fb3 }, { 1.00f, 0xfff6f4 } };
// Looks de IMPRENTA en dirección editorial (hallazgo del QA visual): el canvas (luma 0) es el PAPEL claro y
// las partículas brillantes son la TINTA — el "lienzo claro que cambia TODO" sale del gradient map SOLO.
inline constexpr LookStop kDuotono[]     = { { 0.00f, 0xf5ecd7 }, { 0.45f, 0x3e5c8a }, { 1.00f, 0x0d1b2a } };
inline constexpr LookStop kCianotipo[]   = { { 0.00f, 0xe8f1f2 }, { 0.35f, 0x2f7bb3 }, { 0.70f, 0x0b3d64 },
                                             { 1.00f, 0x06121f } };
inline constexpr LookStop kCromo[]       = { { 0.00f, 0x04060a }, { 0.35f, 0x2c3d52 }, { 0.58f, 0xc9d6e2 },
                                             { 0.72f, 0xf8fbff }, { 0.84f, 0x8fa3b8 }, { 1.00f, 0xffffff } };
inline constexpr LookStop kVaporwave[]   = { { 0.00f, 0x120458 }, { 0.35f, 0x7a04eb }, { 0.65f, 0xfe4164 },
                                             { 0.85f, 0xff9be2 }, { 1.00f, 0xc9fbff } };
inline constexpr LookStop kNeon[]        = { { 0.00f, 0x020208 }, { 0.40f, 0x0d4ba0 }, { 0.70f, 0x19d3da },
                                             { 1.00f, 0xeafffb } };
inline constexpr LookStop kMagma[]       = { { 0.00f, 0x000004 }, { 0.30f, 0x51127c }, { 0.60f, 0xb73779 },
                                             { 0.80f, 0xfc8961 }, { 1.00f, 0xfcfdbf } };
inline constexpr LookStop kFuegoFrio[]   = { { 0.00f, 0x00131a }, { 0.42f, 0x0e5a66 }, { 0.62f, 0xf9a03f },
                                             { 1.00f, 0xffe8c9 } };
inline constexpr LookStop kUltraviole[]  = { { 0.00f, 0x08010d }, { 0.45f, 0x4a0ca8 }, { 0.80f, 0xb16cff },
                                             { 1.00f, 0xf2e7ff } };
inline constexpr LookStop kSepia[]       = { { 0.00f, 0xf4e9d4 }, { 0.50f, 0x6f5b3e }, { 1.00f, 0x100c08 } };
inline constexpr LookStop kRiso[]        = { { 0.00f, 0xffd9a0 }, { 0.49f, 0xff5e29 }, { 0.51f, 0x4a3fc2 },
                                             { 1.00f, 0x10218b } };
inline constexpr LookStop kBosque[]      = { { 0.00f, 0x02100a }, { 0.45f, 0x0e5c3a }, { 0.75f, 0x7dd87d },
                                             { 1.00f, 0xeaffd7 } };
inline constexpr LookStop kPeltre[]      = { { 0.00f, 0x171021 }, { 0.50f, 0x8a5a83 }, { 1.00f, 0xffd6e0 } };
inline constexpr LookStop kOroNegro[]    = { { 0.00f, 0x050403 }, { 0.45f, 0x7a4a12 }, { 0.75f, 0xe8a83c },
                                             { 1.00f, 0xfff3d6 } };
inline constexpr LookStop kEspectro[]    = { { 0.00f, 0x27187e }, { 0.25f, 0x0f7fa8 }, { 0.50f, 0x35c26b },
                                             { 0.75f, 0xf2c04a }, { 1.00f, 0xff5a5f } };
}

// El banco (índice = choice PALETTE del APVTS; 0 = Original/off — el map no corre).
inline constexpr LookRamp kLooks[] = {
    { "Original",     detail::kOriginal,   2, 0x0d1016 },
    { "Thermal",      detail::kTermica,    6, 0x02000d },
    { "Infrared",     detail::kInfrarrojo, 5, 0x0a0011 },
    { "Duotone",      detail::kDuotono,    3, 0x0d1b2a },   // editorial: el papel ya lo pone el map (tinta p/BG)
    { "Cyanotype",    detail::kCianotipo,  4, 0x06121f },   // print: ídem
    { "Chrome",       detail::kCromo,      6, 0x04060a },
    { "Vaporwave",    detail::kVaporwave,  5, 0x120458 },
    { "Neon",         detail::kNeon,       4, 0x020208 },
    { "Magma",        detail::kMagma,      5, 0x000004 },
    { "Cold Fire",    detail::kFuegoFrio,  4, 0x00131a },
    { "Ultraviolet",  detail::kUltraviole, 4, 0x08010d },
    { "Sepia",        detail::kSepia,      3, 0x100c08 },   // platino: el papel ya lo pone el map
    { "Riso",         detail::kRiso,       4, 0x10218b },   // print: ídem
    { "Forest",       detail::kBosque,     4, 0x02100a },
    { "Pewter",       detail::kPeltre,     3, 0x171021 },
    { "Black Gold",   detail::kOroNegro,   4, 0x050403 },
    { "Spectrum",     detail::kEspectro,   5, 0x0d1016 },
};
inline constexpr int kNumLooks = (int) (sizeof (kLooks) / sizeof (kLooks[0]));
inline constexpr int kRampSize = 256;

namespace detail
{
inline float srgbToLin (float c) noexcept
{
    return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f);
}

struct F3 { float x, y, z; };

inline F3 rgbOf (uint32_t hex) noexcept   // sRGB hex → display-LINEAL
{
    return { srgbToLin (((hex >> 16) & 0xFF) / 255.0f),
             srgbToLin (((hex >> 8)  & 0xFF) / 255.0f),
             srgbToLin ((hex & 0xFF) / 255.0f) };
}

// linear sRGB ↔ OKLab (Björn Ottosson, dominio público) — el espacio donde los stops interpolan sin barro.
inline F3 linToOklab (F3 c) noexcept
{
    const float l = 0.4122214708f * c.x + 0.5363325363f * c.y + 0.0514459929f * c.z;
    const float m = 0.2119034982f * c.x + 0.6806995451f * c.y + 0.1073969566f * c.z;
    const float s = 0.0883024619f * c.x + 0.2817188376f * c.y + 0.6299787005f * c.z;
    const float l_ = std::cbrt (l), m_ = std::cbrt (m), s_ = std::cbrt (s);
    return { 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
             1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
             0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_ };
}

inline F3 oklabToLin (F3 c) noexcept
{
    const float l_ = c.x + 0.3963377774f * c.y + 0.2158037573f * c.z;
    const float m_ = c.x - 0.1055613458f * c.y - 0.0638541728f * c.z;
    const float s_ = c.x - 0.0894841775f * c.y - 1.2914855480f * c.z;
    const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    return { +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
             -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
             -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s };
}
}

// Hornea la rampa `lookIdx` → 256 texels RGBA float DISPLAY-LINEAL (A=1). Índice fuera de rango → identidad
// gris (inocua). Interpolación por tramos en OKLab. Determinista puro.
inline std::vector<float> bakeRamp (int lookIdx)
{
    std::vector<float> tex ((size_t) kRampSize * 4, 1.0f);
    const bool valid = lookIdx >= 0 && lookIdx < kNumLooks;
    const LookRamp& look = kLooks[valid ? lookIdx : 0];

    for (int i = 0; i < kRampSize; ++i)
    {
        const float t = (float) i / (float) (kRampSize - 1);
        const LookStop* a = &look.stops[0];
        const LookStop* b = &look.stops[look.numStops - 1];
        for (int s = 0; s < look.numStops - 1; ++s)
            if (t >= look.stops[s].pos && t <= look.stops[s + 1].pos) { a = &look.stops[s]; b = &look.stops[s + 1]; break; }
        const float span = std::max (1e-5f, b->pos - a->pos);
        const float k = std::clamp ((t - a->pos) / span, 0.0f, 1.0f);

        const auto la = detail::linToOklab (detail::rgbOf (a->rgb));
        const auto lb = detail::linToOklab (detail::rgbOf (b->rgb));
        const detail::F3 mixed { la.x + (lb.x - la.x) * k, la.y + (lb.y - la.y) * k, la.z + (lb.z - la.z) * k };
        auto rgb = detail::oklabToLin (mixed);

        tex[(size_t) i * 4 + 0] = std::clamp (rgb.x, 0.0f, 1.0f);
        tex[(size_t) i * 4 + 1] = std::clamp (rgb.y, 0.0f, 1.0f);
        tex[(size_t) i * 4 + 2] = std::clamp (rgb.z, 0.0f, 1.0f);
        tex[(size_t) i * 4 + 3] = 1.0f;
    }
    return tex;
}

// Color de PAPEL del look (BG), en display-lineal. Índice fuera de rango → papel neutro del look 0.
inline std::array<float, 3> paperOf (int lookIdx)
{
    const bool valid = lookIdx >= 0 && lookIdx < kNumLooks;
    const auto c = detail::rgbOf (kLooks[valid ? lookIdx : 0].paper);
    return { c.x, c.y, c.z };
}
}
