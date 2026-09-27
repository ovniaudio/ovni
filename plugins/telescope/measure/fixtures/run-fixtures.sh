#!/bin/bash
# run-fixtures.sh — el juego fijo de Mac contra Windows (prompt 101, hito 5). Corre igual en los dos.
#
#   run-fixtures.sh <telescope-measure> <telescope-measure-fixtures> <carpeta de trabajo>
#
# Genera los archivos, corre la herramienta sobre requests.jsonl (una sola llamada) e imprime, para comparar
# línea por línea entre las dos máquinas:
#   FILE <sha256> <nombre>     cada archivo generado (si difieren, la comparación de mediciones no vale)
#   EXIT <n>                   el exit de la herramienta
#   LINE <n> <sha256>          cada línea de salida
#   OUTPUT <sha256>            la salida entera
#   JSON <n> <línea>           cada línea, entera, para ver QUÉ campo difiere
set -u
TOOL=$1; GEN=$2; WORK=$3
REQ="$(cd "$(dirname "$0")" && pwd)/requests.jsonl"
mkdir -p "$WORK"
cd "$WORK" || exit 1
if command -v sha256sum > /dev/null 2>&1; then sha() { sha256sum "$@"; }; else sha() { shasum -a 256 "$@"; }; fi

"$GEN" . > gen.log 2>&1
gen_rc=$?
echo "GEN_EXIT $gen_rc"
[ "$gen_rc" -eq 0 ] || { cat gen.log; exit 1; }
for f in f*.wav; do echo "FILE $(sha "$f" | cut -c1-64) $f"; done

"$TOOL" < "$REQ" > out.jsonl
rc=$?
echo "EXIT $rc"
n=0
while IFS= read -r l || [ -n "$l" ]; do
    n=$((n + 1))
    echo "LINE $n $(printf '%s\n' "$l" | sha | cut -c1-64)"
done < out.jsonl
echo "OUTPUT $(sha out.jsonl | cut -c1-64)"
echo "LINES $n"
n=0
while IFS= read -r l || [ -n "$l" ]; do n=$((n + 1)); echo "JSON $n $l"; done < out.jsonl
exit "$rc"
