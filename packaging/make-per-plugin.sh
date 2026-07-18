#!/usr/bin/env bash
# packaging/make-per-plugin.sh — Arma los entregables POR PLUGIN + el instalador COMPLETO.
#
# Produce, desde una carpeta de bundles ya buildeados (NAME.vst3 + NAME.component):
#   · OVNI-<NAME>-v<X.Y.Z>.pkg           un instalador macOS por plugin (VST3 + AU)
#   · OVNI-v<X.Y.Z>.pkg                  el instalador macOS con TODO el catálogo (elegible
#                                        por plugin vía "Personalizar"; default = los 7)
#   · OVNI-<NAME>-v<X.Y.Z>-Windows.zip   un ZIP por plugin (si se pasa --winzip con el ZIP completo)
#   · SHA256SUMS.txt                     checksums de todo lo emitido
#
# ¿Por qué .pkg y no solo .dmg? El .pkg instala SOLO con doble click (Installer.app copia los
# bundles a /Library/Audio/Plug-Ins/{VST3,Components} y pide la contraseña de admin él mismo).
# Además, lo que instala el Installer NO queda en cuarentena → desaparece el paso de
# `xattr -dr com.apple.quarantine …` del flujo DMG. El único aviso de Gatekeeper que queda es
# al abrir el .pkg (sin firmar): click derecho → Abrir (macOS ≤ 14) o Ajustes del Sistema →
# Privacidad y seguridad → "Abrir de todos modos" (macOS 15+).
#
# Los component packages usan identificadores POR PLUGIN (com.ovni.plugins.<id>.{vst3,au}) y
# el instalador completo referencia LOS MISMOS componentes → los recibos (pkgutil) quedan
# coherentes se instale por separado o todo junto. BundleIsRelocatable=false en todos: si el
# usuario ya tenía una copia vieja en ~/Library (instalación manual del DMG), el Installer NO
# debe "relocalizar" la nueva ahí — siempre instala en /Library.
#
# Cumplimiento AGPLv3 (§4/§6): cada instalador incluye y muestra la licencia, e instala
# LICENSE.txt + NOTICE.txt + SOURCE.txt en /Library/Audio/Plug-Ins/OVNI Audio/.
#
# Uso:
#   packaging/make-per-plugin.sh --version 0.1.1 --bundles <dir> --outdir <dir> \
#       [--winzip OVNI-v0.1.1-Windows.zip] [--license <file>] [--notice <file>]
#
#   --bundles: carpeta plana con <NAME>.vst3 y <NAME>.component (p.ej. extraídos del DMG del
#              release, o juntados del build). Se detectan los plugins por los pares presentes.
#
# Firma: igual que make-installer.sh, INSTALLER_SIGN_ID opcional (vacío → sin firmar, camino
# gratis; NO falla). Exit 0 si emite todo · 1 ante cualquier falta/fallo.

set -uo pipefail

log()  { printf '  make-per-plugin: %s\n' "$*" >&2; }
fail() { printf '  make-per-plugin: ERROR: %s\n' "$*" >&2; exit 1; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALLER_SIGN_ID="${INSTALLER_SIGN_ID:-}"
PKG_ID="com.ovni.plugins"

VERSION=""; BUNDLES=""; OUTDIR=""; WINZIP=""
LICENSE_FILE="${LICENSE_FILE:-$ROOT/LICENSE}"
NOTICE_FILE="${NOTICE_FILE:-$ROOT/NOTICE.md}"
while [ "$#" -gt 0 ]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --bundles) BUNDLES="${2:-}"; shift 2 ;;
    --outdir)  OUTDIR="${2:-}"; shift 2 ;;
    --winzip)  WINZIP="${2:-}"; shift 2 ;;
    --license) LICENSE_FILE="${2:-}"; shift 2 ;;
    --notice)  NOTICE_FILE="${2:-}"; shift 2 ;;
    *) fail "argumento desconocido: $1" ;;
  esac
done
[ -n "$VERSION" ] || fail "falta --version"
[ -d "${BUNDLES:-}" ] || fail "falta --bundles <dir> (con NAME.vst3 + NAME.component)"
[ -n "$OUTDIR" ] || fail "falta --outdir"
[ -f "$LICENSE_FILE" ] || fail "no encuentro LICENSE en $LICENSE_FILE (AGPLv3 §4)"
command -v pkgbuild >/dev/null 2>&1 || fail "no encuentro pkgbuild (¿macOS?)"
command -v productbuild >/dev/null 2>&1 || fail "no encuentro productbuild (¿macOS?)"
mkdir -p "$OUTDIR"

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# --- Descubrir plugins: pares NAME.vst3 + NAME.component en --bundles. ---
NAMES=()
for v in "$BUNDLES"/*.vst3; do
  [ -e "$v" ] || continue
  n="$(basename "$v" .vst3)"
  [ -d "$BUNDLES/$n.component" ] || fail "$n.vst3 sin su $n.component (el catálogo macOS lleva ambos)"
  NAMES+=("$n")
done
[ "${#NAMES[@]}" -gt 0 ] || fail "no encontré ningún .vst3 en $BUNDLES"
log "plugins: ${NAMES[*]}"

# Descripción corta por plugin (para la vista Personalizar del instalador completo).
desc_of() {
  case "$1" in
    ORBIT)   echo "Binaural movement engine — place sound in real 3D, orbit it, fly it past." ;;
    PULSAR)  echo "Auto-pan with chaotic orbits, real Doppler and binaural width." ;;
    NEBULA)  echo "Reverb of impossible, infinite spaces — an FDN cloud that breathes and freezes." ;;
    DUST)    echo "Binaural echoes scattered as bubbles orbiting the head." ;;
    HALO)    echo "Shimmer that orbits — an endless choir of octaves and fifths." ;;
    HORIZON) echo "Spectral freeze with a pulse — eternal pad to rhythmic stutter." ;;
    AURORA)  echo "Spectral panning — every frequency to its own place in the field." ;;
    SUPERNOVA) echo "Audio-reactive visual synth — drag an image, the particles live with your sound." ;;
    *)       echo "OVNI Audio spatial FX module." ;;
  esac
}

# --- Payload compartido de cumplimiento AGPLv3 (idéntico en TODOS los instaladores para que
#     el component package com.ovni.plugins.license sea el mismo se instale lo que se instale). ---
LIC_ROOT="$WORK/root-license/Library/Audio/Plug-Ins/OVNI Audio"
mkdir -p "$LIC_ROOT"
cp "$LICENSE_FILE" "$LIC_ROOT/LICENSE.txt"
[ -f "$NOTICE_FILE" ] && cp "$NOTICE_FILE" "$LIC_ROOT/NOTICE.txt"
cat > "$LIC_ROOT/SOURCE.txt" <<SOURCE
============================================================
  OVNI Audio — Código fuente / Source code (AGPLv3)
============================================================

Estos plugins son software libre bajo AGPLv3 (texto completo
en LICENSE.txt). Tenés derecho al código fuente completo y
correspondiente de esta versión / You are entitled to the
complete corresponding source of this version:

    · Catálogo — PULSAR, NEBULA, DUST, HALO, HORIZON, AURORA:
        https://github.com/ovniaudio/ovni
    · ORBIT (el flagship):
        https://github.com/ovniaudio/orbita

(source available per AGPLv3 §6)

Versión de este paquete / package version: $VERSION

¿Dudas? https://ovniaudio.com  ·  hello@ovniaudio.com
============================================================
SOURCE
xattr -cr "$WORK/root-license" 2>/dev/null || true
pkgbuild --root "$WORK/root-license" --identifier "$PKG_ID.license" \
  --version "$VERSION" --install-location "/" "$WORK/pkg-license.pkg" >&2 \
  || fail "pkgbuild license falló"

# --- Component packages por plugin (VST3 + AU), NO relocalizables. ---
build_components() { # $1 = NAME
  local name="$1" lower plist i
  lower="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"
  local rv="$WORK/root-$lower-vst3/Library/Audio/Plug-Ins/VST3"
  local ra="$WORK/root-$lower-au/Library/Audio/Plug-Ins/Components"
  mkdir -p "$rv" "$ra"
  ditto "$BUNDLES/$name.vst3" "$rv/$name.vst3" || fail "ditto $name.vst3"
  ditto "$BUNDLES/$name.component" "$ra/$name.component" || fail "ditto $name.component"
  find "$WORK/root-$lower-vst3" "$WORK/root-$lower-au" -name .DS_Store -delete 2>/dev/null
  xattr -cr "$WORK/root-$lower-vst3" "$WORK/root-$lower-au" 2>/dev/null || true
  for kind in vst3 au; do
    local root="$WORK/root-$lower-$kind"
    plist="$WORK/$lower-$kind.plist"
    pkgbuild --analyze --root "$root" "$plist" >/dev/null 2>&1 || fail "pkgbuild --analyze $name/$kind"
    i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
      /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null || true
      i=$((i+1))
    done
    pkgbuild --root "$root" --component-plist "$plist" \
      --identifier "$PKG_ID.$lower.$kind" --version "$VERSION" \
      --install-location "/" "$WORK/pkg-$lower-$kind.pkg" >&2 \
      || fail "pkgbuild $name/$kind falló"
  done
  # Standalone app (solo si el bundle trae <NAME>.app — hoy SUPERNOVA): instala en /Applications,
  # NO relocalizable. Mismo patrón que VST3/AU: root = filesystem desde "/", install-location "/".
  if [ -d "$BUNDLES/$name.app" ]; then
    local rapp="$WORK/root-$lower-app/Applications"
    mkdir -p "$rapp"
    ditto "$BUNDLES/$name.app" "$rapp/$name.app" || fail "ditto $name.app"
    find "$WORK/root-$lower-app" -name .DS_Store -delete 2>/dev/null
    xattr -cr "$WORK/root-$lower-app" 2>/dev/null || true
    plist="$WORK/$lower-app.plist"
    pkgbuild --analyze --root "$WORK/root-$lower-app" "$plist" >/dev/null 2>&1 || fail "pkgbuild --analyze $name/app"
    i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
      /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" 2>/dev/null || true
      i=$((i+1))
    done
    pkgbuild --root "$WORK/root-$lower-app" --component-plist "$plist" \
      --identifier "$PKG_ID.$lower.app" --version "$VERSION" \
      --install-location "/" "$WORK/pkg-$lower-app.pkg" >&2 \
      || fail "pkgbuild $name/app falló"
    log "  + app standalone: $name.app → /Applications"
  fi
}
for n in "${NAMES[@]}"; do log "componentes: $n"; build_components "$n"; done

# --- Resources del Installer: licencia + bienvenida/cierre EN + ES (.lproj). ---
make_resources() { # $1 = dir · $2 = qué instala (frase EN) · $3 = idem ES · $4 = extra EN · $5 = extra ES
  local res="$1"
  mkdir -p "$res/en.lproj" "$res/es.lproj"
  cp "$LICENSE_FILE" "$res/LICENSE.txt"
  cat > "$res/en.lproj/welcome.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>$2</b></p>
<p>Free &amp; open-source (AGPLv3), by OVNI Audio. The installer places everything in the system plug-in folders — no dragging, no Terminal:</p>
<p>&nbsp;&nbsp;VST3 → /Library/Audio/Plug-Ins/VST3<br>&nbsp;&nbsp;AU → /Library/Audio/Plug-Ins/Components</p>
$4
<p>Manuals &amp; the rest of the catalog: <b>ovniaudio.com</b></p>
</body></html>
HTML
  cat > "$res/es.lproj/welcome.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>$3</b></p>
<p>Gratis y open-source (AGPLv3), de OVNI Audio. El instalador deja todo en las carpetas de plugins del sistema — sin arrastrar nada, sin Terminal:</p>
<p>&nbsp;&nbsp;VST3 → /Library/Audio/Plug-Ins/VST3<br>&nbsp;&nbsp;AU → /Library/Audio/Plug-Ins/Components</p>
$5
<p>Manuales y el resto del catálogo: <b>ovniaudio.com</b></p>
</body></html>
HTML
  cat > "$res/en.lproj/conclusion.html" <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>Done.</b> Open your DAW and rescan plug-ins — the OVNI modules will show up in your list (VST3 and AU).</p>
<p>Manuals &amp; support: <b>ovniaudio.com</b> · hello@ovniaudio.com &nbsp;🛸</p>
</body></html>
HTML
  cat > "$res/es.lproj/conclusion.html" <<'HTML'
<!doctype html><html><head><meta charset="utf-8"><style>body{font-family:-apple-system,'Helvetica Neue',sans-serif;font-size:13px}</style></head><body>
<p><b>Listo.</b> Abrí tu DAW y reescaneá los plugins — los módulos de OVNI van a aparecer en tu lista (VST3 y AU).</p>
<p>Manuales y soporte: <b>ovniaudio.com</b> · hello@ovniaudio.com &nbsp;🛸</p>
</body></html>
HTML
}

# --- productbuild común (firma opcional). ---
product() { # $1 dist.xml · $2 resources · $3 out.pkg
  if [ -n "$INSTALLER_SIGN_ID" ]; then
    productbuild --distribution "$1" --package-path "$WORK" --resources "$2" \
      --version "$VERSION" --sign "$INSTALLER_SIGN_ID" "$3" >&2 || fail "productbuild (firmado) $3"
  else
    productbuild --distribution "$1" --package-path "$WORK" --resources "$2" \
      --version "$VERSION" "$3" >&2 || fail "productbuild $3"
  fi
}

# --- Instalador individual por plugin. ---
for n in "${NAMES[@]}"; do
  lower="$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')"
  res="$WORK/res-$lower"
  make_resources "$res" \
    "$n — $(desc_of "$n")" \
    "$n — $(desc_of "$n")" \
    "" ""
  # Fragmentos de la app standalone — sólo si build_components emitió pkg-$lower-app.pkg.
  app_line=""; app_choice_line=""; app_ref_line=""
  if [ -f "$WORK/pkg-$lower-app.pkg" ]; then
    app_line="<line choice=\"app\"/>"
    app_choice_line="    <choice id=\"app\" title=\"$n — Standalone app (→ /Applications)\"><pkg-ref id=\"$PKG_ID.$lower.app\"/></choice>"
    app_ref_line="    <pkg-ref id=\"$PKG_ID.$lower.app\" version=\"$VERSION\">pkg-$lower-app.pkg</pkg-ref>"
  fi
  cat > "$WORK/dist-$lower.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OVNI Audio — $n (v$VERSION)</title>
    <organization>com.ovni</organization>
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <conclusion file="conclusion.html"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/><line choice="au"/>${app_line}<line choice="license"/>
    </choices-outline>
    <choice id="vst3" title="$n VST3"><pkg-ref id="$PKG_ID.$lower.vst3"/></choice>
    <choice id="au" title="$n AU"><pkg-ref id="$PKG_ID.$lower.au"/></choice>
${app_choice_line}
    <choice id="license" title="License &amp; source (AGPLv3)" enabled="false" selected="true">
        <pkg-ref id="$PKG_ID.license"/>
    </choice>
    <pkg-ref id="$PKG_ID.$lower.vst3" version="$VERSION">pkg-$lower-vst3.pkg</pkg-ref>
    <pkg-ref id="$PKG_ID.$lower.au" version="$VERSION">pkg-$lower-au.pkg</pkg-ref>
${app_ref_line}
    <pkg-ref id="$PKG_ID.license" version="$VERSION">pkg-license.pkg</pkg-ref>
</installer-gui-script>
XML
  out="$OUTDIR/OVNI-$n-v$VERSION.pkg"
  product "$WORK/dist-$lower.xml" "$res" "$out"
  log "✓ $out"
done

# --- Instalador COMPLETO (los 7; deseleccionables en "Personalizar"). ---
res="$WORK/res-all"
make_resources "$res" \
  "The complete OVNI catalog — all ${#NAMES[@]} plugins (VST3 + AU each)." \
  "El catálogo OVNI completo — los ${#NAMES[@]} plugins (VST3 + AU cada uno)." \
  "<p>Click <b>Customize</b> during install to pick specific modules.</p>" \
  "<p>Tocá <b>Personalizar</b> durante la instalación para elegir módulos sueltos.</p>"
{
  cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OVNI Audio — ${#NAMES[@]} Plugins (v$VERSION)</title>
    <organization>com.ovni</organization>
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <conclusion file="conclusion.html"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
XML
  for n in "${NAMES[@]}"; do
    lower="$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')"
    printf '        <line choice="%s"/>\n' "$lower"
  done
  printf '        <line choice="license"/>\n    </choices-outline>\n'
  for n in "${NAMES[@]}"; do
    lower="$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')"
    app_pref=""; [ -f "$WORK/pkg-$lower-app.pkg" ] && app_pref="<pkg-ref id=\"$PKG_ID.$lower.app\"/>"
    printf '    <choice id="%s" title="%s" description="%s" start_selected="true">\n        <pkg-ref id="%s.%s.vst3"/><pkg-ref id="%s.%s.au"/>%s\n    </choice>\n' \
      "$lower" "$n" "$(desc_of "$n")" "$PKG_ID" "$lower" "$PKG_ID" "$lower" "$app_pref"
  done
  cat <<XML
    <choice id="license" title="License &amp; source (AGPLv3)" enabled="false" selected="true">
        <pkg-ref id="$PKG_ID.license"/>
    </choice>
XML
  for n in "${NAMES[@]}"; do
    lower="$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')"
    printf '    <pkg-ref id="%s.%s.vst3" version="%s">pkg-%s-vst3.pkg</pkg-ref>\n    <pkg-ref id="%s.%s.au" version="%s">pkg-%s-au.pkg</pkg-ref>\n' \
      "$PKG_ID" "$lower" "$VERSION" "$lower" "$PKG_ID" "$lower" "$VERSION" "$lower"
    [ -f "$WORK/pkg-$lower-app.pkg" ] && printf '    <pkg-ref id="%s.%s.app" version="%s">pkg-%s-app.pkg</pkg-ref>\n' "$PKG_ID" "$lower" "$VERSION" "$lower"
  done
  printf '    <pkg-ref id="%s.license" version="%s">pkg-license.pkg</pkg-ref>\n</installer-gui-script>\n' "$PKG_ID" "$VERSION"
} > "$WORK/dist-all.xml"
out="$OUTDIR/OVNI-v$VERSION.pkg"
product "$WORK/dist-all.xml" "$res" "$out"
log "✓ $out (completo)"

# --- ZIPs de Windows por plugin (desde el ZIP completo del release). ---
if [ -n "$WINZIP" ]; then
  [ -f "$WINZIP" ] || fail "no encuentro --winzip $WINZIP"
  WZ="$WORK/winzip"; mkdir -p "$WZ"
  ( cd "$WZ" && unzip -q "$WINZIP" ) || fail "unzip del ZIP completo falló"
  WROOT="$(find "$WZ" -maxdepth 1 -mindepth 1 -type d | head -1)"
  [ -d "$WROOT/VST3" ] || fail "el ZIP no tiene carpeta VST3/ adentro"
  for n in "${NAMES[@]}"; do
    [ -d "$WROOT/VST3/$n.vst3" ] || { log "aviso: $n.vst3 no está en el ZIP de Windows — lo salteo"; continue; }
    stage="$WORK/OVNI-$n-v$VERSION-Windows"
    mkdir -p "$stage/VST3"
    cp -R "$WROOT/VST3/$n.vst3" "$stage/VST3/"
    for f in LICENSE.txt NOTICE.txt SOURCE.txt; do [ -f "$WROOT/$f" ] && cp "$WROOT/$f" "$stage/"; done
    # perl (no sed): el LEEME shipped viene con CRLF y las anclas $ de sed no matchean con \r.
    # Preservamos los CRLF (Windows/Notepad los necesita) y adaptamos el texto al plugin suelto.
    N="$n" V="$VERSION" perl -pe '
      s/OVNI-v\Q$ENV{V}\E-Windows\.zip/OVNI-$ENV{N}-v$ENV{V}-Windows.zip/g;
      s/Este paquete trae los 7 modulos en formato/Este paquete trae $ENV{N} en formato/;
      s/^(\s*)ORBIT · AURORA · DUST · HALO · HORIZON · NEBULA · PULSAR(\r?)$/$1$ENV{N}   (el resto del catalogo: ovniaudio.com)$2/;
    ' "$WROOT/LEEME PRIMERO.txt" > "$stage/LEEME PRIMERO.txt"
    grep -q "ORBIT · AURORA" "$stage/LEEME PRIMERO.txt" && fail "LEEME de $n: la lista de los 7 no se reemplazó"
    out="$OUTDIR/OVNI-$n-v$VERSION-Windows.zip"
    rm -f "$out"
    ( cd "$WORK" && zip -qrX "$out" "OVNI-$n-v$VERSION-Windows" ) || fail "zip $n falló"
    log "✓ $out"
  done
fi

# --- Checksums de todo lo emitido. ---
( cd "$OUTDIR" && shasum -a 256 OVNI-*"v$VERSION"*.pkg OVNI-*"v$VERSION"*-Windows.zip 2>/dev/null > SHA256SUMS.txt )
log "✓ SHA256SUMS.txt"
[ -z "$INSTALLER_SIGN_ID" ] && log "instaladores SIN FIRMAR (camino gratis): Gatekeeper avisa al abrir el .pkg → click derecho → Abrir (≤14) / Ajustes → Privacidad y seguridad → Abrir de todos modos (15+)."
exit 0
