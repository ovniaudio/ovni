#pragma once

#include "ui-kit/VisualizerBase.h"
#include "ui-kit/Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>   // RangedAudioParameter (la nube ES el control)
#include <array>
#include <atomic>

// =============================================================================
// NÉBULA — LA NUBE VOLUMÉTRICA (el gancho visual vivo + control principal; rediseño
// mockup nebula-a). Subclase de la base del sello (ovni::ui::VisualizerBase): cache
// px-físico + animación a fps + pausa-CPU GRATIS. Acá sólo va el gancho de NÉBULA:
//   · capa ESTÁTICA (NebulaCloudStatic.h): campo nebular profundo, estrellas baked,
//     anillos con glow, halo de cuenca, vignette, rim-shadow, scanlines.
//   · capa VIVA: bloom de la fuente/partículas en 3 capas (SPRITE cacheado por bucket
//     de tinte — nada de gradientes recalculados por frame), motas de polvo que respiran
//     y titilan (LCG determinístico para las posiciones), piso de luminosidad ~0.62, +
//     telemetría mono en las 4 esquinas (valores REALES de los atomics).
//
// Lee telemetría lock-free del processor (size/decay/tone/breath) — NO conoce el
// processor concreto, sólo 4 atomics, por lo que queda desacoplado del DSP.
//
// Mapeo física→imagen (≠ PULSAR, que es un punto/partícula que se mueve):
//   · Radio base de la nube    = Size   (espacio chico → cosmos enorme)
//   · Densidad / poblado       = Decay  (más cola = nube más densa/poblada)
//   · Tinte / profundidad      = Tone   (más damping → magenta más profundo/apagado)
//   · Expansión-contracción    = Breath (inhala/exhala lento; 2 LFOs incoherentes; nunca quieta)
//
// Control mixto: la nube respira sola y reacciona a TODAS las macros; y se ESCULPE
// arrastrando — NO hay cursor ni partícula central: la nube ES el control. Drag 1:1:
//   horizontal = Size (radio) · vertical = Decay (densidad). Knob y drag enlazados
//   (grueso con la mano, fino con la perilla) vía los params del APVTS, como StellarPad.
// =============================================================================
namespace nebula::ui
{

class NebulaCloud : public ovni::ui::VisualizerBase
{
public:
    // Telemetría (lee) + los params size/decay (escribe en el drag). Todos los atomics en 0..1.
    NebulaCloud (std::atomic<float>& size,
                 std::atomic<float>& decay,
                 std::atomic<float>& tone,
                 std::atomic<float>& breath,
                 juce::RangedAudioParameter* sizeParam,
                 juce::RangedAudioParameter* decayParam);
    ~NebulaCloud() override = default;

    // Capacidad del banco de motas de polvo (la cantidad VISIBLE ∝ Decay; ver el .cpp). Público para que
    // la constante de densidad del .cpp lo use como tope. (mockup DUST_N = 240.)
    static constexpr int kMotes = 240;

protected:
    void renderStatic (juce::Graphics& g, int w, int h) override;
    void paintLive (juce::Graphics& g) override;
    bool advanceFrame() override;

    // Esculpir: NO hay cursor — la nube ES el control (X = Size · Y = Decay), drag 1:1.
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

private:
    std::atomic<float>& sizeSrc;
    std::atomic<float>& decaySrc;
    std::atomic<float>& toneSrc;
    std::atomic<float>& breathSrc;
    juce::RangedAudioParameter* sizeP   = nullptr;
    juce::RangedAudioParameter* decayP  = nullptr;

    // Estado cacheado (reflejado en el dibujo). Suavizado para que knob/drag muevan la nube sin saltos.
    float sizeNow = 0.5f, decayNow = 0.5f, toneNow = 0.4f, breathNow = 0.25f;
    float sizeSm = 0.5f, decaySm = 0.5f, toneSm = 0.4f, breathSm = 0.25f;
    float densitySm = 0.5f;   // densidad observada (∝ Decay, suavizada) — la telemetría DENS

    // Respiración: 2 LFOs incoherentes (mockup §step) → inhala/exhala orgánico; centrada en 0.5.
    float lfoA = 0.0f, lfoB = 0.0f;
    float breathPhase01 = 0.5f;   // [0,1] valor de respiración suavizado (lo refleja el dibujo + tele)

    // Motas de polvo: posición en disco unidad + semilla de fase propia (titilar/derivar). Cantidad
    // VISIBLE ∝ decay; radio del disco ∝ size·respiración. (mockup §buildDustGeometry, LCG fijo.)
    struct Mote { float ang = 0.0f, rad = 0.0f, sz = 1.0f, base = 0.75f,
                        ph1 = 0.0f, ph2 = 0.0f, tw = 0.0f, twRate = 1.0f; };
    Mote motes[kMotes] = {};
    void buildMotes();

    // Mapea Tone → tinte de la nube (claro/lila .. magenta profundo/apagado).
    juce::Colour cloudTint (float tone01, float alpha) const noexcept;

    // --- bloom: sprite de glow cacheado por bucket de TINTE (nada de gradientes por frame, como StellarPad) ---
    static constexpr int kTintBuckets = 12;
    std::array<juce::Image, kTintBuckets + 1> bloomSprites;
    const juce::Image& bloomSpriteFor (float tone01);

    void paintBloom (juce::Graphics& g, int w, int h);
    void paintMotes (juce::Graphics& g, int w, int h);
    void paintTelemetry (juce::Graphics& g, int w, int h) const;
    void refreshTelemetry();

    // telemetría (strings cacheadas; se refrescan cada ~0.2 s, mockup §tele).
    int teleCountdown = 0;
    juce::String teleDens, teleRt, teleSize, teleBreath, teleTone, teleMotes;
    float teleToneVal = 0.4f;

    bool dragging = false;   // gesto del drag (begin/end gesture)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NebulaCloud)
};

} // namespace nebula::ui
