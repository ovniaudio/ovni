# ========================================================================================================
# SUPERNOVA — 8º plugin OVNI. Sintetizador visual audio-reactivo (imagen → partículas GPU).
# Diverge del template FX del catálogo: NO procesa audio (pass-through), es una superficie GPU (Metal/D3D11),
# y hospeda una vista nativa hija (CAMetalLayer) dentro del editor JUCE.
#
# El CMake RAÍZ ya pineó JUCE 8.0.13, agregó las libs de shared/ e incluyó shared/. Este dir se auto-registra
# por el glob plugins/* del root (no arranca con '_').
#
# M0: sin backend Metal todavía (se agrega en su Task). Este archivo compila el plugin como editor OVNI vacío.
# ========================================================================================================

juce_add_plugin(supernova
    COMPANY_NAME             "OVNI"
    BUNDLE_ID                "com.ovni.supernova"
    PLUGIN_MANUFACTURER_CODE Ovni
    PLUGIN_CODE              Spnv                     # único (tomados: Plsr Nbla Halo Auro Hrzn Dust Prob)
    FORMATS                  AU VST3                  # el Standalone genérico se reemplaza por app/ (H14)
    PRODUCT_NAME             "SUPERNOVA"
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE                     # triggers MIDI (RF5) + Program-Change → preset
    AU_MAIN_TYPE             "kAudioUnitType_Effect"  # aufx (D2 del roadmap; revisar en M3 vs Logic note-MIDI)
    VST3_CATEGORIES          Fx Analyzer              # visual/analyzer, no 'Fx Spatial'
    COPY_PLUGIN_AFTER_BUILD  TRUE)

# Fuentes del plugin + las bases del chasis (se compilan EN el plugin: JUCE una sola vez).
# .mm (Obj-C++, backend Metal) se suma en la Task de Metal — el glob ya lo prevé.
file(GLOB_RECURSE PLUGIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.mm"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")

target_sources(supernova PRIVATE
    ${PLUGIN_SOURCES}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

target_compile_features(supernova PRIVATE cxx_std_20)

target_include_directories(supernova PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_SOURCE_DIR}/shared)

target_compile_definitions(supernova PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# SUPERNOVA no usa las libs de audio-DSP espacial (ovni_movement/ovni_dsp) — es visual.
# Mantiene ovni_uikit (chrome del sello) + ovni_presets (RF6). juce_gui_extra: hogar de NSViewComponent
# (no estaba linkeado en el catálogo) — necesario para hospedar la CAMetalLayer.
target_link_libraries(supernova PRIVATE
    ovni_uikit
    ovni_presets
    juce::juce_audio_utils
    juce::juce_dsp                        # AudioAnalyzer: FFT/ventaneo
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- Backend Metal (macOS) — primer Obj-C++ y framework-linking del catálogo ----
# Shaders compilados en runtime (solo CLT, sin compilador `metal` offline). Los .mm se compilan como CXX
# (clang los trata como Obj-C++ por la extensión) + ARC — el MISMO patrón con que JUCE compila sus .mm,
# así no hace falta enable_language(OBJCXX) en el project() raíz.
if(APPLE)
    # Sin LANGUAGE override: clang autodetecta .mm como Obj-C++ por la extensión (igual que JUCE con sus .mm).
    # Fijar LANGUAGE CXX metería -x c++ y rompería el parse de Objective-C.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/source/render/metal/MetalRenderer.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/render/metal/SupernovaMTKView.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/render/metal/MetalViewComponent.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/render/metal/MetalSyphonServer.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/image/VisionField.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/video/VideoSource.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/source/video/VideoExporter.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(supernova PRIVATE
        ovni_syphon                           # servidor Syphon (RF7)
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
        "-framework CoreVideo"
        "-framework IOSurface"
        "-framework AVFoundation"             # decodificador de VIDEO (VideoSource)
        "-framework CoreMedia"
        "-framework Vision")                  # saliencia real (VisionField, modo NATURAL)
endif()
# Backend D3D11 (Windows) — M5: if(WIN32) con source/render/d3d11/*.cpp + d3d11/dxgi.

# ---- H14 · LA APP — shell propio (gui_app), reemplaza el Standalone genérico de JUCE ----
# Vive en app/ (fuera de source/ → el plugin AU/VST3 no compila ScreenCaptureKit ni el shell).
# El target arma su propia copia de las fuentes del plugin (patrón OvniSupernovaTests) sin
# definir JucePlugin_* → START_JUCE_APPLICATION toma el camino limpio.
if(APPLE)
    add_subdirectory(app)
endif()
