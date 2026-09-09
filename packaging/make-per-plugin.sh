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
# BUG DEL 2026-09-03 — POR QUÉ ESTE SCRIPT AHORA DESCONFÍA DE LAS VERSIONES.
# El .pkg de ORBIT v0.2.0 dejaba a los usuarios sin el AU. Los bundles se habían buildeado con el
# VERSION sin bumpear, así que declaraban CFBundleVersion 0.1.1; macOS Installer compara ese
# <bundle-version> con lo instalado, encontraba 0.1.1 ya presente y SALTEABA el componente (agravado
# porque VST3 y AU comparten CFBundleIdentifier com.ovni.orbit y el Installer los resuelve como un
# único bundle). Diagnóstico completo: mision-control/bugs/2026-09-03-orbit-au-no-se-instala.md.
# Tres defensas, en orden de cuándo atajan el problema:
#   1. GUARDIA DE VERSIÓN (antes de empaquetar): si un bundle no declara exactamente --version,
#      el script FALLA. Es lo que hubiera atajado el bug el 25-ago, con el bundle en la mano.
#   2. BundleIsVersionChecked=false + BundleOverwriteAction=upgrade en cada component plist: el
#      Installer deja de comparar versiones y SIEMPRE escribe el bundle, aunque el usuario tenga
#      la misma versión (o una "mayor" por error).
#   3. POST-CHECK (después de productbuild): se expande cada .pkg emitido y se verifica que el
#      PackageInfo de cada componente y el Distribution declaren la versión pedida.
# Y desde 0.3.1, en el mismo post-check, una GUARDIA DE ARQUITECTURA: cada ejecutable del payload tiene
# que ser universal (arm64 + x86_64). 0.3.0 estuvo a un pelo de salir thin porque el build/ había quedado
# en el preset `dev`, y eso sólo se nota en una Mac Intel, después de publicar. Test: packaging/tests/.
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

# Escribe una clave en un component plist de pkgbuild. `Set` si ya está (--analyze suele emitirla),
# `Add` con tipo si no — así no dependemos de qué versión de pkgbuild generó el plist.
plist_set() { # $1 = plist · $2 = ruta de la clave (sin ':' inicial) · $3 = tipo · $4 = valor
  /usr/libexec/PlistBuddy -c "Set :$2 $4" "$1" >/dev/null 2>&1 && return 0
  /usr/libexec/PlistBuddy -c "Add :$2 $3 $4" "$1" >/dev/null 2>&1 && return 0
  fail "no pude escribir $2=$4 en $(basename "$1")"
}

# Versión declarada por un bundle (CFBundleShortVersionString del Info.plist).
bundle_version() { # $1 = ruta al .vst3/.component/.app
  /usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null
}

# Valores de un atributo XML, uno por línea. Aplana el XML a una etiqueta por línea primero, así
# funciona igual con el Distribution (indentado) que con el PackageInfo (una sola línea larga).
xml_attr() { # $1 = etiqueta · $2 = atributo · XML por stdin
  tr '\n' ' ' | tr '<' '\n' | grep "^$1[ />]" | sed -n "s/.*[[:space:]]$2=\"\([^\"]*\)\".*/\1/p"
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALLER_SIGN_ID="${INSTALLER_SIGN_ID:-}"
PKG_ID="com.ovni.plugins"
# Arte del instalador (opcional). Si la carpeta no está, se emite igual que siempre, sin branding.
ART_DIR="${ART_DIR:-$ROOT/packaging/installer-resources}"

VERSION=""; BUNDLES=""; OUTDIR=""; WINZIP=""
# COMMIT del árbol que produjo estos bundles. Va al SOURCE.txt para que la oferta de fuente del AGPL §6
# apunte a algo EXACTO: el tag `v<version>` se mueve/renombra, el hash no. Se DERIVA del repo (nunca a
# mano); `--commit` existe sólo para el caso de empaquetar bundles de otro árbol.
COMMIT="${COMMIT:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo "")}"
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
    --commit)  COMMIT="${2:-}"; shift 2 ;;
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

# --- GUARDIA DE VERSIÓN (defensa 1 del bug del 3-sep, ver cabecera). ---
# Cada bundle tiene que declarar EXACTAMENTE --version. Si no, el .pkg saldría prometiendo una
# versión que el payload no tiene y el Installer podría saltear el componente en quien ya tenga
# la vieja. Se falla acá, con el bundle en la mano, y no cuatro semanas después por email.
for n in "${NAMES[@]}"; do
  for b in "$BUNDLES/$n.vst3" "$BUNDLES/$n.component" "$BUNDLES/$n.app"; do
    [ -d "$b" ] || continue
    bv="$(bundle_version "$b")"
    [ -n "$bv" ] || fail "$(basename "$b") no declara CFBundleShortVersionString en su Info.plist"
    [ "$bv" = "$VERSION" ] || fail \
      "$(basename "$b") declara la versión $bv pero se está empaquetando como $VERSION — rebuildeá el bundle con el VERSION bumpeado (o pasá --version $bv)"
  done
done
log "guardia de versión: los bundles declaran $VERSION ✓"

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
    · SUPERNOVA (visual synth, macOS):
        https://github.com/ovniaudio/ovni/tree/v$VERSION   (branch feat/supernova · tag v$VERSION)

(source available per AGPLv3 §6)

Versión de este paquete / package version: $VERSION
Commit exacto de este build / exact build commit: ${COMMIT:-(desconocido)}

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
      plist_set "$plist" "$i:BundleIsRelocatable"    bool   false
      # Defensa 2 del bug del 3-sep: que el Installer NO compare versiones y escriba SIEMPRE.
      plist_set "$plist" "$i:BundleIsVersionChecked" bool   false
      plist_set "$plist" "$i:BundleOverwriteAction"  string upgrade
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
      plist_set "$plist" "$i:BundleIsRelocatable"    bool   false
      plist_set "$plist" "$i:BundleIsVersionChecked" bool   false
      plist_set "$plist" "$i:BundleOverwriteAction"  string upgrade
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

# --- Branding del instalador: fondo del panel izquierdo, claro + oscuro. ---
#
# Installer.app dibuja el <background> en el panel izquierdo, debajo de la lista de pasos: por eso
# el arte va anclado "bottomleft" y en PNG transparente (un PNG opaco recorta un rectángulo sucio).
# <background-darkAqua> NO es opcional — sin él, el texto claro del arte se pierde en modo oscuro.
#
# Degradación elegante: si $ART_DIR no existe o le falta el par de PNG, BG_XML queda vacío y el
# instalador sale exactamente como antes de que existiera el branding.
BG_XML=""
install_background() { # $1 = dir de resources · $2 = slug del módulo (vacío = genérico)
  local res="$1" slug="${2:-}" light dark
  BG_XML=""
  [ -d "$ART_DIR" ] || return 0
  # Arte propio del módulo si existe; si no, el genérico. Un plugin nuevo nunca queda sin marca.
  for cand in "$slug" ""; do
    [ -n "$cand" ] && { light="$ART_DIR/bg-$cand-light.png"; dark="$ART_DIR/bg-$cand-dark.png"; } \
                   || { light="$ART_DIR/bg-light.png";       dark="$ART_DIR/bg-dark.png"; }
    [ -f "$light" ] && [ -f "$dark" ] && break
    light=""; dark=""
  done
  [ -n "$light" ] || { log "aviso: sin arte de instalador para ${slug:-genérico} — sale sin branding"; return 0; }
  cp "$light" "$res/background.png" && cp "$dark" "$res/background-dark.png" || return 0
  BG_XML=$(printf '    <background file="background.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>\n    <background-darkAqua file="background-dark.png" mime-type="image/png" alignment="bottomleft" scaling="proportional"/>')
}

# --- Resources del Installer: licencia + bienvenida/cierre EN + ES (.lproj). ---
make_resources() { # $1 = dir · $2 = frase EN · $3 = idem ES · $4 = extra EN · $5 = extra ES · $6 = slug
  local res="$1" slug="${6:-}"
  mkdir -p "$res/en.lproj" "$res/es.lproj"
  cp "$LICENSE_FILE" "$res/LICENSE.txt"
  install_background "$res" "$slug"
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

# --- POST-CHECK del .pkg emitido (defensa 3 del bug del 3-sep, ver cabecera). ---
# Se expande el producto dentro de $WORK y se exige que TODO lo que declara una versión declare
# $VERSION: el PackageInfo de cada component package, los <pkg-ref> del Distribution y —si
# productbuild todavía sintetizó alguno— los <bundle CFBundle…> del Distribution, que son
# exactamente los que decían 0.1.1 en el .pkg roto de ORBIT v0.2.0.
verify_pkg() { # $1 = .pkg emitido
  local pkg="$1" n x pi comp v seen=0 bin archs bins=0
  n="$(basename "$pkg")"
  x="$WORK/verify-${n%.pkg}"
  rm -rf "$x"
  # --expand-full (no --expand): además del Distribution/PackageInfo hace falta el PAYLOAD desempacado
  # para poder mirar los binarios con lipo. Ver la guardia de arquitectura, más abajo.
  pkgutil --expand-full "$pkg" "$x" >/dev/null 2>&1 || fail "post-check: pkgutil --expand-full falló en $n"
  [ -f "$x/Distribution" ] || fail "post-check: $n no tiene Distribution"

  for pi in "$x"/*.pkg/PackageInfo; do
    [ -f "$pi" ] || fail "post-check: $n no trae ningún component package adentro"
    comp="$(basename "$(dirname "$pi")")"
    v="$(xml_attr pkg-info version < "$pi" | head -1)"
    [ "$v" = "$VERSION" ] || fail "post-check: $n → $comp/PackageInfo declara version=${v:-<vacío>} (esperaba $VERSION)"
    seen=$((seen+1))
  done
  [ "$seen" -gt 0 ] || fail "post-check: $n sin componentes verificables"

  while read -r v; do
    [ "$v" = "$VERSION" ] || fail "post-check: $n → Distribution tiene un <pkg-ref version=\"$v\"> (esperaba $VERSION)"
  done < <(xml_attr pkg-ref version < "$x/Distribution")

  for attr in CFBundleShortVersionString CFBundleVersion; do
    while read -r v; do
      [ "$v" = "$VERSION" ] || fail "post-check: $n → Distribution tiene un <bundle $attr=\"$v\"> (esperaba $VERSION)"
    done < <(xml_attr bundle "$attr" < "$x/Distribution")
  done

  # --- GUARDIA DE ARQUITECTURA ---
  # 0.3.0 estuvo a un pelo de salir arm64-only: el build/ había quedado en el preset `dev` y nadie lo
  # hubiera atrapado. El .pkg se arma igual de contento con un payload thin, y el problema recién
  # aparece en una Mac Intel, después de publicar. Se mira el binario que REALMENTE va adentro del
  # paquete —no el que había en disco cuando se lanzó el script—, que es lo único que se distribuye.
  # -L: sigue los symlinks. Sin eso, un ejecutable que sea un enlace (lo normal en los frameworks
  # versionados de un .app) no es "-type f" y la guardia lo saltearía en silencio.
  while IFS= read -r bin; do
    archs="$(lipo -archs "$bin" 2>/dev/null)"
    case " $archs " in *" x86_64 "*) ;; *) fail "no universal: ${bin#"$x/"} = ${archs:-<no es Mach-O>}" ;; esac
    case " $archs " in *" arm64 "*)  ;; *) fail "no universal: ${bin#"$x/"} = ${archs:-<no es Mach-O>}" ;; esac
    bins=$((bins+1))
  done < <(find -L "$x" -type f -path "*/Contents/MacOS/*" -perm -u+x)
  [ "$bins" -gt 0 ] || fail "post-check: $n no trae ningún ejecutable en el payload (¿bundles vacíos?)"

  # …y POR BUNDLE, no en total: contar todo junto deja pasar un .component vacío colgado del .vst3 que sí
  # trae el suyo. Cada bundle del payload tiene que traer al menos un ejecutable propio.
  while IFS= read -r bundle; do
    [ -n "$(find -L "$bundle/Contents/MacOS" -type f -perm -u+x -print -quit 2>/dev/null)" ] \
      || fail "post-check: $n → ${bundle#"$x/"} sin ejecutable en Contents/MacOS"
  done < <(find "$x" \( -name "*.app" -o -name "*.vst3" -o -name "*.component" \) -type d)

  rm -rf "$x"
  log "  post-check OK: $n declara $VERSION en $seen componentes + Distribution · $bins binarios universales"
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
  verify_pkg "$3"
}

# --- Instalador individual por plugin. ---
for n in "${NAMES[@]}"; do
  lower="$(printf '%s' "$n" | tr '[:upper:]' '[:lower:]')"
  res="$WORK/res-$lower"
  make_resources "$res" \
    "$n — $(desc_of "$n")" \
    "$n — $(desc_of "$n")" \
    "" "" "$lower"
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
${BG_XML}
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
  "<p>Tocá <b>Personalizar</b> durante la instalación para elegir módulos sueltos.</p>" "all"
{
  cat <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>OVNI Audio — ${#NAMES[@]} Plugins (v$VERSION)</title>
    <organization>com.ovni</organization>
${BG_XML}
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
