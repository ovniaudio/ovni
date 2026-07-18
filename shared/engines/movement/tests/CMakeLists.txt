# =====================================================================================
# Harness STANDALONE de validación de S1 (motores DSP), aislado del resto del monorepo.
# JUCE aún no está pineado (lo hace S5) -> acá lo proveemos por FetchContent (JUCE 8.0.13),
# junto con Catch2 y libmysofa. Compila ovni_dsp + ovni_movement + ovni_data y corre los [s1].
#
#   Configurar:  cmake -S shared/engines/movement/tests -B build/s1 \
#                      -DOVNI_JUCE_DIR=/Users/musik/PLUGINS/orbita/JUCE        # opcional: reusa un checkout
#   Compilar:    cmake --build build/s1 -j
#   Correr:      ./build/s1/S1Tests "[s1]"
# =====================================================================================
cmake_minimum_required(VERSION 3.25)
project(ovni_s1_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- JUCE 8.0.13 (reusa OVNI_JUCE_DIR si se pasa; si no, clona el tag) ---------------
if (DEFINED OVNI_JUCE_DIR AND EXISTS "${OVNI_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "[s1] Reusando JUCE de ${OVNI_JUCE_DIR}")
    add_subdirectory(${OVNI_JUCE_DIR} ${CMAKE_BINARY_DIR}/_juce)
else ()
    message(STATUS "[s1] FetchContent JUCE 8.0.13")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif ()

# --- Catch2 v3.8.1 -------------------------------------------------------------------
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# --- libmysofa v1.3.4 (para SofaLoader) ---------------------------------------------
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CODE_COVERAGE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libmysofa
    GIT_REPOSITORY https://github.com/hoene/libmysofa.git
    GIT_TAG v1.3.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(libmysofa)

# --- Módulos de S1 (libs estáticas) -------------------------------------------------
set(OVNI_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
add_subdirectory(${OVNI_SHARED}/data             ${CMAKE_BINARY_DIR}/ovni_data)
add_subdirectory(${OVNI_SHARED}/dsp              ${CMAKE_BINARY_DIR}/ovni_dsp)
add_subdirectory(${OVNI_SHARED}/engines/movement ${CMAKE_BINARY_DIR}/ovni_movement)

# --- Ejecutable de tests ------------------------------------------------------------
add_executable(S1Tests S1Tests.cpp)
target_link_libraries(S1Tests PRIVATE
    Catch2::Catch2WithMain
    ovni_movement
    ovni_dsp
    ovni_data)

# Un SOFA real para validar SofaLoader (el CIPIC que trae libmysofa en sus tests).
set(OVNI_TEST_SOFA "${libmysofa_SOURCE_DIR}/tests/CIPIC_subject_003_hrir_final_itdInDelayField.sofa")
target_compile_definitions(S1Tests PRIVATE OVNI_TEST_SOFA="${OVNI_TEST_SOFA}")
