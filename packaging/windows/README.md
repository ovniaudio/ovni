# Instalador de Windows (Inno Setup) — gratis, pocos clicks

> Esta carpeta tiene dos cosas: el instalador Inno de ORBIT (lo que sigue) y el **build de Windows de
> TELESCOPE** (sección [TELESCOPE — VST3 x64 desde el monorepo privado](#telescope--vst3-x64-desde-el-monorepo-privado), al final).

**Qué es:** la experiencia del pkg de Mac pero en Windows — `OVNI-ORBIT-v<VER>-Setup.exe`:
doble clic → **Install** → listo. Copia `ORBIT.vst3` a `C:\Program Files\Common Files\VST3`
y deja desinstalador en "Agregar o quitar programas". Reemplaza el flujo manual del ZIP
(desbloquear → extraer → copiar a mano).

**Costo: $0.** Inno Setup es gratis/open source; GitHub Actions es gratis en repos
públicos (los runners `windows-latest` ya traen `iscc` preinstalado). Lo único pago del
mundo Windows sería un certificado Authenticode para silenciar SmartScreen — **NO hace
falta**: sin firma, SmartScreen avisa una vez ("Más información → Ejecutar de todos
modos"), mismo esquema gratis que veníamos usando en macOS sin notarizar.

## Archivos

- `OVNI-ORBIT.iss` — script del instalador (parametrizado: `/DAppVersion` y `/DBundleDir`).
  `AppId` fijo `{8E7A2F60-…}` para que los upgrades pisen la misma entrada de desinstalación.
- `ci-job.yml` — job listo para pegar en el `release.yml` del repo público en la ola 2:
  extrae el bundle del zip de Windows, compila el Setup, hace **smoke test real**
  (instalación `/VERYSILENT` en el runner + verificación de que el .vst3 aterrizó) y sube
  el artefacto.

## Compilar a mano (si hay una Windows a mano)

```bat
iscc /DAppVersion=0.2.0 /DBundleDir=stage packaging\windows\OVNI-ORBIT.iss
:: con stage\ORBIT.vst3\ = bundle extraído del OVNI-ORBIT-v0.2.0-Windows.zip
```

## Pendientes al integrarlo (ola 2)

1. Pegar el job de `ci-job.yml` en `release.yml` (ajustar `needs`/nombre del artefacto del zip).
2. Subir el Setup.exe como asset del release y sumar el botón "ORBIT — Windows ↓ (installer)"
   en la web (`build-plugins.py` `win_btn` + copy `dl.install` de Windows: cambiar
   "desbloqueá el ZIP…" por la nota de SmartScreen). El ZIP puede quedar como alternativa.
3. Cuando salgan los otros 6 plugins de audio en Windows: generalizar el .iss (loop de
   bundles o un .iss por plugin — mismo patrón).
4. (Opcional, si SmartScreen molesta a escala) firma Authenticode: Azure Trusted Signing
   (~US$10/mes) o cert clásico — decisión de negocio, nunca requisito.

---

## TELESCOPE — VST3 x64 desde el monorepo privado

**Estado (15-sep):** el job corrió en el monorepo privado. El primer run (`35022704583`) cayó en el build
por orden de targets (ver "Lo que no se probó acá", abajo) y el segundo (`35023674977`) salió verde: MSVC
compila, PE x64, versión 0.1.0, pluginval strictness 10. Su artefacto es el `OVNI-TELESCOPE-v0.1.0-Windows.zip`
del release `telescope-v0.1.0`, y en este repo el mismo job corre como `windows-telescope` en `ci.yml`.
Lo que sigue se escribió antes del primer run y queda como registro; lo que cambió está marcado.

### Archivos

- `.github/workflows/telescope-windows.yml` — el workflow del privado, `TELESCOPE · Windows VST3`.
- `telescope-ci-job.yml` — el MISMO job, para pegar en `jobs:` del `ci.yml` del repo público después
  del snapshot. Los dos bloques tienen que ser iguales; esto tiene que salir vacío:

  ```bash
  diff <(sed -n '/^  windows-telescope:/,$p' .github/workflows/telescope-windows.yml) <(sed -n '/^  windows-telescope:/,$p' packaging/windows/telescope-ci-job.yml)
  ```

El job (`windows-2022`, tope de 60 min, 9 pasos): checkout → caché de `build/_deps` → configure con
Visual Studio 2022 x64, `OVNI_BUILD_TESTS=OFF` y `OVNI_WITH_SOFA=OFF` → build de `telescope_VST3` →
PE x64 (COFF `0x8664`) → todos los campos `Version` de `moduleinfo.json` = `plugins/telescope/VERSION`
(se leen como texto: el SDK de VST3 escribe ese archivo con comas finales y no es JSON estricto) →
pluginval v1.0.4, strictness 10 → sube el bundle.

### Cómo se dispara

- **Push a `feat/telescope`** (lo hace la auditora con GO de Joaquín). El `ci.yml` de macOS no corre:
  sólo mira `main`.
- **Otra vez, sin commit nuevo:** `gh run rerun <id> -R joaquincerrano/ovni`, o `workflow_dispatch`:
  `gh workflow run telescope-windows.yml --ref feat/telescope -R joaquincerrano/ovni`.
- **Ruido que no es este job:** cada push al privado dispara también `.github/workflows/release.yml`,
  que falla en 0 s desde antes ("This run likely failed because of a workflow file issue", p. ej. el
  run 34368329882 del 9-sep). Ese rojo es viejo.
- Los minutos de Windows de un repo privado salen del plan de la cuenta (el job de ORBIT tardó
  5 min 32 s en el repo público). Cada push a la rama es un run: la concurrencia cancela el anterior.

### Dónde queda el artefacto

En el run, *Artifacts* → **`TELESCOPE-windows-vst3`** (30 días):

```bash
gh run download <id> -R joaquincerrano/ovni -n TELESCOPE-windows-vst3 -D ~/Downloads/telescope-win-art
```

`upload-artifact` sube el CONTENIDO del bundle: descomprimido queda `Contents/x86_64-win/TELESCOPE.vst3`
(el PE) + `Contents/Resources/moduleinfo.json`, sin la carpeta `TELESCOPE.vst3/` de afuera.

### El ZIP: `orbit-winzip.sh` corre, pero hoy arma un ZIP de ORBIT

> **Superado (15-sep):** el ZIP de 0.1.0 se armó con una copia parametrizada del script para TELESCOPE
> (nombre del ZIP, `SOURCE.txt` con el commit del snapshot público y el tag, y un `LEEME PRIMERO.txt` propio,
> sin la §3b de ORBIT). La tabla de abajo queda como registro de por qué hizo falta.

```bash
mision-control/scripts/auditora/orbit-winzip/orbit-winzip.sh --artifact ~/Downloads/telescope-win-art --repo <repo> --outdir <dir> --version 0.1.0 --name TELESCOPE --mac-vst3 dist/telescope-v0.1.0/bundles/TELESCOPE.vst3
```

Probado el 15-sep contra un artefacto SIMULADO (un PE32+ x86-64 de mentira + el `moduleinfo.json` real
del bundle de macOS): exit 0, pero lo que emite no se puede publicar como TELESCOPE. El script no se tocó.

| Qué | Lo que emite hoy | Lo que hace falta |
|---|---|---|
| Nombre del ZIP y de la carpeta (`orbit-winzip.sh:48`, fijo) | `OVNI-ORBIT-v0.1.0-Windows.zip` | `OVNI-TELESCOPE-v0.1.0-Windows.zip` |
| Texto de `SOURCE.txt` (`:56-78`, fijo) | "OVNI ORBIT — Código fuente", "ORBIT es software libre", `https://github.com/ovniaudio/orbita/tree/v0.1.0` | TELESCOPE y `https://github.com/ovniaudio/ovni/tree/telescope-v0.1.0` |
| `Commit:` de `SOURCE.txt` (`:54`) | `git rev-parse HEAD` de `--repo`: con el worktree privado, un hash que no existe en el repo público | `--repo` = el clon del snapshot público, parado en el commit del tag |
| `LEEME PRIMERO.txt` (`LEEME-PRIMERO.template.txt:7-10`, `:39-49`, `:81-91`) | "Este paquete trae ORBIT" + la sección 3b del renombre de ORBIT 0.2.x | una plantilla de TELESCOPE, sin 3b |
| `LICENSE` + `NOTICE.md` en la raíz de `--repo` (`:24`) | ✓ los dos existen en la raíz del monorepo | — |
| `VERSION` en la raíz de `--repo` (`:23`) | no existe en el monorepo | pasar `--version 0.1.0` ✓ |
| Nombre del bundle (`:31-33`) | `TELESCOPE`, sin espacio: coincide con `--name TELESCOPE` ✓ | — |
| PE x64 (`:35-37`), `moduleinfo.json` (`:38`), CID contra macOS (`:39-44`) | ✓ con el layout del artefacto; los CID de macOS son `ABCDEF019182FAEB4F766E69546C7363` y `ABCDEF011234ABCD4F766E69546C7363` | — |

### Las guardas de CMake

El configure de CMake recorre todo el monorepo (el glob de `plugins/*`), así que en Windows se
configuran también SUPERNOVA y los otros seis aunque sólo se compile `telescope_VST3`:

- **`OVNI_COPY_PLUGIN_AFTER_BUILD`** (opción del `CMakeLists.txt` raíz): ON en macOS y OFF en el resto.
  Los 10 `COPY_PLUGIN_AFTER_BUILD` (8 plugins, `_probe` y `shared/template/CMakeLists.txt.in`) la usan.
  No es una genex a propósito: JUCE lo lee con `if()` al configurar, y `$<IF:$<PLATFORM_ID:Darwin>,TRUE,FALSE>`
  es un string no vacío, verdadero en todas las plataformas. `-DOVNI_COPY_PLUGIN_AFTER_BUILD=OFF` también
  vale en macOS: compila sin escribir en `~/Library`.
- **SUPERNOVA** saca los `.mm` de su glob fuera de macOS (87 archivos → 79, 0 `.mm`). El resto de lo
  macOS-only ya estaba bajo `if(APPLE)`: `ovni_syphon`, el backend Metal, `app/`, y los exes de SUPERNOVA en `tests/`.
- **TELESCOPE compila con `/utf-8` en MSVC** (`plugins/telescope/CMakeLists.txt`, bajo `if (MSVC)`). Tiene ~196
  literales con UTF-8 crudo (`Strings.h`, `Verdict.cpp`, `FactoryPresets.cpp`). Sin la bandera, MSVC los lee y los
  reescribe en la página de códigos del runner (1252): los bytes sobreviven por casualidad de esa página, no por diseño.
- **Verificado en macOS** (sólo configure, sin build): `build.ninja` y `rules.ninja` salen idénticos
  byte a byte a los de `324ddb6`, o sea que el build de macOS no cambia. Con las opciones del runner
  (tests, SOFA y copia en OFF, JUCE por FetchContent) configura con exit 0, `telescope_VST3` existe y
  no queda ningún paso de copia.

### Lo que no se probó acá

> **Superado en parte (15-sep):** el primer run (`35022704583`) no compiló: MSBuild compilaba `Fonts.cpp`
> antes de que se generara `OvniUikitData.h` (lo arregla `add_dependencies(ovni_uikit ovni_uikit_assets)` en
> `shared/ui-kit/CMakeLists.txt`). El segundo (`35023674977`) pasó MSVC y pluginval strictness 10 con los tests
> de GUI. Lo que sigue sin probarse es un DAW de Windows.

- **MSVC.** No hay Windows en esta Mac: la prueba real es el primer run.
- **pluginval 10 con GUI.** En macOS, TELESCOPE pasó strictness 10 **sin** los tests de GUI
  (pluginval 1.0.4, sobre una copia del bundle firmado, 15-sep). En Windows el job corre con GUI.
- **Un DAW de Windows.** Ni el job ni pluginval lo reemplazan.
