# =====================================================================================
# Build AISLADO del generador de HRIR de produccion (tools/gen_hrir.cpp). NO forma parte
# del build del sello: se configura/compila en un build dir propio (build-genhrir) para
# rehornear los 4 headers HRIR desde un SOFA y NO toca el build/ principal.
#
#   cmake -G Ninja -B build-genhrir -S tools/genhrir \
#       -DOVNI_JUCE_DIR=/Users/musik/PLUGINS/orbita/JUCE \
#       -DFETCHCONTENT_SOURCE_DIR_LIBMYSOFA=/Users/musik/PLUGINS/orbita/build/_deps/libmysofa-src
#   cmake --build build-genhrir
#   ./build-genhrir/gen_hrir <SOFA> <repo-ovni>
# =====================================================================================
cmake_minimum_required(VERSION 3.25)
project(ovni_genhrir VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif ()
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

include(FetchContent)

# JUCE (solo para juce::juce_dsp::FFT). Reusa un checkout local con -DOVNI_JUCE_DIR=...
set(OVNI_JUCE_DIR "" CACHE PATH "Checkout local de JUCE a reusar")
if (OVNI_JUCE_DIR AND EXISTS "${OVNI_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${OVNI_JUCE_DIR}" "${CMAKE_BINARY_DIR}/_juce")
else ()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13)
    FetchContent_MakeAvailable(JUCE)
endif ()

# libmysofa v1.3.4 (target mysofa-static). Reusa: -DFETCHCONTENT_SOURCE_DIR_LIBMYSOFA=...
set(BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(CODE_COVERAGE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libmysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG v1.3.4)
FetchContent_MakeAvailable(libmysofa)

juce_add_console_app(gen_hrir PRODUCT_NAME "gen_hrir")

target_sources(gen_hrir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../gen_hrir.cpp)

target_include_directories(gen_hrir PRIVATE
    ${libmysofa_SOURCE_DIR}/src/hrtf
    ${libmysofa_BINARY_DIR}/src)

target_link_libraries(gen_hrir PRIVATE
    mysofa-static
    juce::juce_dsp
    juce::juce_recommended_config_flags)

set_target_properties(gen_hrir PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
