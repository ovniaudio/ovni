// [supernova][image] — ImageLoader: decodificación, extracción RGBA8, downsample RNF7 (>4096px) y robustez
// ante entrada inválida (spec §8: nunca crashea). Puro sobre juce_graphics, sin GPU ni ensamblar el plugin.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include "image/ImageLoader.h"

using supernova::ImageLoader;

TEST_CASE ("image: downsampleFactor lleva el lado mayor bajo el máximo", "[supernova][image]")
{
    // Bajo el umbral → sin downsample.
    REQUIRE (ImageLoader::downsampleFactor (4096, 4096) == 1);
    REQUIRE (ImageLoader::downsampleFactor (1920, 1080) == 1);

    // Justo por encima → factor 2 (8192/2 = 4096).
    REQUIRE (ImageLoader::downsampleFactor (8192, 100) == 2);
    REQUIRE (ImageLoader::downsampleFactor (4097, 4097) == 2);

    // Muy grande → factor tal que ceil(side/f) <= 4096.
    const int f = ImageLoader::downsampleFactor (16384, 8000);
    REQUIRE (f == 4);
    REQUIRE ((16384 + f - 1) / f <= ImageLoader::kMaxSide);
}

TEST_CASE ("image: fromImage extrae RGBA8 fiel de una imagen chica conocida", "[supernova][image]")
{
    juce::Image img (juce::Image::ARGB, 2, 2, true);
    img.setPixelAt (0, 0, juce::Colour::fromRGBA (255, 0,   0,   255));
    img.setPixelAt (1, 0, juce::Colour::fromRGBA (0,   255, 0,   255));
    img.setPixelAt (0, 1, juce::Colour::fromRGBA (0,   0,   255, 255));
    img.setPixelAt (1, 1, juce::Colour::fromRGBA (10,  20,  30,  200));

    const auto loaded = ImageLoader::fromImage (img);
    REQUIRE (loaded.valid());
    REQUIRE (loaded.width  == 2);
    REQUIRE (loaded.height == 2);
    REQUIRE (loaded.rgba.size() == 2u * 2u * 4u);

    // Píxel (0,0) rojo opaco.
    REQUIRE (loaded.rgba[0] == 255);
    REQUIRE (loaded.rgba[1] == 0);
    REQUIRE (loaded.rgba[2] == 0);
    REQUIRE (loaded.rgba[3] == 255);

    // Píxel (1,1) con alpha < 255. juce::Image::ARGB guarda color PREMULTIPLICADO → el round-trip pierde
    // hasta 1 LSB por canal (detalle de almacenamiento de JUCE, no del loader): toleramos ±2 en RGB. El alpha
    // es exacto. (Las fotos del usuario son opacas → sin pérdida; sólo PNGs semitransparentes rozan esto.)
    const size_t p11 = ((size_t) 1 * 2 + 1) * 4;
    REQUIRE (std::abs ((int) loaded.rgba[p11 + 0] - 10) <= 2);
    REQUIRE (std::abs ((int) loaded.rgba[p11 + 1] - 20) <= 2);
    REQUIRE (std::abs ((int) loaded.rgba[p11 + 2] - 30) <= 2);
    REQUIRE (loaded.rgba[p11 + 3] == 200);
}

TEST_CASE ("image: una imagen gigante se downsamplea bajo el máximo (RNF7)", "[supernova][image]")
{
    // 5000x64: lado mayor 5000 > 4096 → debe bajar. (Angosta para no gastar memoria en el test.)
    juce::Image big (juce::Image::RGB, 5000, 64, true);
    big.clear (big.getBounds(), juce::Colours::orange);

    const auto loaded = ImageLoader::fromImage (big);
    REQUIRE (loaded.valid());
    REQUIRE (juce::jmax (loaded.width, loaded.height) <= ImageLoader::kMaxSide);
    REQUIRE (loaded.width  < 5000);   // efectivamente redujo
    REQUIRE (loaded.rgba.size() == (size_t) loaded.width * loaded.height * 4);
}

TEST_CASE ("image: round-trip codificado PNG → fromEncodedData", "[supernova][image]")
{
    juce::Image img (juce::Image::ARGB, 8, 6, true);
    img.clear (img.getBounds(), juce::Colour::fromRGB (120, 200, 40));

    juce::MemoryOutputStream mos;
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, mos));

    const auto loaded = ImageLoader::fromEncodedData (mos.getData(), mos.getDataSize());
    REQUIRE (loaded.valid());
    REQUIRE (loaded.width  == 8);
    REQUIRE (loaded.height == 6);
    // color plano preservado (± nada, PNG es lossless).
    REQUIRE (loaded.rgba[0] == 120);
    REQUIRE (loaded.rgba[1] == 200);
    REQUIRE (loaded.rgba[2] == 40);
}

TEST_CASE ("image: entrada inválida no crashea y devuelve LoadedImage inválida (spec §8)", "[supernova][image]")
{
    REQUIRE_FALSE (ImageLoader::fromFile (juce::File ("/no/existe/jamas.png")).valid());

    const uint8_t garbage[] = { 0x00, 0x01, 0x02, 0x03, 0xFF, 0xFE };
    REQUIRE_FALSE (ImageLoader::fromEncodedData (garbage, sizeof (garbage)).valid());
    REQUIRE_FALSE (ImageLoader::fromEncodedData (nullptr, 0).valid());

    REQUIRE_FALSE (ImageLoader::fromImage (juce::Image()).valid());   // imagen nula
}
