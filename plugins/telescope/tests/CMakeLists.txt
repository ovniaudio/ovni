# ========================================================================================================
# OvniTelescopeTests — exe de tests ENSAMBLADO de TELESCOPE (processor + editor + motor reales).
# Exe propio (como OvniSupernovaTests) porque createPluginFilter()/factoryPresets() son per-plugin:
# conviviendo con otro plugin darían duplicate-symbol.
#
# Espeja el bloque de OvniSupernovaTests (tests/CMakeLists.txt:518-600) SIN tocarlo.
# ========================================================================================================

file(GLOB TELESCOPE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE TELESCOPE_PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../source/*.cpp")

juce_add_console_app(OvniTelescopeTests PRODUCT_NAME "OvniTelescopeTests")

target_sources(OvniTelescopeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/presets/tests/TestMain.cpp   # main() + ScopedJuceInitialiser_GUI
    ${TELESCOPE_TEST_SOURCES}
    ${TELESCOPE_PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_include_directories(OvniTelescopeTests PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../source)

target_compile_features(OvniTelescopeTests PRIVATE cxx_std_20)

target_compile_definitions(OvniTelescopeTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Adónde escribe el generador de la matriz de idiomas ([strings-dump]). Se compila la ruta en vez de
    # deducirla del cwd: `ctest` corre el exe desde el build dir y `$EXE "[strings-dump]"` a mano desde
    # cualquier lado, y el documento tiene que caer siempre en el mismo lugar del repo.
    TELESCOPE_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../docs")

target_link_libraries(OvniTelescopeTests PRIVATE
    Catch2::Catch2
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# -fno-fast-math EXPLÍCITO (L-2) — el mismo motivo que en el plugin, y acá pesa MÁS: los tests son los
# que comparan al bit ([null], casa-4, la independencia del bloque de [stereo] y [dyn]). Si el exe de tests
# se compilara con matemática relajada, esas comparaciones dejarían de significar lo que dicen.
if (NOT MSVC)
    target_compile_options(OvniTelescopeTests PRIVATE -fno-fast-math)
endif ()

set_target_properties(OvniTelescopeTests PROPERTIES OSX_ARCHITECTURES "arm64")

# ========================================================================================================
# UNA COPIA DEL EXE A PROFUNDIDAD 2, PARA QUE LAS HERRAMIENTAS DEL SELLO LO ENCUENTREN.
#
# `tools/gain-staging-check.sh` (la puerta anti-clip de validate.sh) y `tools/measure-check.sh` localizan
# el runner del plugin con:
#     find "$BUILD_DIR" -maxdepth 5 -type f -name "Ovni<Cap>Tests" -perm -111
#
# Los demás plugins registran sus tests en el `tests/CMakeLists.txt` del RAÍZ, así que su exe queda en
#     build/tests/OvniSupernovaTests_artefacts/Release/OvniSupernovaTests        → profundidad 4 ✓
# TELESCOPE tiene su propio subdirectorio de tests (createPluginFilter()/factoryPresets() son per-plugin:
# conviviendo con otro plugin darían duplicate-symbol), así que el suyo cae en
#     build/plugins/telescope/tests/OvniTelescopeTests_artefacts/Release/OvniTelescopeTests → profundidad 6
# y el `find` NO lo encuentra. Consecuencia medida: `tools/validate.sh telescope` daba
# `gain-staging: no se pudo medir el peak` y la puerta 6 quedaba ROJA con el test [gain] pasando.
#
# `tools/**` es del orquestador y no se toca desde acá, así que la copia va de este lado. Se COPIA y no
# se mueve a propósito: la ruta canónica del artefacto (…/OvniTelescopeTests_artefacts/$<CONFIG>/) es la
# que documentan el README, los prompts y los criterios de aceptación, y tiene que seguir existiendo.
# Tampoco sirve un symlink: el `find` de esas dos herramientas pide `-type f`.
add_custom_command(TARGET OvniTelescopeTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/telescope-tests"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:OvniTelescopeTests>"
            "${CMAKE_BINARY_DIR}/telescope-tests/OvniTelescopeTests"
    COMMENT "TELESCOPE: copia del runner a build/telescope-tests/ (la buscan gain-staging-check.sh y measure-check.sh)"
    VERBATIM)

# Un solo test de ctest para todo el exe: `ctest -R telescope` corre la batería entera.
add_test(NAME telescope COMMAND OvniTelescopeTests)

# --- La versión que declaran los bundles == plugins/telescope/VERSION. ---
# No es un test de Catch2 porque lo que mira no está adentro del exe: son los Info.plist de los tres
# bundles que se distribuyen. El script lee el archivo VERSION del disco (no una versión sustituida por
# CMake), así que también se pone rojo si el CMakeLists deja de leerlo. Ver la cabecera del script.
# Los pares <documento> <ancla> son los documentos que PUBLICAN la versión: si el release sale con un
# CHANGELOG o una ficha que dicen otro número, el test se pone rojo antes de que salga.
# --- La matriz de idiomas commiteada está al día. ---
# El generador es el caso [strings-dump] del mismo exe (oculto con `[.]`: no corre en la suite normal).
# Acá se lo corre contra un temporal y se compara con el archivo del repo: un documento generado que
# nadie vuelve a generar se lee como si fuera cierto, y deja de serlo en el primer cambio de tabla.
add_test(NAME telescope-strings-matrix
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/check-strings-matrix.sh"
                 "$<TARGET_FILE:OvniTelescopeTests>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../docs/strings-matrix.md")

add_test(NAME telescope-version
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/check-bundle-version.sh"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION"
                 "${CMAKE_CURRENT_BINARY_DIR}/../telescope_artefacts/$<CONFIG>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../CHANGELOG.md"   "^## [0-9]"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../docs/ficha.md"  "^\\| Versi")
