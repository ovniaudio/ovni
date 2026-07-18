// [supernova][thumbs][.gpu] — SMOKE del world browser (fix 3): el camino de generateThumbnails() — referencia
// ESTRUCTURADA (no la factory quemada), render al ASPECTO de la fuente (sin barras), exposición MODERADA +
// keyframe asentado → thumbnails NO-NEGROS y CLARAMENTE DISTINTOS entre mundos. Auto-skip sin GPU.
// Si SNV_THUMB_DUMP=<dir> está seteado, vuelca PNGs de una tanda diversa para revisión visual.
#include <catch2/catch_test_macros.hpp>
#include <juce_graphics/juce_graphics.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "params/ParamMapping.h"
#include "params/ParameterIDs.h"
#include "presets/PresetTypes.h"
#include "image/FactoryImage.h"

namespace { double meanLuma (const std::vector<uint8_t>& rgba)
{
    double s = 0; const size_t n = rgba.size() / 4;
    for (size_t i = 0; i < n; ++i) s += 0.2126 * rgba[i*4] + 0.7152 * rgba[i*4+1] + 0.0722 * rgba[i*4+2];
    return n ? s / (double) n : 0.0;
}
// Fracción de píxeles QUEMADOS (los 3 canales > 250) — el bug original era una bola blanca sobreexpuesta.
double blownFraction (const std::vector<uint8_t>& rgba)
{
    size_t blown = 0; const size_t n = rgba.size() / 4;
    for (size_t i = 0; i < n; ++i)
        if (rgba[i*4] > 250 && rgba[i*4+1] > 250 && rgba[i*4+2] > 250) ++blown;
    return n ? (double) blown / (double) n : 0.0;
}
std::vector<uint8_t> renderThumb (int idx, int TW, int TH, const supernova::SourceImage& src)
{
    std::vector<uint8_t> rgba ((size_t) TW * TH * 4);
    const auto& presets = ovni::presets::factoryPresets();
    supernova::MetalRenderer r;                    // fresco por mundo (patrón del browser)
    r.prepare (512, 512);
    r.uploadImage (src);
    const auto& preset = presets[(size_t) idx];
    supernova::ParticleParams pp = supernova::mapParticleParams ([&preset] (const char* id) -> float
    {
        for (const auto& p : preset.params) if (std::strcmp (p.id, id) == 0) return p.value;
        return supernova::params::id::paramDefault (id);
    });
    for (int f = 0; f <= 20; ++f)                  // = generateThumbnails: exposición 0.12, kick suave en f=6
    {
        supernova::AnalysisFrame af {};
        af.rms = 0.12f; af.energy = 0.12f;
        if (f == 6) af.onset = true;
        if (f >= 6 && f < 9) af.bass = 0.45f;
        r.renderOffscreen (af, pp, TW, TH, rgba.data());
    }
    return rgba;
} }

TEST_CASE ("thumbs: el camino del browser rinde mundos NO-negros, NO-quemados y distintos", "[supernova][thumbs][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable()) { WARN ("sin GPU — thumbnail smoke salteado"); SUCCEED(); return; }

    const auto& presets = ovni::presets::factoryPresets();
    REQUIRE (presets.size() >= 30);

    // Referencia estructurada (landscape 320×200) — render al MISMO aspecto → sin barras.
    auto refPx = supernova::makeThumbnailReference (320, 200);
    supernova::SourceImage src { refPx.data(), 320, 200, nullptr, nullptr };
    const int TH = 132, TW = 211;                  // 320/200 ≈ 1.6

    const char* dumpDir = std::getenv ("SNV_THUMB_DUMP");
    const int diverse[] = { 0, 5, 11, 13, 14, 17, 21, 23, 25, 29, 30, 34 };  // color/shape/figure spread
    std::vector<std::vector<uint8_t>> frames;
    double worstBlown = 0.0;

    for (int idx : diverse)   // render + dump TODOS primero (no abortar en un mundo brillante)
    {
        auto rgba = renderThumb (idx, TW, TH, src);
        const double lu = meanLuma (rgba), bl = blownFraction (rgba);
        worstBlown = std::max (worstBlown, bl);
        WARN ("world " << idx << " (" << presets[(size_t) idx].name << ") luma=" << lu << " blown=" << bl);
        CHECK (lu > 3.0);   // se ve
        frames.push_back (rgba);

        if (dumpDir != nullptr)
        {
            juce::Image im (juce::Image::ARGB, TW, TH, false);
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < TH; ++y) for (int x = 0; x < TW; ++x)
            { const uint8_t* s = &rgba[((size_t) y * TW + x) * 4]; bd.setPixelColour (x, y, juce::Colour::fromRGBA (s[0], s[1], s[2], 255)); }
            juce::File f (juce::String (dumpDir) + "/thumb_" + juce::String (idx).paddedLeft ('0', 2) + "_"
                          + juce::String (presets[(size_t) idx].name).replace (" ", "") + ".png");
            f.deleteFile(); juce::FileOutputStream os (f);
            juce::PNGImageFormat png; png.writeImageToStream (im, os);
        }
    }

    // El mundo típico NO es una bola blanca quemada (el bug original). Los explosivos (Supernova/Big Bang)
    // pueden ser brillantes por diseño, pero ni ésos deben quedar 100% blancos.
    WARN ("worst blown fraction = " << worstBlown);
    REQUIRE (worstBlown < 0.20);

    // Distintos entre sí: el promedio de diferencias entre pares supera un umbral claro (no white blobs).
    double sumDiff = 0; int pairs = 0;
    for (size_t a = 0; a < frames.size(); ++a)
        for (size_t b = a + 1; b < frames.size(); ++b)
        {
            double d = 0; const size_t n = frames[a].size();
            for (size_t i = 0; i < n; ++i) d += std::abs ((int) frames[a][i] - (int) frames[b][i]);
            sumDiff += d / (double) n; ++pairs;
        }
    const double avgDiff = pairs ? sumDiff / pairs : 0.0;
    WARN ("avg pairwise diff = " << avgDiff);
    REQUIRE (avgDiff > 5.0);   // los mundos se LEEN distintos (no la misma bola blanca)
}
