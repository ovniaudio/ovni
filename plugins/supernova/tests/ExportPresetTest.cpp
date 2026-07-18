// [supernova][export] — ExportPreset: dimensiones + bitrate de los formatos de export. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include "video/ExportPreset.h"

using namespace supernova;

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
