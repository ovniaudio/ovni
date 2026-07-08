#!/usr/bin/env bash
# tools/measure-check.sh — Determinístico (0 tokens). Espejo de gain-staging-check.sh para las MEDICIONES
# del sello (house-standard §2): los 3 números públicos + IACC. Localiza el ejecutable de tests Catch2 del
# plugin, corre los filtros [alias]/[latency]/[bench]/[measure] y grepea sus líneas (ALIAS_DBFS=, CPU_PCT=,
# LATENCY_REPORTED=, LATENCY_REAL=, IACC=).
#
# Uso:   tools/measure-check.sh <plugin> [--build-dir build]
# Salida: una línea JSON por stdout con las métricas encontradas (las ausentes = null). Progreso por stderr.
# Exit: 0 siempre que pudo correr (la EVALUACIÓN de los gates la hace validate.sh; acá sólo se MIDE y emite).
#
# DEPENDENCIA (house-standard): el plugin provee tests Catch2 etiquetados [alias]/[latency]/[bench]/[measure]
# que imprimen esas líneas (ver plugins/<plugin>/tests/*Test.cpp). Sin ellos, el campo queda null.

set -uo pipefail
log() { printf '  measure: %s\n' "$*" >&2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"

ARGS=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    *)           ARGS+=("$1"); shift ;;
  esac
done
PLUGIN="${ARGS[0]:-}"
[ -n "$PLUGIN" ] || { echo '{"tool":"measure","ok":false,"error":"falta <plugin>"}'; exit 2; }

# Localizar el ejecutable de tests del plugin (PREFERIR el por-plugin: Ovni<Cap>Tests). Mismo find que el
# gate anti-clip. Fallback: cualquier Ovni*Tests (no Presets).
CAP="$(printf '%s' "${PLUGIN:0:1}" | tr '[:lower:]' '[:upper:]')${PLUGIN:1}"
TESTS_BIN="$(find "$BUILD_DIR" -maxdepth 5 -type f -name "Ovni${CAP}Tests" -perm -111 2>/dev/null | head -n1)"
[ -z "$TESTS_BIN" ] && TESTS_BIN="$(find "$BUILD_DIR" -maxdepth 5 -type f -name 'Ovni*Tests' ! -name '*Presets*' -perm -111 2>/dev/null | head -n1)"

if [ -z "$TESTS_BIN" ] || [ ! -x "$TESTS_BIN" ]; then
  log "no hay ejecutable de tests del plugin (¿configuraste/compilaste?) → no se pudo medir"
  echo "{\"tool\":\"measure\",\"plugin\":\"$PLUGIN\",\"ok\":false,\"error\":\"sin ejecutable de tests\"}"
  exit 2
fi

# Corre un filtro Catch2 y devuelve su salida combinada.
run_filter() { local f="$1"; "$TESTS_BIN" "$f" 2>&1; }

# Extrae el valor de una clave 'KEY=' (último match) de un texto.
grab() { printf '%s' "$1" | grep -oE "$2=[-+]?[0-9]+(\.[0-9]+)?" | tail -n1 | cut -d= -f2; }

ALIAS=""; LAT_REP=""; LAT_REAL=""; CPU=""; IACC=""

OUT_ALIAS="$(run_filter "[alias][$PLUGIN]")"; [ -z "$OUT_ALIAS" ] && OUT_ALIAS="$(run_filter "[alias]")"
ALIAS="$(grab "$OUT_ALIAS" ALIAS_DBFS)"

OUT_LAT="$(run_filter "[latency][$PLUGIN]")"; [ -z "$OUT_LAT" ] && OUT_LAT="$(run_filter "[latency]")"
LAT_REP="$(grab "$OUT_LAT" LATENCY_REPORTED)"
LAT_REAL="$(grab "$OUT_LAT" LATENCY_REAL)"

OUT_BENCH="$(run_filter "[bench][$PLUGIN]")"; [ -z "$OUT_BENCH" ] && OUT_BENCH="$(run_filter "[bench]")"
CPU="$(grab "$OUT_BENCH" CPU_PCT)"

OUT_MEAS="$(run_filter "[measure][$PLUGIN]")"; [ -z "$OUT_MEAS" ] && OUT_MEAS="$(run_filter "[measure]")"
IACC="$(grab "$OUT_MEAS" IACC)"

log "alias=${ALIAS:-n/a} dBFS  latency reported=${LAT_REP:-n/a}/real=${LAT_REAL:-n/a}  cpu=${CPU:-n/a}%  iacc=${IACC:-n/a}"

python3 - "$PLUGIN" "${ALIAS:-}" "${LAT_REP:-}" "${LAT_REAL:-}" "${CPU:-}" "${IACC:-}" <<'PY'
import json, sys
plugin = sys.argv[1]
def num(s):
    try: return float(s)
    except: return None
alias, latrep, latreal, cpu, iacc = (num(sys.argv[i]) for i in range(2, 7))
print(json.dumps({
    "tool": "measure", "plugin": plugin, "ok": True,
    "alias_floor_dbfs": alias,
    "latency_reported": (int(latrep) if latrep is not None else None),
    "latency_real":     (int(latreal) if latreal is not None else None),
    "cpu_pct": cpu,
    "iacc": iacc,
}))
PY
