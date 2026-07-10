#!/usr/bin/env bash
# packaging/make-dmg.sh — Arma un .dmg de distribución GRATIS del catálogo OVNI (VST3 + AU).
#
# Es la ALTERNATIVA MANUAL del camino gratis (el principal: los .pkg de make-per-plugin.sh) (sin pagar la cuenta Apple Developer de 99 USD):
# un .dmg que el usuario abre, arrastra los plugins a su carpeta de Plug-Ins, y listo. El plugin
# SIN FIRMAR es 100% legal y funciona (AGPLv3 + JUCE); la única diferencia es que macOS muestra el
# aviso de Gatekeeper la primera vez. El "LÉEME PRIMERO.txt" de adentro explica el workaround exacto.
#
# Qué mete adentro del .dmg:
#   · cada <PLUGIN>.vst3 y <PLUGIN>.component encontrado en el build (VST3 + AU)
#   · un alias a /Library/Audio/Plug-Ins (para arrastrar de un lado al otro)
#   · LÉEME PRIMERO.txt con el workaround de Gatekeeper (click derecho → Abrir / xattr)
#   · LICENSE.txt (texto completo AGPLv3) + SOURCE.txt (oferta de código fuente) — cumplimiento
#     AGPLv3 §4/§6 al distribuir el binario.
#
# Uso:    packaging/make-dmg.sh --version <X.Y.Z> --out <ruta.dmg>
#         packaging/make-dmg.sh --version <X.Y.Z> --out <ruta.dmg> --plugins "pulsar nebula halo"
#           (--plugins filtra por nombre, case-insensitive; sin el flag mete TODO lo que encuentre.)
#
# Env:    BUILD_DIR (build) · CONFIG (Release) · VOL_NAME (nombre del volumen montado)
#         DEV_ID  identidad "Developer ID Application: …" para firmar el .dmg (OPCIONAL).
#                 Si está VACÍA → arma el .dmg SIN FIRMAR (camino gratis, default hoy) y avisa por
#                 stdout. NO falla por falta de identidad: ese es justamente el punto.
#
# Exit:   0 si arma el .dmg · 1 si no encuentra artefactos o falla hdiutil.

set -uo pipefail

log()  { printf '  make-dmg: %s\n' "$*" >&2; }
fail() { printf '  make-dmg: ERROR: %s\n' "$*" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
CONFIG="${CONFIG:-Release}"
VOL_NAME="${VOL_NAME:-OVNI Audio}"
DEV_ID="${DEV_ID:-}"

# --- Cumplimiento AGPLv3 (secciones 4/6): dónde está el LICENSE y a dónde apunta la oferta de fuente.
# SOURCE_URL: URL pública del repositorio fuente. PLACEHOLDER — confirmar la URL real del repo
# (ver reporte). Se puede sobrescribir por entorno: SOURCE_URL=… packaging/make-dmg.sh …
LICENSE_FILE="${LICENSE_FILE:-$ROOT/LICENSE}"
SOURCE_URL="${SOURCE_URL:-https://github.com/ovniaudio}"

# --- args ---
VERSION=""
OUT=""
PLUGINS_FILTER=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --out)     OUT="${2:-}"; shift 2 ;;
    --plugins) PLUGINS_FILTER="${2:-}"; shift 2 ;;
    *) fail "argumento desconocido: $1 (uso: --version <X.Y.Z> --out <ruta.dmg> [--plugins \"a b c\"])" ;;
  esac
done
[ -n "$VERSION" ] || fail "falta --version <X.Y.Z>"
[ -n "$OUT" ]     || fail "falta --out <ruta.dmg>"

command -v hdiutil >/dev/null 2>&1 || fail "no encuentro 'hdiutil' (¿es macOS?)"
[ -d "$BUILD_DIR" ] || fail "no existe BUILD_DIR=$BUILD_DIR"

# ¿Este bundle entra según el filtro --plugins? (sin filtro → entra todo). Match case-insensitive
# por nombre de archivo (p.ej. "halo" matchea HALO.vst3 / HALO.component).
matches_filter() {
  [ -z "$PLUGINS_FILTER" ] && return 0
  local base lower want
  base="$(basename "$1")"
  lower="$(printf '%s' "$base" | tr '[:upper:]' '[:lower:]')"
  for want in $PLUGINS_FILTER; do
    case "$lower" in
      "$(printf '%s' "$want" | tr '[:upper:]' '[:lower:]')".*) return 0 ;;
    esac
  done
  return 1
}

# --- Staging: lo que se ve montado dentro del .dmg. ---
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT

n_added=0
while IFS= read -r bundle; do
  [ -n "$bundle" ] || continue
  matches_filter "$bundle" || continue
  cp -R "$bundle" "$STAGE/"
  n_added=$((n_added+1))
  log "+ $(basename "$bundle")"
done < <(
  find "$BUILD_DIR" -path "*_artefacts/$CONFIG/*" \
    \( -name '*.vst3' -o -name '*.component' \) -prune 2>/dev/null
)

[ "$n_added" -gt 0 ] || fail "no encontré .vst3/.component en $BUILD_DIR/*_artefacts/$CONFIG/ (¿buildeaste? ¿el filtro --plugins matchea?)"

# --- Alias a la carpeta de Plug-Ins del sistema (arrastrar de un lado al otro). ---
# Es un alias real de Finder; si por algún motivo falla (entorno headless raro), seguimos: el
# LÉEME explica igual a dónde copiar. No hacemos fallar el .dmg por esto.
PLUGINS_DIR="/Library/Audio/Plug-Ins"
if [ -d "$PLUGINS_DIR" ]; then
  if ! ln -s "$PLUGINS_DIR" "$STAGE/Carpeta de Plug-Ins (arrastrá acá)" 2>/dev/null; then
    log "aviso: no pude crear el alias a $PLUGINS_DIR (sigo igual; el LÉEME explica el destino)"
  fi
fi

# --- LÉEME PRIMERO.txt: el workaround de Gatekeeper, en criollo. ---
# Texto plano para que se abra con doble click en cualquier Mac (sin depender de markdown).
cat > "$STAGE/LÉEME PRIMERO.txt" <<'LEEME'
============================================================
  OVNI Audio — Plugins (instalación + primera apertura)
============================================================

GRACIAS por probar los plugins de OVNI 🛸. Son gratis y de
código abierto (AGPLv3). Funcionan en cualquier DAW que lea
VST3 o AU en macOS (11.0 Big Sur o superior).

------------------------------------------------------------
1) INSTALAR
------------------------------------------------------------
Copiá los plugins a tu carpeta de Plug-Ins del sistema:

   · los  .vst3        →  /Library/Audio/Plug-Ins/VST3
   · los  .component   →  /Library/Audio/Plug-Ins/Components

Podés arrastrarlos al alias "Carpeta de Plug-Ins" que está
en este disco, o copiarlos a mano a las rutas de arriba.
(macOS te va a pedir tu contraseña: es una carpeta del sistema.)

------------------------------------------------------------
2) PRIMERA APERTURA — el aviso de macOS (Gatekeeper)
------------------------------------------------------------
La primera vez, macOS puede avisar que el plugin "no se puede
abrir porque no se puede verificar el desarrollador" o algo
parecido. Esto NO significa que el plugin esté roto ni que sea
inseguro: solo que todavía no está firmado con una cuenta de
Apple paga. Es legal y normal en software libre.

Hay dos formas de destrabar (elegí UNA):

  A) La fácil — desde el Finder:
     1. En la carpeta de Plug-Ins, hacé CLICK DERECHO sobre el
        plugin (.vst3 o .component).
     2. Elegí "Abrir".
     3. Confirmá "Abrir" en el diálogo.
     Listo: macOS lo recuerda y tu DAW ya lo escanea normal.

  B) La de terminal — quita la marca de "cuarentena":
     Abrí la app Terminal y pegá (ajustá el nombre del plugin):

        xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HALO.vst3"
        xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HALO.component"

     (Hacelo para cada plugin que hayas copiado.)

------------------------------------------------------------
3) USAR
------------------------------------------------------------
Abrí tu DAW y reescaneá los plugins si hace falta. Vas a ver
los plugins de OVNI en la lista. ¡A volar! 🛸

------------------------------------------------------------
¿Dudas? https://ovniaudio.com  ·  hello@ovniaudio.com
============================================================
LEEME

# --- Cumplimiento AGPLv3: LICENSE + oferta de código fuente dentro del .dmg. ---
# La AGPLv3 (secciones 4/6) exige entregar el texto de la licencia y una forma de obtener el fuente
# correspondiente junto con el binario. Los copiamos al volumen montado del .dmg.
[ -f "$LICENSE_FILE" ] || fail "no encuentro el LICENSE en $LICENSE_FILE (requerido por AGPLv3)"
cp "$LICENSE_FILE" "$STAGE/LICENSE.txt"
log "+ LICENSE.txt (AGPLv3)"
# NOTICE (atribuciones de terceros: JUCE, libmysofa, SADIE HRIR, Intel IPP…). Opcional pero pro.
if [ -f "$ROOT/NOTICE.md" ]; then cp "$ROOT/NOTICE.md" "$STAGE/NOTICE.txt"; log "+ NOTICE.txt (atribuciones de terceros)"; fi
cat > "$STAGE/SOURCE.txt" <<SOURCE
============================================================
  OVNI Audio — Código fuente (AGPLv3)
============================================================

Estos plugins son software libre bajo la Licencia Pública
General Affero de GNU, versión 3 (AGPLv3). El texto completo
de la licencia está en el archivo LICENSE.txt de este disco.

Tenés derecho a obtener el código fuente completo y correspondiente
de esta versión. Está disponible públicamente en:

    $SOURCE_URL

(source available per AGPLv3 §6)

Versión de este paquete: $VERSION

¿Dudas? https://ovniaudio.com  ·  hello@ovniaudio.com
============================================================
SOURCE
log "+ SOURCE.txt (oferta de fuente: $SOURCE_URL)"

# --- hdiutil: crear el .dmg comprimido (UDZO) desde el staging. ---
mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
log "hdiutil create ($n_added bundles, vol \"$VOL_NAME\")…"
hdiutil create \
  -volname "$VOL_NAME" \
  -srcfolder "$STAGE" \
  -fs HFS+ \
  -format UDZO \
  -ov \
  "$OUT" >&2 || fail "hdiutil create falló"

# --- Firma del .dmg: OPCIONAL. Sin DEV_ID → camino gratis (no firma, no falla). ---
if [ -n "$DEV_ID" ]; then
  command -v codesign >/dev/null 2>&1 || fail "DEV_ID seteado pero no encuentro 'codesign'"
  log "codesign del .dmg (Developer ID Application)…"
  codesign --force --timestamp --sign "$DEV_ID" "$OUT" >&2 || fail "codesign del .dmg falló"
  codesign --verify --verbose=2 "$OUT" >&2 || fail "verificación de firma del .dmg falló"
  log "✓ .dmg FIRMADO: $OUT (version=$VERSION, bundles=$n_added)"
else
  log "✓ .dmg SIN FIRMAR (camino gratis): $OUT (version=$VERSION, bundles=$n_added)"
  log "  El usuario verá el aviso de Gatekeeper en la 1ra apertura → workaround en 'LÉEME PRIMERO.txt'."
  log "  Para firmar (camino pago), seteá DEV_ID=\"Developer ID Application: … (TEAMID)\"."
fi

exit 0
