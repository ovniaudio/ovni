#pragma once
// LfoBank — moduladores sync al tempo (Phase B). 4 LFOs asignables; cada uno late a una división del BeatClock
// (1/4, 1 bar, etc.), con forma y profundidad, y modula un param destino. Puro (sin JUCE): el editor lee
// valueFor(phaseInBeats) y suma depth·valor al param base. Testeable con fases sintéticas.
#include <cmath>
#include <string>

namespace supernova
{
enum class LfoShape : int { Sine = 0, Triangle, Saw, Square, RampDown, SampleHold };

struct LfoSlot
{
    bool        enabled       = false;
    float       beatsPerCycle = 4.0f;    // 4 = 1 compás, 1 = 1 negra, 0.25 = semicorchea…
    LfoShape    shape         = LfoShape::Sine;
    float       depth         = 0.5f;    // 0..1 — cuánto modula (se escala al rango del destino en el editor)
    float       phaseOffset   = 0.0f;    // 0..1 — corrimiento de fase
    bool        bipolar       = true;    // true: −1..+1 alrededor del valor base; false: 0..1
    std::string target;                  // param id destino (vacío = sin asignar)
};

class LfoBank
{
public:
    static constexpr int kNum = 4;

    LfoSlot&       slot (int i) noexcept { return slots_[i < 0 ? 0 : (i >= kNum ? kNum - 1 : i)]; }
    const LfoSlot& slot (int i) const noexcept { return slots_[i < 0 ? 0 : (i >= kNum ? kNum - 1 : i)]; }

    // Forma de onda: fase 0..1 → salida. Unipolar 0..1 (Square/SampleHold/etc. definidas coherentes).
    static float wave (LfoShape s, double phase01) noexcept
    {
        double p = phase01 - std::floor (phase01);
        switch (s)
        {
            case LfoShape::Sine:      return 0.5f + 0.5f * (float) std::sin (p * 6.283185307179586);
            case LfoShape::Triangle:  return (float) (p < 0.5 ? p * 2.0 : 2.0 - p * 2.0);
            case LfoShape::Saw:       return (float) p;                       // rampa sube
            case LfoShape::RampDown:  return (float) (1.0 - p);               // rampa baja
            case LfoShape::Square:    return p < 0.5 ? 1.0f : 0.0f;
            case LfoShape::SampleHold: return hash01 (std::floor (phase01));  // escalón por ciclo (estable)
        }
        return 0.0f;
    }

    // Valor MODULADOR de un slot dada la posición en beats del BeatClock. bipolar → [−depth, +depth];
    // unipolar → [0, depth]. Slot deshabilitado o sin destino → 0 (identidad).
    float valueFor (int i, double phaseInBeats) const noexcept
    {
        const LfoSlot& s = slot (i);
        if (! s.enabled || s.target.empty() || s.beatsPerCycle <= 1e-6f) return 0.0f;
        const double cyc = phaseInBeats / (double) s.beatsPerCycle + (double) s.phaseOffset;
        const float  w   = wave (s.shape, cyc);                 // 0..1
        return s.bipolar ? (w * 2.0f - 1.0f) * s.depth : w * s.depth;
    }

    // Persistencia compacta: "en,beats,shape,depth,off,bip,target;" por slot.
    std::string serialize() const
    {
        std::string out;
        for (const auto& s : slots_)
        {
            out += (s.enabled ? '1' : '0'); out += ',';
            out += fmt (s.beatsPerCycle) + ',';
            out += std::to_string ((int) s.shape) + ',';
            out += fmt (s.depth) + ',';
            out += fmt (s.phaseOffset) + ',';
            out += (s.bipolar ? '1' : '0'); out += ',';
            out += s.target; out += ';';
        }
        return out;
    }
    void deserialize (const std::string& in)
    {
        for (auto& s : slots_) s = LfoSlot {};
        size_t pos = 0; int idx = 0;
        while (idx < kNum)
        {
            size_t semi = in.find (';', pos);
            if (semi == std::string::npos) break;
            std::string rec = in.substr (pos, semi - pos);
            pos = semi + 1;
            parseSlot (rec, slots_[idx]);
            ++idx;
        }
    }

private:
    static float hash01 (double n) noexcept
    { double s = std::sin (n * 127.1 + 3.7) * 43758.5453; return (float) (s - std::floor (s)); }

    static std::string fmt (float v) { std::string s = std::to_string (v); return s; }

    static void parseSlot (const std::string& rec, LfoSlot& s)
    {
        // en,beats,shape,depth,off,bip,target
        size_t p = 0; int field = 0; std::string tok;
        auto next = [&] (std::string& out) -> bool {
            size_t c = rec.find (',', p);
            if (c == std::string::npos) { out = rec.substr (p); p = rec.size(); return ! out.empty() || field == 6; }
            out = rec.substr (p, c - p); p = c + 1; return true;
        };
        std::string f;
        try {
            if (next (f)) s.enabled = (f == "1");
            if (next (f) && ! f.empty()) s.beatsPerCycle = std::stof (f);
            if (next (f) && ! f.empty()) s.shape = (LfoShape) std::stoi (f);
            if (next (f) && ! f.empty()) s.depth = std::stof (f);
            if (next (f) && ! f.empty()) s.phaseOffset = std::stof (f);
            if (next (f)) s.bipolar = (f == "1");
            std::string t; next (t); s.target = t;
        } catch (...) { s = LfoSlot {}; }
        (void) field; (void) tok;
    }

    LfoSlot slots_[kNum];
};
}
