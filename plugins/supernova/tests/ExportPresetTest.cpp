// [supernova][export] — ExportPreset: dimensiones + bitrate de los formatos de export. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "video/ExportPreset.h"
#include "image/PhotoSequence.h"
#include <vector>

using namespace supernova;
using Catch::Approx;

TEST_CASE ("export: cada formato da las dimensiones correctas", "[supernova][export]")
{
    REQUIRE (exportDims (ExportFormat::HD1080).width == 1920);
    REQUIRE (exportDims (ExportFormat::HD1080).height == 1080);
    REQUIRE (exportDims (ExportFormat::UHD4K).width == 3840);
    REQUIRE (exportDims (ExportFormat::UHD4K).height == 2160);
    REQUIRE (exportDims (ExportFormat::Square1080).width == exportDims (ExportFormat::Square1080).height);
    REQUIRE (exportDims (ExportFormat::Vertical1080).width == 1080);
    REQUIRE (exportDims (ExportFormat::Vertical1080).height == 1920);   // 9:16 vertical (Reels/TikTok)
}

TEST_CASE ("export: el bitrate recomendado crece con la resolución y respeta piso/techo", "[supernova][export]")
{
    const int hd = recommendedBitrate (exportDims (ExportFormat::HD1080), 60);
    const int uhd = recommendedBitrate (exportDims (ExportFormat::UHD4K), 60);
    REQUIRE (uhd > hd);                       // 4K pesa más que 1080p
    REQUIRE (hd >= 4'000'000);                // piso 4 Mbps
    REQUIRE (uhd <= 120'000'000);             // techo 120 Mbps
    // Un tamaño diminuto se clampa al piso.
    REQUIRE (recommendedBitrate ({ 64, 48 }, 1) == 4'000'000);
}

TEST_CASE ("export: los nombres de formato existen", "[supernova][export]")
{
    REQUIRE (std::string (exportFormatName (ExportFormat::Vertical1080)).find ("9:16") != std::string::npos);
    REQUIRE (std::string (exportFormatName (ExportFormat::UHD4K)).find ("4K") != std::string::npos);
}

// El CICLADO de fotos del export (la matemática que decide qué foto muestra cada frame).
TEST_CASE ("export: framesPerPhoto = intervalo × fps, mínimo 1", "[supernova][export]")
{
    REQUIRE (framesPerPhoto (2.0, 60) == 120);
    REQUIRE (framesPerPhoto (8.0, 30) == 240);
    REQUIRE (framesPerPhoto (0.5, 60) == 30);
    REQUIRE (framesPerPhoto (0.0, 60) == 1);    // una foto nunca dura 0 frames
    REQUIRE (framesPerPhoto (2.0, 0)  == 2);    // fps degenerado → tratado como 1
}

TEST_CASE ("export: photoSlotForFrame cicla cada foto su ventana y hace loop", "[supernova][export]")
{
    // 3 fotos, 4 frames cada una → 12 frames por vuelta.
    const int pp = 4, n = 3;
    REQUIRE (photoSlotForFrame (0,  pp, n) == 0);
    REQUIRE (photoSlotForFrame (3,  pp, n) == 0);   // fin de la ventana de la foto 0
    REQUIRE (photoSlotForFrame (4,  pp, n) == 1);   // arranca la foto 1
    REQUIRE (photoSlotForFrame (7,  pp, n) == 1);
    REQUIRE (photoSlotForFrame (8,  pp, n) == 2);   // foto 2
    REQUIRE (photoSlotForFrame (11, pp, n) == 2);
    REQUIRE (photoSlotForFrame (12, pp, n) == 0);   // vuelve a la foto 0 (loop)
    REQUIRE (photoSlotForFrame (16, pp, n) == 1);

    // Bordes: 1 sola foto siempre da 0; perPhoto/numPhotos degenerados no explotan.
    REQUIRE (photoSlotForFrame (99, pp, 1)  == 0);
    REQUIRE (photoSlotForFrame (5,  0,  n)  == 2);   // perPhoto<1 → tratado como 1 → (5/1)%3 = 2
    REQUIRE (photoSlotForFrame (5,  0,  0)  == 0);   // sin fotos → 0
}

// ===================== MEDIA SESSION PRO ronda 2: el export sigue el RELOJ / ORDEN / TRANSICIÓN =====================
// La matemática pura de "exportar lo que el usuario ve": beats → cuadros, kick con gap, el orden (LOOP o
// SHUFFLE) y el BURST marcado en UN solo cuadro por cambio.

TEST_CASE ("export: framesPerPhotoBeats = N beats al BPM del host, en cuadros", "[supernova][export]")
{
    REQUIRE (framesPerPhotoBeats (4.0, 120.0, 60) == 120);   // 4 beats @120 = 2 s → 120 cuadros a 60 fps
    REQUIRE (framesPerPhotoBeats (1.0, 120.0, 30) == 15);    // 0.5 s → 15 cuadros a 30 fps
    REQUIRE (framesPerPhotoBeats (4.0,  60.0, 30) == 120);   // 4 s → 120 cuadros
    REQUIRE (framesPerPhotoBeats (4.0,   0.0, 60) == 120);   // un host sin tempo → 120 BPM
    REQUIRE (framesPerPhotoBeats (0.0, 120.0, 60) == 1);     // una foto nunca dura 0 cuadros
}

TEST_CASE ("export: secondsPerPhotoBeats da la vuelta entera al compás (menú Full photo loop)", "[supernova][export]")
{
    REQUIRE (secondsPerPhotoBeats (4.0, 120.0) == Approx (2.0));    // un compás de 4/4 a 120 BPM
    REQUIRE (secondsPerPhotoBeats (8.0,  90.0) == Approx (8.0 * 60.0 / 90.0));
    REQUIRE (secondsPerPhotoBeats (4.0,   0.0) == Approx (2.0));    // host sin tempo → 120 BPM
    // 12 fotos a un compás por foto = 24 s de vuelta entera (no 12 × el intervalo en segundos).
    REQUIRE (12.0 * secondsPerPhotoBeats (4.0, 120.0) == Approx (24.0));
}

TEST_CASE ("export: onsetFramesFromCounts detecta el edge del contador y NO el cruce del loop", "[supernova][export]")
{
    // Anillo de 4 cuadros de análisis: el contador sube en la posición 2. El clip repite el anillo.
    const std::vector<unsigned> ring { 5u, 5u, 6u, 6u };
    const auto on = onsetFramesFromCounts (ring, 8, 30, 30);   // fps == tasa del anillo: 1 cuadro por frame
    REQUIRE (on == std::vector<int> { 2, 6 });    // el cuadro 4 (vuelta del anillo) NO es onset
    REQUIRE (onsetFramesFromCounts ({}, 8, 30, 30).empty());
    REQUIRE (onsetFramesFromCounts ({ 3u }, 8, 30, 30).empty());          // anillo de 1: sin previo, sin edges
    REQUIRE (onsetFramesFromCounts ({ 1u, 1u, 1u }, 9, 30, 30).empty());  // sin onsets
}

TEST_CASE ("export: kickSwitchFrames respeta el gap mínimo (el reloj se arma en el cuadro 0)", "[supernova][export]")
{
    const std::vector<int> onsets { 10, 20, 45, 50, 100 };
    // gap 1 s a 30 fps = 30 cuadros: 10 y 20 caen dentro del gap del arranque; 45 vale; 50 está pegado a 45.
    REQUIRE (kickSwitchFrames (onsets, 1.0, 30) == std::vector<int> { 45, 100 });
    REQUIRE (kickSwitchFrames (onsets, 0.0, 30) == onsets);          // sin gap: todos
    REQUIRE (kickSwitchFrames ({ 0, 12 }, 0.0, 30) == std::vector<int> { 12 });   // el cuadro 0 no es cambio
    REQUIRE (kickSwitchFrames ({}, 1.0, 30).empty());
}

TEST_CASE ("export: exportSlotPlan sigue el orden dado y marca el cambio en UN solo cuadro", "[supernova][export]")
{
    // LOOP de 3 fotos, 4 cuadros cada una: mismo resultado que photoSlotForFrame + el cambio marcado.
    const auto plan = exportSlotPlan (12, 4, { 0, 1, 2 });
    REQUIRE (plan.size() == 12u);
    for (int f = 0; f < 12; ++f)
        REQUIRE (plan[(size_t) f].slot == photoSlotForFrame (f, 4, 3));
    int changes = 0;
    for (const auto& s : plan) if (s.changed) ++changes;
    REQUIRE (changes == 2);                       // cuadros 4 y 8 (el 0 NO es cambio: la foto ya está puesta)
    REQUIRE (plan[4].changed);
    REQUIRE (plan[8].changed);
    REQUIRE (! plan[0].changed);
    REQUIRE (! plan[5].changed);
}

TEST_CASE ("export: exportSlotPlan con orden de SHUFFLE exporta ESE orden", "[supernova][export]")
{
    const std::vector<int> shuffled { 2, 0, 2, 1 };   // lo que produjo la secuencia (puede repetir con hueco)
    const auto plan = exportSlotPlan (8, 2, shuffled);
    REQUIRE (plan[0].slot == 2);
    REQUIRE (plan[2].slot == 0);
    REQUIRE (plan[4].slot == 2);
    REQUIRE (plan[6].slot == 1);
    REQUIRE (plan[7].slot == 1);
}

TEST_CASE ("export: exportSlotPlan con KICK cambia en los cuadros de onset, no por reloj", "[supernova][export]")
{
    const auto plan = exportSlotPlan (12, 4, { 0, 1, 2 }, { 5, 9 });
    REQUIRE (plan[4].slot == 0);        // el reloj periódico NO manda cuando hay onsets
    REQUIRE (! plan[4].changed);
    REQUIRE (plan[5].changed);
    REQUIRE (plan[5].slot == 1);
    REQUIRE (plan[8].slot == 1);
    REQUIRE (plan[9].changed);
    REQUIRE (plan[9].slot == 2);
    REQUIRE (plan[11].slot == 2);
}

TEST_CASE ("export: exportSlotPlan aguanta bordes (sin fotos, 0 cuadros, perPhoto degenerado)", "[supernova][export]")
{
    REQUIRE (exportSlotPlan (0, 4, { 0, 1 }).empty());
    REQUIRE (exportSlotPlan (3, 4, {}).size() == 3u);
    REQUIRE (exportSlotPlan (3, 4, {})[0].slot == 0);
    REQUIRE (exportSlotPlan (4, 0, { 0, 1 })[1].slot == 1);   // perPhoto<1 → 1 cuadro por foto
    REQUIRE (exportSlotPlan (4, 0, { 0, 1 })[1].changed);
}

// El ORDEN de reproducción sale de la secuencia misma (una COPIA que se camina): con la misma semilla, el
// SHUFFLE que se exporta es EXACTAMENTE el que se vería en vivo.
TEST_CASE ("export: el orden de SHUFFLE del export es determinista con la misma semilla", "[supernova][export]")
{
    auto make = [] (uint32_t seed)
    {
        supernova::PhotoSequence s;
        s.setFiles ({ "/tmp/1.png", "/tmp/2.png", "/tmp/3.png", "/tmp/4.png" });
        s.setOrder (supernova::SeqOrder::Shuffle);
        s.setSeed (seed);
        return playOrderFrom (s, 10);
    };
    const auto a = make (12345u), b = make (12345u), c = make (999u);
    REQUIRE (a.size() == 10u);
    REQUIRE (a == b);                       // misma semilla → mismo orden (el export ve lo que ve el vivo)
    REQUIRE (a != c);                       // otra semilla → otro orden
    REQUIRE (a[0] == 0);                    // arranca en la foto ACTUAL
    for (size_t i = 1; i < a.size(); ++i)
        REQUIRE (a[i] != a[i - 1]);         // shuffle nunca repite la misma dos veces seguidas
    for (int v : a) REQUIRE ((v >= 0 && v < 4));

    // LOOP: el orden es el natural desde la foto actual.
    supernova::PhotoSequence loop;
    loop.setFiles ({ "/tmp/1.png", "/tmp/2.png", "/tmp/3.png" });
    loop.jumpTo (1);
    REQUIRE (playOrderFrom (loop, 4) == std::vector<int> { 1, 2, 0, 1 });
}


// ============================ RONDA 2b · F3 — el export reproduce el análisis a su tasa REAL ============================
// El editor llena el anillo de análisis a 30 Hz (su timer), pero el export consumía UN frame por cuadro del
// clip a 60 fps: la reactividad del MP4 corría al DOBLE de velocidad y el audio muxeado cubría la mitad del
// tramo que el análisis. Estas funciones puras arreglan el mapeo cuadro→análisis y la ventana del loop.

TEST_CASE ("export: analysisIndexForFrame reproduce el anillo a su tasa (30 Hz en un clip de 60 fps)",
           "[supernova][export]")
{
    // 60 fps / 30 Hz: cada frame de análisis dura DOS cuadros de video.
    REQUIRE (analysisIndexForFrame (0, 60, 30, 360) == 0);
    REQUIRE (analysisIndexForFrame (1, 60, 30, 360) == 0);
    REQUIRE (analysisIndexForFrame (2, 60, 30, 360) == 1);
    REQUIRE (analysisIndexForFrame (3, 60, 30, 360) == 1);
    REQUIRE (analysisIndexForFrame (4, 60, 30, 360) == 2);
    REQUIRE (analysisIndexForFrame (719, 60, 30, 360) == 359);   // último cuadro de la vuelta
    REQUIRE (analysisIndexForFrame (720, 60, 30, 360) == 0);     // y vuelve a empezar

    // fps == tasa del anillo: uno a uno (lo que hacía el código viejo, correcto sólo en este caso)
    REQUIRE (analysisIndexForFrame (5, 30, 30, 100) == 5);
    // bordes: sin frames, fps/hz degenerados
    REQUIRE (analysisIndexForFrame (7, 60, 30, 0) == 0);
    REQUIRE (analysisIndexForFrame (7, 0, 0, 10) == 7);
}

TEST_CASE ("export: exportLoopWindow hace que análisis y audio cubran el MISMO tramo", "[supernova][export]")
{
    // Con sonido: el anillo de audio guarda 12 s, así que el loop usa los ÚLTIMOS 12 s de análisis (360 a 30 Hz).
    const auto w = exportLoopWindow (600, 30, 12.0, true);
    REQUIRE (w.count == 360);
    REQUIRE (w.first == 240);                    // los más RECIENTES (el audio del ring es el final)
    // Sin sonido: el anillo entero (20 s de reactividad, nada con qué sincronizar).
    const auto mute = exportLoopWindow (600, 30, 12.0, false);
    REQUIRE (mute.count == 600);
    REQUIRE (mute.first == 0);
    // Anillo más corto que la ventana de audio: se usa entero.
    const auto shortRing = exportLoopWindow (200, 30, 12.0, true);
    REQUIRE (shortRing.count == 200);
    REQUIRE (shortRing.first == 0);
    // Bordes.
    REQUIRE (exportLoopWindow (0, 30, 12.0, true).count == 0);
    REQUIRE (exportLoopWindow (600, 30, 0.0, true).count == 600);
}

TEST_CASE ("export: exportLoopFrames pasa frames de análisis a CUADROS del clip", "[supernova][export]")
{
    REQUIRE (exportLoopFrames (360, 60, 30) == 720);    // 12 s a 60 fps
    REQUIRE (exportLoopFrames (360, 30, 30) == 360);    // 12 s a 30 fps
    REQUIRE (exportLoopFrames (600, 60, 30) == 1200);   // 20 s a 60 fps
    REQUIRE (exportLoopFrames (0, 60, 30) == 0);
    REQUIRE (exportLoopFrames (1, 30, 60) == 1);        // nunca menos de un cuadro
}

TEST_CASE ("export: los onsets del KICK caen en el cuadro correcto a 60 fps (no al doble de velocidad)",
           "[supernova][export]")
{
    const std::vector<unsigned> ring { 5u, 5u, 6u, 6u };   // el contador sube en la posición 2 del anillo
    // A 30 Hz en un clip de 60 fps: la posición 2 del anillo es el cuadro 4, y la vuelta dura 8 cuadros.
    REQUIRE (onsetFramesFromCounts (ring, 16, 60, 30) == std::vector<int> { 4, 12 });
    // El mismo anillo a 30 fps: los cuadros son los índices del anillo.
    REQUIRE (onsetFramesFromCounts (ring, 8, 30, 30) == std::vector<int> { 2, 6 });
    // El clip corta antes de la segunda vuelta.
    REQUIRE (onsetFramesFromCounts (ring, 8, 60, 30) == std::vector<int> { 4 });
}

// ================= RONDA 3 · D4 — el mapeo del KICK también vale con fps < ringHz =================
// `onsetFramesFromCounts` se escribió pensando en 60 fps sobre un anillo de 30 Hz (cada frame de análisis
// dura dos cuadros). A 24 fps la relación se DA VUELTA: hay menos cuadros que frames de análisis, algunos
// índices del anillo no se muestran nunca y dos onsets seguidos pueden caer en el MISMO cuadro. El contrato
// que se sostiene es "el primer cuadro que muestra un índice ≥ k", sin cuadros repetidos.
TEST_CASE ("exportpreset: los onsets del KICK a 24 fps caen en cuadros válidos y sin repetir",
           "[supernova][export]")
{
    // Anillo de 30 Hz con un onset en cada frame de análisis (el peor caso para el aliasing).
    std::vector<unsigned> counts;
    for (unsigned k = 0; k < 30; ++k) counts.push_back (k);

    const int fps = 24, ringHz = 30, totalFrames = 24 * 2;   // 2 s de clip
    const auto onsets = supernova::onsetFramesFromCounts (counts, totalFrames, fps, ringHz);

    REQUIRE (! onsets.empty());
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        REQUIRE (onsets[i] > 0);                       // el cuadro 0 nunca es un cambio
        REQUIRE (onsets[i] < totalFrames);
        if (i > 0) REQUIRE (onsets[i] > onsets[i - 1]);   // estrictamente creciente: sin cuadros repetidos
    }

    // Cada onset cae en un cuadro que YA muestra ese frame de análisis (o uno posterior): nunca antes.
    const int count = (int) counts.size();
    for (int fr : onsets)
        REQUIRE (supernova::analysisIndexForFrame (fr, fps, ringHz, count) >= 1);

    // Y el caso normal (60 fps sobre 30 Hz) sigue dando el doble de cuadros por frame de análisis.
    const auto fast = supernova::onsetFramesFromCounts (counts, 60 * 2, 60, ringHz);
    REQUIRE (fast.size() >= onsets.size());
    REQUIRE (fast[0] == 2);                            // el índice 1 del anillo se ve por primera vez en el cuadro 2
}
