// [supernova][video] — VideoSource: decodifica un .mp4 en su hilo, publica frames RGBA y loopea. El asset
// tiny.mp4 (64×48, 6 frames, patrón testsrc en movimiento) vive en tests/assets. La parte GPU ([.gpu])
// verifica que updateColors recolorea el lattice sin crashear.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "video/VideoSource.h"
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"

namespace
{
juce::File assetFile()
{
    return juce::File (__FILE__).getParentDirectory().getChildFile ("assets/tiny.mp4");
}

// Espera hasta ~3 s a que el hilo del decoder publique un frame. Devuelve true y llena out* si llegó.
bool waitForFrame (supernova::VideoSource& v, std::vector<uint8_t>& out, int& w, int& h)
{
    for (int i = 0; i < 120; ++i)
    {
        if (v.latestFrame (out, w, h)) return true;
        juce::Thread::sleep (30);
    }
    return false;
}
}

TEST_CASE ("video: looksLikeVideo reconoce las extensiones", "[supernova][video]")
{
    REQUIRE (supernova::VideoSource::looksLikeVideo (juce::File ("/tmp/a.mp4")));
    REQUIRE (supernova::VideoSource::looksLikeVideo (juce::File ("/tmp/a.MOV")));
    REQUIRE (! supernova::VideoSource::looksLikeVideo (juce::File ("/tmp/a.png")));
}

TEST_CASE ("video: abre, decodifica frames del tamaño correcto y loopea", "[supernova][video]")
{
    const auto asset = assetFile();
    if (! asset.existsAsFile()) { SUCCEED ("asset tiny.mp4 ausente — test saltado"); return; }

    supernova::VideoSource v;
    REQUIRE (v.open (asset));
    REQUIRE (v.isOpen());
    REQUIRE (v.aspect() == Catch::Approx (64.0f / 48.0f).margin (0.02f));

    std::vector<uint8_t> a, b;
    int aw = 0, ah = 0, bw = 0, bh = 0;
    REQUIRE (waitForFrame (v, a, aw, ah));
    REQUIRE (aw == 64);
    REQUIRE (ah == 48);
    REQUIRE (a.size() == (size_t) aw * ah * 4);

    // Un frame POSTERIOR (el patrón se mueve → difiere). Y sigue entregando frames pasada la duración (loop).
    bool gotDifferent = false;
    for (int i = 0; i < 40 && ! gotDifferent; ++i)
        if (waitForFrame (v, b, bw, bh) && b != a) gotDifferent = true;
    REQUIRE (gotDifferent);

    v.close();
    REQUIRE (! v.isOpen());
}

TEST_CASE ("video: rotate() intercambia el aspecto (foto vertical de celular)", "[supernova][video]")
{
    const auto asset = assetFile();
    if (! asset.existsAsFile()) { SUCCEED ("asset ausente"); return; }

    supernova::VideoSource v;
    REQUIRE (v.open (asset));
    const float a0 = v.aspect();
    v.rotate();
    REQUIRE (v.aspect() == Catch::Approx (1.0f / a0).margin (0.02f));   // 90° → aspecto invertido
    v.close();
}

TEST_CASE ("video: updateColors recolorea el lattice sin crashear", "[supernova][video][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable()) { SUCCEED ("sin GPU Metal — saltado"); return; }
    const auto asset = assetFile();
    if (! asset.existsAsFile()) { SUCCEED ("asset ausente"); return; }

    r.prepare (512, 512);
    auto factory = supernova::makeFactoryImage (512, 512);
    r.uploadImage ({ factory.data(), 512, 512 });

    constexpr int kPx = 128;
    std::vector<uint8_t> base ((size_t) kPx * kPx * 4), after (base.size());
    supernova::ParticleParams pp;
    supernova::AnalysisFrame af;
    for (int f = 0; f < 8; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, base.data()));

    supernova::VideoSource v;
    REQUIRE (v.open (asset));
    std::vector<uint8_t> frame; int w = 0, h = 0;
    REQUIRE (waitForFrame (v, frame, w, h));
    r.updateColors (frame.data(), w, h);                       // camino rápido: solo colores
    for (int f = 0; f < 8; ++f) REQUIRE (r.renderOffscreen (af, pp, kPx, kPx, after.data()));

    const bool changed = (after != base);                      // el video pintó el lattice
    REQUIRE (changed);
    v.close();
}
