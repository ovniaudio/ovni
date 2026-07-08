#!/usr/bin/env bash
# packaging/build-universal.sh — Build UNIVERSAL del catálogo OVNI + verificación lipo (gate duro).
#
# Qué hace:
#   1. Configura con el preset 'release-universal' (CMAKE_OSX_ARCHITECTURES="arm64;x86_64", Release,
#      deployment target 11.0) — salvo SKIP_BUILD=1, que sólo verifica un build ya hecho.
#   2. Buildea todo el árbol (libs + plugins + runners).
#   3. Corre `lipo -archs` sobre el binario de CADA .vst3/.component y FALLA si a alguno le falta
#      arm64 o x86_64. Un artefacto que no es universal NO se distribuye: prometemos universal en los
#      docs, así que acá lo verificamos antes de firmar/empaquetar.
#
# Este script NO firma nada: solo configura, buildea y verifica. No requiere identidad de firma ni
# credenciales de Apple → corre igual en el camino gratis (sin la cuenta Developer de 99 USD). La
# firma (opcional) vive en make-dmg.sh / make-installer.sh / release.yml.
#
# Uso:    packaging/build-universal.sh
# Env:    BUILD_DIR (build) · CONFIG (Release) · SKIP_BUILD (1 = no configura/buildea, sólo verifica)
#
# Exit:   0 si TODOS los artefactos son universales · 1 si falla configure/build o algún artefacto
#         no es universal (o no se encontró ninguno).

set -uo pipefail

log()  { printf '  build-universal: %s\n' "$*" >&2; }
fail() { printf '  build-universal: ERROR: %s\n' "$*" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
CONFIG="${CONFIG:-Release}"
SKIP_BUILD="${SKIP_BUILD:-0}"

command -v lipo >/dev/null 2>&1 || fail "no encuentro 'lipo' (¿es macOS con Xcode CLT?)"

if [ "$SKIP_BUILD" != "1" ]; then
  command -v cmake >/dev/null 2>&1 || fail "no encuentro 'cmake'."
  log "configurando con el preset release-universal…"
  cmake --preset release-universal || fail "falló cmake --preset release-universal"
  log "buildeando ($CONFIG)…"
  cmake --build "$BUILD_DIR" --config "$CONFIG" || fail "falló el build"
else
  log "SKIP_BUILD=1 → sólo verifico el build existente en $BUILD_DIR"
fi

[ -d "$BUILD_DIR" ] || fail "no existe BUILD_DIR=$BUILD_DIR"

# --- Recolectar el binario interno de cada bundle distribuible (VST3 + AU). ---
# (Igual criterio que tools/validate.sh: el binario vive en Contents/MacOS/<nombre>.)
BINARIES=()
while IFS= read -r b; do
  [ -n "$b" ] && BINARIES+=("$b")
done < <(
  find "$BUILD_DIR" -path "*_artefacts/$CONFIG/*" \
    \( -name '*.vst3' -o -name '*.component' \) 2>/dev/null \
  | while read -r bundle; do
      base="$(basename "$bundle")"; name="${base%.*}"
      bin="$bundle/Contents/MacOS/$name"
      [ -f "$bin" ] && printf '%s\n' "$bin"
    done
)

if [ "${#BINARIES[@]}" -eq 0 ]; then
  fail "no encontré artefactos VST3/AU en $BUILD_DIR/*_artefacts/$CONFIG/ (¿buildeaste?)"
fi

# --- Gate: cada binario debe traer arm64 Y x86_64. ---
ALL_OK=1
for bin in "${BINARIES[@]}"; do
  archs="$(lipo -archs "$bin" 2>/dev/null)"
  if printf '%s' "$archs" | grep -q 'arm64' && printf '%s' "$archs" | grep -q 'x86_64'; then
    log "OK  universal: $(basename "$(dirname "$(dirname "$bin")")")/$(basename "$bin") → [$archs]"
  else
    log "NO  universal: $bin → [$archs] (faltan arm64 y/o x86_64)"
    ALL_OK=0
  fi
done

if [ "$ALL_OK" -ne 1 ]; then
  fail "hay artefactos NO universales: no se puede distribuir. Configurá con el preset release-universal."
fi

log "✓ TODOS los artefactos (${#BINARIES[@]}) son universales (arm64 + x86_64)."
exit 0
