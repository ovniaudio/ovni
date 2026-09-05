// [supernova][cards] — smoke de las CARDS procedurales del world browser (WorldCards.h): cada mundo
// rinde una firma NO-negra y CLARAMENTE DISTINTA de sus vecinos (el pedido: diferenciar todo, carga
// instantánea). 100% CPU/JUCE → corre sin GPU, determinista (seed = índice).
// Si SNV_CARD_DUMP=<dir> está seteado, vuelca cards individuales + una CONTACT SHEET para revisión visual.
#include <catch2/catch_test_macros.hpp>
#include <juce_graphics/juce_graphics.h>
#include <cstdlib>
#include "ui/WorldCards.h"
#include "presets/PresetTypes.h"

namespace
{
double cardMeanLuma (const juce::Image& im)
{
    double s = 0;
    for (int y = 0; y < im.getHeight(); y += 3)
        for (int x = 0; x < im.getWidth(); x += 3)
        { const auto c = im.getPixelAt (x, y); s += 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue(); }
    const int n = ((im.getHeight() + 2) / 3) * ((im.getWidth() + 2) / 3);
    return n > 0 ? s / n : 0.0;
}
// Distancia media RGB entre dos cards (muestreo 1/3) — mide "se distinguen a golpe de vista".
double cardDiff (const juce::Image& a, const juce::Image& b)
{
    double s = 0; int n = 0;
    for (int y = 0; y < a.getHeight(); y += 3)
        for (int x = 0; x < a.getWidth(); x += 3, ++n)
        {
            const auto ca = a.getPixelAt (x, y), cb = b.getPixelAt (x, y);
            s += std::abs ((int) ca.getRed() - cb.getRed()) + std::abs ((int) ca.getGreen() - cb.getGreen())
               + std::abs ((int) ca.getBlue() - cb.getBlue());
        }
    return n > 0 ? s / n : 0.0;
}
}

TEST_CASE ("cards: firmas procedurales no-negras, deterministas y distintas entre mundos", "[supernova][cards]")
{
    const auto& presets = ovni::presets::factoryPresets();
    REQUIRE (presets.size() >= 44);   // 50 mundos hoy (la ronda 4 sacó los 4 KALEIDO y sumó SATELLITE)

    constexpr int W = 256, H = 132;
    std::vector<juce::Image> cards;
    for (int i = 0; i < (int) presets.size(); ++i)
        cards.push_back (supernova::cards::renderWorldCard (presets[(size_t) i], i, W, H));

    for (size_t i = 0; i < cards.size(); ++i)
    {
        INFO ("mundo #" << i << " · " << presets[i].name);
        REQUIRE (cards[i].isValid());
        REQUIRE (cardMeanLuma (cards[i]) > 4.0);            // no-negro
    }
    // Determinismo: mismo índice → misma card byte-a-byte (muestreada).
    const auto again = supernova::cards::renderWorldCard (presets[0], 0, W, H);
    REQUIRE (cardDiff (cards[0], again) == 0.0);

    // Distinción: cada card difiere de la SIGUIENTE (vecinas en la grilla) de forma visible.
    int weak = 0;
    for (size_t i = 0; i + 1 < cards.size(); ++i)
    {
        const double d = cardDiff (cards[i], cards[i + 1]);
        INFO (presets[i].name << " vs " << presets[i + 1].name << " diff=" << d);
        if (d < 12.0) ++weak;
    }
    REQUIRE (weak <= 2);   // tolera pares hermanos, pero la grilla en conjunto DIFERENCIA

    // Dump opcional para revisión visual: cards + contact sheet 6 columnas.
    if (const char* dir = std::getenv ("SNV_CARD_DUMP"))
    {
        juce::File out { juce::String (dir) }; out.createDirectory();
        const int cols = 6, rows = ((int) cards.size() + cols - 1) / cols, pad = 8, nameH = 16;
        juce::Image sheet (juce::Image::ARGB, cols * (W + pad) + pad, rows * (H + nameH + pad) + pad, true);
        juce::Graphics g (sheet);
        g.fillAll (juce::Colour (0xff111111));
        for (size_t i = 0; i < cards.size(); ++i)
        {
            const int cx = pad + (int) (i % cols) * (W + pad), cy = pad + (int) (i / cols) * (H + nameH + pad);
            g.drawImageAt (cards[i], cx, cy);
            g.setColour (juce::Colours::white); g.setFont (11.0f);
            g.drawText (presets[i].name, cx, cy + H, W, nameH, juce::Justification::centred);
            juce::FileOutputStream fs (out.getChildFile (juce::String ((int) i).paddedLeft ('0', 2) + "-" + juce::String (presets[i].name) + ".png"));
            if (fs.openedOk()) juce::PNGImageFormat().writeImageToStream (cards[i], fs);
        }
        juce::FileOutputStream fs (out.getChildFile ("_contact-sheet.png"));
        if (fs.openedOk()) juce::PNGImageFormat().writeImageToStream (sheet, fs);
    }
}
