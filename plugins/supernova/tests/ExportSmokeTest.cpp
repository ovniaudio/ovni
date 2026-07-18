// [supernova][exportsmoke][.gpu] — SMOKE del pipeline de EXPORT end-to-end: renderer offscreen + VideoExporter
// escriben un MP4 REAL. Auto-skip sin GPU. ffprobe (afuera) verifica dims/duración/codec; acá: se produce +
// pesa lo esperado. Prueba VideoExporter.begin/pushFrame/finish con frames reales (el camino del botón EXPORT).
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <vector>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "render/RenderScenarios.h"
#include "image/FactoryImage.h"
#include "video/VideoExporter.h"
#include "video/ExportPreset.h"

TEST_CASE ("exportsmoke: renderer + VideoExporter escriben un MP4 reproducible", "[supernova][exportsmoke][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable()) { WARN ("sin GPU — export smoke salteado"); SUCCEED(); return; }
    r.prepare (512, 512);
    auto factory = supernova::makeFactoryImage (512, 512);
    r.uploadImage ({ factory.data(), 512, 512 });

    const int W = 640, H = 360, FPS = 30, FRAMES = 60;   // 2 s
    supernova::VideoExporter ex;
    supernova::VideoExporter::Config cfg;
    cfg.width = W; cfg.height = H; cfg.fps = FPS;
    cfg.bitsPerSecond = supernova::recommendedBitrate ({ W, H }, FPS);
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snv-export-smoke.mp4");
    cfg.path = out.getFullPathName().toStdString();

    REQUIRE (ex.begin (cfg));
    REQUIRE (ex.isActive());

    supernova::ParticleParams pp;
    std::vector<uint8_t> rgba ((size_t) W * H * 4);
    for (int f = 0; f < FRAMES; ++f)
    {
        auto af = supernova::scenarioFrame ("kick", f, FRAMES);
        pp.explode = 0.0f;
        r.renderOffscreen (af, pp, W, H, rgba.data());
        REQUIRE (ex.pushFrame (rgba.data(), W, H));
    }
    REQUIRE (ex.finish());
    REQUIRE (out.existsAsFile());
    REQUIRE (out.getSize() > 10000);   // 2 s de H.264 con contenido pesa >10 KB
}

TEST_CASE ("exportsound: el MP4 sale CON pista de audio AAC (seno 440Hz muxeado por cuadro)",
           "[supernova][exportsound][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable()) { WARN ("sin GPU — export sound smoke salteado"); SUCCEED(); return; }
    r.prepare (512, 512);
    auto factory = supernova::makeFactoryImage (512, 512);
    r.uploadImage ({ factory.data(), 512, 512 });

    const int W = 640, H = 360, FPS = 30, FRAMES = 60, SR = 48000;   // 2 s
    supernova::VideoExporter ex;
    supernova::VideoExporter::Config cfg;
    cfg.width = W; cfg.height = H; cfg.fps = FPS;
    cfg.bitsPerSecond = supernova::recommendedBitrate ({ W, H }, FPS);
    cfg.withAudio = true; cfg.audioSampleRate = SR; cfg.audioChannels = 2;
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snv-export-sound.mp4");
    cfg.path = out.getFullPathName().toStdString();

    REQUIRE (ex.begin (cfg));

    supernova::ParticleParams pp;
    std::vector<uint8_t> rgba ((size_t) W * H * 4);
    std::vector<float> chunk; double phase = 0.0, acc = 0.0;
    const double spf = (double) SR / FPS, w0 = 2.0 * juce::MathConstants<double>::pi * 440.0 / SR;
    for (int f = 0; f < FRAMES; ++f)
    {
        auto af = supernova::scenarioFrame ("kick", f, FRAMES);
        pp.explode = 0.0f;
        r.renderOffscreen (af, pp, W, H, rgba.data());
        // AUDIO PRIMERO: el muxer intercala pidiendo que el audio CUBRA el PTS del video que llega;
        // si empatan (audio hasta t, video en t) el input de video se traba en isReady (visto en f=35).
        acc += spf; const int nA = (int) acc; acc -= nA;             // mismo patrón que exportVideo
        chunk.resize ((size_t) nA * 2);
        for (int k = 0; k < nA; ++k)
        { const float s = 0.4f * (float) std::sin (phase); phase += w0;
          chunk[(size_t) k * 2] = s; chunk[(size_t) k * 2 + 1] = s; }
        REQUIRE (ex.pushAudio (chunk.data(), nA));
        const bool pf = ex.pushFrame (rgba.data(), W, H);
        INFO ("pushFrame err @f=" << f << ": " << ex.lastError());
        REQUIRE (pf);
    }
    const bool fin = ex.finish();
    INFO ("finish error: " << ex.lastError());
    REQUIRE (fin);
    REQUIRE (out.existsAsFile());
    REQUIRE (out.getSize() > 20000);   // video + AAC: más pesado que el mudo
}

// Un campo RGBA de color plano con una leve rampa de luma (para que se formen partículas).
static std::vector<uint8_t> solidField (int w, int h, uint8_t rr, uint8_t gg, uint8_t bb)
{
    std::vector<uint8_t> px ((size_t) w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float ramp = 0.55f + 0.45f * (float) x / (float) w;      // luma varía → hay estructura
            const size_t i = ((size_t) y * w + x) * 4;
            px[i + 0] = (uint8_t) juce::jlimit (0, 255, (int) (rr * ramp));
            px[i + 1] = (uint8_t) juce::jlimit (0, 255, (int) (gg * ramp));
            px[i + 2] = (uint8_t) juce::jlimit (0, 255, (int) (bb * ramp));
            px[i + 3] = 255;
        }
    return px;
}

static double meanAbsDiff (const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += std::abs ((int) a[i] - (int) b[i]);
    return n ? acc / (double) n : 0.0;
}

// [exportsmoke] — el CICLADO de fotos del export (fix): con ≥2 fotos, el clip debe CAMBIAR de contenido en el
// borde de cada foto (antes exportaba una sola imagen el clip entero). Sube dos imágenes distintas por el
// mismo camino que exportVideo (framesPerPhoto/photoSlotForFrame) y escribe un MP4 REAL → ffprobe (afuera)
// verifica duración/frames y el ojo de Joaquín ve las fotos alternarse. Auto-skip sin GPU.
TEST_CASE ("exportsmoke: la secuencia de fotos CICLA en el MP4 (dos fotos, dos ventanas)",
           "[supernova][exportsmoke][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable()) { WARN ("sin GPU — export cycling smoke salteado"); SUCCEED(); return; }
    r.prepare (512, 512);

    const auto warm = solidField (512, 512, 255,  96,  40);   // foto A · fuego
    const auto cool = solidField (512, 512,  40, 120, 255);   // foto B · hielo

    const int W = 640, H = 360, FPS = 30;
    const double interval = 1.0;                              // 1 s por foto
    const int perPhoto = supernova::framesPerPhoto (interval, FPS);   // 30
    const int FRAMES = perPhoto * 2;                          // 2 fotos × 1 s = 2 s

    supernova::VideoExporter ex;
    supernova::VideoExporter::Config cfg;
    cfg.width = W; cfg.height = H; cfg.fps = FPS;
    cfg.bitsPerSecond = supernova::recommendedBitrate ({ W, H }, FPS);
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snv-export-cycle.mp4");
    cfg.path = out.getFullPathName().toStdString();
    REQUIRE (ex.begin (cfg));

    supernova::ParticleParams pp;
    pp.explode = 0.0f;
    std::vector<uint8_t> rgba ((size_t) W * H * 4);
    std::vector<uint8_t> frameA, frameB;   // un frame de cada ventana de foto
    int curSlot = -1;

    for (int f = 0; f < FRAMES; ++f)
    {
        const int slot = supernova::photoSlotForFrame (f, perPhoto, 2);
        if (slot != curSlot)
        {
            curSlot = slot;
            r.uploadImage ({ (slot == 0 ? warm : cool).data(), 512, 512 });
        }
        auto af = supernova::scenarioFrame ("idle", f, FRAMES);
        r.renderOffscreen (af, pp, W, H, rgba.data());
        REQUIRE (ex.pushFrame (rgba.data(), W, H));

        if (f == perPhoto / 2)     frameA = rgba;   // centro de la foto A
        if (f == perPhoto + perPhoto / 2) frameB = rgba;   // centro de la foto B
    }
    REQUIRE (ex.finish());
    REQUIRE (out.existsAsFile());
    REQUIRE (out.getSize() > 10000);

    // La prueba del ciclado: el contenido de la ventana A difiere claramente del de la ventana B.
    REQUIRE (! frameA.empty());
    REQUIRE (! frameB.empty());
    REQUIRE (meanAbsDiff (frameA, frameB) > 6.0);   // fuego vs hielo → diferencia de color neta
}
