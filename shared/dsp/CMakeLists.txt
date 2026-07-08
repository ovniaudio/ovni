# ovni_dsp — DSP compartido HEADLESS (sin GUI): limiter estéreo-linked, de-zipper, SOFA loader.
# Asume que los targets de JUCE ya existen (los pinea S5, o el harness de test de S1 en tests/).
# Includes relativos al raíz del módulo compartido: #include "dsp/StereoLimiter.h"

set(OVNI_DSP_SOURCES
    StereoLimiter.cpp
    LookaheadLimiter.cpp)

set(OVNI_DSP_HEADERS
    StereoLimiter.h
    LookaheadLimiter.h
    Smoothing.h
    SofaLoader.h)

# SofaLoader es FORWARD-LOOKING (los módulos binaurales lo usan; PULSAR no). Requiere libmysofa
# (target mysofa-static). Se puede apagar donde no haya libmysofa disponible.
option(OVNI_WITH_SOFA "Compilar SofaLoader (requiere libmysofa: target mysofa-static)" ON)
if (OVNI_WITH_SOFA)
    list(APPEND OVNI_DSP_SOURCES SofaLoader.cpp)
endif ()

add_library(ovni_dsp STATIC ${OVNI_DSP_SOURCES} ${OVNI_DSP_HEADERS})
target_compile_features(ovni_dsp PUBLIC cxx_std_20)
target_include_directories(ovni_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# juce_dsp arrastra juce_audio_basics + juce_core (headless, sin GUI).
target_link_libraries(ovni_dsp PUBLIC juce::juce_dsp)

if (OVNI_WITH_SOFA)
    target_link_libraries(ovni_dsp PUBLIC mysofa-static)
    target_compile_definitions(ovni_dsp PUBLIC OVNI_WITH_SOFA=1)
    # Headers de libmysofa para <mysofa.h> (belt-and-suspenders; el target ya suele exportarlos).
    if (DEFINED libmysofa_SOURCE_DIR)
        target_include_directories(ovni_dsp PRIVATE
            ${libmysofa_SOURCE_DIR}/src/hrtf
            ${libmysofa_BINARY_DIR}/src)
    endif ()
endif ()
