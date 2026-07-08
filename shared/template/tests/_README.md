# Batería de tests parametrizada del sello (`shared/template/tests/`)

La **medición uniforme** del sello. En vez de copiar-pegar el mismo `AliasTest`/`LatencyTest`/`BenchTest`/…
en cada plugin (que es como nació HALO y como diverge la verdad), acá vive **una sola** implementación de
cada medición. Cada plugin la engancha con 3 líneas y los **3 números públicos** del
[Manifiesto #3](../../../MANIFIESTO.md) (alias floor · latencia · CPU%) salen medidos **igual** para todos.

> Honestidad verificable: la medición es la misma para HALO, NÉBULA, PULSAR y cualquier plugin futuro. Un
> número no se puede "inflar" en un plugin y no en otro porque el código que lo produce es compartido.

---

## Qué hay acá

| Archivo | Tag | Qué mide | Token que emite (lo grepea `tools/measure-check.sh`) |
|---|---|---|---|
| `OvniTestHarness.h` | — | Primitivas: generadores (rosa/blanco/transientes), medidor de imagen estéreo, helpers. **No es un test.** | — |
| `MeasureStub.h` | `[measure]` | Imagen estéreo + IACC (ruido rosa por el processor real) | `IACC=` |
| `AliasStub.h` | `[alias]` | Piso de espurias de un plugin **lineal** (seno puro → energía fuera de la fundamental) | `ALIAS_DBFS=` |
| `LatencyStub.h` | `[latency]` | Latencia reportada == real (impulso por el dry, MIX=0) | `LATENCY_REPORTED=` / `LATENCY_REAL=` |
| `BenchStub.h` | `[bench]` | CPU% de una instancia (10.000 bloques 48k/512) | `CPU_PCT=` |
| `TransientStub.h` | `[transient]` | True-peak inter-sample (proxy 4× de `shared/dsp/TruePeak.h`) + maxJump/boundary | — (gate de true-peak) |
| `NullStub.h` | `[null]` | Bypass **bit-exact** (pass-through puro) | — |
| `BufferSizeStub.h` | `[buffersize]` | Invarianza al block-size (64 vs 512 → misma salida) | — |
| `StateRecallStub.h` | `[staterecall]` | `getState`→`setState` → misma salida | — |

⚠ **`AliasStub.h` es para plugins LINEALES** (reverb/delay FDN, movimiento binaural HRIR): no tienen etapa
de pitch/no-lineal, así que NO se les puede medir un "alias de pitch". Mide el piso de espurias real
(house-standard §1: "reverb/delay lineal puro → 0× OS → no genera armónicos"). Si tu plugin **sí** tiene
una etapa de pitch o no-lineal (granular, saturación, STFT), **no uses este stub**: escribí un `AliasTest`
específico que aísle esa etapa con su oversampling, como hace `plugins/halo/tests/AliasTest.cpp`.

---

## Cómo lo engancha un plugin nuevo

Por cada medición que quieras, creá **un `.cpp` chiquito** en `plugins/<slug>/tests/` que define el contrato
y `#include` el stub. Ejemplo, `plugins/<slug>/tests/AliasTest.cpp`:

```cpp
#define OVNI_PLUGIN_PROCESSOR  myns::MyProcessor   // tu tipo concreto (deriva de ovni::PluginProcessorBase)
#define OVNI_PLUGIN_SLUG       "myslug"            // slug en minúsculas (para los tags + las líneas impresas)
#define OVNI_PLUGIN_TAG        "[myslug]"          // tag Catch2 del plugin (measure-check.sh filtra por él)
#include "PluginProcessor.h"                       // tu processor
#include "template/tests/AliasStub.h"              // genera el TEST_CASE [alias][myslug]
```

Eso es todo: el stub emite el `TEST_CASE` tagueado `[alias][myslug]` que imprime `ALIAS_DBFS=`.

### Personalizar el peor caso (opcional)

Cada stub setea por defecto `mix=1` (wet pleno). Si tu plugin necesita otro peor caso, definí la macro de
setup **antes** del `#include`:

```cpp
#define OVNI_PLUGIN_PROCESSOR pulsar::PulsarProcessor
#define OVNI_PLUGIN_SLUG      "pulsar"
#define OVNI_PLUGIN_TAG       "[pulsar]"
#include "PluginProcessor.h"
// peor caso de PULSAR: binaural denso + cola larga
#define OVNI_ALIAS_SETUP(proc) do { \
    ovni::test::setParam ((proc), "mix",    1.0f); \
    ovni::test::setParam ((proc), "motion", 1.0f); \
    ovni::test::setParam ((proc), "width",  1.0f); } while (0)
#include "template/tests/AliasStub.h"
```

Macros de personalización por stub (todas opcionales, con default sensato):

| Stub | Macro | Default | Para qué |
|---|---|---|---|
| Measure | `OVNI_MEASURE_SETUP(proc)` | `mix=1` | abrir la imagen (width/motion al máximo) |
| Alias | `OVNI_ALIAS_SETUP(proc)` | `mix=1` | peor caso de espurias |
| Latency | `OVNI_LATENCY_MIX_ID` | `"mix"` | id del dry/wet si no es "mix" |
| Bench | `OVNI_BENCH_SETUP(proc)` | defaults | preset a cronometrar |
| Transient | `OVNI_TRANSIENT_SETUP(proc)` | `mix=1` | peor caso de crackle/true-peak |
| Transient | `OVNI_TRANSIENT_TP_CEIL` | `0.97f` | techo true-peak-safe del gate |
| Transient | `OVNI_TRANSIENT_REQUIRE_NO_BOUNDARY` | (off) | endurecer el boundary-check (sólo diseños wet-only) |
| Null | `OVNI_BYPASS_ID` | `"bypass"` | id del bool de bypass |
| BufferSize | `OVNI_BUFFERSIZE_SETUP(proc)` | `mix=1` | preset determinístico |
| BufferSize | `OVNI_BUFFERSIZE_EPS` | `2e-3f` | tolerancia 64-vs-512 |
| StateRecall | `OVNI_STATERECALL_EPS` | `1e-5f` | tolerancia del round-trip |

### Cablear el CMake

El runner del plugin (`Ovni<Cap>Tests`) tiene que compilar:
1. tu `.cpp` de cada test,
2. las **bases del template** que esos stubs necesitan (`PluginProcessorBase.cpp`, `PluginEditorBase.cpp`),
3. tu processor ensamblado (processor/editor/motor + tu `FactoryPresets.cpp`).

En `tests/CMakeLists.txt` se usa la función helper `ovni_add_plugin_tests(<slug> ...)` (ver ese archivo):
globea `plugins/<slug>/tests/*.cpp`, agrega las bases y registra el exe en CTest. Los stubs viven en
`shared/template/tests/` y se incluyen por `#include "template/tests/<Stub>.h"` → `shared/` ya está en los
`target_include_directories` de todos los runners, así que no hay que agregar include dirs nuevos.

---

## Por qué stubs-en-header y no una librería de tests

Cada plugin define **su propio** `createPluginFilter()`/`factoryPresets()` → no pueden convivir dos
processors en el mismo binario (símbolo duplicado). Por eso cada plugin tiene su **exe separado**
(`OvniPulsarTests`, `OvniNebulaTests`, …). El stub-en-header inyecta el `TEST_CASE` en el exe del plugin con
SU processor, sin librería intermedia ni símbolos compartidos conflictivos. La lógica de medición es
compartida (header, una sola copia); el `TEST_CASE` materializado es por-plugin (tag propio, processor
propio).
