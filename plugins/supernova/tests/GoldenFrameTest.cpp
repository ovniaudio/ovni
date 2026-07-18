// [supernova][golden] — regresión perceptual contra los golden frames de referencia (Metal = referencia dorada,
// §9.4). Tag [.gpu]: se auto-saltea si no hay GPU Metal (VM de CI sin Metal → CI verde, filosofía GPU-opcional).
// Reproduce EXACTO el camino del render tool (mismo escenario, grid 512, dt fijo, MetalRenderer fresco) y
// compara el keyframe contra plugins/supernova/tests/golden/<escenario>/frame_%04d.png.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "render/metal/MetalRenderer.h"
#include "render/IRenderer.h"
#include "render/ParticleParams.h"
#include "render/RenderScenarios.h"
#include "image/FactoryImage.h"
#include "GoldenCompare.h"

namespace
{
constexpr int kGrid = 512, kW = 512, kH = 512, kFrames = 60;

// Renderiza el escenario hasta `keyframe` inclusive con un renderer FRESCO (determinista) y devuelve el RGBA.
std::vector<uint8_t> renderScenarioTo (const char* scenario, int keyframe)
{
    supernova::MetalRenderer r;
    r.prepare (kGrid, kGrid);
    auto factory = supernova::makeFactoryImage (kGrid, kGrid);
    r.uploadImage ({ factory.data(), kGrid, kGrid });

    supernova::ParticleParams pp;
    std::vector<uint8_t> rgba ((size_t) kW * kH * 4);
    for (int f = 0; f <= keyframe; ++f)
    {
        auto af = supernova::scenarioFrame (scenario, f, kFrames);
        pp.explode = supernova::scenarioExplode (scenario, f, kFrames) ? 1.0f : 0.0f;
        r.renderOffscreen (af, pp, kW, kH, rgba.data());
    }
    return rgba;
}

juce::File goldenDir()
{
    // Ubicación en el árbol de fuentes (relativa a este .cpp): plugins/supernova/tests/golden/
    return juce::File (__FILE__).getParentDirectory().getChildFile ("golden");
}

std::vector<uint8_t> loadGolden (const juce::File& png, int& w, int& h)
{
    std::vector<uint8_t> out;
    const juce::Image img = juce::ImageFileFormat::loadFrom (png);
    if (img.isNull()) return out;
    w = img.getWidth(); h = img.getHeight();
    out.resize ((size_t) w * h * 4);
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const juce::Colour c = bd.getPixelColour (x, y);
            uint8_t* p = &out[((size_t) y * w + x) * 4];
            p[0] = c.getRed(); p[1] = c.getGreen(); p[2] = c.getBlue(); p[3] = c.getAlpha();
        }
    return out;
}

struct Case { const char* scenario; int keyframe; };
}

TEST_CASE ("golden: frames Metal coinciden con la referencia dorada", "[supernova][golden][.gpu]")
{
    supernova::MetalRenderer probe;
    if (! probe.isAvailable())
    {
        WARN ("sin GPU Metal — golden test salteado (GPU-opcional)");
        SUCCEED();
        return;
    }

    const Case cases[] = {
        { "idle", 40 }, { "kick", 20 }, { "kick", 40 },
        { "sustained-bass", 50 }, { "treble-shimmer", 40 },
        { "rms-breathe", 30 }, { "explode-param", 24 },
    };

    int checked = 0;
    for (const auto& c : cases)
    {
        const juce::File png = goldenDir().getChildFile (c.scenario)
                                          .getChildFile (juce::String::formatted ("frame_%04d.png", c.keyframe));
        if (! png.existsAsFile())
        {
            WARN (("golden ausente (correr tools/supernova-goldens.sh): " + png.getFullPathName()).toStdString());
            continue;
        }
        int gw = 0, gh = 0;
        const auto golden = loadGolden (png, gw, gh);
        REQUIRE (gw == kW);
        REQUIRE (gh == kH);

        const auto actual = renderScenarioTo (c.scenario, c.keyframe);
        const auto diff = supernova::compareRGBA (actual.data(), golden.data(), kW, kH);

        INFO ("escenario=" << c.scenario << " keyframe=" << c.keyframe
                           << " mae=" << diff.mae << " maxDelta=" << diff.maxDelta);
        REQUIRE (diff.mae < 1.5);        // Metal→Metal: cuasi-exacto (margen por driver/ACES)
        REQUIRE (diff.maxDelta < 16);
        ++checked;
    }

    if (checked == 0)
        WARN ("ningún golden presente — generar con tools/supernova-goldens.sh");
}
