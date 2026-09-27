# ========================================================================================================
# telescope-measure — la herramienta de consola que mide archivos y tramos con el motor de TELESCOPE y devuelve
# JSON (prompt 101, F3 de TELESCOPE 0.2; contrato D-100). EYEPIECE la lleva adentro y la llama; nunca linkea el
# motor. La especificación es SCHEMA.md, al lado.
#
#   telescope_measure_version   escribe TelescopeMeasureVersion.h en cada build (versión + sha del motor)
#   telescope_measure_core      el núcleo (pedido → línea de JSON), aparte del main() para probarlo en proceso
#   telescope-measure           el binario: main() + el núcleo + ovni_analysis + juce_core/audio_formats/dsp
#
# Nada de interfaz: si algo de acá necesitara juce_gui_*, no linkearía (ver el reporte del 101, hito 2).
# ========================================================================================================

set(_tm_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_tm_gen}")

add_custom_target(telescope_measure_version
    COMMAND ${CMAKE_COMMAND}
            -DVERSION_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../VERSION
            -DREPO_DIR=${CMAKE_SOURCE_DIR}
            -DOUT=${_tm_gen}/TelescopeMeasureVersion.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/EngineVersion.cmake
    BYPRODUCTS ${_tm_gen}/TelescopeMeasureVersion.h
    COMMENT "telescope-measure: versión y sha del motor"
    VERBATIM)

add_library(telescope_measure_core STATIC
    MeasureCore.cpp
    MeasureCore.h
    EngineVersion.cpp
    Sha256.h)
target_compile_features(telescope_measure_core PUBLIC cxx_std_20)
target_include_directories(telescope_measure_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${_tm_gen})
add_dependencies(telescope_measure_core telescope_measure_version)

# Patrón [S3], como ovni_analysis: los headers y las definiciones de JUCE, sin su unity.
foreach (_jmod juce_core juce_audio_formats juce_dsp)
    target_include_directories(telescope_measure_core PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_INCLUDE_DIRECTORIES>)
    target_compile_definitions(telescope_measure_core PRIVATE
        $<TARGET_PROPERTY:juce::${_jmod},INTERFACE_COMPILE_DEFINITIONS>)
endforeach ()
target_compile_definitions(telescope_measure_core PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(telescope_measure_core
    PUBLIC  ovni_analysis
    PRIVATE juce::juce_recommended_config_flags)

if (MSVC)
    target_compile_options(telescope_measure_core PRIVATE /utf-8)
else ()
    target_compile_options(telescope_measure_core PRIVATE -fno-fast-math)
endif ()

juce_add_console_app(telescope-measure PRODUCT_NAME "telescope-measure")
target_sources(telescope-measure PRIVATE main.cpp)
target_compile_features(telescope-measure PRIVATE cxx_std_20)
target_compile_definitions(telescope-measure PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(telescope-measure PRIVATE
    telescope_measure_core
    juce::juce_core
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_recommended_config_flags)
if (MSVC)
    target_compile_options(telescope-measure PRIVATE /utf-8)
else ()
    target_compile_options(telescope-measure PRIVATE -fno-fast-math)
endif ()

# El generador del juego fijo para comparar la Mac con Windows (hito 5): C++ puro, sin JUCE y sin un float en el
# camino de la señal (ver su encabezado). -ffp-contract=off por las dudas: no hay a·b + c en float, y que siga así.
add_executable(telescope-measure-fixtures fixtures/MakeFixtures.cpp)
target_compile_features(telescope-measure-fixtures PRIVATE cxx_std_20)
if (NOT MSVC)
    target_compile_options(telescope-measure-fixtures PRIVATE -ffp-contract=off)
endif ()
