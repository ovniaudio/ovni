# SUPERNOVA app (gui_app). Compila su propia copia de las fuentes del plugin (patrón OvniSupernovaTests),
# más el shell (app/*). NO define JucePlugin_* → START_JUCE_APPLICATION toma el camino limpio.
# Captura del audio del sistema sin drivers: Core Audio process taps (macOS 14.2+, permiso de AUDIO)
# con ScreenCaptureKit de fallback en macOS 13 … 14.1 (permiso de PANTALLA). Ver app/audio/SystemCapture.h.

juce_add_gui_app(supernova_app
    PRODUCT_NAME "SUPERNOVA"
    BUNDLE_ID    "com.ovni.supernova.app"
    COMPANY_NAME "OVNI"
    ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"   # wordmark OVNI roto en polvo — render del propio motor
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "SUPERNOVA usa el microfono/entrada de audio para reaccionar al sonido."
    # NSAudioCaptureUsageDescription = el texto del cartel del permiso CHICO ("System Audio Recording
    # Only") que pide el backend de process taps (D-33). JUCE no tiene una opcion dedicada para esta clave,
    # asi que se mergea cruda: PLIST_TO_MERGE exige un DOCUMENTO <plist> entero (juce_PlistOptions.cpp lo
    # parsea y descarta en silencio cualquier cosa que no tenga el tag <plist>) — un fragmento suelto de
    # <key>/<string> NO entra. El bundle NO localiza (no hay .lproj propios) -> solo EN, como el resto de la UI.
    PLIST_TO_MERGE "<plist><dict><key>NSAudioCaptureUsageDescription</key><string>SUPERNOVA listens to what's playing on your Mac to move the particles. Nothing is recorded or stored.</string></dict></plist>")

# ---- anti "2 SUPERNOVA" (Spotlight/Launchpad) ---------------------------------------------------
# El .app del build aparecía como SEGUNDA app junto a /Applications/SUPERNOVA.app: en macOS moderno
# `.metadata_never_index` YA NO se honra — el mecanismo soportado es el sufijo `.noindex` en el
# NOMBRE de la carpeta (Spotlight salta esos árboles). Movemos los productos del target ahí por
# construcción (JuceLibraryCode queda donde estaba; el bundle sigue estas props nativas de CMake).
# validate.sh matchea ambos patrones (*_artefacts y *_artefacts.noindex).
set_target_properties(supernova_app PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/supernova_app_artefacts.noindex/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/supernova_app_artefacts.noindex/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/supernova_app_artefacts.noindex/$<CONFIG>")

# Fuentes del plugin (motor + editor) — glob igual que el target de tests.
file(GLOB_RECURSE _snv_src CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/*.mm)

# Fuentes propias del shell (todos los .cpp/.mm bajo app/, menos el hueco de Windows).
target_sources(supernova_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MainComponent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppTopBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppAudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppRelaunch.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemAudioSource.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemAudioTapSource.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemCapture.mm
    ${_snv_src}
    ${CMAKE_SOURCE_DIR}/shared/template/PluginProcessorBase.cpp
    ${CMAKE_SOURCE_DIR}/shared/template/PluginEditorBase.cpp)

# ARC en TODOS los .mm (con TARGET_DIRECTORY: este CMakeLists no vive en source/).
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/render/metal/MetalRenderer.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/render/metal/SupernovaMTKView.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/render/metal/MetalViewComponent.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/render/metal/MetalSyphonServer.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/image/VisionField.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/video/VideoSource.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/../source/video/VideoExporter.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemAudioSource.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemAudioTapSource.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/SystemCapture.mm
    ${CMAKE_CURRENT_SOURCE_DIR}/AppRelaunch.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc" TARGET_DIRECTORY supernova_app)

target_include_directories(supernova_app PRIVATE
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/../source
    ${CMAKE_CURRENT_SOURCE_DIR}/..)          # #include "app/..."

target_compile_features(supernova_app PRIVATE cxx_std_20)
target_compile_definitions(supernova_app PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# Frameworks POSTERIORES al deployment target (11.0): ScreenCaptureKit es 12.3+ y los process taps de
# CoreAudio son 14.2+. No hace falta -weak_framework a mano — clang los debilita solo porque las APIs están
# anotadas API_AVAILABLE y el target es más viejo. Verificado sobre el binario de 0.3.1 con nm/otool:
#   ScreenCaptureKit → LC_LOAD_WEAK_DYLIB
#   _AudioHardwareCreateProcessTap · _AudioHardwareDestroyProcessTap · _OBJC_CLASS_$_CATapDescription
#   · _OBJC_CLASS_$_SCStream (y compañía) → "undefined weak external"
# O sea: en un macOS viejo resuelven a null y no se llaman nunca, porque cada uso está detrás de su
# @available (14.2 para los taps, 13.0 para SCK). La app abre; System Audio queda sin backend y el aviso
# lo dice. Lo que NO está verificado en hardware es el arranque real en macOS 13 (no hay VM acá).
target_link_libraries(supernova_app PRIVATE
    ovni_uikit ovni_presets ovni_syphon
    juce::juce_audio_utils juce::juce_audio_devices juce::juce_gui_extra juce::juce_dsp
    juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags
    "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework CoreVideo"
    "-framework IOSurface" "-framework AVFoundation" "-framework CoreMedia" "-framework Vision" "-framework ImageIO"
    "-framework ScreenCaptureKit" "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreGraphics")
