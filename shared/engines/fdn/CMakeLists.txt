# ovni_fdn — motor REVERB FDN (Feedback Delay Network: Householder lossless + T60(ω) + breath/freeze).
# Lo hereda NÉBULA (y más adelante HALO). Librería estática headless. Depende de ovni_dsp (limiter + de-zipper).
# Includes relativos al raíz de shared/: #include "engines/fdn/FdnReverb.h" (y "dsp/StereoLimiter.h").

add_library(ovni_fdn STATIC
    FdnReverb.cpp
    FdnReverb.h)

target_compile_features(ovni_fdn PUBLIC cxx_std_20)
target_include_directories(ovni_fdn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# ovni_dsp ya expone juce::juce_dsp (PUBLIC); se nombra juce_dsp explícito por claridad.
target_link_libraries(ovni_fdn PUBLIC ovni_dsp juce::juce_dsp)
