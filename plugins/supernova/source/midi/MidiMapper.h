#pragma once
#include <cstdint>

// MidiMapper — C++ PURO (sin JUCE): un mensaje MIDI crudo → evento de trigger tipado (RF5). Testeable con
// bytes sintéticos, sin estado global. Layout de teclado (contrato por NÚMERO de nota; los nombres C-x son
// convención C3=60): 24–35 = rayo direccional (12 semitonos → 12 direcciones), 36–47 = explosión
// omnidireccional, 48–71 = cambio de preset, 72–87 = cue del tile 1..16 de la sesión, 88/89/90 = foto
// siguiente / anterior / aleatoria, Program-Change → preset. Release (NoteOff / vel 0) → None (los triggers
// son edge-on-attack). Canal ignorado.
namespace supernova
{
enum class MidiTriggerType : uint8_t { None, Explosion, DirectionalRay, PresetChange, PhotoCue };

// Qué pide un PhotoCue: un tile concreto de la sesión, o un movimiento relativo (el VJ toca el pad sin
// mirar la tira, que en inmersivo/fullscreen no está en pantalla).
enum class PhotoCueKind : uint8_t { Tile = 0, Next, Prev, Random };

// POD de salida (viaja por la cola SPSC; trivially-copyable).
struct MidiTriggerEvent
{
    MidiTriggerType type = MidiTriggerType::None;
    float angle    = 0.0f;   // rad CCW desde +X (solo DirectionalRay)
    float strength = 0.0f;   // 0..1 desde la velocity (Explosion/DirectionalRay)
    int   preset   = -1;     // índice de preset (solo PresetChange)
    bool  fromProgramChange = false;   // true si vino de un Program-Change (no de una nota)
    PhotoCueKind cue = PhotoCueKind::Tile;   // qué cue pide (solo PhotoCue)
    int   photo    = -1;     // tile 0-based (solo PhotoCue con cue == Tile; los relativos van en -1)

    bool isValid() const noexcept { return type != MidiTriggerType::None; }
};

class MidiMapper
{
public:
    // Límites inyectables → sin constantes globales, testeable con el presetCount real. Si algún DAW numera la
    // octava corrida, se ajustan los rangos acá sin tocar la lógica.
    struct Config
    {
        int rayLo = 24, rayHi = 35;
        int expLo = 36, expHi = 47;
        int prsLo = 48, prsHi = 71;
        int presetCount = 24;   // = (int) ovni::presets::factoryPresets().size() en el plugin
        // CUE DE FOTOS (ronda 3): 16 tiles seguidos + 3 teclas relativas. 16 entra justo en un pad de 4×4.
        // Van DESPUÉS de presetCount a propósito: hay tests (y podría haber código) que arman este Config
        // por posición — meterlos en el medio les cambiaría el significado de los campos en silencio.
        int cueLo = 72, cueHi = 87;
        int cueNext = 88, cuePrev = 89, cueRandom = 90;
    };

    MidiMapper() noexcept {}                          // usa los defaults de Config
    explicit MidiMapper (Config c) noexcept : cfg (c) {}

    // Núcleo: UN mensaje MIDI (status + 2 data bytes) → evento. Pura, const, sin allocations ni estado mutable.
    MidiTriggerEvent map (uint8_t status, uint8_t data1, uint8_t data2) const noexcept;

    // Conveniencia para el plugin (canal ignorado; velocity 0 → None).
    MidiTriggerEvent mapNoteOn (int noteNumber, int velocity) const noexcept;

private:
    MidiTriggerEvent mapNote (int note, int vel) const noexcept;   // reparte por rango
    Config cfg;
};
}
