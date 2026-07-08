# ========================================================================================================
# HALO — plugin del catálogo OVNI (SPC·02). Shimmer reverb scale-aware: FDN (motor de NÉBULA) con un
# PitchShifter (voces consonantes del ScaleQuantizer) + EQ de banda en el lazo de feedback. Familia FDN
# (color magenta). Estrena el módulo shared/engines/pitch.
#
# El CMake RAÍZ (S5) ya hizo, ANTES de este add_subdirectory: pinear JUCE 8.0.13, add_subdirectory de las
# libs de shared/ (ovni_fdn / ovni_pitch / ovni_dsp / ovni_uikit / ovni_presets) e include de shared/.
# ========================================================================================================

juce_add_plugin(halo
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.halo"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Halo
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "HALO"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE             # interceptar Program Change (preset por MIDI) -> NO quitar
    AU_MAIN_TYPE             "kAudioUnitType_Effect"   # FX de AUDIO (aufx), como NÉBULA/ÓRBITA
    VST3_CATEGORIES          Fx Spatial
    COPY_PLUGIN_AFTER_BUILD  TRUE)            # se copia a ~/Library/Audio/Plug-Ins (recargar con Cmd+Q)

# Fuentes del plugin + las fuentes BASE del template (el chasis processor/editor se compila EN el plugin,
# así JUCE se compila una sola vez acá; no hay lib intermedia que duplique JUCE).
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")

target_sources(halo PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)
# NOTA: HALO NO usa SpatialEngine/Reflections (HRTF/HRIR). La órbita del shimmer es un pan binaural ITD/ILD
# hecho en HaloEngine.cpp (sin convolución ni datos HRIR). Por eso este plugin NO compila esos sources ni
# linkea ovni_data — a diferencia de PULSAR/ÓRBITA, que sí usan el motor HRTF.

target_compile_features(halo PRIVATE cxx_std_20)

target_include_directories(halo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui/...", "engines/...", "template/..."

target_compile_definitions(halo PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello. HALO usa el motor REVERB FDN (ovni_fdn, compartido con NÉBULA) + el módulo de pitch
# (ovni_pitch: PitchShifter granular poly) + el movimiento (ovni_movement: Trajectory) que da el azimut θ(t)
# de la órbita. El pan binaural (ITD/ILD) lo hace HaloEngine con sus propias líneas de delay y shelf — NO
# carga datos HRIR, así que NO linkea ovni_data. Todos arrastran ovni_dsp + juce_dsp PUBLIC.
target_link_libraries(halo PRIVATE
    ovni_fdn
    ovni_pitch
    ovni_movement
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
