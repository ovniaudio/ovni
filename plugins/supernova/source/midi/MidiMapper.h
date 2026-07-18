#pragma once
#include <cstdint>

// MidiMapper — C++ PURO (sin JUCE): un mensaje MIDI crudo → evento de trigger tipado (RF5). Testeable con
// bytes sintéticos, sin estado global. Layout de teclado (contrato por NÚMERO de nota; los nombres C-x son
// convención C3=60): 24–35 = rayo direccional (12 semitonos → 12 direcciones), 36–47 = explosión
// omnidireccional, 48–71 = cambio de preset, Program-Change → preset. Release (NoteOff / vel 0) → None
// (los triggers son edge-on-attack). Canal ignorado.
namespace supernova
{
enum class MidiTriggerType : uint8_t { None, Explosion, DirectionalRay, PresetChange };

// POD de salida (viaja por la cola SPSC; trivially-copyable).
struct MidiTriggerEvent
{
    MidiTriggerType type = MidiTriggerType::None;
    float angle    = 0.0f;   // rad CCW desde +X (solo DirectionalRay)
    float strength = 0.0f;   // 0..1 desde la velocity (Explosion/DirectionalRay)
    int   preset   = -1;     // índice de preset (solo PresetChange)
    bool  fromProgramChange = false;   // true si vino de un Program-Change (no de una nota)

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
