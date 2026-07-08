# ========================================================================================================
# DUST — plugin del catálogo OVNI (MOV·03). Ecos binaurales tipo burbujas: multi-tap delay con feedback
# estable (motor propio en source/engine/) + banco de 16 direcciones HRIR FIJAS del anillo SADIE II
# (shared/engines/binaural/HrirRing.h, header-only) con reparto por ley de potencia entre buses adyacentes
# → costo de convolución FIJO + anti-click estructural. Familia Movimiento (cian).
#
# NOTA binaural: a diferencia de PULSAR, DUST NO usa SpatialEngine/Reflections (su espacializador propio
# BubbleField consume SOLO el header HrirRing.h) → no compila esos .cpp ni los necesita (el exe de tests
# OvniDustTests refleja lo mismo).
#
# El CMake RAÍZ (S5) ya hizo, ANTES de este add_subdirectory: pinear JUCE 8.0.13, add_subdirectory de las
# libs de shared/ (ovni_data / ovni_movement / ovni_dsp / ovni_uikit / ovni_presets) e include de shared/.
# Por eso acá ya existen los targets juce::* y ovni_*.
# ========================================================================================================

juce_add_plugin(dust
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.dust"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Dust
    FORMATS                  AU VST3 Standalone
    PRODUCT_NAME             "DUST"
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

target_sources(dust PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(dust PRIVATE cxx_std_20)

target_include_directories(dust PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)   # convención del CONTRACT: #include "presets/...", "ui/...", "engines/...", "template/..."

target_compile_definitions(dust PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# Libs del sello (cada una arrastra sus módulos JUCE vía link INTERFACE) + utilidades. DUST es de
# la familia Movimiento (briefing §9.2): ovni_movement + ovni_dsp (StereoLimiter/Smoothing) +
# ovni_data (datos HRIR horneados, INTERFACE) + ovni_uikit + ovni_presets.
target_link_libraries(dust PRIVATE
    ovni_movement
    ovni_dsp
    ovni_data
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
