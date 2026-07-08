# plugins/_probe/CMakeLists.txt — PLUGIN DE PRUEBA de la integración (S5). NO es del catálogo: junta las
# 4 piezas (ovni_movement + ovni_dsp + ovni_uikit + ovni_presets) + el template y prueba que ENCAJAN
# (compila universal + pluginval lvl8 + auval). Escrito a mano: scaffold.sh rechaza el slug con '_' inicial.
# Identidad: target/dir _probe · PRODUCT_NAME PROBE · CODE Prob · MANU Ovni · BUNDLE com.ovni.probe.
juce_add_plugin(_probe
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.probe"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Prob
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "PROBE"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE              # interceptar Program Change (preset por MIDI) — NO quitar
    AU_MAIN_TYPE             "kAudioUnitType_Effect"   # [S5] FX de audio (aufx), no 'aumf' Music-Effect:
                                                       # NEEDS_MIDI_INPUT haría 'aumf' por defecto; forzamos
                                                       # 'aufx' (como ÓRBITA) y la MIDI/Program-Change igual llega.
    VST3_CATEGORIES          Fx Spatial
    COPY_PLUGIN_AFTER_BUILD  TRUE)             # copia el .component a ~/Library/Audio/Plug-Ins → auval lo ve

file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")

# Fuentes del plugin + las bases del template (el chasis se compila EN el plugin → JUCE una sola vez acá,
# y el editor base ve ovni::ui). Patrón [S3]: no hay lib ovni_template.
target_sources(_probe PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(_probe PRIVATE cxx_std_20)

target_include_directories(_probe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # #include "engines/...", "ui-kit/...", "presets/...", "template/..."

target_compile_definitions(_probe PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Las 4 piezas del sello (cada una arrastra sus módulos JUCE por link INTERFACE) + utils de JUCE.
# (Sin juce_recommended_lto_flags a propósito: es el plugin de prueba; LTO sólo alargaría el link.)
target_link_libraries(_probe PRIVATE
    ovni_movement
    ovni_dsp
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
