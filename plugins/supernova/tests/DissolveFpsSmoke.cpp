// [supernova][live][dissolvefps][.] — RENDIMIENTO del FUNDIDO en display REAL. Oculto por default (tag [.]);
// se corre a mano:  OvniSupernovaTests "[dissolvefps]"
//
// Por qué acá y no en la app: la app standalone no tiene forma de recibir fotos por línea de comandos ni de
// restaurar una sesión al abrir, así que arrancarla desde un script la deja con la imagen de fábrica — o sea
// sin un solo fundido que medir. Esto abre una ventana DE VERDAD con la misma SupernovaView, la misma
// CAMetalLayer y el mismo VBlankAttachment, y por lo tanto imprime las mismas líneas "[supernova] N fps" que
// la app: es el camino vivo, no un offscreen.
//
// Fotos: SNV_FPS_PHOTOS (dos rutas separadas por ':') o, por default, las dos de ~/Pictures. Si no están, se
// saltea con un WARN en vez de fallar.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_extra/juce_gui_extra.h>
#include <cstdio>
#include <unistd.h>
#include <vector>
#include "ui/SupernovaView.h"
#include "image/ImageLoader.h"
#include "image/PhotoSequence.h"
#include "render/Dissolve.h"
#include "analysis/TripleBuffer.h"

namespace
{
constexpr float kFpsFloor = 55.0f;   // el piso pedido en el M4

juce::StringArray photoPaths()
{
    if (auto env = juce::SystemStats::getEnvironmentVariable ("SNV_FPS_PHOTOS", {}); env.isNotEmpty())
        return juce::StringArray::fromTokens (env, ":", {});
    const auto pics = juce::File::getSpecialLocation (juce::File::userPicturesDirectory);
    return { pics.getChildFile ("OVNI-03-bsas-costa.jpg").getFullPathName(),
             pics.getChildFile ("OVNI-04-bsas-grilla.jpg").getFullPathName() };
}

// Un beat sintético publicado desde el message thread: graves + onset cada `beatMs`. Es lo que el análisis
// real le da al renderer, sin depender del permiso de audio del sistema.
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
}

TEST_CASE ("live: fps durante los FUNDIDOS, dos fotos grandes en SEQ (2 s y kick 0,25 s)",
           "[supernova][live][dissolvefps][.]")
{
    const auto paths = photoPaths();
    std::vector<supernova::LoadedImage> photos;
    for (const auto& p : paths)
    {
        auto li = supernova::ImageLoader::fromFile (juce::File (p));
        if (li.valid()) photos.push_back (std::move (li));
    }
    if (photos.size() < 2)
    {
        WARN ("faltan las dos fotos (SNV_FPS_PHOTOS o ~/Pictures/OVNI-03/04) — smoke de fps salteado");
        SUCCEED();
        return;
    }
    for (size_t i = 0; i < photos.size(); ++i)
        std::fprintf (stderr, "[fps-smoke] foto %zu: %dx%d\n", i, photos[i].width, photos[i].height);

    supernova::SupernovaView view;
    if (! view.gpuAvailable()) { WARN ("sin GPU — smoke de fps salteado"); SUCCEED(); return; }

    view.setOpaque (true);
    view.setBounds (60, 60, 1280, 720);            // tamaño de trabajo realista (la app abre parecido)
    view.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    view.setVisible (true);

    FakeBeat beat;
    view.setAnalysisSource (&beat.buf);

    // Una pasada: alterna las dos fotos cada `changeMs` con la duración de fundido que ese reloj pide, y
    // devuelve (fps mínimo observado, activeParticles mínimo).
    auto run = [&] (const char* label, double changeMs, double dissolveSecs,
                    const supernova::ParticleParams& pp, int seconds)
    {
        view.setParams (pp);
        float    minFps    = 1.0e9f;
        unsigned minActive = 0xFFFFFFFFu;
        int      changes   = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        double nextChange = t0 + changeMs;
        double lastSample = t0;
        // Un primer corte para salir del gradiente de fábrica antes de empezar a medir.
        {
            auto first = std::make_shared<supernova::LoadedImage> (photos[0]);
            view.loadImage (first, 0.0);
        }
        while (juce::Time::getMillisecondCounterHiRes() - t0 < seconds * 1000.0)
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            beat.publish (now, changeMs);
            if (now >= nextChange)
            {
                nextChange = now + changeMs;
                auto img = std::make_shared<supernova::LoadedImage> (photos[(size_t) (++changes) % photos.size()]);
                view.loadImage (img, dissolveSecs);   // FUNDIDO: exactamente lo que hace el editor
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (8);
            // Muestrear sólo después del primer segundo (el fps rolling necesita 30 cuadros para existir).
            if (now - t0 > 1200.0 && now - lastSample > 100.0)
            {
                lastSample = now;
                minFps    = juce::jmin (minFps, view.lastFps());
                minActive = juce::jmin (minActive, view.activeParticles());
            }
        }
        std::fprintf (stderr, "[fps-smoke] %s · cambios=%d · fps MÍNIMO=%.1f · active MÍNIMO=%u de %u\n",
                      label, changes, minFps, minActive, view.totalParticles());
        return std::make_pair (minFps, minActive);
    };

    supernova::ParticleParams plain;                       // lo que el usuario ve al abrir
    supernova::ParticleParams heavy;                       // peor caso: estela + constelación + volumen
    heavy.trailAmt = 0.6f; heavy.linksAmt = 0.7f; heavy.depthAmt = 0.8f; heavy.formAmt = 0.0f;

    // Las duraciones salen de la MISMA regla que usa el editor.
    const double dSeconds = supernova::dissolveSecondsFor (supernova::SeqClock::Seconds, 2.0, 4.0, 120.0, 1.0);
    const double dKick    = supernova::dissolveSecondsFor (supernova::SeqClock::Kick,    8.0, 4.0, 120.0, 0.25);
    std::fprintf (stderr, "[fps-smoke] duraciones: SEQ 2 s → %.4f s · KICK 0,25 s → %.4f s\n", dSeconds, dKick);

    const auto a = run ("SEQ 2 s  · mundo default", 2000.0, dSeconds, plain, 10);
    const auto b = run ("KICK 0,25 s · mundo default", 250.0, dKick, plain, 10);
    const auto c = run ("SEQ 2 s  · mundo pesado (trails+links+3D)", 2000.0, dSeconds, heavy, 10);
    const auto d = run ("KICK 0,25 s · mundo pesado (trails+links+3D)", 250.0, dKick, heavy, 10);

    view.removeFromDesktop();

    CHECK (a.first >= kFpsFloor);
    CHECK (b.first >= kFpsFloor);
    CHECK (c.first >= kFpsFloor);
    CHECK (d.first >= kFpsFloor);
    // La densidad adaptativa no puede recortar partículas por culpa del fundido.
    const unsigned total = view.totalParticles();
    CHECK (a.second == total);
    CHECK (b.second == total);
}

// [supernova][live][dissolveshot][.] — EVIDENCIA VISUAL: deja una ventana REAL congelada en una fase del
// fundido para que alguien de afuera la capture. Oculto por default; se corre a mano:
//     SNV_SHOT_PHASE=0.5 SNV_SHOT_HOLD_MS=9000 OvniSupernovaTests "[dissolveshot]"
//
// Dos cosas obligan a este diseño:
//   · el permiso de GRABACIÓN DE PANTALLA lo tiene la terminal, no este binario: `screencapture` lanzado
//     desde adentro del test devuelve 0 bytes (con -l y con -R). Quien captura tiene que ser el shell.
//   · el Dissolve sólo avanza cuando corre render(), y render() corre en el VBlank del message loop; al
//     dejar de bombear el loop, el fundido queda CONGELADO en su fase y la última imagen presentada se
//     queda en pantalla. Así la latencia de screencapture no corre la fase capturada.
// La ventana se lista con winshot.swift (owner=OvniSupernovaTests) para sacar el window id.
TEST_CASE ("live: congelar un fundido en una fase para capturarlo de afuera",
           "[supernova][live][dissolveshot][.]")
{
    const auto paths = photoPaths();
    std::vector<supernova::LoadedImage> photos;
    for (const auto& p : paths)
    {
        auto li = supernova::ImageLoader::fromFile (juce::File (p));
        if (li.valid()) photos.push_back (std::move (li));
    }
    if (photos.size() < 2) { WARN ("faltan las dos fotos — captura salteada"); SUCCEED(); return; }

    const double phase  = juce::SystemStats::getEnvironmentVariable ("SNV_SHOT_PHASE", "0.5").getDoubleValue();
    const int    holdMs = juce::SystemStats::getEnvironmentVariable ("SNV_SHOT_HOLD_MS", "9000").getIntValue();

    supernova::SupernovaView view;
    if (! view.gpuAvailable()) { WARN ("sin GPU — captura salteada"); SUCCEED(); return; }
    view.setOpaque (true);
    view.setBounds (80, 80, 1100, 620);
    view.addToDesktop (juce::ComponentPeer::windowIsTemporary);
    view.setVisible (true);

    FakeBeat beat;
    view.setAnalysisSource (&beat.buf);
    // Params de MUNDO, no los defaults crudos: con particleSize = 1 el glifo mide ~2 px sobre un drawable
    // retina y la foto queda como un polvillo ilegible. Los mundos de fábrica suben SIZE y GLOW; acá se
    // hace lo mismo para que la captura muestre la FOTO, que es lo que se viene a documentar.
    // …y LIENZO QUIETO (intensity 0, el camino de CLEAR): con la foto asentada y sin movimiento, lo único
    // que cambia entre las tres capturas es la MEZCLA, que es lo que se viene a documentar. Con el motor en
    // marcha el hervor de las partículas tapa la diferencia entre fases.
    supernova::ParticleParams pp;
    pp.particleSize = 5.0f;
    pp.glow         = 0.8f;
    pp.intensity    = 0.0f;
    pp.chaos        = 0.0f;
    pp.jitterGain   = 0.0f;
    pp.breatheGain  = 0.0f;
    view.setParams (pp);

    auto pump = [&] (int ms)
    {
        beat.publish (juce::Time::getMillisecondCounterHiRes(), 500.0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    };

    // La ventana no existe en el WindowServer hasta que el message loop corre.
    for (int i = 0; i < 60; ++i) pump (16);

    // Foto A asentada (por corte), y después el fundido REAL de 0,7 s hacia la B.
    auto a = std::make_shared<supernova::LoadedImage> (photos[0]);
    view.loadImage (a, 0.0);
    for (int i = 0; i < 60; ++i) pump (16);

    const double dissolve = supernova::dissolveSecondsFor (supernova::SeqClock::Seconds, 2.0, 4.0, 120.0, 1.0);
    auto b = std::make_shared<supernova::LoadedImage> (photos[1]);
    view.loadImage (b, dissolve);

    const double targetMs = juce::jlimit (0.0, 1.0, phase) * dissolve * 1000.0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < targetMs) pump (2);

    // Congelado: de acá en adelante NO se bombea el loop, así que no hay más render() y la pantalla se
    // queda con este cuadro exacto. El shell captura durante este rato.
    std::fprintf (stderr, "[shot] CONGELADO en fase %.2f (%.0f ms de %.0f) — %d ms para capturar\n",
                  phase, targetMs, dissolve * 1000.0, holdMs);
    std::fflush (stderr);
    juce::Thread::sleep (holdMs);

    view.removeFromDesktop();
    SUCCEED();
}
