#include "VisualizerBase.h"

namespace ovni::ui
{
VisualizerBase::VisualizerBase (int fps)
{
    startTimerHz (juce::jlimit (1, 120, fps));
}
VisualizerBase::~VisualizerBase() { stopTimer(); }

void VisualizerBase::resized()
{
    // Rehornear con el nuevo tamaño en el próximo paint (ahí tenemos la escala física real).
    staticDirty = true;
}

void VisualizerBase::ensureStaticLayer (juce::Graphics& g)
{
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;

    const float scale = juce::jmax (1.0f, (float) g.getInternalContext().getPhysicalPixelScaleFactor());
    const int   pw    = juce::jmax (1, juce::roundToInt ((float) w * scale));
    const bool  need  = staticDirty || staticLayer.isNull()
                         || std::abs (staticScale - scale) > 0.01f
                         || staticLayer.getWidth() != pw;
    if (! need) return;

    staticScale = scale;
    const int ph = juce::jmax (1, juce::roundToInt ((float) h * scale));
    staticLayer = juce::Image (juce::Image::ARGB, pw, ph, true);
    juce::Graphics sg (staticLayer);
    sg.addTransform (juce::AffineTransform::scale (scale));   // dibujo lógico, render físico (nítido)
    renderStatic (sg, w, h);
    staticDirty = false;
}

void VisualizerBase::paint (juce::Graphics& g)
{
    ensureStaticLayer (g);
    if (staticLayer.isValid())
        g.drawImageTransformed (staticLayer, juce::AffineTransform::scale (1.0f / staticScale));
    paintLive (g);
}

void VisualizerBase::timerCallback()
{
    if (! isShowing()) return;                  // CPU: no animar con la ventana cerrada

    // REDUCED MOTION (accesibilidad): si el sistema/usuario pide menos animación, NO animamos. Avanzamos un
    // único frame coherente (estado actual de las macros) y pausamos el repaint → el visualizador queda quieto
    // pero legible, sin movimiento que maree. Reanuda si se reactiva la animación.
    if (prefersReducedMotion())
    {
        const bool wasReduced = reducedMotion;
        reducedMotion = true;
        if (! wasReduced) { advanceFrame(); repaint(); }   // un frame estático coherente, una sola vez
        return;
    }
    if (reducedMotion) { reducedMotion = false; settleFrames = 0; }   // se reactivó la animación

    const bool changed = advanceFrame();
    settleFrames = changed ? 0 : (settleFrames + 1);
    if (settleFrames > settleHold) return;      // todo quieto y la cola ya colapsó -> sin repaint
    repaint();
}
}
