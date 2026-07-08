# Harness de test STANDALONE para ovni_presets (validación en aislamiento [s3]).
# JUCE todavía no está pineado en el monorepo (lo hace S5); este CMake trae JUCE 8.0.13 + Catch2 vía
# FetchContent para correr los tests sin el resto de la fábrica. Para reusar checkouts locales y evitar
# descargas, pasá:
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/ruta/a/JUCE  -DFETCHCONTENT_SOURCE_DIR_CATCH2=/ruta/a/catch2-src
cmake_minimum_required(VERSION 3.25)
project(ovni_presets_tests VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.13)
FetchContent_MakeAvailable(JUCE)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1)
FetchContent_MakeAvailable(Catch2)

# La librería REAL bajo prueba (confirma que ovni_presets archiva como lib estática). Binario aparte.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR}/ovni_presets_build)

# Ejecutable de tests: compila las fuentes de presets DIRECTAMENTE (JUCE se compila UNA sola vez acá,
# vía juce_add_console_app, que inicializa el target para compilar las unity sources de los módulos).
juce_add_console_app(PresetsTests PRODUCT_NAME "OVNI Presets Tests")

target_sources(PresetsTests PRIVATE
    TestMain.cpp
    PresetsTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ABState.cpp)

target_include_directories(PresetsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)   # shared/ -> #include "presets/..."

target_compile_features(PresetsTests PRIVATE cxx_std_20)

target_compile_definitions(PresetsTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1   # runDispatchLoopUntil en los tests
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(PresetsTests PRIVATE
    Catch2::Catch2
    juce::juce_audio_utils
    juce::juce_gui_basics
    juce::juce_recommended_config_flags)
