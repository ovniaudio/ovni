#pragma once
// UndoStack — deshacer/rehacer genérico (Phase C · UX). RANDOM/CLEAR eran puertas de UNA vía → el usuario
// dejaba de explorar por miedo a perder su look. Snapshot-based: se captura el estado ANTES de una acción
// destructiva; undo lo restaura y manda el actual a redo. Puro (template, sin JUCE) → testeable con strings.
#include <vector>
#include <optional>

namespace supernova
{
template <typename Snapshot>
class UndoStack
{
public:
    explicit UndoStack (int maxDepth = 32) : maxDepth_ (maxDepth < 1 ? 1 : maxDepth) {}

    // Registra el estado ANTES de una acción destructiva (RANDOM/CLEAR/preset/escena). Limpia el redo (una
    // nueva rama invalida el futuro rehecho, semántica de editor estándar).
    void push (const Snapshot& before)
    {
        undo_.push_back (before);
        if ((int) undo_.size() > maxDepth_) undo_.erase (undo_.begin());   // depth acotada
        redo_.clear();
    }

    bool canUndo() const noexcept { return ! undo_.empty(); }
    bool canRedo() const noexcept { return ! redo_.empty(); }

    // Deshacer: pasá el estado ACTUAL (va a redo) y recibí el estado a restaurar. nullopt si no hay nada.
    std::optional<Snapshot> undo (const Snapshot& current)
    {
        if (undo_.empty()) return std::nullopt;
        redo_.push_back (current);
        Snapshot s = undo_.back(); undo_.pop_back();
        return s;
    }

    // Rehacer: simétrico.
    std::optional<Snapshot> redo (const Snapshot& current)
    {
        if (redo_.empty()) return std::nullopt;
        undo_.push_back (current);
        Snapshot s = redo_.back(); redo_.pop_back();
        return s;
    }

    void clear() noexcept { undo_.clear(); redo_.clear(); }
    std::size_t undoDepth() const noexcept { return undo_.size(); }
    std::size_t redoDepth() const noexcept { return redo_.size(); }

private:
    std::vector<Snapshot> undo_, redo_;
    int maxDepth_;
};
}
