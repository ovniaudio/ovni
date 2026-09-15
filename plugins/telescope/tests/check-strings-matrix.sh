#!/usr/bin/env bash
# plugins/telescope/tests/check-strings-matrix.sh — la matriz de idiomas commiteada está al día.
#
# Regenera docs/strings-matrix.md a un temporal con el mismo generador que la escribió
# (`OvniTelescopeTests "[strings-dump]"`, con TELESCOPE_STRINGS_OUT apuntando al temporal) y hace `cmp`
# contra el archivo del repo. Si alguien agrega una clave, cambia una traducción o borra una tabla y no
# regenera el documento, esto se pone rojo — que es exactamente el punto: un documento generado que nadie
# vuelve a generar es peor que no tenerlo, porque se lee como si fuera cierto.
#
# Uso:  check-strings-matrix.sh <exe de tests> <strings-matrix.md commiteado>
# Exit: 0 si son idénticos · 1 si difieren, falta, o el generador falló.
set -uo pipefail

EXE="${1:-}"
COMMITTED="${2:-}"

[ -x "${EXE:-}" ]     || { printf '  strings-matrix: ERROR: no encuentro el exe de tests en %s\n' "$EXE"; exit 1; }
[ -f "${COMMITTED:-}" ] || { printf '  strings-matrix: ERROR: no existe %s — generalo con: %s "[strings-dump]"\n' "$COMMITTED" "$EXE"; exit 1; }

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
FRESH="$TMP/strings-matrix.md"

if ! TELESCOPE_STRINGS_OUT="$FRESH" "$EXE" "[strings-dump]" > "$TMP/log" 2>&1; then
  printf '  strings-matrix: ERROR: el generador falló\n'
  tail -20 "$TMP/log" | sed 's/^/      /'
  exit 1
fi
[ -s "$FRESH" ] || { printf '  strings-matrix: ERROR: el generador no escribió nada en %s\n' "$FRESH"; exit 1; }

if cmp -s "$FRESH" "$COMMITTED"; then
  printf '  strings-matrix: ✓ %s está al día (%s bytes)\n' "$(basename "$COMMITTED")" "$(wc -c < "$COMMITTED" | tr -d ' ')"
  exit 0
fi

printf '  strings-matrix: ✗ %s está VIEJO. Regeneralo con:\n' "$(basename "$COMMITTED")"
printf '      %s "[strings-dump]"\n' "$EXE"
printf '  primeras diferencias:\n'
diff -u "$COMMITTED" "$FRESH" 2>/dev/null | head -30 | sed 's/^/      /'
exit 1
