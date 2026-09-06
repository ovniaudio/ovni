#!/usr/bin/env bash
# packaging/tests/test-arch-gate.sh — el .pkg NO puede salir con un payload que no sea universal.
#
# Por qué existe: 0.3.0 estuvo a un pelo de salir arm64-only. El build/ había quedado configurado con el
# preset `dev` (arm64) y NADIE lo hubiera atrapado — make-per-plugin.sh arma el .pkg igual de contento, y
# el problema recién aparece en una Mac Intel, después de publicar. La guardia de VERSIÓN existe desde el
# bug de ORBIT del 3-sep; la de ARQUITECTURA faltaba.
#
# Arma bundles de mentira (Info.plist + un ejecutable de verdad hecho con clang) y comprueba las dos caras:
#   · arm64-only  → el empaquetado tiene que FALLAR, y decir por qué.
#   · universal   → el empaquetado tiene que PASAR (si no, la guardia sería un "siempre falla" inútil).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SCRIPT="$ROOT/packaging/make-per-plugin.sh"
VERSION="9.9.9"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0
ok()   { printf '  arch-gate: ✓ %s\n' "$*"; }
bad()  { printf '  arch-gate: ✗ %s\n' "$*"; fails=$((fails+1)); }

# Un bundle mínimo pero REAL: Info.plist con la versión y un Mach-O ejecutable en Contents/MacOS.
make_bundle() { # $1 = ruta del bundle · $2 = nombre · $3... = flags de arch para clang
  local dir="$1" name="$2"; shift 2
  mkdir -p "$dir/Contents/MacOS"
  cat > "$dir/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>com.ovni.archgate.$name</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>BNDL</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
</dict></plist>
PLIST
  # Archivo de verdad, no stdin: clang no arma un universal (varios -arch) leyendo de "-".
  local src="$WORK/fake.c"
  [ -f "$src" ] || echo 'int main(void){return 0;}' > "$src"
  clang "$@" -o "$dir/Contents/MacOS/$name" "$src" 2>/dev/null \
    || { echo "  arch-gate: no pude compilar el ejecutable de mentira ($*)" >&2; exit 2; }
}

make_case() { # $1 = subdir · $2... = flags de arch
  local sub="$1"; shift
  local b="$WORK/$sub/bundles"
  mkdir -p "$b" "$WORK/$sub/out"
  make_bundle "$b/ARCHGATE.vst3"      ARCHGATE "$@"
  make_bundle "$b/ARCHGATE.component" ARCHGATE "$@"
}

run_pkg() { # $1 = subdir → deja la salida en $WORK/$1/log
  INSTALLER_SIGN_ID= ART_DIR=/nonexistent \
    "$SCRIPT" --version "$VERSION" --bundles "$WORK/$1/bundles" --outdir "$WORK/$1/out" \
    > "$WORK/$1/log" 2>&1
}

# ---------------------------------------------------------------- caso 1: arm64-only → tiene que FALLAR
make_case thin -arch arm64
if run_pkg thin; then
  bad "un payload arm64-only se empaquetó igual (esto es lo que pasó a punto de pasar en 0.3.0)"
else
  if grep -q "no universal" "$WORK/thin/log"; then
    ok "arm64-only rechazado: $(grep -m1 'no universal' "$WORK/thin/log" | sed 's/^ *//')"
  else
    bad "falló, pero no por la arquitectura — el mensaje no dice 'no universal':"
    tail -3 "$WORK/thin/log" | sed 's/^/      /'
  fi
fi

# ---------------------------------------------------------------- caso 2: universal → tiene que PASAR
make_case fat -arch arm64 -arch x86_64
if run_pkg fat; then
  ok "universal (arm64 + x86_64) aceptado"
else
  bad "un payload universal fue rechazado — la guardia se come lo bueno:"
  tail -5 "$WORK/fat/log" | sed 's/^/      /'
fi

[ "$fails" -eq 0 ] && { printf '  arch-gate: OK\n'; exit 0; }
printf '  arch-gate: %d fallo(s)\n' "$fails"; exit 1
