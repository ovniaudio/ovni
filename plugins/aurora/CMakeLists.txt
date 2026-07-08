# ========================================================================================================
# AURORA — plugin del catálogo OVNI (SPL·01). Paneo ESPECTRAL mono-safe: STFT/OLA (motor compartido
# ovni_stft, sembrado por este plugin y reusado por HORIZON) + reparto L/R de magnitud POR BIN con ley
# de potencia y FASE PRESERVADA (cero comb, cero phasiness). Familia Espectral (color verde).
#
# El CMake RAÍZ (S5) ya hizo, ANTES de este add_subdirectory: pinear JUCE 8.0.13, add_subdirectory de las
# libs de shared/ (ovni_stft / ovni_dsp / ovni_uikit / ovni_presets) e include de shared/. Por eso acá
# ya existen los targets juce::* y ovni_*.
# ========================================================================================================

juce_add_plugin(aurora
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.aurora"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Auro
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "AURORA"
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

target_sources(aurora PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(aurora PRIVATE cxx_std_20)

target_include_directories(aurora PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui/...", "engines/...", "template/..."

target_compile_definitions(aurora PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello. AURORA usa el motor STFT/OLA (ovni_stft, compartido con HORIZON) — NO usa
# ovni_movement (no hay Trayectoria: el "movimiento" es el MOTION del abanico espectral, un LFO
# propio del motor) ni ovni_data (no hay HRIR: el paneo es por reparto de magnitud, no binaural).
target_link_libraries(aurora PRIVATE
    ovni_stft
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
