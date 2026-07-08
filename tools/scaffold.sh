#!/usr/bin/env bash
# tools/scaffold.sh — Determinístico (0 tokens). Crea plugins/<plugin>/ desde shared/template/.
#
# Uso:   tools/scaffold.sh <plugin> <CODE> <MANU> [DISPLAY] [--force]
#   <plugin>   slug en minúsculas (= PROJECT_NAME de CMake y target <plugin>_All). p.ej. pulsar
#   <CODE>     PLUGIN_CODE de JUCE, 4 chars, 1ra mayúscula. p.ej. Plsr
#   <MANU>     MANUFACTURER_CODE de JUCE, 4 chars, 1ra mayúscula. p.ej. Ovni
#   [DISPLAY]  PRODUCT_NAME para el DAW (MAYÚSCULAS). default = uppercase(<plugin>). p.ej. PULSAR
#   --force    sobreescribe plugins/<plugin>/ si ya existe
#
# Reemplaza los placeholders del CONTRATO: @PLUGIN_NAME@ @PLUGIN_CODE@ @MANUFACTURER_CODE@
# y, si el template los expone, @PRODUCT_NAME@ y @BUNDLE_ID@ (defensivo).
# Crea la tabla de presets de fábrica VACÍA si el template no la trae.
# Imprime un JSON de resultado por stdout (lo parsea el Workflow). Progreso por stderr.

set -uo pipefail

log() { printf '  scaffold: %s\n' "$*" >&2; }
die_json() { # <msg>
  printf '{"tool":"scaffold","ok":false,"error":%s}\n' "$(json_str "$1")"
  exit 1
}
json_str() { # escapa un string a JSON
  python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$1" 2>/dev/null \
    || printf '"%s"' "$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')"
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --- args ---
FORCE=0
ARGS=()
for a in "$@"; do
  if [ "$a" = "--force" ]; then FORCE=1; else ARGS+=("$a"); fi
done
[ "${#ARGS[@]}" -ge 3 ] || die_json "uso: scaffold.sh <plugin> <CODE> <MANU> [DISPLAY] [--force]"

PLUGIN="${ARGS[0]}"
CODE="${ARGS[1]}"
MANU="${ARGS[2]}"
DISPLAY="${ARGS[3]:-$(printf '%s' "$PLUGIN" | tr '[:lower:]' '[:upper:]')}"

# --- validaciones ---
[ "${#CODE}" -eq 4 ] || die_json "PLUGIN_CODE debe tener 4 chars (recibí '$CODE')"
[ "${#MANU}" -eq 4 ] || die_json "MANUFACTURER_CODE debe tener 4 chars (recibí '$MANU')"
printf '%s' "$PLUGIN" | grep -Eq '^[a-z][a-z0-9_]*$' \
  || die_json "<plugin> debe ser slug en minúsculas (^[a-z][a-z0-9_]*$): '$PLUGIN'"

TEMPLATE="$ROOT/shared/template"
DEST="$ROOT/plugins/$PLUGIN"

[ -d "$TEMPLATE" ] || die_json "no existe el template en $TEMPLATE (lo provee S3). ¿Corriste S3?"
if [ -e "$DEST" ]; then
  if [ "$FORCE" -eq 1 ]; then log "borrando $DEST (--force)"; rm -rf "$DEST"; else
    die_json "ya existe plugins/$PLUGIN (usá --force para sobreescribir)"
  fi
fi

# --- copia ---
log "copiando shared/template → plugins/$PLUGIN"
mkdir -p "$ROOT/plugins"
cp -R "$TEMPLATE" "$DEST" || die_json "falló copiar el template"
# limpiar artefactos del template que NO van en la instancia
rm -f "$DEST/_README.md"
find "$DEST" -name '*.bak' -delete 2>/dev/null

# --- reemplazo de placeholders ---
replace_token() { # <token> <valor>
  local token="$1" value="$2"
  TOKEN="$token" VALUE="$value" find "$DEST" -type f \
    \( -name '*.in' -o -name '*.txt' -o -name '*.h' -o -name '*.hpp' -o -name '*.cpp' \
       -o -name '*.cmake' -o -name '*.md' -o -name 'CMakeLists*' \) \
    -exec perl -pi -e 's/\Q$ENV{TOKEN}\E/$ENV{VALUE}/g' {} +
}
log "reemplazando placeholders (PLUGIN_NAME=$PLUGIN CODE=$CODE MANU=$MANU PRODUCT=$DISPLAY)"
replace_token '@PLUGIN_NAME@'        "$PLUGIN"
replace_token '@PLUGIN_CODE@'        "$CODE"
replace_token '@MANUFACTURER_CODE@'  "$MANU"
replace_token '@PRODUCT_NAME@'       "$DISPLAY"          # defensivo (si el template lo expone)
replace_token '@BUNDLE_ID@'          "com.ovni.$PLUGIN"  # defensivo

# CMakeLists.txt.in → CMakeLists.txt
if [ -f "$DEST/CMakeLists.txt.in" ]; then
  mv "$DEST/CMakeLists.txt.in" "$DEST/CMakeLists.txt"
  log "CMakeLists.txt.in → CMakeLists.txt"
fi

# --- tabla de presets de fábrica VACÍA (si el template no la trae) ---
PRESET_TABLE_CREATED=false
PRESETS_DIR="$DEST/source/presets"
if ! find "$DEST" -type f -name 'FactoryPresets.cpp' | grep -q .; then
  mkdir -p "$PRESETS_DIR"
  cat > "$PRESETS_DIR/FactoryPresets.cpp" <<CPP
// FactoryPresets.cpp — tabla de presets de fábrica de $DISPLAY.
// Generado vacío por tools/scaffold.sh. El stage Generate del orquestador la rellena
// con los modos con nombre del diseño (presetModes). La INTERFAZ (FactoryPreset/PresetParam
// + factoryPresets()) la define shared/presets/PresetTypes.h (S3).
#include "presets/PresetTypes.h"

namespace ovni::presets
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        // PLUGIN: agregá acá los presets de fábrica (modos con nombre que enseñan el rango).
        static const std::vector<FactoryPreset> presets {};
        return presets;
    }
}
CPP
  PRESET_TABLE_CREATED=true
  log "tabla de presets vacía creada en source/presets/FactoryPresets.cpp"
fi

# --- resultado JSON ---
python3 - "$PLUGIN" "$CODE" "$MANU" "$DISPLAY" "plugins/$PLUGIN" "$PRESET_TABLE_CREATED" <<'PY'
import json, sys
plugin, code, manu, display, dest, ptc = sys.argv[1:7]
print(json.dumps({
    "tool": "scaffold",
    "ok": True,
    "plugin": plugin,
    "code": code,
    "manu": manu,
    "display": display,
    "dest": dest,
    "presetTableCreated": ptc == "true",
}))
PY
log "OK → plugins/$PLUGIN"
