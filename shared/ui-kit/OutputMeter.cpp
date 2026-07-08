#include "OutputMeter.h"
#include <cmath>

namespace ovni::ui
{
// gain (lineal) -> dBFS. Fórmula directa (evita depender de juce_audio_basics en un UI-kit).
static inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (juce::jmax (1.0e-4f, g)); }

// LED de clip: apagado = puntito gris; encendido = ámbar (empujando) -> rojo (clip), con glow.
static void drawClipLed (juce::Graphics& g, juce::Rectangle<float> cell, float clip)
{
    const float d = 9.0f;
    const float on = juce::jlimit (0.0f, 1.0f, clip);
    juce::Rectangle<float> led (0.0f, 0.0f, d, d);
    led.setCentre (cell.getX() + d * 0.5f + 2.0f, cell.getCentreY());

    const juce::Colour onCol = (on < 0.5f) ? theme::amber
                                           : theme::amber.interpolatedWith (theme::red, (on - 0.5f) * 2.0f);
    if (on > 0.02f)                                              // glow al encender
    {
        g.setColour (onCol.withAlpha (0.35f * on));
        g.fillEllipse (led.expanded (4.0f * on));
    }
    g.setColour (juce::Colour (0xff20262e).interpolatedWith (onCol, juce::jlimit (0.0f, 1.0f, on * 2.0f)));
    g.fillEllipse (led);                                        // cuerpo
    g.setColour (theme::line.withAlpha (0.6f));
    g.drawEllipse (led, 0.8f);                                  // aro
    if (on > 0.25f)                                            // specular cuando enciende fuerte
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f * on));
        g.fillEllipse (led.getX() + d * 0.28f, led.getY() + d * 0.22f, d * 0.26f, d * 0.26f);
    }
}

OutputMeter::OutputMeter (std::atomic<float>& peak, std::atomic<float>& clip)
    : peakSrc (peak), clipSrc (clip)
{
    startTimerHz (30);
}
OutputMeter::~OutputMeter() { stopTimer(); }

void OutputMeter::timerCallback()
{
    if (! isShowing()) return;
    const float pk = peakSrc.load (std::memory_order_relaxed);
    peakSm = juce::jmax (pk, peakSm * 0.82f);   // attack inmediato, release suave
    const float cl = clipSrc.load (std::memory_order_relaxed);
    clipSm = juce::jmax (cl, clipSm * 0.90f);   // el LED destella y cae suave
    // CPU: sólo repintar si el meter o el LED cambiaron (en silencio sostenido no repinta).
    if (std::abs (peakSm - lastDrawnPeak) < 1.0e-4f && std::abs (clipSm - lastDrawnClip) < 1.0e-4f)
        return;
    lastDrawnPeak = peakSm; lastDrawnClip = clipSm;
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // ---- modo VERTICAL (mockup pulsar-a §meterCol): LED arriba · barra vertical · caption "OUT" ----
    // Se activa solo en celdas claramente más altas que anchas (columna de utilidad angosta).
    if (b.getHeight() > b.getWidth() * 1.2f)
    {
        paintVertical (g, b);
        return;
    }

    // (el divisor de sección lo dibuja el editor; el meter no pinta su propio borde = sin doble línea)
    auto area = b.reduced (16.0f, 9.0f);

    // --- readout: OUTPUT + dB ---
    const float peakDb = gainToDb (peakSm);
    const bool  hot    = peakDb > -1.0f;
    // El LED toma el clip INSTANTÁNEO (enciende al instante del pico); clipSm sólo extiende la cola del destello.
    const float clip   = juce::jmax (clipSm, clipSrc.load (std::memory_order_relaxed));
    auto row = area.removeFromTop (26.0f);
    auto lbl = row.removeFromLeft (130);
    g.setColour (theme::mut);
    g.setFont (fonts::mono (11.5f));
    g.drawText ("OUTPUT", lbl.removeFromLeft (58).toNearestInt(), juce::Justification::bottomLeft);
    drawClipLed (g, lbl.removeFromLeft (22).withTrimmedTop (6.0f), clip);   // LED de clip junto al label
    g.setColour (hot ? theme::red : theme::txt);
    g.setFont (fonts::mono (21.0f));
    const juce::String val = (peakSm < 1.0e-3f) ? juce::String ("-inf")
                                                : juce::String (peakDb, 1);
    g.drawText (val + " dB", row.toNearestInt(), juce::Justification::bottomRight);

    area.removeFromTop (7.0f);

    // --- barra del meter ---
    auto bar = area.removeFromTop (9.0f);
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (bar, 2.0f);

    const float w01 = juce::jlimit (0.0f, 1.0f, juce::jmap (peakDb, -48.0f, 0.0f, 0.0f, 1.0f));
    if (w01 > 0.001f)
    {
        juce::ColourGradient grad (theme::cyanD, bar.getX(), 0.0f, theme::red, bar.getRight(), 0.0f, false);
        grad.addColour (0.62, theme::cyan);
        grad.addColour (0.84, theme::amber);
        g.setGradientFill (grad);
        g.saveState();
        g.reduceClipRegion (bar.withWidth (bar.getWidth() * w01).getSmallestIntegerContainer());
        g.fillRoundedRectangle (bar, 2.0f);
        g.restoreState();
    }
    // ticks cada 10%
    g.setColour (juce::Colour (0x99000000));
    for (int i = 1; i < 10; ++i)
    {
        const float x = bar.getX() + bar.getWidth() * 0.1f * (float) i;
        g.drawVerticalLine ((int) x, bar.getY(), bar.getBottom());
    }

    area.removeFromTop (8.0f);

    // --- footer perf: latencia (0 = plugins del sello son zero-latency) + sample rate ---
    auto perf = area.removeFromTop (15.0f);
    g.setColour (theme::mut);
    g.setFont (fonts::mono (10.5f));
    g.drawText ("0.0 ms latency", perf.removeFromLeft (140).toNearestInt(), juce::Justification::centredLeft);
    const int sr = (int) sampleRate;
    const juce::String khz = (sr > 0) ? juce::String (sr / 1000.0, (sr % 1000) ? 1 : 0) + " kHz"
                                      : juce::String ("48 kHz");
    g.setColour (juce::Colour (0xff46e4a6));
    g.drawText (khz, perf.toNearestInt(), juce::Justification::centredRight);
}

void OutputMeter::paintVertical (juce::Graphics& g, juce::Rectangle<float> b)
{
    const float peakDb = gainToDb (peakSm);
    const float clip   = juce::jmax (clipSm, clipSrc.load (std::memory_order_relaxed));

    // LED de clip arriba, centrado
    auto ledCell = b.removeFromTop (14.0f);
    drawClipLed (g, ledCell.withSizeKeepingCentre (9.0f, 9.0f).withX (ledCell.getCentreX() - 5.5f), clip);
    b.removeFromTop (4.0f);

    // caption "OUT" abajo
    auto cap = b.removeFromBottom (12.0f);
    g.setColour (theme::fnt);
    g.setFont (fonts::mono (7.5f));
    g.drawText ("OUT", cap.toNearestInt(), juce::Justification::centred);
    b.removeFromBottom (3.0f);

    // barra vertical: pozo inset + hairline + fill cian→ámbar→rojo + peak-hold
    auto bar = b.withSizeKeepingCentre (12.0f, b.getHeight());
    g.setColour (juce::Colour (0xff05070a));
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (theme::lineSoft);
    g.drawRoundedRectangle (bar, 2.0f, 1.0f);

    const float lvl01 = juce::jlimit (0.0f, 1.0f, juce::jmap (peakDb, -48.0f, 0.0f, 0.0f, 1.0f));
    auto in = bar.reduced (1.5f);
    if (lvl01 > 0.001f)
    {
        juce::ColourGradient grad (theme::cyanD, 0.0f, in.getBottom(), theme::red, 0.0f, in.getY(), false);
        grad.addColour (0.62, theme::cyan);
        grad.addColour (0.82, theme::amber);
        g.setGradientFill (grad);
        g.fillRect (in.withTrimmedTop (in.getHeight() * (1.0f - lvl01)));
        // línea de peak-hold
        g.setColour (theme::txt.withAlpha (0.85f));
        g.fillRect (in.getX(), in.getY() + in.getHeight() * (1.0f - lvl01), in.getWidth(), 1.2f);
    }
}
}
