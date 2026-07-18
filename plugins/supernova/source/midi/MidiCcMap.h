#pragma once
// MidiCcMap — MIDI-LEARN: mapa CC (control change) → parámetro (Phase B · control en vivo). Hoy MidiMapper solo
// mapea NOTAS a triggers y DESCARTA los CC; esto los captura y los rutea a CUALQUIER knob/macro. Vive en el
// MESSAGE THREAD (el editor drena una cola lock-free de CC crudos y consulta este mapa) → puede usar std/heap.
// Puro (sin JUCE), testeable. La persistencia serializa a un string plano que va al ValueTree del plugin.
#include <string>
#include <vector>
#include <optional>
#include <sstream>

namespace supernova
{
struct CcHit { std::string paramId; float value01 = 0.0f; };   // param destino + valor normalizado 0..1

class MidiCcMap
{
public:
    // Arma "learn": el PRÓXIMO Cc entrante queda asignado a este param (con su rango destino). El editor lo
    // dispara con click-derecho → "MIDI learn" sobre un knob.
    void armLearn (const std::string& paramId, float lo = 0.0f, float hi = 1.0f) noexcept
    { learning_ = true; armParam_ = paramId; armLo_ = lo; armHi_ = hi; }
    void cancelLearn() noexcept { learning_ = false; armParam_.clear(); }
    bool isLearning() const noexcept { return learning_; }
    const std::string& learningParam() const noexcept { return armParam_; }

    // Procesa un CC entrante (value 0..127). Si está en learn → lo BINDEA al param armado (un CC controla UN
    // param; un param lo controla UN CC → se limpian colisiones) y devuelve el hit. Si no, si el CC está
    // asignado → devuelve el hit resuelto. Si no → nullopt.
    std::optional<CcHit> feed (int cc, int value7)
    {
        const float v = (float) (value7 < 0 ? 0 : (value7 > 127 ? 127 : value7)) / 127.0f;
        if (learning_)
        {
            assign (cc, armParam_, armLo_, armHi_);
            const std::string p = armParam_;
            const float lo = armLo_, hi = armHi_;
            learning_ = false; armParam_.clear();
            return CcHit { p, lo + (hi - lo) * v };
        }
        for (const auto& a : assigns_)
            if (a.cc == cc)
                return CcHit { a.paramId, a.lo + (a.hi - a.lo) * v };
        return std::nullopt;
    }

    // Asignación directa (un CC ↔ un param). Limpia cualquier uso previo de ese CC o de ese param.
    void assign (int cc, const std::string& paramId, float lo = 0.0f, float hi = 1.0f)
    {
        clearCc (cc);
        clearParam (paramId);
        assigns_.push_back ({ cc, paramId, lo, hi });
    }
    void clearCc (int cc)
    {
        for (size_t i = 0; i < assigns_.size(); ++i)
            if (assigns_[i].cc == cc) { assigns_.erase (assigns_.begin() + (long) i); return; }
    }
    void clearParam (const std::string& paramId)
    {
        for (size_t i = 0; i < assigns_.size(); ++i)
            if (assigns_[i].paramId == paramId) { assigns_.erase (assigns_.begin() + (long) i); return; }
    }
    void clearAll() { assigns_.clear(); learning_ = false; armParam_.clear(); }

    int ccForParam (const std::string& paramId) const   // -1 si no está asignado
    {
        for (const auto& a : assigns_) if (a.paramId == paramId) return a.cc;
        return -1;
    }
    std::string paramForCc (int cc) const
    {
        for (const auto& a : assigns_) if (a.cc == cc) return a.paramId;
        return {};
    }
    size_t size() const noexcept { return assigns_.size(); }

    // Persistencia: "cc,param,lo,hi;cc,param,lo,hi;..." (el param no lleva comas ni ; por contrato APVTS id).
    std::string serialize() const
    {
        std::ostringstream os;
        for (const auto& a : assigns_) os << a.cc << ',' << a.paramId << ',' << a.lo << ',' << a.hi << ';';
        return os.str();
    }
    void deserialize (const std::string& s)
    {
        clearAll();
        std::stringstream ss (s);
        std::string rec;
        while (std::getline (ss, rec, ';'))
        {
            if (rec.empty()) continue;
            std::stringstream rs (rec);
            std::string ccStr, param, loStr, hiStr;
            if (! std::getline (rs, ccStr, ',')) continue;
            if (! std::getline (rs, param, ',')) continue;
            std::getline (rs, loStr, ',');
            std::getline (rs, hiStr, ',');
            try {
                const int cc = std::stoi (ccStr);
                const float lo = loStr.empty() ? 0.0f : std::stof (loStr);
                const float hi = hiStr.empty() ? 1.0f : std::stof (hiStr);
                if (! param.empty()) assigns_.push_back ({ cc, param, lo, hi });
            } catch (...) { /* registro corrupto → se omite */ }
        }
    }

private:
    struct Assign { int cc; std::string paramId; float lo, hi; };
    std::vector<Assign> assigns_;
    bool        learning_ = false;
    std::string armParam_;
    float       armLo_ = 0.0f, armHi_ = 1.0f;
};
}
