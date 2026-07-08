# ovni_pitch — motor de PITCH compartido (spec §3): ScaleQuantizer (header-only puro) + PitchShifter
# (granular splice-overlap, polifónico, multi-voz). Reusable, testeable solo, 0 dependencias de plugin.
# Lo estrena HALO (scale-aware shimmer) y queda disponible para el resto del catálogo.
# Depende de ovni_dsp (StereoLimiter + de-zipper). Includes relativos al raíz de shared/:
#   #include "engines/pitch/PitchShifter.h"   ·   #include "engines/pitch/ScaleQuantizer.h"
# (y "dsp/StereoLimiter.h", "dsp/Smoothing.h").

add_library(ovni_pitch STATIC
    PitchShifter.cpp
    PitchShifter.h
    ScaleQuantizer.h)         # header-only puro; se lista para que aparezca en el IDE

target_compile_features(ovni_pitch PUBLIC cxx_std_20)
target_include_directories(ovni_pitch PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ovni_dsp ya expone juce::juce_dsp (PUBLIC); se nombra juce_dsp explícito por claridad.
target_link_libraries(ovni_pitch PUBLIC ovni_dsp juce::juce_dsp)
