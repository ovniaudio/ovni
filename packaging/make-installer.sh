#!/usr/bin/env bash
# packaging/make-installer.sh — Arma un instalador .pkg del catálogo OVNI (VST3 + AU).
#
# Instala:
#   · los .vst3 en  /Library/Audio/Plug-Ins/VST3
#   · los .component (AU) en  /Library/Audio/Plug-Ins/Components
# (rutas SYSTEM, las que escanean todos los DAW; por eso el .pkg pide admin al usuario.)
#
# Estrategia: dos component-packages (uno VST3, uno AU) con pkgbuild → un product archive con
# productbuild. El product archive es el que firmamos con la identidad de INSTALADOR
# (Developer ID Installer) y el que se notariza después (release.yml).
#
# Nota de distribución: el DMG (packaging/make-dmg.sh) es el ENTREGABLE GRATIS PRINCIPAL. Este .pkg
# se mantiene como alternativa (instala en rutas SYSTEM con un doble-click guiado). Tanto el .pkg
# como el .dmg SIN FIRMAR son 100% legales y funcionan: el usuario solo ve el aviso de Gatekeeper la
# primera vez (workaround: click derecho → Abrir, o `xattr -dr com.apple.quarantine …`). Ver README.
#
# Uso:    packaging/make-installer.sh --version <X.Y.Z> --out <ruta.pkg>
#         packaging/make-installer.sh --version <X.Y.Z> --out <ruta.pkg> --plugins "aurora dust halo horizon nebula pulsar"
#           (--plugins filtra por nombre, case-insensitive; sin el flag mete TODO lo que encuentre.
#            release.yml SIEMPRE pasa el set completo del catálogo para que el .pkg == el .dmg.)
#
# Cumplimiento AGPLv3: el .pkg instala también, junto a los plugins, el texto completo de la
# licencia (LICENSE → …/OVNI Audio/LICENSE.txt) y una oferta de código fuente (SOURCE.txt), como
# exigen las secciones 4/6 de la AGPLv3 al distribuir binarios.
# Env:    BUILD_DIR (build) · CONFIG (Release)
#         INSTALLER_SIGN_ID  identidad "Developer ID Installer: …" para firmar el .pkg (OPCIONAL).
#                            Si está VACÍA → arma un .pkg SIN FIRMAR (camino gratis, default hoy) y
#                            avisa por stdout; NO falla. Si está presente → firma (camino pago).
#
# Exit:   0 si arma el .pkg (firmado o no) · 1 si falta algún artefacto o falla pkgbuild/productbuild.

set -uo pipefail

log()  { printf '  make-installer: %s\n' "$*" >&2; }
fail() { printf '  make-installer: ERROR: %s\n' "$*" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
CONFIG="${CONFIG:-Release}"
INSTALLER_SIGN_ID="${INSTALLER_SIGN_ID:-}"

# Identidad del paquete (para pkgbuild --identifier y la versión del Release).
PKG_IDENTIFIER="com.ovni.plugins"

# --- Cumplimiento AGPLv3 (secciones 4/6): dónde está el LICENSE y a dónde apunta la oferta de fuente.
# SOURCE_URL: URL pública del repositorio fuente. PLACEHOLDER — confirmar la URL real del repo
# (ver reporte). Se puede sobrescribir por entorno: SOURCE_URL=… packaging/make-installer.sh …
LICENSE_FILE="${LICENSE_FILE:-$ROOT/LICENSE}"
SOURCE_URL="${SOURCE_URL:-https://github.com/ovniaudio/ovni}"

# --- args ---
VERSION=""
OUT=""
PLUGINS_FILTER=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --out)     OUT="${2:-}"; shift 2 ;;
    --plugins) PLUGINS_FILTER="${2:-}"; shift 2 ;;
    *) fail "argumento desconocido: $1 (uso: --version <X.Y.Z> --out <ruta.pkg> [--plugins \"a b c\"])" ;;
  esac
done
[ -n "$VERSION" ] || fail "falta --version <X.Y.Z>"
[ -n "$OUT" ]     || fail "falta --out <ruta.pkg>"

# ¿Este bundle entra según el filtro --plugins? (sin filtro → entra todo). Match case-insensitive
# por nombre de archivo (p.ej. "halo" matchea HALO.vst3 / HALO.component). Mismo criterio que
# make-dmg.sh, para que el .pkg y el .dmg lleven EXACTAMENTE el mismo set de plugins.
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

command -v pkgbuild     >/dev/null 2>&1 || fail "no encuentro 'pkgbuild' (¿es macOS?)"
command -v productbuild >/dev/null 2>&1 || fail "no encuentro 'productbuild' (¿es macOS?)"
[ -d "$BUILD_DIR" ] || fail "no existe BUILD_DIR=$BUILD_DIR"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
VST3_ROOT="$WORK/root-vst3/Library/Audio/Plug-Ins/VST3"
AU_ROOT="$WORK/root-au/Library/Audio/Plug-Ins/Components"
mkdir -p "$VST3_ROOT" "$AU_ROOT"

# --- Juntar los artefactos universales en el payload de cada componente. ---
n_vst3=0; n_au=0
while IFS= read -r vst3; do
  [ -n "$vst3" ] || continue
  matches_filter "$vst3" || continue
  cp -R "$vst3" "$VST3_ROOT/"; n_vst3=$((n_vst3+1)); log "VST3: $(basename "$vst3")"
done < <(find "$BUILD_DIR" -path "*_artefacts/$CONFIG/VST3/*.vst3" -prune 2>/dev/null)

while IFS= read -r au; do
  [ -n "$au" ] || continue
  matches_filter "$au" || continue
  cp -R "$au" "$AU_ROOT/"; n_au=$((n_au+1)); log "AU: $(basename "$au")"
done < <(find "$BUILD_DIR" -path "*_artefacts/$CONFIG/AU/*.component" -prune 2>/dev/null)

[ "$n_vst3" -gt 0 ] || fail "no encontré ningún .vst3 en $BUILD_DIR/*_artefacts/$CONFIG/VST3/"
[ "$n_au"   -gt 0 ] || fail "no encontré ningún .component en $BUILD_DIR/*_artefacts/$CONFIG/AU/"

# --- Payload de cumplimiento AGPLv3: LICENSE + oferta de código fuente. ---
# La AGPLv3 (secciones 4/6) exige que al distribuir el binario entreguemos el texto de la licencia
# y una forma de obtener el código fuente correspondiente. Los instalamos en el disco del usuario
# junto a los plugins, en /Library/Audio/Plug-Ins/OVNI Audio/.
LICENSE_ROOT="$WORK/root-license/Library/Audio/Plug-Ins/OVNI Audio"
mkdir -p "$LICENSE_ROOT"
[ -f "$LICENSE_FILE" ] || fail "no encuentro el LICENSE en $LICENSE_FILE (requerido por AGPLv3)"
cp "$LICENSE_FILE" "$LICENSE_ROOT/LICENSE.txt"
log "LICENSE → OVNI Audio/LICENSE.txt (AGPLv3)"
cat > "$LICENSE_ROOT/SOURCE.txt" <<SOURCE
============================================================
  OVNI Audio — Código fuente (AGPLv3)
============================================================

Estos plugins son software libre bajo la Licencia Pública
General Affero de GNU, versión 3 (AGPLv3). El texto completo
de la licencia está en el archivo LICENSE.txt de esta carpeta.

Tenés derecho a obtener el código fuente completo y correspondiente
de esta versión. Está disponible públicamente en:

    $SOURCE_URL

(source available per AGPLv3 §6)

Versión de este paquete: $VERSION

¿Dudas? https://ovniaudio.com  ·  hello@ovniaudio.com
============================================================
SOURCE
log "SOURCE.txt → OVNI Audio/SOURCE.txt (oferta de fuente: $SOURCE_URL)"

# --- Component packages (pkgbuild). --install-location fija el destino SYSTEM de cada formato. ---
log "pkgbuild VST3 ($n_vst3 plugins)…"
pkgbuild --root "$WORK/root-vst3" \
  --identifier "$PKG_IDENTIFIER.vst3" \
  --version "$VERSION" \
  --install-location "/" \
  "$WORK/ovni-vst3.pkg" >&2 || fail "pkgbuild VST3 falló"

log "pkgbuild AU ($n_au plugins)…"
pkgbuild --root "$WORK/root-au" \
  --identifier "$PKG_IDENTIFIER.au" \
  --version "$VERSION" \
  --install-location "/" \
  "$WORK/ovni-au.pkg" >&2 || fail "pkgbuild AU falló"

log "pkgbuild LICENSE + SOURCE (cumplimiento AGPLv3)…"
pkgbuild --root "$WORK/root-license" \
  --identifier "$PKG_IDENTIFIER.license" \
  --version "$VERSION" \
  --install-location "/" \
  "$WORK/ovni-license.pkg" >&2 || fail "pkgbuild LICENSE falló"

# --- Resources del instalador: el texto de la AGPLv3 que se muestra/acepta durante el install. ---
INSTALLER_RESOURCES="$WORK/resources"
mkdir -p "$INSTALLER_RESOURCES"
cp "$LICENSE_FILE" "$INSTALLER_RESOURCES/LICENSE.txt"

# --- Distribution XML: junta los componentes (VST3 + AU + LICENSE) en un solo instalador. ---
# El componente 'license' instala LICENSE.txt + SOURCE.txt en disco (AGPLv3 §4/§6) y NO es
# deseleccionable (enabled="false"). El <license> muestra la AGPLv3 para aceptar durante el install.
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OVNI Audio — Plugins</title>
    <organization>com.ovni</organization>
    <license file="LICENSE.txt"/>
    <!-- Requiere macOS 11.0+ (coherente con el deployment target del build universal). -->
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="license"/>
    </choices-outline>
    <choice id="vst3" title="VST3"><pkg-ref id="$PKG_IDENTIFIER.vst3"/></choice>
    <choice id="au"   title="Audio Unit"><pkg-ref id="$PKG_IDENTIFIER.au"/></choice>
    <choice id="license" title="Licencia y fuente (AGPLv3)" enabled="false" selected="true">
        <pkg-ref id="$PKG_IDENTIFIER.license"/>
    </choice>
    <pkg-ref id="$PKG_IDENTIFIER.vst3"    version="$VERSION">ovni-vst3.pkg</pkg-ref>
    <pkg-ref id="$PKG_IDENTIFIER.au"      version="$VERSION">ovni-au.pkg</pkg-ref>
    <pkg-ref id="$PKG_IDENTIFIER.license" version="$VERSION">ovni-license.pkg</pkg-ref>
</installer-gui-script>
XML

# --- productbuild: arma el product archive final. FIRMA del instalador acá. ---
mkdir -p "$(dirname "$OUT")"
if [ -n "$INSTALLER_SIGN_ID" ]; then
  # Firma con "Developer ID Installer: …". Necesaria para que el .pkg pase Gatekeeper +
  # sea notarizable. La identidad la provee release.yml desde el secret APPLE_DEVELOPER_ID_INSTALLER.
  log "productbuild + firma (Developer ID Installer)…"
  productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --resources "$INSTALLER_RESOURCES" \
    --version "$VERSION" \
    --sign "$INSTALLER_SIGN_ID" \
    "$OUT" >&2 || fail "productbuild (firmado) falló"
else
  # Sin firma (camino gratis, default hoy): el .pkg funciona y es legal; el usuario ve el aviso de
  # Gatekeeper la 1ra vez (click derecho → Abrir / `xattr -dr com.apple.quarantine …`). NO falla.
  log "productbuild SIN firma (INSTALLER_SIGN_ID vacío → camino gratis: .pkg sin firmar)…"
  productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --resources "$INSTALLER_RESOURCES" \
    --version "$VERSION" \
    "$OUT" >&2 || fail "productbuild (sin firma) falló"
fi

if [ -n "$INSTALLER_SIGN_ID" ]; then
  log "✓ instalador FIRMADO: $OUT (VST3=$n_vst3, AU=$n_au, +LICENSE/SOURCE AGPLv3, version=$VERSION)"
else
  log "✓ instalador SIN FIRMAR (camino gratis): $OUT (VST3=$n_vst3, AU=$n_au, +LICENSE/SOURCE AGPLv3, version=$VERSION)"
  log "  El usuario verá el aviso de Gatekeeper al abrir el .pkg → click derecho → Abrir."
  log "  Para firmar (camino pago), pasá INSTALLER_SIGN_ID=\"Developer ID Installer: … (TEAMID)\"."
fi
exit 0
