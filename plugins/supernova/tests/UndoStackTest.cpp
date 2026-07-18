// [supernova][undo] — UndoStack: deshacer/rehacer genérico. Puro, sin GPU.
#include <catch2/catch_test_macros.hpp>
#include <string>
#include "presets/UndoStack.h"

using supernova::UndoStack;

TEST_CASE ("undo: push registra el estado previo; undo lo restaura", "[supernova][undo]")
{
    UndoStack<std::string> h;
    REQUIRE_FALSE (h.canUndo());
    // Estado "A" → hago RANDOM → "B". Antes del RANDOM capturo "A".
    h.push ("A");
    REQUIRE (h.canUndo());
    auto restored = h.undo ("B");        // el actual es "B"
    REQUIRE (restored.has_value());
    REQUIRE (*restored == "A");
    REQUIRE (h.canRedo());
    REQUIRE_FALSE (h.canUndo());
}

TEST_CASE ("undo: redo re-aplica lo deshecho", "[supernova][undo]")
{
    UndoStack<std::string> h;
    h.push ("A");
    auto a = h.undo ("B");               // volvimos a A, redo tiene B
    REQUIRE (*a == "A");
    auto b = h.redo ("A");               // rehacer → B
    REQUIRE (b.has_value());
    REQUIRE (*b == "B");
    REQUIRE (h.canUndo());
    REQUIRE_FALSE (h.canRedo());
}

TEST_CASE ("undo: una nueva acción limpia el redo (rama nueva)", "[supernova][undo]")
{
    UndoStack<std::string> h;
    h.push ("A");
    h.undo ("B");                        // redo = [B]
    REQUIRE (h.canRedo());
    h.push ("A");                        // nueva acción destructiva → el futuro rehecho muere
    REQUIRE_FALSE (h.canRedo());
}

TEST_CASE ("undo: multi-nivel respeta el orden LIFO", "[supernova][undo]")
{
    UndoStack<std::string> h;
    h.push ("s0"); h.push ("s1"); h.push ("s2");   // 3 acciones
    REQUIRE (h.undoDepth() == 3);
    REQUIRE (*h.undo ("s3") == "s2");
    REQUIRE (*h.undo ("s2") == "s1");
    REQUIRE (*h.undo ("s1") == "s0");
    REQUIRE_FALSE (h.canUndo());
}

TEST_CASE ("undo: la profundidad está acotada (descarta lo más viejo)", "[supernova][undo]")
{
    UndoStack<std::string> h (3);
    h.push ("a"); h.push ("b"); h.push ("c"); h.push ("d");   // 'a' se descarta
    REQUIRE (h.undoDepth() == 3);
    REQUIRE (*h.undo ("e") == "d");
    REQUIRE (*h.undo ("d") == "c");
    REQUIRE (*h.undo ("c") == "b");
    REQUIRE_FALSE (h.canUndo());          // 'a' ya no está
}

TEST_CASE ("undo: undo/redo vacíos devuelven nullopt", "[supernova][undo]")
{
    UndoStack<std::string> h;
    REQUIRE_FALSE (h.undo ("x").has_value());
    REQUIRE_FALSE (h.redo ("x").has_value());
}
