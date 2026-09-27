#!/usr/bin/env bash
# packaging/check-fonts.sh — las fuentes que viajan adentro de TELESCOPE son las originales, y su licencia viaja con ellas.
#
# POR QUÉ EXISTE (F5 de TELESCOPE 0.2). El ui-kit del sello (shared/ui-kit/assets/fonts/) mete cuatro fuentes ADENTRO
# del binario del plugin, como datos: Clash Grotesk y General Sans (Indian Type Foundry, por Fontshare, ITF Free Font
# License 2.0) y JetBrains Mono (SIL Open Font License 1.1). Hasta la 0.1.0 el instalador llevaba la AGPL, el NOTICE y el
# SOURCE, pero ninguna de esas dos licencias. Y la FFL, §02, prohíbe MODIFICAR las fuentes de Fontshare (recortarlas,
# convertirlas, renombrarlas): una fuente "optimizada" por alguien sería un incumplimiento que no hace ruido.
# Es el mismo candado que EYEPIECE (tests/LicensesCheck.cmake de su repo), con los mismos sha256.
#
# Qué exige (exit 1 si algo falla, y dice qué):
#   · cada .ttf/.otf de --fonts tiene su sha256 en la tabla de abajo y coincide byte a byte (la original);
#     y cada fuente de la tabla existe: una fuente nueva entra con su sha256 acá;
#   · en --licenses están los tres textos: ITF-Free-Font-License.txt, ITF-Fontshare-fonts.txt y JetBrainsMono-OFL.txt;
#   · el aviso de Fontshare nombra cada fuente FFL por su archivo, y --notice (el NOTICE.md) nombra las cuatro;
#   · con --binary (repetible): los bytes de cada fuente están, tal cual y enteros, adentro de ese binario. Es lo que
#     prueba que lo que se distribuye lleva la original, no sólo que el repo la tiene.
#
# Uso: packaging/check-fonts.sh --fonts <dir> --licenses <dir> --notice <NOTICE.md> [--binary <archivo>]...
set -uo pipefail

FONTS=""; LICENSES=""; NOTICE=""; BINARIES=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --fonts)    FONTS="${2:-}"; shift 2 ;;
    --licenses) LICENSES="${2:-}"; shift 2 ;;
    --notice)   NOTICE="${2:-}"; shift 2 ;;
    --binary)   BINARIES+=("${2:-}"); shift 2 ;;
    *) printf '  fonts: ERROR: argumento desconocido: %s\n' "$1"; exit 1 ;;
  esac
done

fails=0
ok()  { printf '  fonts: ✓ %s\n' "$*"; }
bad() { printf '  fonts: ✗ %s\n' "$*"; fails=$((fails+1)); }

# <archivo> <sha256> <licencia>. Las originales, iguales a las que lleva EYEPIECE (medido el 26-sep-2026).
TABLE="ClashGrotesk-Semibold.ttf 99555a997a12589b786dcc0b85c0edbb5335d9c9629cb8bbd264db4b1eac5b6d FFL
GeneralSans-Medium.ttf 21a95d0f3b25dbe4ccbe3d7f45664069249ff1ccd030172878e7f68d85833cea FFL
GeneralSans-Regular.ttf 2e758fefe5a80a0c910505473de241a74cf6dd914ad6d576d571b610ee8efaf1 FFL
JetBrainsMono-Regular.ttf e6fd0d7e91550b3ed2b735d4312474362c4716edc4fc0577a0f61ed782d5aed1 OFL"

[ -d "$FONTS" ]    || { printf '  fonts: ERROR: falta --fonts <dir>\n'; exit 1; }
[ -d "$LICENSES" ] || { printf '  fonts: ERROR: falta --licenses <dir>\n'; exit 1; }
[ -f "$NOTICE" ]   || { printf '  fonts: ERROR: falta --notice <NOTICE.md>\n'; exit 1; }

for t in ITF-Free-Font-License.txt ITF-Fontshare-fonts.txt JetBrainsMono-OFL.txt; do
  [ -s "$LICENSES/$t" ] && ok "licencia $t" || bad "falta $LICENSES/$t"
done
grep -q "ITF Free Font License" "$LICENSES/ITF-Free-Font-License.txt" 2>/dev/null || bad "ITF-Free-Font-License.txt no es el texto de la FFL"
grep -q "SIL OPEN FONT LICENSE Version 1.1" "$LICENSES/JetBrainsMono-OFL.txt" 2>/dev/null || bad "JetBrainsMono-OFL.txt no es el texto de la OFL 1.1"

# La tabla manda en los dos sentidos.
while read -r name sha lic; do
  f="$FONTS/$name"
  if [ ! -f "$f" ]; then bad "falta la fuente $name en $FONTS"; continue; fi
  got="$(shasum -a 256 "$f" | cut -d' ' -f1)"
  if [ "$got" = "$sha" ]; then ok "$name es la original ($lic, sha256 ${sha:0:16}…)"
  else bad "$name CAMBIÓ: sha256 $got, y la original es $sha (la FFL 2.0, §02, prohíbe modificar las de Fontshare)"; fi
  if [ "$lic" = "FFL" ]; then
    grep -qF "$name" "$LICENSES/ITF-Fontshare-fonts.txt" 2>/dev/null || bad "el aviso de Fontshare no nombra $name"
  fi
  grep -qF "$name" "$NOTICE" || bad "$(basename "$NOTICE") no nombra $name"
done <<< "$TABLE"
for f in "$FONTS"/*.ttf "$FONTS"/*.otf; do
  [ -e "$f" ] || continue
  n="$(basename "$f")"
  printf '%s\n' "$TABLE" | grep -q "^$n " || bad "la fuente $n viaja y no tiene su sha256 en la tabla de packaging/check-fonts.sh"
done

# Los bytes de cada fuente, enteros, adentro de cada binario que se distribuye.
for bin in ${BINARIES[@]+"${BINARIES[@]}"}; do
  if [ ! -f "$bin" ]; then bad "no existe el binario $bin"; continue; fi
  while read -r name sha lic; do
    [ -f "$FONTS/$name" ] || continue
    if python3 - "$bin" "$FONTS/$name" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read(); f = open(sys.argv[2], 'rb').read()
sys.exit(0 if f in b else 1)
PY
    then ok "$name está entera adentro de $(basename "$(dirname "$(dirname "$bin")")")/…/$(basename "$bin")"
    else bad "$name NO está entera adentro de $bin"; fi
  done <<< "$TABLE"
done

[ "$fails" -eq 0 ] && { printf '  fonts: OK — 4 fuentes originales, sus licencias y el aviso\n'; exit 0; }
printf '  fonts: %d problema(s)\n' "$fails"; exit 1
