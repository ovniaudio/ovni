# ovni_stft — motor STFT/OLA compartido del eje espectral (verde): análisis/síntesis Hann periódica
# al 75% de solape (hop = N/4) con COLA verificada, FrameProcessor enchufable (identity por default) y
# latencia exacta = N. Lo siembra AURORA (SPL·01) y lo reusa HORIZON (SPL·02) tal cual. Librería
# estática headless. Includes relativos al raíz de shared/: #include "engines/stft/StftEngine.h".

add_library(ovni_stft STATIC
    StftEngine.cpp
    StftEngine.h)

target_compile_features(ovni_stft PUBLIC cxx_std_20)
target_include_directories(ovni_stft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ovni_dsp ya expone juce::juce_dsp (PUBLIC); se nombra juce_dsp explícito por claridad.
target_link_libraries(ovni_stft PUBLIC ovni_dsp juce::juce_dsp)
