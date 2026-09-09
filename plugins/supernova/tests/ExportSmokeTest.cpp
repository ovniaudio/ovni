// [supernova][exportsmoke][.gpu] — SMOKE del pipeline de EXPORT end-to-end: renderer offscreen + VideoExporter
// escriben un MP4 REAL. Auto-skip sin GPU. ffprobe (afuera) verifica dims/duración/codec; acá: se produce +
// pesa lo esperado. Prueba VideoExporter.begin/pushFrame/finish con frames reales (el camino del botón EXPORT).
#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <vector>
#include <set>
#include <algorithm>
#include "render/metal/MetalRenderer.h"
#include "render/ParticleParams.h"
#include "render/RenderScenarios.h"
#include "analysis/AnalysisFrame.h"
#include "image/FactoryImage.h"
#include "video/VideoExporter.h"
#include "video/ExportPreset.h"
#include "image/PhotoSequence.h"
#include "render/Dissolve.h"
#include "video/ExportAudioLoop.h"
#include <catch2/catch_approx.hpp>

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

// [exportsmoke] — MEDIA SESSION PRO ronda 2 + 2b: el export sigue el RELOJ de la secuencia Y reproduce el
// análisis a su tasa REAL, con sonido. Con el reloj en BEATS (4 beats a 120 BPM = 2 s por foto) un clip de
// 4 s cambia de foto UNA vez; con el reloj viejo (SECONDS, intervalo por defecto de 8 s) no cambiaba nunca.
// Y el anillo de análisis corre a 30 Hz: un clip a 60 fps consume DOS cuadros por frame de análisis (antes
// uno, o sea al doble de velocidad) y el audio muxeado cubre el mismo tramo. Camino idéntico al de
// SupernovaEditor::exportVideo; escribe un MP4 real. Auto-skip sin GPU.
TEST_CASE ("exportsmoke: en BEATS, con sonido y el análisis a su tasa real (30 Hz en un clip de 60 fps)",
           "[supernova][exportsmoke][.gpu]")
{
    supernova::MetalRenderer r;
    if (! r.isAvailable()) { WARN ("sin GPU — export beats smoke salteado"); SUCCEED(); return; }
    r.prepare (512, 512);

    const auto warm = solidField (512, 512, 255,  96,  40);   // foto A · fuego
    const auto cool = solidField (512, 512,  40, 120, 255);   // foto B · hielo

    const int W = 640, H = 360, FPS = 60, SECONDS = 4, SR = 48000;
    const int FRAMES = SECONDS * FPS;                         // 240 cuadros
    const int RING_HZ = 30;                                   // = SupernovaEditor::kAnalysisRingHz
    const double BPM = 120.0;
    // Leído de la constante, no copiado: el 12,0 de antes se quedó viejo cuando el anillo pasó a 30 s.
    const double AUDIO_SECS = (double) supernova::kExportAudioRingSeconds;

    // ---- el plan de fotos (reloj BEATS) ----
    supernova::PhotoSequence seq;
    seq.setFiles ({ "/tmp/snv-beats-a.png", "/tmp/snv-beats-b.png" });   // sintética: sólo da orden y reloj
    seq.setClock (supernova::SeqClock::Beats);
    seq.setIntervalBeats (4.0);                               // un compás de 4/4 = 2 s a 120 BPM
    seq.setBurst (true);

    const int perPhoto = supernova::framesPerPhotoBeats (seq.intervalBeats(), BPM, FPS);
    REQUIRE (perPhoto == 120);                                // 2 s × 60 fps
    const auto plan = supernova::exportSlotPlan (FRAMES, perPhoto, supernova::playOrderFrom (seq, 4));
    REQUIRE (plan.size() == (size_t) FRAMES);
    int changes = 0;
    for (const auto& sl : plan) if (sl.changed) ++changes;
    REQUIRE (changes >= 1);
    REQUIRE (plan[120].changed);                              // justo en el compás
    REQUIRE (supernova::framesPerPhoto (seq.intervalSeconds(), FPS) > FRAMES);   // el reloj viejo no cambiaba

    // ---- la ventana de análisis: kExportAudioRingSeconds a 30 Hz, el MISMO tramo que cubre el audio ----
    std::vector<supernova::AnalysisFrame> ring;
    const int RING_N = (int) (AUDIO_SECS * RING_HZ);          // 900 a 30 s
    for (int k = 0; k < RING_N; ++k) ring.push_back (supernova::scenarioFrame ("kick", k, RING_N));
    const auto win = supernova::exportLoopWindow ((int) ring.size(), RING_HZ, AUDIO_SECS, true);
    REQUIRE (win.first == 0);
    REQUIRE (win.count == RING_N);
    const int loopV = supernova::exportLoopFrames (win.count, FPS, RING_HZ);
    REQUIRE (loopV == (int) (AUDIO_SECS * FPS));              // los mismos segundos, en cuadros del clip

    // ---- audio: el mismo tramo de seno, loopeado con el MISMO período que el análisis ----
    const size_t ringF   = (size_t) (AUDIO_SECS * SR);
    const size_t periodA = std::min (ringF, (size_t) std::llround ((double) loopV * SR / (double) FPS));
    REQUIRE (periodA == ringF);                               // análisis y audio cubren exactamente lo mismo
    std::vector<float> audio (ringF * 2);
    { double ph = 0.0; const double w0 = 2.0 * juce::MathConstants<double>::pi * 220.0 / SR;
      for (size_t k = 0; k < ringF; ++k) { const float v = 0.35f * (float) std::sin (ph); ph += w0;
                                           audio[k * 2] = v; audio[k * 2 + 1] = v; } }

    supernova::VideoExporter ex;
    supernova::VideoExporter::Config cfg;
    cfg.width = W; cfg.height = H; cfg.fps = FPS;
    cfg.bitsPerSecond = supernova::recommendedBitrate ({ W, H }, FPS);
    cfg.withAudio = true; cfg.audioSampleRate = SR; cfg.audioChannels = 2;
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("snv-export-beats.mp4");
    cfg.path = out.getFullPathName().toStdString();
    REQUIRE (ex.begin (cfg));

    supernova::ParticleParams pp;
    std::vector<uint8_t> rgba ((size_t) W * H * 4);
    std::vector<uint8_t> frameA, frameB, prevFrame, changeFrame;
    std::vector<float> aChunk;
    std::set<int> analysisUsed;
    double aAcc = 0.0; size_t aPos = 0;
    const double samplesPerFrame = (double) SR / FPS;
    int curSlot = -1, bursts = 0;

    // FUNDIDO en el EXPORT: la MISMA duración que en pantalla, derivada del MISMO reloj con que se armó el
    // plan (BEATS 4 @ 120 BPM = 2 s por foto → 45% son 0,9 s, que el tope corta en 0,7 s). La primera foto
    // del clip entra con 0 (no hay desde qué disolver). renderOffscreen avanza con el paso fijo de 1/60 que
    // come TODA la física del clip, así que el fundido se ve, respecto del resto del movimiento, igual que
    // en pantalla. Camino idéntico al de SupernovaEditor::exportVideo.
    const double dissolve = supernova::dissolveSecondsFor (seq.clock(), seq.intervalSeconds(),
                                                           seq.intervalBeats(), BPM, seq.kickGapSeconds());
    REQUIRE (dissolve == Catch::Approx (0.7));

    for (int f = 0; f < FRAMES; ++f)
    {
        const int slot = plan[(size_t) f].slot;
        if (slot != curSlot)
        {
            r.setDissolveSeconds (curSlot < 0 ? 0.0 : dissolve);   // la primera foto: corte
            curSlot = slot;
            r.uploadImage ({ (slot == 0 ? warm : cool).data(), 512, 512 });
        }

        const int ai = supernova::analysisIndexForFrame (f, FPS, RING_HZ, win.count);
        analysisUsed.insert (ai);
        const auto af = ring[(size_t) (win.first + ai)];
        pp.explode = (seq.burst() && plan[(size_t) f].changed) ? 1.0f : 0.0f;   // BURST: un solo cuadro
        if (pp.explode > 0.0f) ++bursts;
        r.renderOffscreen (af, pp, W, H, rgba.data());

        aAcc += samplesPerFrame;                              // AUDIO PRIMERO (el muxer lo exige)
        const int nA = (int) aAcc; aAcc -= (double) nA;
        if (nA > 0)
        {
            aChunk.resize ((size_t) nA * 2);
            for (int k = 0; k < nA; ++k)
            {
                const size_t src = ((aPos + (size_t) k) % periodA) * 2;
                aChunk[(size_t) k * 2 + 0] = audio[src + 0];
                aChunk[(size_t) k * 2 + 1] = audio[src + 1];
            }
            aPos = (aPos + (size_t) nA) % periodA;
            REQUIRE (ex.pushAudio (aChunk.data(), nA));
        }
        REQUIRE (ex.pushFrame (rgba.data(), W, H));
        if (f == 60)  frameA = rgba;                          // centro de la foto A
        if (f == 180) frameB = rgba;                          // centro de la foto B
        if (f == 119) prevFrame   = rgba;                     // el cuadro ANTES del cambio (compás 120)
        if (f == 120) changeFrame = rgba;                     // …y el del cambio
    }
    REQUIRE (bursts == changes);                              // una explosión por cambio, ni una de más

    // El MP4 ya no tiene el escalón. Se mide el delta medio entre el cuadro 120 (el del compás) y el 119,
    // re-rindiendo el mismo tramo con las cuatro combinaciones de (corte|fundido) × (con|sin BURST), para
    // separar dos cosas que caen en el MISMO cuadro: el cambio de foto, que es lo que este trabajo suaviza,
    // y la explosión del BURST, que es deliberada y tiene que seguir golpeando.
    auto deltaAt120 = [&] (double dissolveSecs, bool useBurst)
    {
        supernova::MetalRenderer rc;
        rc.prepare (512, 512);
        std::vector<uint8_t> px ((size_t) W * H * 4), prev (px.size());
        supernova::ParticleParams cp;
        int cs = -1;
        for (int f = 0; f <= 120; ++f)
        {
            const int slot = plan[(size_t) f].slot;
            if (slot != cs)
            {
                rc.setDissolveSeconds (cs < 0 ? 0.0 : dissolveSecs);   // la primera foto del clip: corte
                cs = slot;
                rc.uploadImage ({ (slot == 0 ? warm : cool).data(), 512, 512 });
            }
            const int ai = supernova::analysisIndexForFrame (f, FPS, RING_HZ, win.count);
            cp.explode = (useBurst && plan[(size_t) f].changed) ? 1.0f : 0.0f;
            if (f == 120) prev = px;                                   // px todavía tiene el cuadro 119
            rc.renderOffscreen (ring[(size_t) (win.first + ai)], cp, W, H, px.data());
        }
        return meanAbsDiff (prev, px);
    };

    const double cutNoBurst  = deltaAt120 (0.0,      false);
    const double fadeNoBurst = deltaAt120 (dissolve, false);
    const double cutBurst    = deltaAt120 (0.0,      true);
    const double fadeBurst   = deltaAt120 (dissolve, true);
    const double realPath    = meanAbsDiff (prevFrame, changeFrame);   // el del MP4 que se acaba de escribir
    WARN ("EXPORT cuadro 120 vs 119 — sin burst: corte " << cutNoBurst << " → fundido " << fadeNoBurst
          << " · con burst: corte " << cutBurst << " → fundido " << fadeBurst
          << " · el clip escrito: " << realPath);

    // Aislado del BURST, que es lo que este trabajo arregla: el corte salta y el fundido no.
    REQUIRE (cutNoBurst  > 8.0);                     // el export de siempre SÍ saltaba (el bug, en el MP4)
    REQUIRE (fadeNoBurst < cutNoBurst / 8.0);
    // Con BURST el cuadro del cambio sigue golpeando —la explosión es el efecto pedido— pero golpea MENOS,
    // porque lo que estalla ya se re-arma con los colores nuevos entrando en vez de aparecer de una.
    REQUIRE (fadeBurst < cutBurst * 0.6);
    // Y el clip realmente escrito recorre el mismo camino que la medición con burst (el test no mide otra cosa).
    REQUIRE (realPath == Catch::Approx (fadeBurst).epsilon (0.25));

    // 4 s de clip = 4 s de análisis: 120 frames del anillo, no 240 (eso era correr al doble de velocidad).
    REQUIRE ((int) analysisUsed.size() == SECONDS * RING_HZ);
    REQUIRE (ex.finish());
    REQUIRE (out.existsAsFile());
    REQUIRE (out.getSize() > 20000);                          // video + AAC
    REQUIRE (meanAbsDiff (frameA, frameB) > 6.0);             // fuego vs hielo: cambió de verdad
}
