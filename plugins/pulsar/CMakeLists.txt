# ========================================================================================================
# Plantilla de CMake para un plugin del sello OVNI.
# tools/scaffold.sh (S4) reemplaza los @PLACEHOLDERS@ y copia este archivo a plugins/<nombre>/CMakeLists.txt.
#   pulsar        nombre del target + PRODUCT_NAME (sin espacios; ej. PULSAR)
#   Plsr        4 chars, único, al menos una mayúscula (ej. Plsr)
#   Ovni  4 chars, primera mayúscula para AU (ej. Ovni)
#
# El CMake RAÍZ (S5) ya hizo, ANTES de este add_subdirectory: pinear JUCE 8.0.13, add_subdirectory de las
# libs de shared/ (ovni_movement / ovni_dsp / ovni_uikit / ovni_presets) e include de shared/. Por eso acá
# ya existen los targets juce::* y ovni_*.
# ========================================================================================================

juce_add_plugin(pulsar
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.pulsar"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Plsr
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "PULSAR"
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

target_sources(pulsar PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp
    # Motor BINAURAL HRIR (portado de ÓRBITA, curado a ovni::engines): da la apertura/externalización
    # real (FIR por oído + ITD + near-field + reflexiones + crosstalk). Headers vía el include de shared/.
    ${CMAKE_SOURCE_DIR}/shared/engines/binaural/SpatialEngine.cpp
    ${CMAKE_SOURCE_DIR}/shared/engines/binaural/Reflections.cpp)

target_compile_features(pulsar PRIVATE cxx_std_20)

target_include_directories(pulsar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui/...", "engines/...", "template/..."

target_compile_definitions(pulsar PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello (cada una arrastra sus módulos JUCE vía link INTERFACE) + utilidades.
target_link_libraries(pulsar PRIVATE
    ovni_movement
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
