#!/usr/bin/env bash
# plugins/telescope/tests/check-bundle-version.sh — los bundles construidos declaran la versión que dice
# plugins/telescope/VERSION, y nadie más.
#
# POR QUÉ EXISTE. Hasta el release-prep de 0.1.0, `plugins/telescope/CMakeLists.txt` no pasaba VERSION a
# juce_add_plugin, así que TELESCOPE heredaba el `project(OVNI VERSION 0.3.2)` del raíz: los tres bundles
# decían 0.3.2 y `auval` imprimía "Component Version: 0.3.2" para un plugin que nunca se había publicado.
# Ese número es el que mira el Installer de macOS para decidir si escribe o saltea un componente — el bug
# que dejó a los usuarios de ORBIT v0.2.0 sin el AU (mision-control/bugs/2026-09-03-orbit-au-no-se-instala.md)
# y el motivo de la guardia de versión de packaging/make-per-plugin.sh. Esa guardia atrapa el problema con
# el bundle ya construido, en la mesa de empaquetado; ésta lo atrapa en `ctest`, que es donde duele menos.
#
# Uso:  check-bundle-version.sh <archivo VERSION> <dir de artefactos> [<doc> <ancla> ...]
#
#   · <archivo VERSION>    plugins/telescope/VERSION — la ÚNICA fuente de verdad. El test lo lee del disco
#                          en vez de recibir la versión sustituida por CMake: así también falla si el
#                          CMakeLists deja de leer el archivo.
#   · <dir de artefactos>  …/telescope_artefacts/<CONFIG>, con VST3/ AU/ Standalone/ adentro.
#   · <doc> <ancla>        pares: un documento que publica la versión y el regex (grep -E) de las LÍNEAS
#                          donde la declara. Se exige que TODAS esas líneas digan la misma versión que
#                          VERSION, y que haya al menos una. Va con ancla y no "la primera x.y.z del
#                          archivo" porque los documentos empiezan citando otras versiones (el CHANGELOG
#                          linkea Keep a Changelog 1.1.0) y porque la ficha declara la versión DOS veces,
#                          una por idioma: si sólo se mirara la primera, la tabla en inglés podría quedar
#                          vieja sin que nadie se entere. Un release cuya ficha dice otra cosa es una
#                          ficha que miente.
#
# Exit: 0 si todo declara lo mismo · 1 si algo no coincide o falta.
set -uo pipefail

VERSION_FILE="${1:-}"
ART_DIR="${2:-}"
shift 2 2>/dev/null || true

fails=0
ok()  { printf '  version: ✓ %s\n' "$*"; }
bad() { printf '  version: ✗ %s\n' "$*"; fails=$((fails+1)); }

[ -f "$VERSION_FILE" ] || { printf '  version: ERROR: no encuentro el archivo VERSION en %s\n' "$VERSION_FILE"; exit 1; }
WANT="$(tr -d '[:space:]' < "$VERSION_FILE")"
[ -n "$WANT" ] || { printf '  version: ERROR: %s está vacío\n' "$VERSION_FILE"; exit 1; }
printf '  version: VERSION declara %s (%s)\n' "$WANT" "$VERSION_FILE"

# --- Los tres bundles construidos. ---
check_bundle() { # $1 = ruta del bundle
  local b="$1" n short build
  n="$(basename "$b")"
  if [ ! -d "$b" ]; then
    bad "$n no existe en $ART_DIR — construí telescope_VST3 telescope_AU telescope_Standalone antes de este test"
    return
  fi
  short="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$b/Contents/Info.plist" 2>/dev/null)"
  build="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$b/Contents/Info.plist" 2>/dev/null)"
  [ "$short" = "$WANT" ] || bad "$n CFBundleShortVersionString = ${short:-<vacío>} (esperaba $WANT)"
  [ "$build" = "$WANT" ] || bad "$n CFBundleVersion = ${build:-<vacío>} (esperaba $WANT)"
  [ "$short" = "$WANT" ] && [ "$build" = "$WANT" ] && ok "$n declara $WANT en las dos claves"
}
check_bundle "$ART_DIR/VST3/TELESCOPE.vst3"
check_bundle "$ART_DIR/AU/TELESCOPE.component"
check_bundle "$ART_DIR/Standalone/TELESCOPE.app"

# --- Los documentos que publican la versión (pares doc + ancla). ---
while [ "$#" -ge 2 ]; do
  doc="$1"; anchor="$2"; shift 2
  d="$(basename "$doc")"
  if [ ! -f "$doc" ]; then bad "$d no existe ($doc)"; continue; fi
  lines="$(grep -E "$anchor" "$doc")"
  if [ -z "$lines" ]; then
    bad "$d no tiene ninguna línea que declare la versión (ancla: $anchor)"
    continue
  fi
  n=0; nbad=0
  while IFS= read -r line; do
    [ -n "$line" ] || continue
    n=$((n+1))
    got="$(printf '%s' "$line" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"
    [ "$got" = "$WANT" ] || { bad "$d declara ${got:-<ninguna>} en: $line"; nbad=$((nbad+1)); }
  done <<EOF_LINES
$lines
EOF_LINES
  [ "$nbad" -eq 0 ] && ok "$d declara $WANT en sus $n línea(s) de versión"
done
[ "$#" -eq 0 ] || bad "quedó un argumento suelto sin su ancla: $1"

[ "$fails" -eq 0 ] && { printf '  version: OK — todo declara %s\n' "$WANT"; exit 0; }
printf '  version: %d desajuste(s)\n' "$fails"; exit 1
