// [supernova][canvas] — CanvasFormat (MEDIA SESSION PRO): AUTO sigue la orientación del media, los presets
// fijos, el rect letterbox/pillarbox y el preset de export que coincide. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include "image/CanvasFormat.h"

using namespace supernova;
using Catch::Approx;

TEST_CASE ("canvas: AUTO clasifica por orientación (vertical 9:16 · cuadrado 1:1 · horizontal 16:9 · sin media FREE)",
           "[supernova][canvas]")
{
    REQUIRE (autoAspectFor (0.0f)   == 0.0f);                       // sin media → FREE
    REQUIRE (autoAspectFor (-1.0f)  == 0.0f);
    REQUIRE (autoAspectFor (0.5625f) == Approx (9.0f / 16.0f));    // foto vertical de celular
    REQUIRE (autoAspectFor (0.75f)   == Approx (9.0f / 16.0f));    // 3:4 también es vertical
    REQUIRE (autoAspectFor (1.0f)    == Approx (1.0f));
    REQUIRE (autoAspectFor (0.95f)   == Approx (1.0f));            // casi cuadrado = cuadrado
    REQUIRE (autoAspectFor (1.5f)    == Approx (16.0f / 9.0f));    // 3:2 → 16:9
    REQUIRE (autoAspectFor (2.4f)    == Approx (16.0f / 9.0f));
}

TEST_CASE ("canvas: canvasAspectFor resuelve AUTO / FREE / presets", "[supernova][canvas]")
{
    REQUIRE (canvasAspectFor (CanvasFormat::Free, 0.5f)        == 0.0f);
    REQUIRE (canvasAspectFor (CanvasFormat::Auto, 0.75f)       == Approx (9.0f / 16.0f));
    REQUIRE (canvasAspectFor (CanvasFormat::Wide16x9, 0.5f)    == Approx (16.0f / 9.0f));   // el preset manda
    REQUIRE (canvasAspectFor (CanvasFormat::Tall9x16, 2.0f)    == Approx (9.0f / 16.0f));
    REQUIRE (canvasAspectFor (CanvasFormat::Square1x1, 2.0f)   == Approx (1.0f));
    REQUIRE (canvasAspectFor (CanvasFormat::Portrait4x5, 2.0f) == Approx (0.8f));
    REQUIRE (canvasAspectFor (CanvasFormat::Classic4x3, 0.5f)  == Approx (4.0f / 3.0f));
    REQUIRE (canvasFormatFromInt (99) == CanvasFormat::Auto);   // basura del state → AUTO
    REQUIRE (canvasFormatFromInt (3)  == CanvasFormat::Tall9x16);
}

TEST_CASE ("canvas: fitRect centra el rect más grande con el aspecto (letterbox / pillarbox)", "[supernova][canvas]")
{
    // Área ancha, lienzo 16:9 → alto completo, ancho 888, centrado.
    auto r = fitRect (0, 0, 1000, 500, 16.0f / 9.0f);
    REQUIRE (r.h == 500);
    REQUIRE (r.w == 888);
    REQUIRE (r.x == 56);
    REQUIRE (r.y == 0);

    // Área vertical, lienzo 9:16 → ancho completo, alto 711.
    r = fitRect (10, 20, 400, 800, 9.0f / 16.0f);
    REQUIRE (r.w == 400);
    REQUIRE (r.h == 711);
    REQUIRE (r.x == 10);
    REQUIRE (r.y == 20 + (800 - 711) / 2);

    // Área ancha, lienzo 9:16 → pillarbox fuerte.
    r = fitRect (0, 0, 1100, 500, 9.0f / 16.0f);
    REQUIRE (r.h == 500);
    REQUIRE (r.w == 281);
    REQUIRE (r.x == (1100 - 281) / 2);

    // Aspecto 0 (FREE) → el área entera.
    r = fitRect (5, 6, 300, 200, 0.0f);
    REQUIRE ((r.x == 5 && r.y == 6 && r.w == 300 && r.h == 200));

    // Mismo aspecto → sin barras.
    r = fitRect (0, 0, 1920, 1080, 16.0f / 9.0f);
    REQUIRE ((r.w == 1920 && r.h == 1080));
}

TEST_CASE ("canvas: el export default coincide con el lienzo", "[supernova][canvas]")
{
    REQUIRE (defaultExportFormat (9.0f / 16.0f) == ExportFormat::Vertical1080);
    REQUIRE (defaultExportFormat (0.8f)         == ExportFormat::Vertical1080);   // 4:5 → vertical
    REQUIRE (defaultExportFormat (1.0f)         == ExportFormat::Square1080);
    REQUIRE (defaultExportFormat (16.0f / 9.0f) == ExportFormat::HD1080);
    REQUIRE (defaultExportFormat (4.0f / 3.0f)  == ExportFormat::HD1080);
    REQUIRE (defaultExportFormat (0.0f)         == ExportFormat::HD1080);         // FREE → 1080p como hoy
}

TEST_CASE ("canvas: etiquetas del chip", "[supernova][canvas]")
{
    REQUIRE (std::strcmp (aspectLabel (0.0f), "FREE") == 0);
    REQUIRE (std::strcmp (aspectLabel (16.0f / 9.0f), "16:9") == 0);
    REQUIRE (std::strcmp (aspectLabel (9.0f / 16.0f), "9:16") == 0);
    REQUIRE (std::strcmp (aspectLabel (1.0f), "1:1") == 0);
    REQUIRE (std::strcmp (aspectLabel (0.8f), "4:5") == 0);
    REQUIRE (std::strcmp (aspectLabel (1.3333f), "4:3") == 0);
    REQUIRE (std::strcmp (canvasFormatName (CanvasFormat::Auto), "AUTO") == 0);
}
