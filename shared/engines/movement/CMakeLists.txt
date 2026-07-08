# ovni_movement — motor de MOVIMIENTO (trayectoria + Doppler + paneo estéreo, SIN HRTF).
# Librería estática headless. Depende de ovni_dsp (limiter + de-zipper).
# Includes relativos al raíz del módulo: #include "engines/movement/MovementEngine.h"

add_library(ovni_movement STATIC
    Trajectory.cpp
    MovementEngine.cpp
    Trajectory.h
    MovementEngine.h
    MovementSource.h)

target_compile_features(ovni_movement PUBLIC cxx_std_20)
target_include_directories(ovni_movement PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ovni_dsp ya expone juce::juce_dsp (PUBLIC); se nombra juce_dsp explícito por claridad.
target_link_libraries(ovni_movement PUBLIC ovni_dsp juce::juce_dsp)
