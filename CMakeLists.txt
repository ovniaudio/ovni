# =============================================================================================
# OVNI — CMake RAÍZ (sesión S5, integración). Junta las 4 piezas de shared/ y el plugin de prueba
# _probe que las usa todas. Ver CONTRACT.md (§"Cómo S5 integra") y docs/plans/S5-integracion.md.
#
# Qué hace:
#   1. Pinea JUCE 8.0.13 (reusa un checkout local con -DOVNI_JUCE_DIR=... ; si no, FetchContent del tag).
#   2. Provee libmysofa v1.3.4 (target mysofa-static) para ovni_dsp/SofaLoader  [OVNI_WITH_SOFA, default ON].
#   3. add_subdirectory de cada lib del sello: ovni_data / ovni_dsp / ovni_movement / ovni_uikit / ovni_presets.
#   4. Agrega shared/ a los include dirs (convención del CONTRACT: #include "engines/...", "ui-kit/...", etc.).
#   5. add_subdirectory(plugins/_probe) — el plugin de prueba que linkea las 4 piezas + el template.
#   6. Tests unificados [s1]+[s2]+[s3]+[gain] (OVNI_BUILD_TESTS, default ON; ver tests/CMakeLists.txt).
#
# [S5] NOTA DE RECONCILIACIÓN: NO se usa el cmake/ de Pamplejuce que vendoró S3 — referencia submódulos
#   que ovni no tiene (JUCE, melatonin_inspector, IPP, CLAP). En su lugar usamos la API de JUCE pura
#   (juce_add_plugin / juce_add_binary_data / juce_add_console_app), que es lo que ya probaron los harness
#   de S1/S2/S3. El cmake/ queda en el repo como referencia. Ver docs/CONTRACT-CHANGES.md [S5].
#
# Universal: pasá -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" al configurar (los artefactos del plugin salen
#   universales; el target de tests se fuerza a arm64 nativo por velocidad — no necesita ser universal).
#
#   Configurar:  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#                  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
#                  -DOVNI_JUCE_DIR=/Users/musik/PLUGINS/orbita/JUCE \
#                  -DFETCHCONTENT_SOURCE_DIR_LIBMYSOFA=/Users/musik/PLUGINS/orbita/build/_deps/libmysofa-src \
#                  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/Users/musik/PLUGINS/orbita/build/_deps/catch2-src
#   Compilar:    cmake --build build
# =============================================================================================
cmake_minimum_required(VERSION 3.25)

# Deployment target por defecto (antes de project()): base segura para un universal arm64+x86_64.
if (NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS deployment target")
endif ()

# Universal por DEFAULT (antes de project(): CMAKE_OSX_ARCHITECTURES debe fijarse pre-project para tomar
# efecto). Si nadie pasó -DCMAKE_OSX_ARCHITECTURES y NO es un build Debug, salimos universales arm64+x86_64.
# Así el build de distribución es universal sin tener que acordarse del flag (los docs ya lo prometen).
# Para iterar rápido en arm64: usá el preset 'dev' o pasá -DCMAKE_OSX_ARCHITECTURES=arm64 explícito.
if (APPLE AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
    if (NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal por default en Release")
        message(STATUS "[ovni] Universal por default (Release): CMAKE_OSX_ARCHITECTURES=arm64;x86_64")
    endif ()
endif ()

project(OVNI VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OVNI_WITH_SOFA  "Compilar SofaLoader (requiere libmysofa: target mysofa-static)" ON)
option(OVNI_BUILD_TESTS "Compilar el runner de tests unificado [s1]+[s2]+[s3]+[gain]"     ON)

include(FetchContent)

# ---------------------------------------------------------------------------------------------
# 1. JUCE 8.0.13 — reusa OVNI_JUCE_DIR si apunta a un checkout válido; si no, clona el tag.
# ---------------------------------------------------------------------------------------------
set(OVNI_JUCE_DIR "" CACHE PATH "Checkout local de JUCE 8.0.13 a reusar (evita el clone)")
if (OVNI_JUCE_DIR AND EXISTS "${OVNI_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "[ovni] Reusando JUCE de ${OVNI_JUCE_DIR}")
    add_subdirectory("${OVNI_JUCE_DIR}" "${CMAKE_BINARY_DIR}/_juce")
else ()
    message(STATUS "[ovni] FetchContent JUCE 8.0.13")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif ()

# ---------------------------------------------------------------------------------------------
# 2. libmysofa v1.3.4 — provee mysofa-static para ovni_dsp/SofaLoader (forward-looking: binaurales).
#    Reusar checkout: -DFETCHCONTENT_SOURCE_DIR_LIBMYSOFA=/ruta/a/libmysofa-src
# ---------------------------------------------------------------------------------------------
if (OVNI_WITH_SOFA)
    set(BUILD_TESTS   OFF CACHE BOOL "" FORCE)   # apaga los tests/coverage de libmysofa
    set(CODE_COVERAGE OFF CACHE BOOL "" FORCE)
    message(STATUS "[ovni] libmysofa v1.3.4 (mysofa-static)")
    FetchContent_Declare(libmysofa
        GIT_REPOSITORY https://github.com/hoene/libmysofa.git
        GIT_TAG v1.3.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libmysofa)
endif ()

# ---------------------------------------------------------------------------------------------
# 3. Catch2 v3.8.1 — sólo si compilamos tests. Reusar: -DFETCHCONTENT_SOURCE_DIR_CATCH2=/ruta/catch2-src
# ---------------------------------------------------------------------------------------------
if (OVNI_BUILD_TESTS)
    message(STATUS "[ovni] Catch2 v3.8.1")
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)   # catch_discover_tests()
    enable_testing()
    include(CTest)
    include(Catch)
endif ()

# ---------------------------------------------------------------------------------------------
# 4. Libs del sello (cada una define una lib estática ovni_<mod> con el patrón JUCE-por-genex de [S3]).
#    Orden por dependencias: data/dsp primero, luego movement (usa dsp), ui-kit y presets.
# ---------------------------------------------------------------------------------------------
add_subdirectory(shared/data)               # ovni_data   (INTERFACE, HRIR horneados)
add_subdirectory(shared/dsp)                # ovni_dsp    (limiter + de-zipper + SofaLoader)
add_subdirectory(shared/engines/movement)   # ovni_movement (trayectoria + Doppler + paneo)
add_subdirectory(shared/engines/fdn)        # ovni_fdn    (reverb FDN: Householder lossless + T60(ω); NÉBULA/HALO)
add_subdirectory(shared/engines/pitch)      # ovni_pitch  (ScaleQuantizer + PitchShifter granular poly; HALO)
add_subdirectory(shared/engines/stft)       # ovni_stft   (StftEngine OLA Hann 75%; AURORA/HORIZON)
add_subdirectory(shared/ui-kit)             # ovni_uikit  (+ ovni_uikit_assets, fonts del sello)
add_subdirectory(shared/presets)            # ovni_presets (PresetManager + ABState + tipos)

# ovni_syphon — Syphon-Framework (BSD, vendorizado en third_party/, subconjunto Metal, commit 71351d4).
# Static lib propia (no framework embebido → sin rpath/codesign anidado en los 3 bundles). Sólo SUPERNOVA la
# linkea. ARC (las fuentes de master son ARC). NOTICE.md lista la atribución (RNF5).
if(APPLE)
    file(GLOB _syphon_src CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/third_party/Syphon-Framework/*.m
        ${CMAKE_SOURCE_DIR}/third_party/Syphon-Framework/*.c)
    add_library(ovni_syphon STATIC ${_syphon_src})
    # Syphon usa un prefix header (Cocoa, con guard __OBJC__ → seguro para el .c). Fuera de Xcode hay que
    # force-incluirlo (las fuentes no importan Foundation por su cuenta).
    target_compile_options(ovni_syphon PRIVATE -fobjc-arc -Wno-deprecated-declarations -Wno-everything
        -include ${CMAKE_SOURCE_DIR}/third_party/Syphon-Framework/Syphon_Prefix.pch)
    target_include_directories(ovni_syphon PUBLIC ${CMAKE_SOURCE_DIR}/third_party/Syphon-Framework)
    target_link_libraries(ovni_syphon PUBLIC
        "-framework Metal" "-framework IOSurface" "-framework IOKit"
        "-framework Foundation" "-framework CoreFoundation")
    set_target_properties(ovni_syphon PROPERTIES OSX_ARCHITECTURES "arm64;x86_64")
endif()

# Convención del CONTRACT (regla 3): includes relativos al raíz de shared/. Cada lib ya lo exporta
# PUBLIC, pero lo agregamos global por las dudas (lo consumen los plugins y el runner de tests).
include_directories(${CMAKE_SOURCE_DIR}/shared)

# ---------------------------------------------------------------------------------------------
# 5. _probe — canario de integración (juntaba las 4 piezas). SACADO DEL CATÁLOGO (2026-06-06):
#    ya NO se compila ni instala como plugin → no aparece en el DAW. Su carpeta source/ se CONSERVA
#    porque tests/ la ensambla como fixture ([s1]+[s2]+[gain]+[snapshot] en OvniTests). Los plugins
#    reales (PULSAR/NÉBULA) ya corren sus propios tests sobre el plugin real.
# ---------------------------------------------------------------------------------------------
# add_subdirectory(plugins/_probe)   # <- desactivado a propósito; NO reactivar (volvería a Ableton)

# ---------------------------------------------------------------------------------------------
# 5b. Plugins del catálogo: registro AUTOMÁTICO. Cualquier plugins/<slug>/ con CMakeLists.txt se
#     agrega, EXCEPTO los que empiezan con '_' (p.ej. _probe: canario de integración, NO va al DAW).
#     Antes esto era una lista hardcodeada (pulsar/nebula/halo) que había que editar a mano por plugin.
# ---------------------------------------------------------------------------------------------
file(GLOB OVNI_PLUGIN_DIRS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/plugins" "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
list(SORT OVNI_PLUGIN_DIRS)
foreach (plugin_dir IN LISTS OVNI_PLUGIN_DIRS)
    if (IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${plugin_dir}"
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/${plugin_dir}/CMakeLists.txt"
        AND NOT plugin_dir MATCHES "^_")
        message(STATUS "[ovni] plugin del catálogo: ${plugin_dir}")
        add_subdirectory(plugins/${plugin_dir})
    endif ()
endforeach ()

# ---------------------------------------------------------------------------------------------
# 6. Runner de tests unificado [s1]+[s2]+[s3]+[gain] (ctest). Nativo arm64 (no necesita universal).
# ---------------------------------------------------------------------------------------------
if (OVNI_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
