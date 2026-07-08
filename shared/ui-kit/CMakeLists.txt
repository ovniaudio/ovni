# =============================================================================
# ovni_uikit — UI-kit del SELLO OVNI (namespace ovni::ui). Curado desde ÓRBITA source/ui.
# Lo consumen el template (shared/template) y cada plugin. S5 hace `add_subdirectory(shared/ui-kit)`
# desde el CMake raíz y agrega `shared/` a los include dirs. Ver ../../CONTRACT.md (regla 5).
# Asume que los targets de JUCE ya existen (los pinea S5, o el harness de QA en tests/).
# =============================================================================

# --- Fuentes del sello embebidas (Clash Grotesk / General Sans / JetBrains Mono) -------------
# BinaryData con HEADER/NAMESPACE únicos (OvniUikitData) -> NO colisiona con el `BinaryData` que
# cualquier plugin del catálogo genere para sus propios assets.
file(GLOB OVNI_UIKIT_FONTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/*.ttf")
juce_add_binary_data(ovni_uikit_assets
    HEADER_NAME "OvniUikitData.h"
    NAMESPACE   "OvniUikitData"
    SOURCES     ${OVNI_UIKIT_FONTS})
set_target_properties(ovni_uikit_assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# --- La librería (headers Controls/Panel/Gallery son header-only; .cpp compilan acá) ---------
add_library(ovni_uikit STATIC
    Fonts.cpp
    KnobLookAndFeel.cpp
    VisualizerBase.cpp
    OutputMeter.cpp)

target_compile_features(ovni_uikit PUBLIC cxx_std_20)

# Consumidores incluyen `#include "ui-kit/Theme.h"` (relativo al raíz de shared/, convención del
# CONTRACT regla 3 — mismo patrón que `presets/…` y `dsp/…`). El CONTRACT escribió "ui/Theme.h";
# la carpeta es `ui-kit/` -> ver docs/CONTRACT-CHANGES.md. Internamente los headers se incluyen
# same-dir ("Theme.h").
target_include_directories(ovni_uikit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Compilar NUESTRAS fuentes con los headers/defs de JUCE, SIN arrastrar las "unity sources" de los
# módulos al .a (patrón [S3] del CONTRACT-CHANGES). El target final del plugin (juce_add_plugin)
# linkea JUCE y compila su unity UNA sola vez. El include root de JUCE es común a todos los módulos.
foreach (_jmod juce_gui_basics juce_audio_processors)
    target_include_directories(ovni_uikit PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(ovni_uikit PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_COMPILE_DEFINITIONS>)
endforeach ()

# Fonts.cpp necesita OvniUikitData.h en compile-time (su símbolo se resuelve al linkear en el final).
target_include_directories(ovni_uikit PRIVATE
    $<TARGET_PROPERTY:ovni_uikit_assets,INTERFACE_INCLUDE_DIRECTORIES>)

# El target final linkea JUCE (compila su unity una vez) + las fonts embebidas.
target_link_libraries(ovni_uikit INTERFACE
    juce::juce_gui_basics
    juce::juce_audio_processors
    ovni_uikit_assets)

# alias namespaced (consumo claro desde S5 / template)
add_library(ovni::uikit ALIAS ovni_uikit)
