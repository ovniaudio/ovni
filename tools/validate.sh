#!/usr/bin/env bash
# tools/validate.sh — Determinístico (0 tokens). Valida un plugin del catálogo OVNI.
#
# Uso:   tools/validate.sh <plugin> [CODE] [MANU]
#   <plugin>  slug del plugin (target CMake <plugin>_All). p.ej. pulsar
#   [CODE]    PLUGIN_CODE para auval. Si se omite, se autodetecta del CMakeLists del plugin.
#   [MANU]    MANUFACTURER_CODE para auval. Si se omite, se autodetecta.
#
# Env overrides: BUILD_DIR (build) · CONFIG (Release) · CEILING (0.85) · TEST_FILTER (regex ctest)
#                · PLUGINVAL (ruta a un binario pluginval ya instalado)
#
# Puertas (fail-fast, lo barato primero): build → universal(lipo) → pluginval lvl8 (VST3+AU)
#                                          → auval → tests(ctest) → gain-staging (anti-clip).
# Imprime el validate-report (schemas/validate-report.json) por stdout. Progreso por stderr.
# Exit: 0 si ok · 1 si falla alguna puerta. NUNCA aborta a mitad: siempre emite un reporte.

set -uo pipefail
log() { printf '  validate[%s]: %s\n' "${PLUGIN:-?}" "$*" >&2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS="$ROOT/tools"
PLUGIN="${1:-}"
CODE_ARG="${2:-}"
MANU_ARG="${3:-}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
CONFIG="${CONFIG:-Release}"
CEILING="${CEILING:-0.85}"
TEST_FILTER="${TEST_FILTER:-}"

WORK="$(mktemp -d 2>/dev/null || echo /tmp/ovni-validate-$$)"; mkdir -p "$WORK"
ERR_FILE="$WORK/errors.txt"; : > "$ERR_FILE"
LOG_FILE="$WORK/log.txt"; : > "$LOG_FILE"
add_err() { printf '%s\n' "$*" >> "$ERR_FILE"; }
cleanup() { rm -rf "$WORK" 2>/dev/null; }
trap cleanup EXIT

# estado de cada puerta (1=pase 0=falla, "skip"=no aplicable)
BUILD_OK=0; WARNINGS=0; TARGET="${PLUGIN}_All"
UNIVERSAL_OK=0; ARCHS=""
PV_VST3="failed"; PV_AU="failed"
AUVAL_OK=0; AUVAL_STATUS="failed"; CODE=""; MANU=""
T_TOTAL=0; T_PASS=0; T_FAIL=0; T_FAILURES=""
PEAK="-1"; CLIP="false"
STAGE="build"

# ── Mediciones del sello (house-standard §2: los 3 números públicos + IACC). Gates H7/H8/H9.
#    ALIAS_FLOOR: gate H7 (dBFS). Default −96 (house §1, resampler limpio). HALO declara el peaje del granular
#    splice (§3) → se invoca con ALIAS_FLOOR=-55. CPU_BUDGET: gate H9 (WARN, no rompe el build — hardware del
#    runner ≠ el de Joaquín; el número se publica). LAT_TOL: gate H8 (samples).
ALIAS_FLOOR="${ALIAS_FLOOR:--96}"
CPU_BUDGET="${CPU_BUDGET:-5}"
LAT_TOL="${LAT_TOL:-1}"
M_ALIAS="null"; M_LAT_REP="null"; M_LAT_REAL="null"; M_CPU="null"; M_IACC="null"
H_ALIAS="skip"; H_LATENCY="skip"; H_CPU="skip"

emit_and_exit() { # <ok 0|1>
  local ok="$1"
  OK="$ok" PLUGIN="$PLUGIN" TARGET="$TARGET" WARNINGS="$WARNINGS" \
  BUILD_OK="$BUILD_OK" UNIVERSAL_OK="$UNIVERSAL_OK" ARCHS="$ARCHS" \
  PV_VST3="$PV_VST3" PV_AU="$PV_AU" \
  AUVAL_OK="$AUVAL_OK" AUVAL_STATUS="$AUVAL_STATUS" CODE="$CODE" MANU="$MANU" \
  T_TOTAL="$T_TOTAL" T_PASS="$T_PASS" T_FAIL="$T_FAIL" T_FAILURES="$T_FAILURES" \
  PEAK="$PEAK" CEILING="$CEILING" CLIP="$CLIP" STAGE="$STAGE" \
  M_ALIAS="$M_ALIAS" M_LAT_REP="$M_LAT_REP" M_LAT_REAL="$M_LAT_REAL" M_CPU="$M_CPU" M_IACC="$M_IACC" \
  ALIAS_FLOOR="$ALIAS_FLOOR" CPU_BUDGET="$CPU_BUDGET" LAT_TOL="$LAT_TOL" \
  H_ALIAS="$H_ALIAS" H_LATENCY="$H_LATENCY" H_CPU="$H_CPU" \
  ERR_FILE="$ERR_FILE" LOG_FILE="$LOG_FILE" \
  python3 "$TOOLS/_emit_report.py"
  [ "$ok" = "1" ] && exit 0 || exit 1
}

[ -n "$PLUGIN" ] || { add_err "uso: validate.sh <plugin> [CODE] [MANU]"; STAGE="build"; emit_and_exit 0; }

# ============ Puerta 0: LEGAL (LICENSE + NOTICE) ============
# Gate de release del sello: ningún tag sin licencia + avisos de terceros. Es la puerta más barata
# (0 build) → va primero. La marca OVNI = honestidad verificable: distribuir un binario sin LICENSE/NOTICE
# es violación de licencia (JUCE) y un claim de marca falso. Ver skill/references/legal.md.
STAGE="legal"
MISSING_LEGAL=""
[ -f "$ROOT/LICENSE" ]   || MISSING_LEGAL="$MISSING_LEGAL LICENSE"
[ -f "$ROOT/NOTICE.md" ] || MISSING_LEGAL="$MISSING_LEGAL NOTICE.md"
if [ -n "$MISSING_LEGAL" ]; then
  add_err "gate legal: faltan en la raíz del repo:$MISSING_LEGAL. Ningún tag/release sin LICENSE + NOTICE.md (atribuciones de terceros)."
  log "gate legal FALLÓ — faltan:$MISSING_LEGAL"
  emit_and_exit 0
fi
log "gate legal OK (LICENSE + NOTICE.md presentes)"

# ============ Puerta 1: BUILD ============
STAGE="build"
if [ ! -d "$BUILD_DIR" ]; then
  log "no existe BUILD_DIR=$BUILD_DIR (configurá con: cmake -B build -G Ninja -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64')"
  add_err "build dir no encontrado: $BUILD_DIR. Configurá CMake antes de validar (es trabajo de S5)."
  emit_and_exit 0
fi
log "build target $TARGET ($CONFIG)…"
( cmake --build "$BUILD_DIR" --config "$CONFIG" --target "$TARGET" ) > "$LOG_FILE" 2>&1
BUILD_RC=$?
WARNINGS="$(grep -c -E 'warning:' "$LOG_FILE" 2>/dev/null || true)"; WARNINGS="${WARNINGS:-0}"
if [ "$BUILD_RC" -ne 0 ]; then
  log "build FALLÓ (rc=$BUILD_RC)"
  grep -E 'error:|Error|undefined|ld: ' "$LOG_FILE" | head -n 40 >> "$ERR_FILE"
  [ -s "$ERR_FILE" ] || tail -n 40 "$LOG_FILE" >> "$ERR_FILE"
  emit_and_exit 0
fi
BUILD_OK=1; log "build OK (${WARNINGS} warnings)"

# ============ Puerta 2: UNIVERSAL (lipo) ============
STAGE="universal"
BINARIES=()
while IFS= read -r bin; do
  [ -n "$bin" ] && BINARIES+=("$bin")
done < <(
  find "$BUILD_DIR" -path "*_artefacts/$CONFIG/*" \
    \( -name '*.vst3' -o -name '*.component' -o -name '*.app' \) 2>/dev/null \
  | while read -r bundle; do
      base="$(basename "$bundle")"; name="${base%.*}"
      b="$bundle/Contents/MacOS/$name"
      [ -f "$b" ] && printf '%s\n' "$b"
    done
)
if [ "${#BINARIES[@]}" -eq 0 ]; then
  log "no encontré artefactos en $BUILD_DIR/*_artefacts/$CONFIG/"
  add_err "no se encontraron binarios VST3/AU/Standalone tras el build."
  emit_and_exit 0
fi
UNIVERSAL_OK=1
for bin in "${BINARIES[@]}"; do
  a="$(lipo -archs "$bin" 2>/dev/null)"
  ARCHS="$ARCHS$(basename "$bin")=[$a] "
  if ! printf '%s' "$a" | grep -q 'arm64' || ! printf '%s' "$a" | grep -q 'x86_64'; then
    UNIVERSAL_OK=0
    add_err "no universal: $bin → archs '$a' (faltan arm64 y/o x86_64)"
  fi
done
if [ "$UNIVERSAL_OK" -ne 1 ]; then log "universal FALLÓ"; emit_and_exit 0; fi
log "universal OK ($ARCHS)"

# ============ Puerta 3: PLUGINVAL lvl8 (VST3 + AU) ============
STAGE="pluginval"
PV_BIN="${PLUGINVAL:-}"
if [ -z "$PV_BIN" ]; then
  CACHED="$BUILD_DIR/pluginval.app/Contents/MacOS/pluginval"
  if [ -x "$CACHED" ]; then
    PV_BIN="$CACHED"
  elif command -v curl >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1; then
    log "descargando pluginval v1.0.4…"
    # Pineado a una release fija (no /releases/latest): reproducibilidad — la puerta mide siempre
    # contra la MISMA versión de pluginval; una release nueva no cambia el veredicto sin querer.
    if curl -fsSL -o "$BUILD_DIR/pluginval.zip" \
        "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/pluginval_macOS.zip" 2>>"$LOG_FILE" \
       && unzip -oq "$BUILD_DIR/pluginval.zip" -d "$BUILD_DIR" 2>>"$LOG_FILE"; then
      PV_BIN="$CACHED"
    fi
  fi
fi
run_pluginval() { # <ruta-artefacto> → echo passed|failed
  local art="$1"
  if [ -z "$PV_BIN" ] || [ ! -x "$PV_BIN" ]; then echo "skipped"; return; fi
  if [ -z "$art" ] || [ ! -e "$art" ]; then echo "skipped"; return; fi
  if "$PV_BIN" --strictness-level 8 --validate-in-process "$art" >>"$LOG_FILE" 2>&1; then
    echo "passed"
  else
    echo "failed"
  fi
}
# Acotar la selección AL plugin bajo prueba. Los artefactos viven en
# build/plugins/<plugin>/<plugin>_artefacts/$CONFIG/{VST3,AU}/ (el dir usa el slug, p.ej.
# pulsar_artefacts, aunque el PRODUCT_NAME difiera: _probe_artefacts → PROBE.component).
# Antes el glob era "*_artefacts/…" SIN acotar: con artefactos de varios plugins en el árbol
# compartido, `find … | head -n1` elegía CUALQUIERA en orden de recorrido del FS (no
# determinístico). A veces devolvía el _probe (AU no registrado en macOS) → pluginval
# "Num plugins found: 0 … FAILURE" → puerta 3 roja espuria, intermitente. Acotar a
# plugins/$PLUGIN/${PLUGIN}_artefacts vuelve la selección determinística y evita colisiones
# de slugs que sean sufijo de otro (p.ej. "probe" vs "_probe").
VST3_ART="$(find "$BUILD_DIR" -path "*/plugins/$PLUGIN/${PLUGIN}_artefacts/$CONFIG/VST3/*.vst3" 2>/dev/null | head -n1)"
AU_ART="$(find "$BUILD_DIR" -path "*/plugins/$PLUGIN/${PLUGIN}_artefacts/$CONFIG/AU/*.component" 2>/dev/null | head -n1)"
PV_VST3="$(run_pluginval "$VST3_ART")"
PV_AU="$(run_pluginval "$AU_ART")"
log "pluginval VST3=$PV_VST3 AU=$PV_AU"
if [ "$PV_VST3" != "passed" ] || [ "$PV_AU" != "passed" ]; then
  [ "$PV_VST3" = "skipped" ] && add_err "pluginval VST3: skipped (binario no disponible)." || true
  [ "$PV_VST3" = "failed" ]  && { add_err "pluginval VST3 FALLÓ (lvl8):"; grep -iE 'fail|error|\*\*\*' "$LOG_FILE" | tail -n 20 >> "$ERR_FILE"; }
  [ "$PV_AU" = "skipped" ]   && add_err "pluginval AU: skipped (binario no disponible)." || true
  [ "$PV_AU" = "failed" ]    && add_err "pluginval AU FALLÓ (lvl8)." || true
  emit_and_exit 0
fi

# ============ Puerta 4: AUVAL ============
STAGE="auval"
CODE="$CODE_ARG"; MANU="$MANU_ARG"
PLUGIN_CMAKE="$ROOT/plugins/$PLUGIN/CMakeLists.txt"
if [ -z "$CODE" ] && [ -f "$PLUGIN_CMAKE" ]; then
  CODE="$(grep -E 'PLUGIN_CODE' "$PLUGIN_CMAKE" | grep -oE '"[A-Za-z0-9]{4}"' | head -n1 | tr -d '"')"
fi
if [ -z "$MANU" ] && [ -f "$PLUGIN_CMAKE" ]; then
  MANU="$(grep -E 'PLUGIN_MANUFACTURER_CODE|MANUFACTURER_CODE' "$PLUGIN_CMAKE" | grep -oE '"[A-Za-z0-9]{4}"' | head -n1 | tr -d '"')"
fi
if [ "$(uname -s)" != "Darwin" ] || ! command -v auval >/dev/null 2>&1; then
  AUVAL_STATUS="skipped"; AUVAL_OK=0
  add_err "auval: skipped (no es macOS o auval no disponible)."
  log "auval skipped"
elif [ -z "$CODE" ] || [ -z "$MANU" ]; then
  AUVAL_STATUS="skipped"; AUVAL_OK=0
  add_err "auval: skipped (no pude determinar CODE/MANU; pasalos como args)."
  log "auval skipped (sin códigos)"
else
  log "auval -v aufx $CODE $MANU…"
  if auval -v aufx "$CODE" "$MANU" >>"$LOG_FILE" 2>&1; then
    AUVAL_OK=1; AUVAL_STATUS="passed"
  else
    # Falso negativo común: tras renombrar/recompilar un AU, el registro de macOS queda stale
    # ("Cannot open component: -1") aunque el binario sea válido (pluginval AU pasa). Refrescamos el
    # AudioComponentRegistrar y reintentamos UNA vez antes de declarar fallo (robustez Fase 2/3).
    log "auval falló — refresco AudioComponentRegistrar y reintento…"
    killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
    sleep 2
    if auval -v aufx "$CODE" "$MANU" >>"$LOG_FILE" 2>&1; then
      AUVAL_OK=1; AUVAL_STATUS="passed"
    else
      AUVAL_OK=0; AUVAL_STATUS="failed"
      add_err "auval FALLÓ para aufx $CODE $MANU (incluso tras refrescar el registro AU)."
    fi
  fi
fi
if [ "$AUVAL_OK" -ne 1 ]; then emit_and_exit 0; fi

# ============ Puerta 5: TESTS (ctest) ============
STAGE="tests"
if command -v ctest >/dev/null 2>&1; then
  CT_OUT="$WORK/ctest.txt"
  if [ -n "$TEST_FILTER" ]; then
    ( ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$TEST_FILTER" ) > "$CT_OUT" 2>&1
  else
    ( ctest --test-dir "$BUILD_DIR" --output-on-failure ) > "$CT_OUT" 2>&1
  fi
  # parse "X% tests passed, Y tests failed out of Z"
  PASSED_LINE="$(grep -E '% tests passed' "$CT_OUT" | tail -n1)"
  T_TOTAL="$(printf '%s' "$PASSED_LINE" | grep -oE 'out of [0-9]+' | grep -oE '[0-9]+' | tail -n1)"; T_TOTAL="${T_TOTAL:-0}"
  T_FAIL="$(printf '%s' "$PASSED_LINE" | grep -oE '[0-9]+ tests failed' | grep -oE '^[0-9]+' | tail -n1)"; T_FAIL="${T_FAIL:-0}"
  T_PASS=$(( T_TOTAL - T_FAIL ))
  if [ "${T_FAIL:-0}" -gt 0 ]; then
    T_FAILURES="$(grep -E 'Failed|\*\*\*Failed' "$CT_OUT" | head -n 20 | tr '\n' ';')"
    add_err "tests rojos: $T_FAILURES"
    log "tests FALLARON ($T_FAIL/$T_TOTAL)"
    emit_and_exit 0
  fi
  log "tests OK ($T_PASS/$T_TOTAL)"
else
  log "ctest no disponible → tests skipped"
  add_err "ctest no disponible: no se corrieron tests."
fi

# ============ Puerta 6: GAIN-STAGING (anti-clip) ============
STAGE="gain"
GAIN_JSON="$("$TOOLS/gain-staging-check.sh" "$PLUGIN" --ceiling "$CEILING" --build-dir "$BUILD_DIR" 2>/dev/null)"
PEAK="$(printf '%s' "$GAIN_JSON" | python3 -c 'import json,sys;
try: print(json.load(sys.stdin).get("peak",-1))
except: print(-1)' 2>/dev/null || echo -1)"
if python3 -c "import sys; sys.exit(0 if float('$PEAK')>=0 and float('$PEAK')<=float('$CEILING') else 1)"; then
  CLIP="false"; log "gain OK (peak=$PEAK ≤ ceiling=$CEILING)"
else
  if python3 -c "import sys; sys.exit(0 if float('$PEAK')<0 else 1)"; then
    add_err "gain-staging: no se pudo medir el peak (¿falta el test [gain] del template?)."
    CLIP="false"; log "gain no medido"
    # no medir no es clip, pero rompe el verde porque no se pudo confirmar anti-clip
    emit_and_exit 0
  else
    CLIP="true"; add_err "CLIP: peak=$PEAK > ceiling=$CEILING. Rampear ganancias por-sample + limiter."
    log "gain FALLÓ — CLIP"
    emit_and_exit 0
  fi
fi

# ============ Puertas H7/H8/H9: MEDICIONES (house-standard §2) ============
# Los 3 números públicos (alias floor / latencia / CPU) + IACC se MIDEN con measure-check.sh (espejo del
# gate anti-clip). H7 (alias) y H8 (latencia) son DUROS (rompen el verde); H9 (CPU) es WARN (el budget es
# hardware-dependiente — se publica el número, no se hace fallar el build). Si el plugin no provee los tests
# [alias]/[latency]/[bench]/[measure], los campos quedan null y las puertas quedan "skip" (no rompen).
STAGE="measure"
MEAS_JSON="$("$TOOLS/measure-check.sh" "$PLUGIN" --build-dir "$BUILD_DIR" 2>/dev/null)"
extract() { printf '%s' "$MEAS_JSON" | python3 -c "import json,sys
try:
    d=json.load(sys.stdin); v=d.get('$1')
    print('null' if v is None else v)
except: print('null')" 2>/dev/null || echo null; }
M_ALIAS="$(extract alias_floor_dbfs)"
M_LAT_REP="$(extract latency_reported)"
M_LAT_REAL="$(extract latency_real)"
M_CPU="$(extract cpu_pct)"
M_IACC="$(extract iacc)"
log "mediciones: alias=$M_ALIAS dBFS  latency rep=$M_LAT_REP/real=$M_LAT_REAL  cpu=$M_CPU%  iacc=$M_IACC"

# ── H7: alias floor < ALIAS_FLOOR (dBFS). Gate DURO.
if [ "$M_ALIAS" != "null" ]; then
  if python3 -c "import sys; sys.exit(0 if float('$M_ALIAS') < float('$ALIAS_FLOOR') else 1)"; then
    H_ALIAS="passed"; log "H7 alias OK ($M_ALIAS < $ALIAS_FLOOR dBFS)"
  else
    H_ALIAS="failed"; add_err "H7 alias: piso=$M_ALIAS dBFS NO cumple < $ALIAS_FLOOR dBFS (más OS o declarar el peaje)."
    log "H7 alias FALLÓ"; emit_and_exit 0
  fi
fi

# ── H8: latencia reportada == real (±LAT_TOL). Gate DURO.
if [ "$M_LAT_REP" != "null" ] && [ "$M_LAT_REAL" != "null" ]; then
  if python3 -c "import sys; sys.exit(0 if abs(int('$M_LAT_REP')-int('$M_LAT_REAL'))<=int('$LAT_TOL') else 1)"; then
    H_LATENCY="passed"; log "H8 latencia OK (rep=$M_LAT_REP real=$M_LAT_REAL, ±$LAT_TOL)"
  else
    H_LATENCY="failed"; add_err "H8 latencia: reportada=$M_LAT_REP ≠ real=$M_LAT_REAL (>$LAT_TOL samples). Reportar PDC correcto."
    log "H8 latencia FALLÓ"; emit_and_exit 0
  fi
fi

# ── H9: CPU vs budget. WARN (no rompe el verde; el número se publica).
if [ "$M_CPU" != "null" ]; then
  if python3 -c "import sys; sys.exit(0 if float('$M_CPU') <= float('$CPU_BUDGET') else 1)"; then
    H_CPU="passed"; log "H9 CPU OK ($M_CPU% ≤ budget $CPU_BUDGET%)"
  else
    H_CPU="warn"; WARNINGS=$((WARNINGS+1))
    add_err "H9 CPU (WARN): $M_CPU% > budget $CPU_BUDGET% (no rompe el build; medido en el runner, no en el hw final)."
    log "H9 CPU WARN ($M_CPU% > $CPU_BUDGET%)"
  fi
fi

# ============ VERDE ============
STAGE="ok"
log "✓ TODAS las puertas en verde"
emit_and_exit 1
