// [supernova][ramps] — el ramp compiler del COLOR LAB (puro, sin GPU): el bake CPU es la fuente única de la
// textura de paleta → estas propiedades son las que mantienen los goldens estables y el port D3D11 byte-igual.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "color/LookRamps.h"

using namespace supernova::color;

TEST_CASE ("ramps: el banco tiene los 17 looks y todos con stops sanos (pos creciente 0→1)", "[supernova][ramps]")
{
    REQUIRE (kNumLooks == 17);
    for (int i = 0; i < kNumLooks; ++i)
    {
        const auto& l = kLooks[i];
        REQUIRE (l.numStops >= 2);
        REQUIRE (l.stops[0].pos == 0.0f);
        REQUIRE (l.stops[l.numStops - 1].pos == 1.0f);
        for (int s = 1; s < l.numStops; ++s)
            REQUIRE (l.stops[s].pos > l.stops[s - 1].pos);
    }
}

TEST_CASE ("ramps: el bake es determinista, acotado [0,1] y clava los extremos de los stops", "[supernova][ramps]")
{
    for (int i = 1; i < kNumLooks; ++i)
    {
        const auto a = bakeRamp (i);
        const auto b = bakeRamp (i);
        REQUIRE (a.size() == (size_t) kRampSize * 4);
        REQUIRE (a == b);                              // byte-determinista (goldens + paridad D3D11)
        for (float v : a) { REQUIRE (v >= 0.0f); REQUIRE (v <= 1.0f); }

        // El texel 0 y el 255 reproducen los stops extremos (ida y vuelta OKLab sin deriva perceptible).
        const auto first = supernova::color::detail::rgbOf (kLooks[i].stops[0].rgb);
        const auto last  = supernova::color::detail::rgbOf (kLooks[i].stops[kLooks[i].numStops - 1].rgb);
        REQUIRE (std::abs (a[0] - first.x) < 0.02f);
        REQUIRE (std::abs (a[1] - first.y) < 0.02f);
        REQUIRE (std::abs (a[2] - first.z) < 0.02f);
        const size_t e = (size_t) (kRampSize - 1) * 4;
        REQUIRE (std::abs (a[e + 0] - last.x) < 0.02f);
        REQUIRE (std::abs (a[e + 1] - last.y) < 0.02f);
        REQUIRE (std::abs (a[e + 2] - last.z) < 0.02f);
    }
}

TEST_CASE ("ramps: OKLab round-trip (lineal→Lab→lineal) no degrada el color", "[supernova][ramps]")
{
    using namespace supernova::color::detail;
    const F3 samples[] = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.5f, 0.1f, 0.8f },
                           { 0.02f, 0.9f, 0.3f }, { 0.7f, 0.7f, 0.05f } };
    for (const auto& c : samples)
    {
        const auto back = oklabToLin (linToOklab (c));
        REQUIRE (std::abs (back.x - c.x) < 1e-3f);
        REQUIRE (std::abs (back.y - c.y) < 1e-3f);
        REQUIRE (std::abs (back.z - c.z) < 1e-3f);
    }
}

TEST_CASE ("ramps: índice fuera de rango degrada inocuo (identidad del look 0 + papel neutro)", "[supernova][ramps]")
{
    const auto oob = bakeRamp (999);
    const auto z   = bakeRamp (0);
    REQUIRE (oob == z);
    const auto p = paperOf (-3);
    for (float v : p) { REQUIRE (v >= 0.0f); REQUIRE (v <= 1.0f); }
}
