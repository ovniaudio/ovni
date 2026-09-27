#!/bin/bash
# check-measure-cli.sh — lo que de telescope-measure sólo se ve desde afuera del proceso (prompt 101, hito 3).
#
#   check-measure-cli.sh <telescope-measure> <telescope-measure-fixtures>
#
#   CLI[exit]     con pedidos buenos y malos, exit 0 y una línea por pedido.
#                 control: con la salida cerrada no puede escribir, y sale distinto de 0 (3).
#   CLI[bytes]    dos corridas del mismo juego dan el mismo sha256.
#                 control: otro tramo del mismo archivo da otro sha256.
#   CLI[disco]    corre con el directorio de trabajo y TMPDIR/TMP/TEMP en una carpeta vacía: la carpeta queda
#                 vacía, y los archivos medidos quedan iguales byte a byte (sha256, tamaño y fecha).
#                 control: un archivo creado en la carpeta lo ve el mismo chequeo.
#   CLI[version]  (F4 de la 0.2) `--version` imprime una línea «telescope-measure <versión> <sha>», sale 0 sin
#                 leer la entrada, y la versión y el sha son los del objeto `engine` de las líneas de JSON.
#                 control: sin el argumento, con la entrada vacía, no imprime nada.
set -u
TOOL=$1; GEN=$2
WORK=$(mktemp -d "${TMPDIR:-/tmp}/f3-measure-cli.XXXXXX") || exit 1
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/audio" "$WORK/sandbox"
if command -v sha256sum > /dev/null 2>&1; then sha() { sha256sum "$@" | cut -c1-64; }; else sha() { shasum -a 256 "$@" | cut -c1-64; }; fi
stamp() { stat -f '%m %z' "$1" 2> /dev/null || stat -c '%Y %s' "$1"; }
fail=0
ok()  { echo "CLI[$1] ok · $2"; }
bad() { echo "CLI[$1] ROJO · $2"; fail=1; }

"$GEN" "$WORK/audio" > /dev/null || { echo "no se pudo generar el juego fijo"; exit 1; }
A="$WORK/audio/f01-noise-48k-stereo.wav"; B="$WORK/audio/f10-dynamic-48k-stereo.wav"
cat > "$WORK/req.jsonl" <<JSON
{"file":"$A"}
{"file":"$B","from_s":10,"to_s":30}
hola
{"file":"$A","to":3}
{"file":"$WORK/audio/no-existe.wav"}
JSON
before=$(for f in "$WORK"/audio/*; do echo "$(sha "$f") $(stamp "$f") $f"; done)

run() { (cd "$WORK/sandbox" && TMPDIR="$WORK/sandbox" TMP="$WORK/sandbox" TEMP="$WORK/sandbox" "$TOOL" < "$1" > "$2"); }

# ---- CLI[exit] ----
run "$WORK/req.jsonl" "$WORK/out1"; rc=$?
lines=$(grep -c . "$WORK/out1")
if [ "$rc" -eq 0 ] && [ "$lines" -eq 5 ]; then ok exit "exit 0 y 5 líneas para 5 pedidos (3 negativas adentro)"; else bad exit "exit $rc, $lines líneas"; fi
(cd "$WORK/sandbox" && "$TOOL" < "$WORK/req.jsonl" >&-) 2> /dev/null; rc_closed=$?
if [ "$rc_closed" -ne 0 ]; then ok exit "control: con la salida cerrada sale $rc_closed"; else bad exit "control: con la salida cerrada salió 0"; fi

# ---- CLI[bytes] ----
run "$WORK/req.jsonl" "$WORK/out2"
s1=$(sha "$WORK/out1"); s2=$(sha "$WORK/out2")
if [ "$s1" = "$s2" ]; then ok bytes "dos corridas, el mismo sha256 ($s1)"; else bad bytes "$s1 != $s2"; fi
sed 's/"to_s":30/"to_s":31/' "$WORK/req.jsonl" > "$WORK/req2.jsonl"
run "$WORK/req2.jsonl" "$WORK/out3"
s3=$(sha "$WORK/out3")
if [ "$s3" != "$s1" ]; then ok bytes "control: otro tramo, otro sha256 ($s3)"; else bad bytes "control: otro tramo dio el mismo sha256"; fi

# ---- CLI[version] ----
v=$("$TOOL" --version < /dev/null); rc=$?
eng=$(head -1 "$WORK/out1" | sed -n 's/.*"engine":{"name":"telescope-measure","version":"\([^"]*\)","sha":"\([^"]*\)"}.*/telescope-measure \1 \2/p')
if [ "$rc" -eq 0 ] && [ "$(printf '%s\n' "$v" | grep -c .)" -eq 1 ] && [ -n "$eng" ] && [ "$v" = "$eng" ]; then
    ok version "«${v}», exit 0, igual al engine de las líneas"
else
    bad version "exit $rc · «${v}» · engine «${eng}»"
fi
quiet=$("$TOOL" < /dev/null | grep -c .)
if [ "$quiet" -eq 0 ]; then ok version "control: sin --version y sin pedidos, 0 líneas"; else bad version "control: sin --version imprimió $quiet líneas"; fi

# ---- CLI[disco] ----
left=$(ls -A "$WORK/sandbox" | grep -c .)
after=$(for f in "$WORK"/audio/*; do echo "$(sha "$f") $(stamp "$f") $f"; done)
if [ "$left" -eq 0 ]; then ok disco "la carpeta de trabajo y temporal quedó vacía"; else bad disco "quedaron $left archivos: $(ls -A "$WORK/sandbox")"; fi
if [ "$before" = "$after" ]; then ok disco "los $(echo "$before" | grep -c .) archivos medidos, iguales (sha256, tamaño y fecha)"; else bad disco "los archivos medidos cambiaron"; fi
touch "$WORK/sandbox/control"
left=$(ls -A "$WORK/sandbox" | grep -c .)
if [ "$left" -eq 1 ]; then ok disco "control: un archivo escrito en la carpeta se ve (1)"; else bad disco "control: el chequeo no vio el archivo"; fi

exit "$fail"
