#!/usr/bin/env bash
# tools/gain-staging-check.sh — Determinístico (0 tokens). Peak de salida con señal full-scale.
#
# Uso:   tools/gain-staging-check.sh <plugin> [--ceiling 0.85] [--build-dir build]
#
# El guard anti-clip del orquestador. Corre el test [gain] del plugin —que pasa una señal
# FULL-SCALE por el plugin ensamblado e imprime `PEAK=<peak lineal>`— y lo grepea.
#
# DEPENDENCIA DEL TEMPLATE (S3/S5): cada plugin (o el template base) provee un test Catch2
# etiquetado [gain] que procesa full-scale y hace `std::cout << "PEAK=" << peak << "\n";`.
# Ver docs/CONTRACT-CHANGES.md (S4). Sin ese test, este script reporta method="none" (no mide).
#
# Salida: una línea JSON por stdout {plugin,peak,ceiling,clip,method,ok}. Peak legible por stderr.
# Exit: 0 si peak ≤ ceiling · 1 si clip · 2 si no se pudo medir.

set -uo pipefail
log() { printf '  gain: %s\n' "$*" >&2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CEILING="0.85"
BUILD_DIR="$ROOT/build"

ARGS=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --ceiling)   CEILING="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    *)           ARGS+=("$1"); shift ;;
  esac
done
PLUGIN="${ARGS[0]:-}"
[ -n "$PLUGIN" ] || { echo '{"tool":"gain","ok":false,"method":"none","peak":-1,"error":"falta <plugin>"}'; exit 2; }
[ "$(uname -s)" = "Darwin" ] || true

emit() { # <peak> <method>
  local peak="$1" method="$2"
  python3 - "$PLUGIN" "$peak" "$CEILING" "$method" <<'PY'
import json, sys
plugin, peak, ceiling, method = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
clip = (peak > ceiling) if peak >= 0 else False
ok = (peak >= 0) and (not clip)
print(json.dumps({"tool":"gain","plugin":plugin,"peak":peak,"ceiling":ceiling,
                  "clip":clip,"method":method,"ok":ok}))
PY
}

# localizar el ejecutable de tests Catch2 (lo arma S5/S3). PREFERIR el binario POR-PLUGIN:
# pulsar -> OvniPulsarTests (PRODUCT_NAME sin espacios) -> el filtro [gain][pulsar] corre el test del
# PulsarProcessor REAL y NO el [gain] del _probe. Fallback: cualquier Ovni*Tests (no el de Presets).
CAP="$(printf '%s' "${PLUGIN:0:1}" | tr '[:lower:]' '[:upper:]')${PLUGIN:1}"
TESTS_BIN="$(find "$BUILD_DIR" -maxdepth 5 -type f -name "Ovni${CAP}Tests" -perm -111 2>/dev/null | head -n1)"
[ -z "$TESTS_BIN" ] && TESTS_BIN="$(find "$BUILD_DIR" -maxdepth 5 -type f -name 'Ovni*Tests' ! -name '*Presets*' -perm -111 2>/dev/null | head -n1)"

run_filter() { # corre un filtro Catch2 y devuelve la salida
  local filter="$1"
  if [ -n "$TESTS_BIN" ] && [ -x "$TESTS_BIN" ]; then
    "$TESTS_BIN" "$filter" 2>&1
  elif command -v ctest >/dev/null 2>&1 && [ -d "$BUILD_DIR" ]; then
    ctest --test-dir "$BUILD_DIR" -R "gain" -V 2>&1
  fi
}

if [ -z "$TESTS_BIN" ] && { ! command -v ctest >/dev/null 2>&1 || [ ! -d "$BUILD_DIR" ]; }; then
  log "no hay ejecutable de tests ni ctest/build (¿corriste S5?) → no se pudo medir"
  emit "-1" "none"
  exit 2
fi

# probar etiquetas de más específica a más general
PEAK=""
for filter in "[gain][$PLUGIN]" "[$PLUGIN][gain]" "[gain]"; do
  OUT="$(run_filter "$filter")"
  PEAK="$(printf '%s' "$OUT" | grep -oE 'PEAK=[0-9]+(\.[0-9]+)?' | tail -n1 | cut -d= -f2)"
  [ -n "$PEAK" ] && { log "filtro '$filter' → PEAK=$PEAK"; break; }
done

if [ -z "$PEAK" ]; then
  log "no encontré 'PEAK=' en la salida del test [gain] (¿el template lo provee?) → no se pudo medir"
  emit "-1" "none"
  exit 2
fi

emit "$PEAK" "test[gain]"
# exit code según clip
python3 -c "import sys; sys.exit(1 if float('$PEAK') > float('$CEILING') else 0)"
