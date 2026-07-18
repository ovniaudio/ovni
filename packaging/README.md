# OVNI — Packaging & Release

Cómo se construye, empaqueta y distribuye el catálogo de plugins OVNI. Doctrina del sello:
**honestidad verificable** — cada claim (universal, firmado, notarizado) se prueba con un comando
reproducible; nada se afirma "de palabra".

La distribución arranca **GRATIS**, sin pagar la cuenta Apple Developer (99 USD/año). El plugin
**sin firmar es 100% legal y funciona**: el usuario solo ve el aviso de Gatekeeper la primera vez y
lo destraba con un click extra. La firma + notarización quedan **opcionales**, listas para el día
que se cargue el $99.

## Dos caminos (lo decide la presencia de los secrets de Apple)

| | **Camino GRATIS** (default HOY) | **Camino PAGO** (cuando se cargue el $99) |
|---|---|---|
| Requiere | nada (solo el repo) | cuenta Apple Developer + secrets cargados |
| Firma de plugins | no | sí (Developer ID Application, hardened runtime) |
| Firma del .dmg/.pkg | no | sí (Developer ID Application / Installer) |
| Notarización | no | sí (notarytool + staple) |
| Gatekeeper para el usuario | aviso la 1ra vez → click derecho → Abrir | abre directo, sin aviso |
| ¿Es legal? | **sí** (AGPLv3 + JUCE) | sí |
| ¿`release.yml` falla? | **no** | no |

El interruptor en `release.yml` es el secret `APPLE_DEVELOPER_ID_APP`:
- **vacío/ausente** → arma `.dmg` + `.pkg` **sin firmar** y los publica igual (camino gratis).
- **presente** → firma + notariza + staplea (camino pago).

Ningún step falla por falta de secrets: los pasos de firma están guardados con
`if: ${{ secrets.APPLE_DEVELOPER_ID_APP != '' }}` y hay un step inverso que empaqueta sin firmar
cuando no hay secrets.

## ¿Por qué es legal sin el $99?

- Los plugins son **software libre bajo AGPLv3** (más JUCE, con licencia compatible). Distribuir
  binarios propios no requiere ninguna cuenta paga de Apple.
- La cuenta Apple Developer (99 USD/año) **no es un requisito legal**: solo habilita la *firma con
  Developer ID* + *notarización*, que es lo que hace que macOS abra el plugin **sin** el aviso de
  Gatekeeper. Es comodidad para el usuario, no permiso legal.
- Sin firma, el binario **no es "ilegal" ni "inseguro"**: es exactamente el mismo binario, solo que
  el usuario hace un click extra la primera vez. Así se distribuyen miles de plugins gratis / open
  source en macOS.

## El workaround de Gatekeeper (para el usuario final)

La primera vez, macOS puede avisar que el plugin "no se puede abrir porque no se puede verificar el
desarrollador". Dos formas de destrabar (cualquiera sirve):

**A) La fácil — desde el Finder:**

1. Click DERECHO sobre el plugin (`.vst3` o `.component`) en la carpeta de Plug-Ins.
2. Elegí **"Abrir"**.
3. Confirmá **"Abrir"** en el diálogo.

macOS lo recuerda y el DAW lo escanea normal de ahí en más.

**B) La de terminal — quitar la marca de cuarentena:**

```bash
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/HALO.vst3"
xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/HALO.component"
```

(Una línea por cada plugin instalado.)

Este texto va **dentro del .dmg** como `LÉEME PRIMERO.txt` (lo genera `packaging/make-dmg.sh`
automáticamente) y debería ir **también en la web** (página de descarga), con el mismo redactado.
Sugerencia para la web: un bloque "Primera apertura en macOS" justo debajo del botón de descarga,
con las dos opciones (A y B) y la frase honesta: *"el plugin es gratis y open source; sin firma de
Apple, macOS pide un click extra la primera vez — no es un problema del plugin"*.

## Piezas

| Archivo | Qué hace |
|---|---|
| `CMakePresets.json` (raíz) | preset `release-universal` (distribución) y `dev` (arm64-only, rápido). |
| `packaging/build-universal.sh` | configura+buildea universal y **verifica con `lipo -archs`** que cada artefacto traiga arm64 **y** x86_64. **No firma nada** → corre sin credenciales. |
| `packaging/make-dmg.sh` | **entregable gratis principal**: arma el `.dmg` con los `.vst3`/`.component` + alias a `/Library/Audio/Plug-Ins` + `LÉEME PRIMERO.txt`. Sin firma por default; firma opcional con `DEV_ID`. |
| `packaging/make-installer.sh` | **alternativa**: arma el `.pkg` (VST3 → `/Library/Audio/Plug-Ins/VST3`, AU → `…/Components`). Sin firma por default; firma opcional con `INSTALLER_SIGN_ID`. |
| `packaging/make-per-plugin.sh` | **entregable actual de la web**: `.pkg` POR PLUGIN + `.pkg` completo (con "Personalizar") + ZIPs de Windows por plugin + `SHA256SUMS.txt`, desde una carpeta plana de bundles. Los `.pkg` instalan con doble click y **sin cuarentena** (adiós `xattr`). Identificadores por plugin (`com.ovni.plugins.<id>.{vst3,au}`) y `BundleIsRelocatable=false`. |
| `.github/workflows/ci.yml` | CI: build + ctest + validate por plugin, archiva reportes. |
| `.github/workflows/release.yml` | Release: build universal → (firma opcional) → `.dmg` + `.pkg` → (notarización opcional) → publica. No falla sin secrets. |

## Probar local (sin credenciales — camino gratis)

```bash
# 1) Build universal + gate lipo (sin firmar nada):
packaging/build-universal.sh

# 2) Armar el .dmg GRATIS (entregable principal, sin firma):
packaging/make-dmg.sh --version 0.0.0-test --out /tmp/OVNI-test.dmg --plugins "aurora dust halo horizon nebula pulsar"
#   (sin --plugins mete todos los .vst3/.component que encuentre en el build)

# Inspeccionar el contenido del .dmg:
hdiutil attach /tmp/OVNI-test.dmg          # monta el volumen "OVNI Audio"
ls -la "/Volumes/OVNI Audio"               # ves los plugins + LÉEME PRIMERO.txt
hdiutil detach "/Volumes/OVNI Audio"

# 3) (Opcional) Armar el .pkg de alternativa, también sin firma:
packaging/make-installer.sh --version 0.0.0-test --out /tmp/OVNI-test.pkg
pkgutil --payload-files /tmp/OVNI-test.pkg   # ver qué instalaría
```

Iterar rápido en Apple Silicon (build ~2x, NO universal):

```bash
cmake --preset dev && cmake --build build
```

## Assets por plugin (así se armaron los de v0.1.1)

Los 15 assets por-plugin del release v0.1.1 (7 `.pkg` mac + `OVNI-v0.1.1.pkg` completo + 7 `.zip`
win + `SHA256SUMS.txt`) se **reempaquetaron desde los binarios EXACTOS ya shipeados** (DMG/ZIP del
release, checksum verificado antes de tocar nada) — bit-idénticos a lo que ya estaba QA'd:

```bash
# 1) extraer los bundles del DMG shipped a una carpeta plana:
hdiutil attach -nobrowse -readonly OVNI-v0.1.1.dmg
mkdir bundles && for b in "/Volumes/OVNI Audio"/*.vst3 "/Volumes/OVNI Audio"/*.component; do
  ditto "$b" "bundles/$(basename "$b")"; done
hdiutil detach "/Volumes/OVNI Audio"

# 2) armar TODO (pkgs individuales + completo + zips win + checksums):
packaging/make-per-plugin.sh --version 0.1.1 --bundles bundles --outdir out \
    --winzip OVNI-v0.1.1-Windows.zip

# 3) publicar:
gh release upload v0.1.1 out/*.pkg out/*.zip out/SHA256SUMS.txt
```

**Para el próximo release**: cuando se arregle `release.yml` (pendiente conocido: el catálogo real
de 7 se arma a mano porque ORBIT vive en `ovniaudio/orbita`), adoptar `make-per-plugin.sh` para los
`.pkg` en lugar de `make-installer.sh` — usa identificadores POR plugin, así los recibos de pkgutil
quedan coherentes entre "instalar uno" e "instalar todo". La web (`sello/web`) espera estos nombres
de asset: `OVNI-v<V>.pkg` · `OVNI-<NAME>-v<V>.pkg` · `OVNI-<NAME>-v<V>-Windows.zip`, resueltos por
el catch-all `/download/*` de `_redirects` (actualizar la versión ahí + `plugins.json` "version" +
el hero de `index.html` al releasear).

## Deployment target: 11.0 (decisión documentada)

El repo tenía dos números en conflicto: el `CMakeLists.txt` raíz usa **11.0** (y el `CMakeCache`
real confirma 11.0), pero `cmake/PamplejuceMacOS.cmake` declara **10.14** ("Mojave"). Ese
`cmake/` es vendoreado de referencia y **no lo usa el build raíz** (ver nota [S5] en el CMakeLists).

**Elegimos 11.0 (Big Sur)** y unificamos en `CMakePresets.json`:

- 11.0 es la **primera versión de macOS con soporte nativo arm64**. Un binario universal
  arm64+x86_64 no puede ejecutar su slice arm64 en macOS < 11.0. Declarar 10.14 sería prometer
  compatibilidad que el binario **no puede cumplir** en Apple Silicon — exactamente el tipo de
  claim falso que el sello no firma.
- Es lo que el build **ya producía** (no cambia el binario, sólo deja de mentir el header del
  Pamplejuce de referencia).

Si en el futuro se quiere bajar a 10.14/10.15 (cubrir Intel viejo), hay que: (a) verificar que JUCE
8.0.13 + libmysofa compilen contra ese SDK, y (b) confirmar que ningún slice arm64 herede ese
mínimo. Hasta entonces, **11.0 es la promesa honesta**.

## Firma LOCAL de la app SUPERNOVA (audio del sistema / TCC) — NO confundir con distribución

La app de escritorio captura audio vía ScreenCaptureKit y necesita el permiso de **Grabación de
pantalla**. macOS ata ese permiso a la firma: con firma **ad-hoc** el permiso muere en cada rebuild
(filas fantasma, deniega en silencio). El fix es una identidad **self-signed estable** — gratis,
sin Apple Developer:

```bash
./packaging/make-signing-cert.sh   # una vez por máquina: crea "SUPERNOVA Local" en el llavero
./packaging/deploy-app.sh          # cada deploy: copia + firma estable + lsregister
./packaging/run-app-logged.sh      # lanzar con diagnóstico [sysaudio] capturado (ruta estable)
```

Historia completa, reglas y troubleshooting: **`docs/AUDIO-TCC.md`**. El $99 de Apple Developer
sigue siendo SOLO para distribuir (sección siguiente); no aporta nada al audio local.

## Activar el camino PAGO (el día que se cargue el $99)

Cuando Joaquín pague la cuenta Apple Developer, el camino pago se activa **solo cargando los secrets**
(el código de `release.yml` ya está escrito para detectarlos). Pasos:

1. Pagar la cuenta **Apple Developer Program** (99 USD/año).
2. Generar dos certificados Developer ID desde
   [developer.apple.com/account/resources/certificates](https://developer.apple.com/account/resources/certificates):
   - **Developer ID Application** — firma los `.vst3`/`.component` (y el `.dmg`).
   - **Developer ID Installer** — firma el `.pkg`.
3. Exportar ambos de Keychain como `.p12` (con contraseña) y pasarlos a base64:

   ```bash
   base64 -i DeveloperID_Application.p12 | pbcopy   # → APPLE_DEV_ID_APP_CERT_P12_BASE64
   base64 -i DeveloperID_Installer.p12   | pbcopy   # → APPLE_DEV_ID_INSTALLER_CERT_P12_BASE64
   ```

4. Generar una **app-specific password** en
   [appleid.apple.com](https://appleid.apple.com) (Sign-In and Security → App-Specific Passwords) —
   NO la contraseña real del Apple ID.
5. Cargar los secrets en GitHub → *Settings → Secrets and variables → Actions* (tabla abajo).

Apenas exista `APPLE_DEVELOPER_ID_APP`, el próximo tag `v*` firmará + notarizará automáticamente y el
usuario dejará de ver el aviso de Gatekeeper. Para **volver al camino gratis**, basta con borrar (o
vaciar) `APPLE_DEVELOPER_ID_APP`.

### Secrets a cargar (solo camino PAGO)

| Secret | Qué es | Dónde se saca |
|---|---|---|
| `APPLE_DEVELOPER_ID_APP` | **interruptor**: identidad de firma de app, p.ej. `Developer ID Application: Joaquin Cerrano (TEAMID1234)` | `security find-identity -v -p codesigning` |
| `APPLE_DEVELOPER_ID_INSTALLER` | identidad del instalador, p.ej. `Developer ID Installer: Joaquin Cerrano (TEAMID1234)` | idem |
| `APPLE_DEV_ID_APP_CERT_P12_BASE64` | cert *Developer ID Application* (.p12) en base64 | Keychain → exportar → `base64 -i …` |
| `APPLE_DEV_ID_INSTALLER_CERT_P12_BASE64` | cert *Developer ID Installer* (.p12) en base64 | Keychain → exportar → `base64 -i …` |
| `APPLE_DEV_ID_CERT_PASSWORD` | contraseña con la que exportaste los `.p12` | la elegís vos al exportar |
| `APPLE_ID` | tu Apple ID (mail de la cuenta developer) | tu cuenta |
| `APPLE_TEAM_ID` | Team ID de 10 chars del Apple Developer Program | developer.apple.com → Membership |
| `APPLE_APP_PASSWORD` | app-specific password (NO la real) | appleid.apple.com |
| `KEYCHAIN_PASSWORD` | string aleatorio; sólo protege el keychain temporal del runner | inventalo (`openssl rand -base64 24`) |

### Disparar un release (cualquier camino)

```bash
git tag v0.1.0
git push origin v0.1.0     # arranca release.yml
```

(o desde la pestaña Actions → Release → *Run workflow*, pasando el tag a mano.)

## Verificar lo que descarga el usuario

El sello publica los comandos de auditoría, no sólo el binario.

**Camino gratis (sin firma):** confirmá que el binario es universal y que el aviso de Gatekeeper es
el único trámite:

```bash
# Cada plugin es universal (arm64 + x86_64):
lipo -archs "/Library/Audio/Plug-Ins/VST3/HALO.vst3/Contents/MacOS/HALO"   # → x86_64 arm64

# Sin firma: spctl lo rechaza con "unnotarized" → ese es el aviso esperado, no un bug:
spctl -a -vvv --type install /tmp/OVNI-test.dmg 2>&1 || true   # → "rejected" (sin notarizar)
```

**Camino pago (firmado + notarizado):**

```bash
# El .pkg/.dmg está firmado y notarizado:
spctl -a -vvv --type install OVNI-v0.1.0.dmg   # → "accepted, source=Notarized Developer ID"
pkgutil --check-signature OVNI-v0.1.0.pkg

# Tras instalar, cada plugin es universal y está firmado:
lipo -archs "/Library/Audio/Plug-Ins/VST3/HALO.vst3/Contents/MacOS/HALO"
codesign -dv --verbose=4 "/Library/Audio/Plug-Ins/VST3/HALO.vst3"
```
