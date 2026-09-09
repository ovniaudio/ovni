// [supernova][live][exportparitylive][.] — PARIDAD REAL: la VENTANA contra el CUADRO DEL CLIP.
//
// El resto de [exportparity] mide invariantes por dentro (offscreen contra offscreen). Esto mide lo que se
// reclamó en el prompt 45: que el MP4 se vea como la pantalla. Abre una ventana DE VERDAD (la misma
// SupernovaView, la misma CAMetalLayer, el mismo VBlank que la app), la deja corriendo con un beat de
// 126 BPM sobre la foto del kit de prensa, la CONGELA para que el shell la capture, y después rinde el
// cuadro 0 del clip por el MISMO camino que el botón EXPORT: renderer fresco + invariancia al tamaño +
// fases heredadas + calentamiento + el anillo de análisis que la ventana acaba de producir.
//
// Oculto por default (tag [.]). Se corre a mano; ver mision-control/revisiones/45-*.md:
//     SNV_PARITY_OUT=/tmp/45 OvniSupernovaTests "[exportparitylive]"
// y, mientras imprime "CONGELADO", desde el shell (el permiso de grabación lo tiene la terminal, no este
// binario):  screencapture -x -l <window id de winshot.swift> /tmp/45/ventana.png
//
// La ventana se abre a 960x540 lógicos: en Retina el drawable es 1920x1080 EXACTO, el mismo tamaño que el
// cuadro del clip → se comparan píxel a píxel, sin reescalar.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdio>
#include <vector>
#include "ui/SupernovaView.h"
#include "image/DecodedImageCache.h"
#include "image/ImageLoader.h"
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "analysis/TripleBuffer.h"
#include "video/ExportPreset.h"
#include "video/ExportWarmup.h"
#include "video/ExportOnsets.h"

namespace
{
constexpr double kBpm = 126.0;
constexpr int    kRingHz = 30;      // kAnalysisRingHz: la tasa a la que el editor guarda el análisis
constexpr int    kFps    = 60;

// Los knobs de la sesión del 7-sep: mundo Parallax con INTENSITY 60, FORM 0 y FIGURE Imagen.
supernova::ParticleParams parallaxLive()
{
    supernova::ParticleParams pp;
    pp.intensity    = 0.60f;                     // el valor con el que salieron las tomas descartadas
    pp.chaos        = 0.14f;
    pp.particleSize = 0.5f + 0.38f * 3.5f;
    pp.glow         = 0.56f;
    pp.curlScale    = 0.30f + 0.30f * 2.40f;
    pp.homeStrength = 0.20f + 0.70f * 2.40f;
    pp.momentum     = 0.80f + 0.42f * 0.19f;
    pp.radialGain   = 0.20f + 0.45f * 2.00f;
    pp.jitterGain   = 0.18f * 0.32f;
    pp.breatheGain  = 0.45f * 0.56f;
    pp.depthAmt     = 0.88f;                     // DEPTH 88
    pp.rotYRad      = -16.0f * 0.01745329f;      // ROT Y −16°
    pp.rotXRad      =  -6.0f * 0.01745329f;      // ROT X −6°
    pp.orbitRate    =  12.0f / 100.0f * 0.5235988f;   // ORBIT 12
    pp.densityAmt   = 0.72f;
    pp.satAmt       = 55.0f / 50.0f;
    pp.formAmt      = 0.0f;                      // FORM 0 · FIGURE Imagen: manda la foto
    pp.formMode     = 0;
    return pp;
}

// El beat sintético que ya usa el smoke del fundido, a 126 BPM.
struct FakeBeat
{
    supernova::TripleBuffer<supernova::AnalysisFrame> buf;
    unsigned onsets = 0;
    double   lastBeat = 0.0;
    void publish (double nowMs, double beatMs)
    {
        supernova::AnalysisFrame af;
        af.rms = af.energy = 0.45f;
        af.treble = 0.25f;
        if (nowMs - lastBeat >= beatMs) { lastBeat = nowMs; ++onsets; af.onset = true; af.bass = 0.9f; }
        else                              af.bass = 0.15f;
        af.onsetCount = onsets;
        buf.writeSlot() = af;
        buf.publish();
    }
};

bool writePng (const std::vector<uint8_t>& rgba, int w, int h, const juce::File& of)
{
    juce::Image image (juce::Image::ARGB, w, h, false);
    {
        juce::Image::BitmapData bd (image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const uint8_t* p = &rgba[((size_t) y * w + x) * 4];
                bd.setPixelColour (x, y, juce::Colour::fromRGBA (p[0], p[1], p[2], p[3]));
            }
    }
    of.deleteFile();
    juce::FileOutputStream os (of);
    if (! os.openedOk()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (image, os);
}
}

TEST_CASE ("live: la VENTANA y el cuadro 0 del clip, con la misma foto y el mismo beat",
           "[supernova][live][exportparitylive][.]")
{
    // Sin SNV_PARITY_PHOTO no hay foto que comparar: este smoke es MANUAL (tag [.]) y la foto la pone
    // quien lo corre. No se hornea una ruta de la máquina de nadie.
    const juce::String photoPath = juce::SystemStats::getEnvironmentVariable ("SNV_PARITY_PHOTO", "");
    if (photoPath.isEmpty())
    {
        WARN ("SNV_PARITY_PHOTO sin definir: pasá la ruta de una foto para correr este smoke.");
        return;
    }
    const juce::File outDir { juce::SystemStats::getEnvironmentVariable ("SNV_PARITY_OUT", "/tmp/45-paridad") };
    const int holdMs = juce::SystemStats::getEnvironmentVariable ("SNV_PARITY_HOLD_MS", "9000").getIntValue();
    outDir.createDirectory();

    const juce::File photo { photoPath };
    if (! photo.existsAsFile()) { WARN ("falta la foto de la paridad — smoke salteado"); SUCCEED(); return; }

    // La foto entra por el MISMO decode que el editor (Vision + máscara), que es el que el export copia.
    const auto base = supernova::decodeBaseImage (photo);
    if (! base.valid()) { WARN ("la foto no decodifica — smoke salteado"); SUCCEED(); return; }

    supernova::SupernovaView view;
    if (! view.gpuAvailable()) { WARN ("sin GPU — smoke salteado"); SUCCEED(); return; }
    view.setOpaque (true);
    view.setBounds (60, 60, 960, 540);              // Retina → drawable 1920x1080 exacto
    view.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    view.setVisible (true);

    FakeBeat beat;
    view.setAnalysisSource (&beat.buf);
    const auto pp = parallaxLive();
    view.setParams (pp);

    const double beatMs = 60000.0 / kBpm;
    auto pump = [&] (int ms)
    {
        beat.publish (juce::Time::getMillisecondCounterHiRes(), beatMs);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    };
    for (int i = 0; i < 60; ++i) pump (16);         // la ventana no existe en el WindowServer hasta bombear

    view.loadImage (std::make_shared<supernova::LoadedImage> (base), 0.0);

    // 5 s de música, guardando el análisis a 30 Hz — exactamente lo que el editor mete en su anillo y lo que
    // el export reproduce después.
    std::vector<supernova::AnalysisFrame> ring;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    double nextSample = t0;
    while (juce::Time::getMillisecondCounterHiRes() - t0 < 5000.0)
    {
        pump (4);
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now >= nextSample) { ring.push_back (view.lastFrame()); nextSample += 1000.0 / kRingHz; }
    }
    REQUIRE (ring.size() > (size_t) kRingHz);

    // CONGELADO: sin bombear no hay más render() y la última imagen presentada se queda en pantalla. El
    // shell captura ahora (screencapture -x -l <wid>): la latencia no corre la fase.
    const auto phases = view.viewPhases();
    std::fprintf (stderr, "[paridad] CONGELADO — %d ms para capturar la ventana con "
                          "screencapture -x -l <wid>  (fases rot=%.3f orbit=%.3f hue=%.3f, anillo %d frames)\n",
                  holdMs, phases.rotate, phases.orbit, phases.hue, (int) ring.size());
    std::fflush (stderr);
    juce::Thread::sleep (holdMs);

    // ---- EL CUADRO DEL CLIP, por el mismo camino que el botón EXPORT ----
    // SNV_PARITY_LEGACY=1 rinde el cuadro con la RECETA VIEJA del export (sin invariancia al tamaño, sin
    // heredar las fases, sin calentamiento, con el decode pelado y sin máscara, y con el golpe entrando en
    // los dos cuadros que comparten frame de análisis) — el "antes" del prompt 45, para medir contra el
    // mismo cuadro de ventana.
    const bool legacy = juce::SystemStats::getEnvironmentVariable ("SNV_PARITY_LEGACY", "0") == "1";
    const int W = 1920, H = 1080;
    supernova::MetalRenderer r;
    r.prepare (supernova::kParticleGrid, supernova::kParticleGrid);
    if (! legacy)
    {
        r.setOffscreenSizeInvariance (true);        // como en pantalla
        r.setViewPhases (phases);                   // el encuadre que tiene la ventana
        r.uploadImage (base.source (true));         // la máscara viaja siempre
    }
    else
    {
        auto pelada = supernova::ImageLoader::fromFile (photo);   // sin Vision, sin máscara
        r.uploadImage (pelada.source (false));
    }

    const int loopV = supernova::exportLoopFrames ((int) ring.size(), kFps, kRingHz);
    supernova::ExportOnsetState onsetState;
    std::vector<uint8_t> rgba ((size_t) W * H * 4);
    auto step = [&] (int videoFrame)
    {
        const int aIdx = supernova::analysisIndexForFrame (videoFrame, kFps, kRingHz, (int) ring.size());
        auto af = ring[(size_t) aIdx];
        if (! legacy && ! supernova::exportOnsetStep (onsetState, aIdx).keepOnset) af.onset = false;
        REQUIRE (r.renderOffscreen (af, pp, W, H, rgba.data()));
    };
    if (! legacy)
        for (int k = 0; k < supernova::kExportWarmupFrames; ++k)
            step (supernova::exportWarmupVideoFrame (k, supernova::kExportWarmupFrames, loopV));
    step (0);                                        // el CUADRO 0 del clip

    const juce::File clipPng = outDir.getChildFile (legacy ? "clip-cuadro0-legacy.png" : "clip-cuadro0.png");
    REQUIRE (writePng (rgba, W, H, clipPng));
    std::fprintf (stderr, "[paridad] cuadro 0 del clip → %s (%dx%d)\n",
                  clipPng.getFullPathName().toRawUTF8(), W, H);
    std::fflush (stderr);

    view.removeFromDesktop();
    SUCCEED();
}
