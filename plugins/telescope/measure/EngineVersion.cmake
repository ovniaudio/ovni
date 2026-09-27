# ========================================================================================================
# EngineVersion.cmake — escribe TelescopeMeasureVersion.h en CADA build (cmake -P, desde measure/CMakeLists.txt).
#
# telescope-measure firma cada línea con la versión y el sha del motor (`engine`, contrato D-100). Si el sha se
# fijara al configurar, un commit sin reconfigurar dejaría un binario que dice ser otro. Por eso corre en cada
# build y sólo reescribe el header si cambió (así no recompila nada cuando no hace falta).
#
#   VERSION_FILE  plugins/telescope/VERSION
#   REPO_DIR      la raíz del repo
#   OUT           el header generado
#
# El sha son 12 hex de HEAD; si el árbol tiene cambios en archivos que git sigue, lleva "+dirty": un binario
# compilado con cambios sin commitear no puede decir que es ese commit. Sin git, "unknown".
# ========================================================================================================
file(READ "${VERSION_FILE}" _version)
string(STRIP "${_version}" _version)

set(_sha "unknown")
find_program(_git git)
if (_git)
    execute_process(COMMAND "${_git}" -C "${REPO_DIR}" rev-parse --short=12 HEAD
                    OUTPUT_VARIABLE _head RESULT_VARIABLE _rc OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if (_rc EQUAL 0 AND _head)
        set(_sha "${_head}")
        execute_process(COMMAND "${_git}" -C "${REPO_DIR}" status --porcelain --untracked-files=no
                        OUTPUT_VARIABLE _dirty RESULT_VARIABLE _rc2 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (NOT _rc2 EQUAL 0 OR _dirty)
            set(_sha "${_sha}+dirty")
        endif ()
    endif ()
endif ()

set(_text "// GENERADO por measure/EngineVersion.cmake en cada build. No se edita ni se commitea.\n#pragma once\n#define TELESCOPE_MEASURE_VERSION \"${_version}\"\n#define TELESCOPE_MEASURE_SHA \"${_sha}\"\n")
set(_old "")
if (EXISTS "${OUT}")
    file(READ "${OUT}" _old)
endif ()
if (NOT _old STREQUAL _text)
    file(WRITE "${OUT}" "${_text}")
endif ()
