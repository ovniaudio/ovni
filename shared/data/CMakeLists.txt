# ovni_data — datos HRIR horneados (header-only): HrirData.h (90° fijo) + HrirRing.h (anillo 72 dirs).
# FORWARD-LOOKING: los consumen los módulos binaurales (ÓRBITA/POLVO), no el motor de movimiento base.
# INTERFACE: sólo expone el include path (#include "data/HrirRing.h").

add_library(ovni_data INTERFACE)
target_include_directories(ovni_data INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(ovni_data INTERFACE cxx_std_20)
