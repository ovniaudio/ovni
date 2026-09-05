#pragma once
// SceneBank — banco de ESCENAS/snapshots del usuario (Phase B · performance). Guardás TU estado exacto (los 30
// params morphables) como "Escena N" y lo lanzás con click/nota/CC; el CROSSFADE al recuperar reusa el
// PresetMorph que ya existe (mismo ease A→B). Puro (sin JUCE), testeable. Persiste al ValueTree del plugin.
#include "presets/PresetMorph.h"     // MorphSnapshot (30 params morphables)
#include "util/LocaleSafeNumber.h"   // to_chars/from_chars: la persistencia no depende del locale
#include <string>
#include <sstream>

namespace supernova
{
class SceneBank
{
public:
    static constexpr int kNum = 8;   // 8 escenas (grid tipo clip-launch)

    void save (int slot, const MorphSnapshot& s) noexcept
    {
        if (slot < 0 || slot >= kNum) return;
        scenes_[slot] = s; used_[slot] = true;
    }
    bool has (int slot) const noexcept { return slot >= 0 && slot < kNum && used_[slot]; }
    // Devuelve el snapshot guardado (el editor arranca el crossfade con morph.start(live, recall(slot))).
    MorphSnapshot recall (int slot) const noexcept
    { return (slot >= 0 && slot < kNum) ? scenes_[slot] : MorphSnapshot {}; }
    void clear (int slot) noexcept { if (slot >= 0 && slot < kNum) used_[slot] = false; }
    void clearAll() noexcept { for (int i = 0; i < kNum; ++i) used_[i] = false; }

    int count() const noexcept { int n = 0; for (int i = 0; i < kNum; ++i) n += used_[i] ? 1 : 0; return n; }

    // Persistencia: por slot ocupado "slot:v0,v1,...,v29;".
    std::string serialize() const
    {
        std::string out;
        for (int i = 0; i < kNum; ++i)
        {
            if (! used_[i]) continue;
            out += num::toString (i) + ':';
            for (int k = 0; k < MorphSnapshot::N; ++k)
                out += num::toString (scenes_[i].v[k]) + (k + 1 < MorphSnapshot::N ? ',' : ';');
        }
        return out;
    }
    void deserialize (const std::string& in)
    {
        clearAll();
        std::stringstream ss (in);
        std::string rec;
        while (std::getline (ss, rec, ';'))
        {
            if (rec.empty()) continue;
            const size_t colon = rec.find (':');
            if (colon == std::string::npos) continue;
            const int slot = num::toInt (rec.substr (0, colon), -1);
            if (slot < 0 || slot >= kNum) continue;                    // registro corrupto → se omite
            std::stringstream vs (rec.substr (colon + 1));
            std::string tok; int k = 0;
            MorphSnapshot s;
            while (std::getline (vs, tok, ',') && k < MorphSnapshot::N)
            { if (! tok.empty()) s.v[k] = num::toFloat (tok, s.v[k]); ++k; }
            if (k >= MorphSnapshot::N) { scenes_[slot] = s; used_[slot] = true; }
        }
    }

private:
    MorphSnapshot scenes_[kNum];
    bool          used_[kNum] = { false };
};
}
