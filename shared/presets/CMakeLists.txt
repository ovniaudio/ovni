# ovni_presets — infra de presets del sello OVNI (PresetManager + ABState + tipos).
# Headless: sólo depende de juce_audio_processors (sin GUI). Cada plugin provee su tabla
# `factoryPresets()`. Lo consumen el template (shared/template) y cada plugin. S5 hace
# `add_subdirectory(shared/presets)` desde el CMake raíz y agrega `shared/` a los include dirs.
add_library(ovni_presets STATIC
    PresetManager.cpp
    ABState.cpp)

# Permite `#include "presets/PresetManager.h"` (relativo al raíz de shared/) — convención del CONTRACT.
target_include_directories(ovni_presets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_features(ovni_presets PUBLIC cxx_std_20)

# Compilar NUESTRAS fuentes con los headers/defs de JUCE, SIN arrastrar las "unity sources" de los módulos
# al .a. Si linkeáramos `PUBLIC juce::juce_audio_processors`, JUCE adjunta sus fuentes como INTERFACE
# sources y se compilarían DENTRO de cada lib estática (.a de ~100MB + JUCE recompilado por módulo). En su
# lugar tomamos sólo include-dirs + defs por genex; el target final del plugin (juce_add_plugin) linkea
# JUCE y compila su unity UNA sola vez. (Ver docs/CONTRACT-CHANGES.md.)
target_include_directories(ovni_presets PRIVATE
    $<TARGET_PROPERTY:juce::juce_audio_processors,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(ovni_presets PRIVATE
    $<TARGET_PROPERTY:juce::juce_audio_processors,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(ovni_presets INTERFACE juce::juce_audio_processors)
