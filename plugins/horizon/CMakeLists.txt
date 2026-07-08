# ========================================================================================================
# HORIZON — plugin del catálogo OVNI (SPL·02). Freeze ESPECTRAL RÍTMICO: REUSA el motor STFT/OLA
# compartido (ovni_stft, sembrado por AURORA) + captura de frame + scheduler de gate raised-cosine
# + whisper + spread + duck. Familia Espectral (color verde). Hermano de AURORA en el eje STFT.
#
# El CMake RAÍZ (S5) ya hizo, ANTES de este add_subdirectory: pinear JUCE 8.0.13, add_subdirectory de las
# libs de shared/ (ovni_stft / ovni_dsp / ovni_uikit / ovni_presets) e include de shared/. Por eso acá
# ya existen los targets juce::* y ovni_*.
# ========================================================================================================

juce_add_plugin(horizon
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.horizon"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Hrzn
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "HORIZON"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE             # interceptar Program Change (preset por MIDI) -> NO quitar
    AU_MAIN_TYPE             "kAudioUnitType_Effect"   # [S5] FX de AUDIO (aufx). Sin esto, NEEDS_MIDI_INPUT
                                                       # haría que JUCE lo registre como 'aumf' (Music Effect,
                                                       # UX equivocada: aparece bajo instrumentos). Forzamos
                                                       # 'aufx' (como ÓRBITA); la Program-Change igual llega.
    VST3_CATEGORIES          Fx Spatial
    COPY_PLUGIN_AFTER_BUILD  TRUE)            # se copia a ~/Library/Audio/Plug-Ins (recargar con Cmd+Q)

# Fuentes del plugin + las fuentes BASE del template (el chasis processor/editor se compila EN el plugin,
# así JUCE se compila una sola vez acá; no hay lib intermedia que duplique JUCE).
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")

target_sources(horizon PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(horizon PRIVATE cxx_std_20)

target_include_directories(horizon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui/...", "engines/...", "template/..."

target_compile_definitions(horizon PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello. HORIZON usa el motor STFT/OLA (ovni_stft, compartido con AURORA) — NO usa
# ovni_movement (no hay Trayectoria: el "ritmo" es el gate del re-trigger, un scheduler propio del
# motor) ni ovni_data (no hay HRIR: el spread es des-correlación de fase por bin, no binaural).
target_link_libraries(horizon PRIVATE
    ovni_stft
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
