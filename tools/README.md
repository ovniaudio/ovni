# tools/ — scripts determinísticos de la fábrica OVNI (0 tokens)

> El principio rector del orquestador: **el LLM solo piensa; todo lo mecánico es determinístico.** Estos scripts son ese "mecánico". El Workflow (`orchestrator/build-catalog.workflow.js`) los corre vía un runner mínimo y **parsea su JSON en JS** (no un agente). También corren standalone.

## `scaffold.sh <plugin> <CODE> <MANU> [DISPLAY] [--force]`

Crea `plugins/<plugin>/` desde `shared/template/`. Copia, reemplaza placeholders (sed/perl) y crea la tabla de presets vacía si falta.

- **Entrada:** `<plugin>` slug minúsculas (= `PROJECT_NAME` → target `<plugin>_All`), `<CODE>`/`<MANU>` 4 chars JUCE, `[DISPLAY]` PRODUCT_NAME (default = uppercase del slug).
- **Salida (stdout):** `{"tool":"scaffold","ok":true,"plugin","code","manu","display","dest","presetTableCreated"}`. Progreso por stderr.
- **Placeholders del CONTRATO:** `@PLUGIN_NAME@`, `@PLUGIN_CODE@`, `@MANUFACTURER_CODE@`. **Defensivos (si el template los expone):** `@PRODUCT_NAME@`, `@BUNDLE_ID@`.

## `validate.sh <plugin> [CODE] [MANU]`

Las **puertas duras** de la rúbrica, en orden fail-fast: build `<plugin>_All` → universal (`lipo` arm64+x86_64) → pluginval lvl8 (VST3+AU) → auval (`aufx CODE MANU`, autodetecta del CMakeLists si no se pasan) → tests (`ctest`) → anti-clip (`gain-staging-check.sh`).

- **Salida (stdout):** el `validate-report` (ver `orchestrator/schemas/validate-report.json`). `ok` = AND de todas las puertas. `stage` = primera que falló. `errors` = lo exacto que recibe el fix.
- **Exit:** 0 si `ok`, 1 si no. **Nunca aborta a mitad**: siempre emite un reporte parseable.
- **Env:** `BUILD_DIR` (default `build`), `CONFIG` (`Release`), `CEILING` (`0.85`), `TEST_FILTER` (regex ctest), `PLUGINVAL` (ruta a un binario ya instalado; si no, lo descarga de Tracktion y lo cachea en `$BUILD_DIR`).
- Emite el JSON vía `_emit_report.py` (escaping correcto de logs/errores).

## `gain-staging-check.sh <plugin> [--ceiling 0.85] [--build-dir build]`

El guard anti-clip: corre el test `[gain]` del plugin (full-scale → `PEAK=<peak lineal>`) y lo grepea.

- **Salida (stdout):** `{"tool":"gain","plugin","peak","ceiling","clip","method","ok"}`.
- **Exit:** 0 si `peak ≤ ceiling`, 1 si clip, 2 si no se pudo medir.

## Contrato con el template (S3) y el build (S5) — lo que estos scripts ASUMEN

Para que `validate.sh`/`gain-staging-check.sh` midan de verdad (en la sesión main, post-S5), el template + el build deben proveer:

1. **Target CMake `<plugin>_All`** (Pamplejuce/JUCE lo crea con `juce_add_plugin`), artefactos en `build/<Proj>_artefacts/<CONFIG>/{VST3,AU,Standalone}/`. ✅ convención estándar de ÓRBITA.
2. **Códigos en el `CMakeLists.txt` del plugin** como `set(... "Plsr")` (4 chars entre comillas) para que `validate.sh` autodetecte CODE/MANU para `auval`.
3. **Un test Catch2 etiquetado `[gain]`** (por plugin, o en el template base) que pase una señal **full-scale** por el plugin ensamblado e imprima exactamente `PEAK=<peak lineal>` por stdout. Sin él, `gain-staging-check.sh` reporta `method:"none"` y la puerta anti-clip queda sin confirmar (no pasa). → **dependencia anotada en `docs/CONTRACT-CHANGES.md` [S4]**.
4. **`ctest`** descubre los tests (`catch_discover_tests`, como ÓRBITA).

Mientras S1–S3 escriben el C++ en paralelo, los scripts degradan elegante: sin `build/` reportan la puerta `build` en rojo con el motivo, sin crashear.
