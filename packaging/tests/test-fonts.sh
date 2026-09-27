#!/usr/bin/env bash
# packaging/tests/test-fonts.sh — el candado de las fuentes (packaging/check-fonts.sh) ve lo que cuida.
#
# Verde sobre el repo tal como está, y ROJO en cada una de las formas de romperlo: una fuente tocada en un byte (la FFL
# 2.0, §02, prohíbe modificar las de Fontshare), un texto de licencia que falta, un NOTICE que no nombra una fuente, una
# fuente nueva sin su sha256, y un binario que no lleva una fuente entera. Todo sobre COPIAS en un temporal.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHECK="$ROOT/packaging/check-fonts.sh"
FONTS="$ROOT/shared/ui-kit/assets/fonts"
LICS="$ROOT/packaging/licenses"
NOTICE="$ROOT/NOTICE.md"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
fails=0
ok()  { printf '  test-fonts: ✓ %s\n' "$*"; }
bad() { printf '  test-fonts: ✗ %s\n' "$*"; fails=$((fails+1)); }

expect() { # $1 = 0 (verde) o 1 (rojo) · $2 = qué · $3 = texto que tiene que aparecer (rojo) · $4... = argumentos
  local want="$1" what="$2" needle="$3"; shift 3
  "$CHECK" "$@" > "$WORK/out.log" 2>&1; local rc=$?
  if [ "$want" = 0 ]; then
    [ "$rc" = 0 ] && ok "$what: verde" || { bad "$what: esperaba verde y dio $rc"; sed 's/^/      /' "$WORK/out.log"; }
  else
    if [ "$rc" != 0 ] && grep -qF "$needle" "$WORK/out.log"; then ok "$what: rojo, y dice «${needle}»"
    else bad "$what: esperaba rojo con «${needle}» y dio $rc"; sed 's/^/      /' "$WORK/out.log"; fi
  fi
}

expect 0 "el repo" "" --fonts "$FONTS" --licenses "$LICS" --notice "$NOTICE"

# Una fuente tocada en un solo byte.
cp -R "$FONTS" "$WORK/f1"
python3 - "$WORK/f1/GeneralSans-Medium.ttf" <<'PY'
import sys
p = sys.argv[1]; b = bytearray(open(p, 'rb').read()); b[len(b) // 2] ^= 0x01; open(p, 'wb').write(bytes(b))
PY
expect 1 "una fuente con un byte cambiado" "GeneralSans-Medium.ttf CAMBIÓ" --fonts "$WORK/f1" --licenses "$LICS" --notice "$NOTICE"

# Falta el texto de la FFL.
cp -R "$LICS" "$WORK/l1"; rm "$WORK/l1/ITF-Free-Font-License.txt"
expect 1 "sin el texto de la FFL" "falta" --fonts "$FONTS" --licenses "$WORK/l1" --notice "$NOTICE"

# El NOTICE no nombra JetBrains Mono.
grep -v "JetBrainsMono-Regular.ttf" "$NOTICE" > "$WORK/NOTICE.md"
expect 1 "un NOTICE que no nombra una fuente" "no nombra JetBrainsMono-Regular.ttf" --fonts "$FONTS" --licenses "$LICS" --notice "$WORK/NOTICE.md"

# Una fuente nueva, sin su sha256 en la tabla.
cp -R "$FONTS" "$WORK/f2"; cp "$FONTS/GeneralSans-Regular.ttf" "$WORK/f2/Nueva-Regular.ttf"
expect 1 "una fuente nueva sin sha256" "Nueva-Regular.ttf viaja y no tiene su sha256" --fonts "$WORK/f2" --licenses "$LICS" --notice "$NOTICE"

# Un «binario» que lleva las cuatro enteras: verde. Y uno al que le falta el último byte de una: rojo.
{ printf 'cabecera'; cat "$FONTS"/*.ttf; printf 'cola'; } > "$WORK/bin-ok"
expect 0 "un binario con las cuatro enteras" "" --fonts "$FONTS" --licenses "$LICS" --notice "$NOTICE" --binary "$WORK/bin-ok"
{ printf 'cabecera'; for f in "$FONTS"/*.ttf; do
    if [ "$(basename "$f")" = "ClashGrotesk-Semibold.ttf" ]; then head -c "$(( $(stat -f %z "$f") - 1 ))" "$f"; else cat "$f"; fi
  done; } > "$WORK/bin-corta"
expect 1 "un binario con una fuente recortada" "ClashGrotesk-Semibold.ttf NO está entera" --fonts "$FONTS" --licenses "$LICS" --notice "$NOTICE" --binary "$WORK/bin-corta"

[ "$fails" -eq 0 ] && { printf '  test-fonts: OK\n'; exit 0; }
printf '  test-fonts: %d fallo(s)\n' "$fails"; exit 1
