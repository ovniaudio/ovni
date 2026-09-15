#!/usr/bin/env bash
# packaging/tests/test-flags.sh — las dos banderas del release por plugin: --source-line y --no-full.
#
# Por qué existen, y por qué se testean:
#
#   --source-line  El bloque de repos del SOURCE.txt que se instala en
#                  /Library/Audio/Plug-Ins/OVNI Audio/ estaba ESCRITO A MANO adentro del script, con el
#                  catálogo de 2026-07 congelado. Cada plugin nuevo salía con un SOURCE.txt que no lo
#                  nombraba: el usuario instalaba TELESCOPE y el archivo que le dice dónde está el fuente
#                  correspondiente (AGPLv3 §6) hablaba de otros ocho plugins. Es un incumplimiento que no
#                  hace ruido — nadie abre ese archivo hasta que hace falta.
#   --no-full      make-per-plugin.sh SIEMPRE emitía además el instalador "completo". Con un solo plugin
#                  en --bundles eso da un "OVNI Audio — 1 Plugins" que no es el catálogo: es el mismo
#                  .pkg individual con otro nombre y otro sha, listo para que alguien lo publique por
#                  error creyendo que trae todo.
#
# Se arman bundles de mentira (Info.plist + un Mach-O universal de verdad hecho con clang), como en
# test-arch-gate.sh, y se mira lo que el .pkg REALMENTE trae adentro (pkgutil --expand-full), no lo que
# el script dice por stderr.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT/packaging/make-per-plugin.sh"
VERSION="9.9.9"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0
ok()  { printf '  flags: ✓ %s\n' "$*"; }
bad() { printf '  flags: ✗ %s\n' "$*"; fails=$((fails+1)); }

make_bundle() { # $1 = ruta del bundle · $2 = nombre
  local dir="$1" name="$2"
  mkdir -p "$dir/Contents/MacOS"
  cat > "$dir/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>com.ovni.flags.$name</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
</dict></plist>
PLIST
  local src="$WORK/fake.c"
  [ -f "$src" ] || echo 'int main(void){return 0;}' > "$src"
  clang -arch arm64 -arch x86_64 -o "$dir/Contents/MacOS/$name" "$src" 2>/dev/null \
    || { echo "  flags: no pude compilar el ejecutable de mentira" >&2; exit 2; }
}

B="$WORK/bundles"; mkdir -p "$B"
make_bundle "$B/FLAGTEST.vst3"      FLAGTEST
make_bundle "$B/FLAGTEST.component" FLAGTEST

run_pkg() { # $1 = subdir de salida · $2... = banderas extra
  local sub="$1"; shift
  mkdir -p "$WORK/$sub"
  INSTALLER_SIGN_ID= ART_DIR=/nonexistent \
    "$SCRIPT" --version "$VERSION" --bundles "$B" --outdir "$WORK/$sub" "$@" \
    > "$WORK/$sub.log" 2>&1
}

# Devuelve el SOURCE.txt que quedó DENTRO del .pkg (no el que el script tenía en /tmp).
source_txt_of() { # $1 = .pkg
  local x="$WORK/x-$RANDOM"
  pkgutil --expand-full "$1" "$x" >/dev/null 2>&1 || return 1
  cat "$x"/*/Payload/Library/Audio/Plug-Ins/OVNI\ Audio/SOURCE.txt 2>/dev/null
  rm -rf "$x"
}

# ------------------------------------------------------- caso 1: sin banderas → todo como siempre
if run_pkg base; then
  [ -f "$WORK/base/OVNI-FLAGTEST-v$VERSION.pkg" ] \
    && ok "sin banderas: emite el individual" \
    || bad "sin banderas: no emitió OVNI-FLAGTEST-v$VERSION.pkg"
  [ -f "$WORK/base/OVNI-v$VERSION.pkg" ] \
    && ok "sin banderas: emite el completo (comportamiento histórico intacto)" \
    || bad "sin banderas: dejó de emitir el completo — eso rompería el release del catálogo"
  BASE_SRC="$(source_txt_of "$WORK/base/OVNI-FLAGTEST-v$VERSION.pkg")"
  printf '%s' "$BASE_SRC" | grep -qi "flagtest" \
    && bad "sin --source-line el SOURCE.txt ya nombraba al plugin (el test no prueba nada)" \
    || ok "sin --source-line el SOURCE.txt NO nombra al plugin (que es el bug que la bandera arregla)"
else
  bad "sin banderas: el empaquetado falló"; tail -5 "$WORK/base.log" | sed 's/^/      /'
fi

# ------------------------------------------------- caso 2: --no-full → NO sale el instalador completo
if run_pkg nofull --no-full; then
  [ -f "$WORK/nofull/OVNI-FLAGTEST-v$VERSION.pkg" ] \
    && ok "--no-full: el individual sigue saliendo" \
    || bad "--no-full: se comió también el individual"
  [ -f "$WORK/nofull/OVNI-v$VERSION.pkg" ] \
    && bad "--no-full: emitió igual el completo OVNI-v$VERSION.pkg" \
    || ok "--no-full: no hay OVNI-v$VERSION.pkg"
  grep -q "OVNI-v$VERSION.pkg" "$WORK/nofull/SHA256SUMS.txt" 2>/dev/null \
    && bad "--no-full: SHA256SUMS.txt igual lista el completo" \
    || ok "--no-full: SHA256SUMS.txt no lista el completo"
else
  bad "--no-full: el empaquetado falló"; tail -5 "$WORK/nofull.log" | sed 's/^/      /'
fi

# ------------------------------------------ caso 3: --source-line ×2 → las dos líneas, en el .pkg
L1="· FLAGTEST (analyzer): https://github.com/ovniaudio/ovni/tree/flagtest-v$VERSION"
L2="· SEGUNDA LINEA DE PRUEBA: https://example.invalid/segunda"
if run_pkg src --no-full --source-line "$L1" --source-line "$L2"; then
  SRC="$(source_txt_of "$WORK/src/OVNI-FLAGTEST-v$VERSION.pkg")"
  if [ -z "$SRC" ]; then
    bad "--source-line: no pude leer el SOURCE.txt de adentro del .pkg"
  else
    printf '%s' "$SRC" | grep -qF "$L1" && ok "--source-line: la 1ª línea está en el SOURCE.txt del .pkg" \
                                        || bad "--source-line: falta la 1ª línea en el SOURCE.txt"
    printf '%s' "$SRC" | grep -qF "$L2" && ok "--source-line: la 2ª línea también (la bandera es repetible)" \
                                        || bad "--source-line: falta la 2ª línea (¿no es repetible?)"
    printf '%s' "$SRC" | grep -q "ovniaudio/orbita" && ok "--source-line: el bloque fijo del catálogo sigue entero" \
                                                    || bad "--source-line: se perdió el bloque fijo (ORBIT ya no figura)"
    printf '%s' "$SRC" | grep -q "source available per AGPLv3" && ok "--source-line: el pie AGPLv3 §6 sigue ahí" \
                                                                || bad "--source-line: se perdió el pie AGPLv3 §6"
  fi
else
  bad "--source-line: el empaquetado falló"; tail -5 "$WORK/src.log" | sed 's/^/      /'
fi

# --------------------------- caso 4: la línea de SUPERNOVA no puede llevar la versión de OTRO paquete
# El .pkg de TELESCOPE 0.1.0 decía "SUPERNOVA … tree/v0.1.0 (tag v0.1.0)": el $VERSION del paquete metido
# en la línea de un plugin que no viaja adentro. El SOURCE.txt es con lo que se cumple el AGPLv3 §6;
# mandar a alguien a un tag que no es su versión no es un detalle de redacción.
if run_pkg nosnv --no-full; then
  SRC="$(source_txt_of "$WORK/nosnv/OVNI-FLAGTEST-v$VERSION.pkg")"
  printf '%s' "$SRC" | grep -q "tree/v$VERSION" \
    && bad "sin SUPERNOVA en el paquete, el SOURCE.txt igual apunta a tree/v$VERSION" \
    || ok "sin SUPERNOVA en el paquete, su línea NO lleva la versión ajena"
  printf '%s' "$SRC" | grep -q "SUPERNOVA" \
    && ok "…y SUPERNOVA sigue nombrada (el catálogo entero se sigue ofreciendo)" \
    || bad "se perdió la línea de SUPERNOVA del SOURCE.txt"
else
  bad "caso SUPERNOVA-ausente: el empaquetado falló"; tail -5 "$WORK/nosnv.log" | sed 's/^/      /'
fi

# --------------------- caso 5: con SUPERNOVA adentro, la línea SÍ lleva la versión (comportamiento viejo)
SB="$WORK/snv-bundles"; mkdir -p "$SB"
make_bundle "$SB/SUPERNOVA.vst3"      SUPERNOVA
make_bundle "$SB/SUPERNOVA.component" SUPERNOVA
mkdir -p "$WORK/snv"
if INSTALLER_SIGN_ID= ART_DIR=/nonexistent "$SCRIPT" --version "$VERSION" --bundles "$SB" \
     --outdir "$WORK/snv" --no-full > "$WORK/snv.log" 2>&1; then
  SRC="$(source_txt_of "$WORK/snv/OVNI-SUPERNOVA-v$VERSION.pkg")"
  printf '%s' "$SRC" | grep -q "tree/v$VERSION" \
    && ok "con SUPERNOVA adentro, su línea sí lleva tree/v$VERSION (release de SUPERNOVA intacto)" \
    || bad "con SUPERNOVA adentro se perdió el tree/v$VERSION — eso rompe SU release"
else
  bad "caso SUPERNOVA-presente: el empaquetado falló"; tail -5 "$WORK/snv.log" | sed 's/^/      /'
fi

# ------------------------------------------------- caso 6: --source-line sin texto → error, no silencio
if "$SCRIPT" --version "$VERSION" --bundles "$B" --outdir "$WORK/bad" --source-line > "$WORK/bad.log" 2>&1; then
  bad "--source-line sin texto se aceptó en silencio"
else
  grep -q "source-line" "$WORK/bad.log" && ok "--source-line sin texto falla y lo dice" \
                                        || bad "--source-line sin texto falla, pero el mensaje no lo nombra"
fi

[ "$fails" -eq 0 ] && { printf '  flags: OK\n'; exit 0; }
printf '  flags: %d fallo(s)\n' "$fails"; exit 1
