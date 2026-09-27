# ========================================================================================================
# TELESCOPE — analizador espacial del sello OVNI (9º plugin). Generado con
#   tools/scaffold.sh telescope Tlsc Ovni TELESCOPE
# y ajustado a mano (categoría VST3 Analyzer, tests propios).
#
# Se registra SOLO: el CMake raíz (§5b) hace GLOB de plugins/* y add_subdirectory de todo lo que tenga
# CMakeLists.txt y no empiece con '_'. Cero líneas tocadas fuera de esta carpeta.
# ========================================================================================================

# ---------------------------------------------------------------------------------------------
# VERSIÓN PROPIA (D-51). TELESCOPE se versiona SOLO: `plugins/telescope/VERSION` es la única fuente.
#
# Sin esto el plugin hereda `project(OVNI VERSION 0.3.2)` del raíz — que es la versión de SUPERNOVA — y
# los tres bundles salen declarando 0.3.2 para un plugin que nunca se publicó. Eso no es cosmético: el
# Installer de macOS decide con ese número si escribe o saltea un componente (el bug que dejó sin AU a
# quienes instalaron ORBIT v0.2.0), y `auval` lo imprime como "Component Version".
#
# El archivo se lee con file(READ) —no con un set() a mano— para que el número viva en UN solo lugar:
# el mismo que leen el test `telescope-version`, el SOURCE.txt del .pkg, el CHANGELOG y la ficha.
# CONFIGURE_DEPENDS no existe para file(READ): se agrega el archivo a CMAKE_CONFIGURE_DEPENDS para que
# tocar VERSION vuelva a configurar solo, en vez de dejar un build viejo declarando la versión anterior.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TELESCOPE_VERSION)
string(STRIP "${TELESCOPE_VERSION}" TELESCOPE_VERSION)
if (NOT TELESCOPE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "[telescope] VERSION inválido: '${TELESCOPE_VERSION}' (se espera x.y.z)")
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
message(STATUS "[telescope] versión ${TELESCOPE_VERSION} (plugins/telescope/VERSION)")

# ---------------------------------------------------------------------------------------------
# ovni_analysis — EL MOTOR DE MEDICIÓN COMO BIBLIOTECA (prompt 101, F3 de TELESCOPE 0.2; contrato D-100).
#
# Son los archivos de hoy de `source/analysis/**`, SIN MOVERLOS: la biblioteca se define sobre sus rutas. La
# usan el plugin, el runner de tests y la herramienta `telescope-measure` (measure/), que es como EYEPIECE lleva
# el motor: un proceso aparte, nunca linkeado.
#
# SÓLO juce_core, juce_audio_formats y juce_dsp (lo medido en EYEPIECE-PLUGINS-PROPIOS §2bis). Si algún día un
# archivo del motor necesita algo de interfaz, no compila acá: quedó del lado equivocado.
#
# Patrón [S3] del sello (como ovni_presets y ovni_uikit): compila NUESTRAS fuentes con los include dirs y las
# definiciones de esos tres módulos, y deja a JUCE como dependencia INTERFACE. El target final (el plugin, el
# runner o la herramienta) compila la unity de JUCE una sola vez; linkear los módulos PUBLIC metería JUCE
# entero adentro del .a.
#
# LAS BANDERAS DE LA ARITMÉTICA son las del plugin: -O3 de Release (juce_recommended_config_flags),
# -fno-fast-math (L-2) y /utf-8 en MSVC (Verdict.cpp tiene literales UTF-8 crudos). Que el número no cambie AL
# BIT al pasar de compilarse en el plugin a compilarse acá lo sostiene [golden] (tests/GoldenAnalysisTest.cpp).
# ---------------------------------------------------------------------------------------------
file(GLOB_RECURSE OVNI_ANALYSIS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/analysis/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/analysis/*.h")

add_library(ovni_analysis STATIC ${OVNI_ANALYSIS_SOURCES})
target_compile_features(ovni_analysis PUBLIC cxx_std_20)

# `#include "analysis/…"` y `#include "data/…"` (Verdict.cpp lee las tablas de source/data/, que son headers).
target_include_directories(ovni_analysis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source)

foreach (_jmod juce_core juce_audio_formats juce_dsp)
    target_include_directories(ovni_analysis PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(ovni_analysis PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_COMPILE_DEFINITIONS>)
endforeach ()

target_compile_definitions(ovni_analysis PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(ovni_analysis
    PRIVATE   juce::juce_recommended_config_flags
    INTERFACE juce::juce_core juce::juce_audio_formats juce::juce_dsp)

if (MSVC)
    target_compile_options(ovni_analysis PRIVATE /utf-8)
else ()
    target_compile_options(ovni_analysis PRIVATE -fno-fast-math)
endif ()

juce_add_plugin(telescope
    VERSION                  ${TELESCOPE_VERSION}     # propia, NO la del project() raíz (D-51)
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.telescope"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Tlsc                      # libre: Prob Halo Dust Auro Hrzn Spnv Plsr Nbla tomados
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "TELESCOPE"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE             # interceptar Program Change (preset por MIDI) -> NO quitar
    AU_MAIN_TYPE             "kAudioUnitType_Effect"   # FX de AUDIO (aufx), como el resto del catálogo.
    VST3_CATEGORIES          Fx Analyzer      # TELESCOPE mide, no procesa: el host lo agrupa con los medidores.
    COPY_PLUGIN_AFTER_BUILD  ${OVNI_COPY_PLUGIN_AFTER_BUILD})   # ON sólo en macOS (raíz): pisa el TELESCOPE.vst3/.component de ~/Library (el firmado también)

# Fuentes del plugin + las fuentes BASE del template (el chasis processor/editor se compila EN el plugin,
# así JUCE se compila una sola vez acá; no hay lib intermedia que duplique JUCE).
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
# El motor lo trae ovni_analysis (arriba): compilarlo acá también lo duplicaría.
list(FILTER PLUGIN_SOURCES EXCLUDE REGEX "/source/analysis/")

target_sources(telescope PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(telescope PRIVATE cxx_std_20)

target_include_directories(telescope PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui-kit/...", "template/..."

target_compile_definitions(telescope PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello. ovni_movement queda FUERA a propósito: TELESCOPE no mueve nada (no usa el MovementEngine),
# sólo mide. ovni_dsp entra por el de-zipper/limiter que el chasis referencia.
target_link_libraries(telescope PRIVATE
    ovni_analysis
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# F4 de la 0.2 (T8): la tarjeta que presenta a EYEPIECE busca la app por bundle id con LaunchServices
# (LSCopyApplicationURLsForBundleIdentifier, ui/EyepieceDetect.cpp). Es sólo de la Mac: EYEPIECE 0.1 sale sólo
# para Mac (D-113) y en Windows la tarjeta no se compila.
if (APPLE)
    target_link_libraries(telescope PRIVATE "-framework CoreServices")
endif()

# ---------------------------------------------------------------------------------------------
# -fno-fast-math EXPLÍCITO (hallazgo L-2 del revisor del 48).
#
# TELESCOPE apoya tres garantías en que el compilador NO reescriba la aritmética de punto flotante:
#   · [null]  — el pass-through es BIT-EXACTO: el buffer sale idéntico al que entró.
#   · casa-4  — la misma señal da los mismos números AL BIT venga en bloques de 1 o de 4096.
#   · [stereo]/[dyn] — la independencia del bloque se verifica con `==`, no con tolerancia.
#
# -ffast-math / -Ofast / -ffp-model=fast rompen las tres: habilitan reasociación, contracción a FMA y
# -ffinite-math-only (que además convierte los guardas de NaN/inf en código muerto — y este plugin PUBLICA
# casos borde como -inf clampeado). Hoy el build usa sólo -O3 y ninguna de esas banderas; esto lo deja
# ESCRITO para que nadie las agregue "para acelerar" sin que el build se lo diga.
if (NOT MSVC)
    target_compile_options(telescope PRIVATE -fno-fast-math)
endif ()

# ---------------------------------------------------------------------------------------------
# /utf-8 en MSVC (prompt 67, build de Windows). Strings.h, Verdict.cpp y FactoryPresets.cpp tienen ~196
# literales con UTF-8 crudo (Á, Í, ·, —…). Sin BOM y sin /utf-8, MSVC lee el archivo en la página de códigos
# del sistema (1252 en los runners de GitHub) y reescribe los literales en esa misma página: los bytes
# sobreviven sólo porque 1252 mapea los 256 valores. Con /utf-8 los lee y los escribe como UTF-8, igual que
# clang en macOS, sin depender del runner. Es un if() de configure: en macOS no existe y el build no cambia.
if (MSVC)
    target_compile_options(telescope PRIVATE /utf-8)
endif ()

# ---------------------------------------------------------------------------------------------
# Tests propios (exe OvniTelescopeTests). El raíz ya hizo FetchContent_MakeAvailable(Catch2) ANTES
# de recorrer plugins/ (§3, l.89-98 vs §5b, l.155) -> Catch2::Catch2 existe acá: add_subdirectory directo.
# tests/CMakeLists.txt del raíz NO se toca (es de los otros plugins).
# ---------------------------------------------------------------------------------------------
# La herramienta de consola que EYEPIECE lleva adentro (D-100). Antes que los tests: el runner prueba su núcleo.
add_subdirectory(measure)

if (OVNI_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
