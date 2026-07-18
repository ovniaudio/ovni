# Re-bake del HRIR de producción — SADIE II KU100

Receta de punta a punta para hornear los headers HRIR del sello (`HrirData.h` fijo +
`HrirRing.h` de movimiento) desde el dataset de **producción**: **SADIE II — KU100**
(maniquí Neumann KU100, Univ. de York, **Apache-2.0**, cita DOI `10.3390/app8112029`).

> **Estado real (hoy):** los headers horneados que viven en el repo **todavía derivan
> del fixture CIPIC subject_003** (un SOFA de **test** de libmysofa, humano asimétrico,
> licencia gris para uso comercial). El re-bake a SADIE II KU100 descrito acá es un
> **paso de producción PENDIENTE**, a ejecutar **offline** (requiere bajar el SOFA de
> York; probablemente no haya red en la sesión que lee esto). Mientras no se ejecute,
> lo horneado se atribuye como derivado de CIPIC (ver `NOTICE.md`,
> `shared/legal/Attributions.h`, `skill/references/legal.md`).
>
> **Importante:** **HALO NO usa HRIR** (es un pan binaural por ITD/ILD, sin convolución
> HRTF). El HRIR sólo lo consume **PULSAR** y futuros plugins HRTF. El re-bake **no**
> afecta a HALO.

---

## 0. ¿Existe un generador real?

**Sí.** El generador real vive en el repo hermano **ÓRBITA**:
`/Users/musik/PLUGINS/orbita/tests/GenHrir.cpp`. No hay (todavía) una copia propia en
`ovni/tools/` — este documento es la receta para usar el de ÓRBITA y portar la salida a
las 4 ubicaciones de ÓVNI. (Verificado con `find /Users/musik/PLUGINS -iname 'GenHrir*'`.)

`GenHrir.cpp` son **dos TEST_CASE de Catch2** que se corren como parte del target `Tests`:

| Tag | Genera | Salida (en ÓRBITA) |
|---|---|---|
| `[gen]`     | HRIR **fijo** (M1, dirección 90° izquierda) | `source/dsp/HrirData.h` |
| `[genring]` | **anillo** de movimiento (M2, 72 azimuts a elevación 0) | `source/dsp/HrirRing.h` |

El SOFA de entrada y el directorio de salida llegan por **define de CMake**
(`/Users/musik/PLUGINS/orbita/CMakeLists.txt`, ~líneas 182-184):

```cmake
target_compile_definitions(Tests PRIVATE
    HRTF_SOFA_PATH="${libmysofa_SOURCE_DIR}/tests/CIPIC_subject_003_hrir_final_itdInDelayField.sofa"
    ORBITA_REPO_DIR="${CMAKE_SOURCE_DIR}")
```

Para el re-bake de producción hay que **apuntar `HRTF_SOFA_PATH` al SOFA de SADIE II
KU100** (paso b) en vez del fixture CIPIC.

---

## a. Bajar el SOFA de SADIE II — KU100 (48 kHz)

1. Ir a la base de datos SADIE II: <https://www.york.ac.uk/sadie-project/database.html>
2. Bajar el **KU100** (no un sujeto humano), en **formato SOFA / AES69**, **48 kHz**.
   - Es el maniquí Neumann KU100 → simétrico L/R por construcción (ventaja sobre CIPIC).
   - Licencia **Apache-2.0**: redistribución comercial OK con atribución; conservar el
     aviso de copyright "Copyright 2018, University of York".
3. Guardarlo en una ruta estable, p. ej. `~/PLUGINS/_datasets/SADIE_KU100_48k.sofa`
   (fuera de los repos; los `.sofa` no se commitean).

> **Nota de simetría:** SADIE KU100 ya es simétrico, así que el paso de **simetrización
> L/R** que hace `[genring]` (promediar cada dirección con su espejo) deja de ser
> estrictamente necesario. Es **inocuo** dejarlo (promediar dos canales casi idénticos no
> cambia nada), pero si se quiere fidelidad máxima se puede saltear ese bloque. Documentar
> la decisión en el comentario del header generado.

## b. Cargarlo con libmysofa

El generador ya usa libmysofa (`mysofa_open` / `mysofa_getfilter_float`) vía la API "easy",
que resamplea al sample rate pedido (48000), normaliza y arma el KD-tree de direcciones.
Sólo hay que cambiar de dónde lee:

- **Opción simple (recomendada):** editar el define en `orbita/CMakeLists.txt` para que
  `HRTF_SOFA_PATH` apunte al SOFA de SADIE KU100 bajado en el paso (a), reconfigurar CMake
  y recompilar el target `Tests`. **OJO:** `CMakeLists.txt` raíz está bajo otra
  responsabilidad — coordinar antes de tocarlo, o usar la opción de override por línea de
  comando si el build lo permite.
- La convención cartesiana de mysofa que usa el generador: `x` = frente, `y` = izquierda,
  `z` = arriba. `(0,1,0)` = 90° a la izquierda (lo que hornea `[gen]`).

## c. Muestrear el anillo de azimuts que usa `HrirRing`

Los números **reales** que esperan los headers de ÓVNI (verificados en
`shared/data/HrirRing.h` y `shared/engines/binaural/HrirRing.h`):

| Constante | Valor | Significado |
|---|---|---|
| `kNumDirs`       | **72**     | direcciones del anillo (azimut, elevación 0) |
| `kRingStepDeg`   | **5.0°**   | paso = 360 / 72 |
| `kRingTaps`      | **218**    | taps por IR (= `len` que devuelve mysofa al SR pedido) |
| `kRingSampleRate`| **48000**  | Hz |
| `kRingL` / `kRingR` | `std::array<float, 15696>` | plano: índice `d*kRingTaps + t` (72 × 218 = 15696) |
| `kRingDelayL` / `kRingDelayR` | `std::array<float, 72>` | ITD por dirección (samples), fuera de la IR |

Y para el HRIR **fijo** (`HrirData.h`):

| Constante | Valor |
|---|---|
| `kHrirLength`      | **218** taps |
| `kHrirSampleRate`  | **48000** Hz |
| `kHrirL` / `kHrirR`| `std::array<float, 218>` |
| `kHrirDelayL` / `kHrirDelayR` | ITD (samples) |

> **`kRingTaps`/`kHrirLength` dependen del SOFA:** `len` lo decide `mysofa_open` para el SR
> pedido. Con el fixture CIPIC dio **218**. Con SADIE KU100 podría dar **otro valor** → los
> `std::array<float, N>` se re-emiten con el N nuevo automáticamente (el generador escribe
> `v.size()`), pero **cualquier código que asuma 218 a mano debe revisarse**. Confirmar el
> `len` real que reporta el `WARN` del generador tras el re-bake.

El generador del anillo (`[genring]`) hace, además del muestreo, este pipeline **offline**
(costo runtime cero) — replicarlo tal cual:

1. Para cada `d` en `0..71`: `θ = d*5°`, `(cosθ, sinθ, 0)` → `mysofa_getfilter_float` →
   IR L/R + delays.
2. **Simetrización L/R** (promediar con la dirección espejo `-θ`). Ver nota de simetría en
   el paso (a) — con KU100 es casi un no-op.
3. **Ecualización de campo difuso (DFE) parcial + band-limitada** + reconstrucción a
   **fase mínima** (cepstrum real): `alpha=0.6` por debajo de 3 kHz, taper 3→6 kHz,
   `alpha=0` arriba de 6 kHz (deja intactos los notches del pabellón = el cue direccional).
   Banda de referencia mid-band 300-6000 Hz. Es **cut-only**: hay un `REQUIRE` que verifica
   que ninguna banda se recaliente (`newMaxMag <= rawMaxMag*1.05`).
4. **Normalización de energía L2 por dirección** al mismo objetivo (`kTargetE = 0.70`),
   escalando ambos oídos por el MISMO factor → el ILD queda intacto, sólo se iguala la
   loudness por azimut (para una órbita a radio constante todas suenan igual de fuerte).

## d. Emitir `HrirData.h` / `HrirRing.h` con el formato actual

El generador escribe con `std::showpoint << std::setprecision(9)` (literales `float`
válidos, sufijo `f`), 6 valores por línea para los anillos/IRs y 8 por línea para los
delays. **Mantener ese formato byte-compatible.**

**Diferencia clave entre ÓRBITA y ÓVNI** (no olvidar al portar):

| | ÓRBITA (generador) | ÓVNI (destino) |
|---|---|---|
| Namespace | `orbita` | **`ovni::data`** |
| Ruta | `source/dsp/Hrir*.h` | **`shared/data/Hrir*.h` _y_ `shared/engines/binaural/Hrir*.h`** (2 copias de cada uno → **4 archivos**) |

Pasos:

1. Correr en ÓRBITA, ya con `HRTF_SOFA_PATH` → SADIE KU100:
   ```sh
   # desde el build de orbita, target Tests compilado
   ./Tests "[gen]"      # regenera HrirData.h
   ./Tests "[genring]"  # regenera HrirRing.h
   ```
2. Tomar los dos headers generados en `orbita/source/dsp/`.
3. **Portar a ÓVNI**: cambiar el namespace `orbita` → `ovni::data` y copiar cada header a
   sus **dos** ubicaciones de ÓVNI (`shared/data/` y `shared/engines/binaural/`). Mantener
   las 4 copias idénticas salvo el comentario de cabecera.
4. **Actualizar el comentario de cabecera generado** para que diga **SADIE II KU100
   (Apache-2.0, DOI 10.3390/app8112029)** en vez de CIPIC, y dejar la cita firme.
5. **Actualizar atribuciones** si hiciera falta: `NOTICE.md` y
   `shared/legal/Attributions.h` ya listan SADIE II KU100 como dataset de producción —
   cambiar el "estado HOY" para reflejar que ya está horneado SADIE (y NO seguir diciendo
   que lo horneado es CIPIC).

> Mejora opcional (YAGNI hasta que duela): portar `GenHrir.cpp` a `ovni/tools/` con
> namespace `ovni::data`, salida directa a las 4 rutas y `HRTF_SOFA_PATH` propio, para no
> depender de ÓRBITA ni del paso manual de port. Sólo vale la pena si el re-bake se va a
> repetir seguido.

---

## Checklist del re-bake (offline)

- [ ] SADIE II KU100 SOFA 48k bajado (Apache-2.0), ruta estable fuera de los repos.
- [ ] `HRTF_SOFA_PATH` apunta a ese SOFA (coordinar el cambio en `CMakeLists.txt`).
- [ ] `Tests` recompila; `[gen]` y `[genring]` corren sin fallar (mirar el `REQUIRE`
      cut-only y los `WARN` con `len`, delays y `safeGain`).
- [ ] `len`/taps real anotado (puede diferir de 218).
- [ ] Headers portados a las **4** rutas de ÓVNI con namespace `ovni::data`.
- [ ] Comentario de cabecera + atribuciones actualizados a SADIE II KU100.
- [ ] `NOTICE.md` / `Attributions.h` / `legal.md`: "estado HOY" pasa de "fixture CIPIC" a
      "SADIE II KU100 horneado".
- [ ] PULSAR (y cualquier plugin HRTF) re-validado de oído con el HRIR nuevo.
