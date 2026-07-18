#include "midi/MidiMapper.h"

namespace supernova
{
namespace
{
constexpr float kTwoPi = 6.283185307179586f;
inline int clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
}

MidiTriggerEvent MidiMapper::mapNote (int note, int vel) const noexcept
{
    MidiTriggerEvent e;
    const float strength = (float) clampi (vel, 0, 127) / 127.0f;

    if (note >= cfg.rayLo && note <= cfg.rayHi)
    {
        e.type     = MidiTriggerType::DirectionalRay;
        e.angle    = (float) (note - cfg.rayLo) * (kTwoPi / 12.0f);   // 12 semitonos → 12 direcciones
        e.strength = strength;
    }
    else if (note >= cfg.expLo && note <= cfg.expHi)
    {
        e.type     = MidiTriggerType::Explosion;
        e.strength = strength;
    }
    else if (note >= cfg.prsLo && note <= cfg.prsHi)
    {
        e.type   = MidiTriggerType::PresetChange;
        e.preset = clampi (note - cfg.prsLo, 0, cfg.presetCount - 1);
    }
    // fuera de rango → None (default)
    return e;
}

MidiTriggerEvent MidiMapper::map (uint8_t status, uint8_t data1, uint8_t data2) const noexcept
{
    const uint8_t hi = status & 0xF0;   // tipo (nibble alto); canal (nibble bajo) IGNORADO

    if (hi == 0x90 && data2 > 0)   return mapNote ((int) data1, (int) data2);   // NoteOn con velocity>0
    if (hi == 0x90 || hi == 0x80)  return {};                                    // NoteOn vel0 / NoteOff → None

    if (hi == 0xC0)                                                              // Program-Change
    {
        MidiTriggerEvent e;
        e.type   = MidiTriggerType::PresetChange;
        e.preset = clampi ((int) data1, 0, cfg.presetCount - 1);
        e.fromProgramChange = true;
        return e;
    }
    return {};   // CC / pitch-bend / etc → None
}

MidiTriggerEvent MidiMapper::mapNoteOn (int noteNumber, int velocity) const noexcept
{
    if (velocity <= 0) return {};
    return mapNote (noteNumber, velocity);
}
}
