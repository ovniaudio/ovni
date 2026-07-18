#pragma once
#include <cstdint>
#include <cmath>

// Compare perceptual de frames RGBA (§9.4, tolerancia — no bit-exacto). Header-only, sin deps. Metal→Metal usa
// mae<1 && maxDelta<8 (cuasi-exacto, margen por driver); cross-backend (D3D11 vs golden Metal, M5) usaría
// tolerancia perceptual mayor. Ignora el canal alpha (siempre 255 en la salida compuesta).
namespace supernova
{
struct ImageDiff { double mae = 0.0; int maxDelta = 0; };

inline ImageDiff compareRGBA (const uint8_t* a, const uint8_t* b, int w, int h) noexcept
{
    ImageDiff d;
    double sum = 0.0;
    const size_t n = (size_t) w * (size_t) h;
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 3; ++c)   // RGB (ignora alpha)
        {
            const int delta = std::abs ((int) a[i * 4 + c] - (int) b[i * 4 + c]);
            sum += delta;
            if (delta > d.maxDelta) d.maxDelta = delta;
        }
    d.mae = n > 0 ? sum / (double) (n * 3) : 0.0;
    return d;
}
}
