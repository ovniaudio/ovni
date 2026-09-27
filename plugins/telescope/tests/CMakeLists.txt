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
# El motor viene de ovni_analysis, como en el plugin (prompt 101): el runner prueba la MISMA biblioteca.
list(FILTER TELESCOPE_PLUGIN_SOURCES EXCLUDE REGEX "/source/analysis/")

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

# PILA DE 16 MB EN WINDOWS (el doble de los 8 MB del hilo principal de macOS, donde la suite pasa: MSVC
# reusa menos que clang los huecos de la pila y los tests copian cuadros grandes por valor). Los tests arman el
# TelescopeProcessor EN LA PILA (`telescope::TelescopeProcessor proc;`) y el processor es grande; el hilo
# principal de Windows trae 1 MB y el primer caso de [budget] y de [editorbudget] moría con "SIGSEGV -
# Stack overflow" en el runner (prompt 96, run 36084052846). En el host no pasa: el processor lo crea el
# host en el heap. Sólo toca al runner de tests.
if (MSVC)
    target_link_options(OvniTelescopeTests PRIVATE "/STACK:16777216")
    # /utf-8, igual que el plugin (plugins/telescope/CMakeLists.txt, prompt 67): el runner compila las mismas
    # fuentes, y desde la F2b de la 0.2 las frases de VERDICT (data/Rules.h) llevan tildes, diéresis y ß.
    target_compile_options(OvniTelescopeTests PRIVATE /utf-8)
endif()

target_compile_definitions(OvniTelescopeTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Adónde escribe el generador de la matriz de idiomas ([strings-dump]). Se compila la ruta en vez de
    # deducirla del cwd: `ctest` corre el exe desde el build dir y `$EXE "[strings-dump]"` a mano desde
    # cualquier lado, y el documento tiene que caer siempre en el mismo lugar del repo.
    TELESCOPE_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../docs"
    # F2 de la 0.2: en el exe de tests el tema NO se lee del OVNI.settings de quien corre los tests (si
    # Joaquín eligió el claro, la suite entera pintaría en claro). Ver ui/ThemePreference.h.
    TELESCOPE_TEST_BUILD=1)

# Las rutas que leen las pruebas del motor y de telescope-measure (prompt 101). En su propio bloque, y no
# agregadas al de arriba, para que la F3 se junte con la F2 sin tocar la misma línea.
target_compile_definitions(OvniTelescopeTests PRIVATE
    # Los vuelcos dorados del motor ([golden]). Misma razón que TELESCOPE_DOCS_DIR: el test los lee (y, con
    # TELESCOPE_GOLDEN_WRITE=1, los escribe) siempre en el mismo lugar del repo, se corra desde donde se corra.
    TELESCOPE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
    # La especificación de telescope-measure: MEASURE[codigos] exige que sus códigos y los del código coincidan.
    TELESCOPE_MEASURE_SCHEMA="${CMAKE_CURRENT_SOURCE_DIR}/../measure/SCHEMA.md")

target_link_libraries(OvniTelescopeTests PRIVATE
    Catch2::Catch2
    ovni_analysis
    telescope_measure_core
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# F4 de la 0.2 (T8): lo mismo que el plugin (ver plugins/telescope/CMakeLists.txt), sólo en la Mac.
if (APPLE)
    target_link_libraries(OvniTelescopeTests PRIVATE "-framework CoreServices")
endif()

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

# --- telescope-measure desde afuera del proceso (prompt 101): exit, disco y bytes, con sus controles. ---
# El runner prueba el núcleo en proceso ([measure]); esto prueba el binario que viaja adentro de EYEPIECE.
# Compilar el runner compila también la herramienta y el generador, así ctest nunca corre un binario viejo.
add_dependencies(OvniTelescopeTests telescope-measure telescope-measure-fixtures)
add_test(NAME telescope-measure-cli
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../measure/check-measure-cli.sh"
                 "$<TARGET_FILE:telescope-measure>"
                 "$<TARGET_FILE:telescope-measure-fixtures>")

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
                 "${CMAKE_CURRENT_SOURCE_DIR}/../CHANGELOG.md"   "first:^## [0-9]"
                 "${CMAKE_CURRENT_SOURCE_DIR}/../docs/ficha.md"  "^\\| Versi")
