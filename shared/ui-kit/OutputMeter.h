#pragma once
#include "Theme.h"
#include "Fonts.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace ovni::ui
{
// Readout + meter de salida del sello: "OUTPUT" + dB + barra horizontal cian→ámbar→rojo con ticks +
// LED de clip (ámbar→rojo, lee el clip instantáneo) + footer perf. DESACOPLADO del processor: lee
// dos atomics que el plugin actualiza (no conoce ningún PluginProcessor concreto), por lo que sirve
// a cualquier plugin del catálogo.
class OutputMeter : public juce::Component, private juce::Timer
{
public:
    // peak = pico de salida final, lineal 0..1 (post output-gain).
    // clip = "estás empujando" 0..1 (limiter reduciendo, o pico cerca de 0 dBFS) → enciende el LED.
    OutputMeter (std::atomic<float>& peak, std::atomic<float>& clip);
    ~OutputMeter() override;

    // Sample rate para el footer (kHz). Llamar desde el message thread (editor). Display-only.
    void setSampleRate (double sr) noexcept { sampleRate = sr; }

    // Fija los valores suavizados del display (para demos/snapshots; el timer no corre fuera de pantalla).
    void prime (float peak01, float clip01) noexcept { peakSm = peak01; clipSm = clip01; }

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void paintVertical (juce::Graphics&, juce::Rectangle<float>);   // columna angosta (mockup pulsar-a)

    std::atomic<float>& peakSrc;
    std::atomic<float>& clipSrc;
    double sampleRate = 0.0;
    float  peakSm = 0.0f;     // pico suavizado (ataque inmediato, caída lenta)
    float  clipSm = 0.0f;     // LED de clip suavizado (destella y cae suave -> visible)
    float  lastDrawnPeak = -1.0f, lastDrawnClip = -1.0f;  // CPU: no repintar si nada cambió (silencio)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputMeter)
};
}
