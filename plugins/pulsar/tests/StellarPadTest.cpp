// [stellarpad][pulsar] — el FÓSFORO acumula la forma del atractor (larga exposición real, no una estela
// fija). Pumpeando muchos frames con la trayectoria EN MOVIMIENTO (interleave processBlock + dbgPump, igual
// que el shot4k) el visual ilumina MÁS píxeles en la región del pozo que con pocos frames. Headless
// (ScopedJuceInitialiser_GUI lo provee TestMain de S3). NO toca DSP: sólo cuenta píxeles del snapshot real.
#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIDs.h"
#include <cmath>

namespace
{
// Píxeles "encendidos" (claramente por encima del pozo oscuro ~#0e1523) en la ventana CENTRAL del pad
// (evita rails y telemetría de esquinas). Snapshot real del editor a 1x.
int litPixels (juce::AudioProcessorEditor& ed)
{
    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), false, 1.0f);
    if (! img.isValid()) return -1;
    const int w = img.getWidth(), h = img.getHeight();
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int n = 0;
    // El POZO vive ahora a la IZQUIERDA (layout ÓRBITA): muestreá su mitad izquierda-centro.
    const int x0 = w * 8 / 100, x1 = w * 50 / 100;
    const int y0 = h * 24 / 100, y1 = h * 76 / 100;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const auto c = bd.getPixelColour (x, y);
            if ((int) c.getRed() + (int) c.getGreen() + (int) c.getBlue() > 150)   // pozo ≈ 14+21+35 = 70
                ++n;
        }
    return n;
}

// Mueve la trayectoria: cada bloque empuja audio (el atractor orbita) + un frame del visual.
void pumpMoving (pulsar::PulsarProcessor& proc, pulsar::PulsarEditor& pe, int blocks)
{
    int s = 0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int nn = 0; nn < 512; ++nn)
                d[nn] = 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) (s + nn) / 48000.0f);
        }
        s += 512;
        proc.processBlock (buf, midi);
        pe.dbgPump (1);
    }
}
} // namespace

TEST_CASE ("stellarpad: el fósforo acumula la forma del atractor (larga exposición)", "[stellarpad][pulsar]")
{
    namespace pid = pulsar::params::id;
    pulsar::PulsarProcessor proc;
    // SHAPE 0.66 = Lorenz (caótico): cubre un área 2D → la acumulación se nota fuerte. MOTION/SMEAR vivos.
    if (auto* sh = proc.apvts.getParameter (pid::SHAPE))  sh->setValueNotifyingHost (0.66f);
    if (auto* m  = proc.apvts.getParameter (pid::MOTION)) m->setValueNotifyingHost (0.78f);
    if (auto* s  = proc.apvts.getParameter (pid::SMEAR))  s->setValueNotifyingHost (0.50f);
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* pe = dynamic_cast<pulsar::PulsarEditor*> (ed.get());
    REQUIRE (pe != nullptr);
    ed->setBounds (0, 0, ed->getWidth(), ed->getHeight());

    pumpMoving (proc, *pe, 12);
    const int few = litPixels (*ed);
    REQUIRE (few >= 0);

    pumpMoving (proc, *pe, 240);
    const int many = litPixels (*ed);
    REQUIRE (many >= 0);

    // Larga exposición: tras muchos frames de movimiento la forma ACUMULADA ilumina MÁS que con pocos.
    CHECK (many > few);
    // Y algo real se dibujó (no quedó el pozo vacío).
    CHECK (many > 200);
}
