# TELESCOPE 🔭 — ficha técnica / technical data sheet

> Cada número de esta ficha lo produce un test del repositorio contra una señal sintética; la columna
> "test" dice cuál. Lo que no se mide, no figura.
>
> Every number here is produced by a test in the repository against a synthetic signal; the "test" column
> names it. What is not measured is not listed.

*Fuente de la versión: [`../VERSION`](../VERSION). El test `telescope-version` la compara contra los tres
bundles construidos y contra este archivo.*

---

# ES · castellano

## Identidad

| | |
|---|---|
| Nombre | **TELESCOPE** |
| Versión | **0.1.0** |
| Tipo | Analizador de audio (no procesa) |
| Fabricante | OVNI Audio |
| Códigos AU | `aufx` · plugin `Tlsc` · fabricante `Ovni` |
| Categoría VST3 | `Fx｜Analyzer` |
| Formatos macOS | **VST3 + AU**, binario universal `arm64 + x86_64`, mínimo **macOS 11.0** |
| Formato Windows | **VST3 x64** — Windows 10+, ZIP sin firma (`OVNI-TELESCOPE-v0.1.0-Windows.zip` en el release; SmartScreen puede avisar) |
| Standalone | se construye (`telescope_Standalone`, shell genérico de JUCE) pero **el instalador 0.1.0 no lo distribuye**: instala VST3 + AU. TELESCOPE mide lo que pasa por la cadena del DAW; el standalone sólo escucha la entrada de audio que le elijas |
| Canales | entrada mono o estéreo → **salida estéreo** (el DAW instancia mono→estéreo) |
| Licencia | **AGPLv3** · fuente: <https://github.com/ovniaudio/ovni> |

## Impacto en el audio

| Magnitud | Valor | Test |
|---|---|---|
| Latencia declarada y real | **0 muestras** | `[telescope][null]` |
| Cola (tail) | **0** | `[telescope][null]` |
| Pass-through | **bit-exacto**: la salida es la entrada, muestra a muestra, comparada con `==` | `[telescope][null]` — bypass on/off, las 13 lentes, bloques de 1 / 7 / 64 / 4096, fuente mono: `NULL_MISMATCHES=0` en los 5 casos |
| Pico con entrada a escala completa | **1.000000** (identidad, 0 diferencias) | `[gain][telescope]` |

El chasis compartido del sello (`PluginProcessorBase`) inyecta `inGain`, `output` y `monoSafe` en **todo**
plugin del catálogo. TELESCOPE no los expone en su UI porque no procesa, pero el host **sí los va a
listar**. Con los tres en default (0 dB, 0 dB, off) el audio es bit-exacto — es lo que verifica el test de
arriba. Limitación conocida del chasis.

## Qué mide cada lente

| Lente | Magnitud | Norma o definición | Test |
|---|---|---|---|
| **LOUDNESS** | Integrated / Short-term / Momentary (LUFS) | ITU-R BS.1770, doble compuerta (absoluta −70 LUFS, relativa −10 LU) | `[telescope][ebu]` — Tech 3341 tests 1-5 |
| | LRA (LU) | EBU Tech 3342, compuertas −70 / −20 LU, P95 − P10 | `[telescope][ebu]` — Tech 3342 tests 1-4 |
| | True peak (dBTP) | BS.1770-5 Anexo 2 §3, tabla **literal** del FIR polifásico (4 fases × 12 taps) | `[telescope][tp]` — Tech 3341 tests 15-19 |
| | K-weighting | BS.1770 Anexo 1, recalculado por sample rate desde el prototipo analógico | `[telescope][kw]` — error máx. **8.9 × 10⁻¹⁶** contra la tabla publicada a 48 kHz |
| **DYNAMICS** | PSR (dB) | pico real de los últimos 3 s − short-term | `[telescope][dyn]` |
| | PLR (dB) | pico real desde el RESET − integrado (AES TD1004) | `[telescope][dyn]` |
| | True peak **por canal** (dBTP) | el mismo FIR del Anexo 2 sobre la señal cruda, un máximo por canal (hop y desde el RESET) | `[telescope][tp]` — seno a −6 dBFS sólo en L: **L = −5.9933 dBTP, R = el piso**; el canal en silencio no hereda el pico del otro |
| | Momentary / short-term **parciales** (LUFS) | la misma media sobre `min(hops, N)` hops: válidos desde el PRIMER hop | `[telescope][ebu]` — el parcial a 1.0 s está a **0.00008 LU** del oficial a 3.0 s, y al llenarse la ventana es **igual al bit** |
| | Eventos de clip | primera muestra sobre el umbral, cierra 100 ms por debajo | `[telescope][dyn]` — 10 ráfagas → **10 eventos** |
| **SPECTRUM** | dBFS por bin | `20·log10(2·|X_k| / (N·CG))`, `CG = Σw/N` | `[telescope][spectrum]` — seno en bin exacto: **−20.0000 dB** con las 3 ventanas |
| | Bandas ⅓ de octava | ISO 266, 30 bandas | `[telescope][spectrum]` |
| | Bandas Bark | Zwicker, 24 bandas críticas | `[telescope][spectrum]` — ±0.26 dB sobre rosa; +1.008 dB/banda sobre blanco (teórico +1.003) |
| **SPECTROGRAM** | Nivel × tiempo × frecuencia | potencia instantánea, 512 filas log 20 Hz-20 kHz | `[telescope][spectrogram]` — barrido monótono ρ **0.99998**; determinismo byte a byte |
| **WATERFALL** | Los mismos datos en profundidad | proyección oblicua por software, oclusión por horizonte | `[telescope][waterfall]` · `[telescope][waterfall][horizon]` — idéntico al pintor literal |
| **CQT** | Espectro por nota | Q constante, `B = 24`, `f_min = 27.5 Hz`, `Q = 34.127`; kernels de Brown & Puckette (1992) podados a `0.0054·max` | `[telescope][cqt]` — mismo dB en 3 octavas (−19.998 / −19.997 / −20.000) |
| | Cromagrama | 12 clases, normalizado al máximo | `[telescope][cqt]` — vecino a **0.196** (derivado 0.20) |
| | Tonalidad | correlación de Pearson contra los 24 perfiles de Krumhansl & Kessler (1982) | `[telescope][cqt]` — siempre con confianza y % del tiempo |
| **SPIRAL** | Mapeo nota → (ángulo, radio) | `turns(k) = log2(27.5/16.3516) + k/B`, constante = **0.75 exacto** | `[telescope][settings]` — `positionFor` a 10⁻⁹ |
| **SCOPE** | corr · width · balance · mono loss | `ΣLR/√(ΣLL·ΣRR)` · `√(ΣSS/ΣMM)` · `10log10(ΣRR/ΣLL)` · `10log10ΣMM − 10log10((ΣLL+ΣRR)/2)` | `[telescope][stereo]` |
| **POLAR LEVEL** (el modo llamado HEMISFERIO hasta el 57c; *hemisferio* queda como nombre del plegado) | Envolvente por dirección, **un rayo por grado** | `θ = 90° + 2·atan2(R−L, R+L)`; se dibujan los **181 rayos** de 0° a 180°, cada uno una cuña de 1° desde el origen, en **dos capas**: PROMEDIO relleno (la envolvente promediada en el TIEMPO, τ = 0.3 s, en energía y por bin — nunca en ángulo, que sería inventar anchura) y PICO como contorno fino (la retención con decaimiento, 12/24/48 dB/s). Lo que cae fuera de fase (θ > 180°) se **pliega** sobre la base con `θ' = 360° − θ` y se dibuja en color de alerta, con su porcentaje —Σ(l²+r²) de los pares con l·r < 0 sobre el total— al lado. El radio es **amplitud relativa a la dirección más fuerte** (−6 dB → medio radio, −12 → un cuarto), con opción en dB de piso −24 | `[telescope][hemis]` — mono enciende **1 de 181** rayos sobre −20 dB relativos y ruido independiente **181 de 181** sobre −12; mono 90°, sólo L 0°, sólo R 180°, L=−R **100 % fuera de fase y 0 píxeles de dato bajo la base**; decaimiento con **0.00 %** de error |
| **BAND CORRELATION** | Las 5 sumas por banda de ⅓ de octava | las mismas fórmulas, sobre los bins de la STFT | `[telescope][bands]` — cruce contra el medidor del tiempo: Δcorr **0.00076**, Δmono loss **0.0033 dB** |
| **STEREO SPECTROGRAM** | Coherencia por bin (color) + nivel (brillo) | `Σ Re(L_k·R_k*) / √(Σ|L_k|²·Σ|R_k|²)`, suavizada sobre la ventana | `[telescope][sgram-st]` — L=R → 255, L=−R → 0, silencio → 128 |
| **FIELD** | Paneo por energía × frecuencia | `pan = (ΣRR−ΣLL)/(ΣRR+ΣLL) = −cos 2θ` con ley de potencia constante | `[telescope][pan]` — error peor **4.9 × 10⁻⁸** · `[telescope][field]` (grilla 64×96) |
| **TONAL BALANCE** | Delta de tilt a igual loudness | cada curva menos su propio LUFS integrado | `[telescope][ref]` · `[telescope][tonal]` — identidad de suma cero con error máx. **0.006 dB** sobre 21 bandas |
| | Potencia de banda por **solapamiento fraccionario** | `overlap_k = max(0, min(hi,(k+½)·binHz) − max(lo,(k−½)·binHz))`, `P_b = (Σ P_k·overlap_k/binHz)·(hi−lo)/Σ overlap_k`. El segundo factor vale 1 mientras los bins cubran la banda entera y sólo corrige la de 20 kHz a 44.1 k | `[telescope][tonal][bands]` — **30 de 30** bandas con medición (antes 29: la de 40 Hz caía entre dos `ceil`); 40 Hz a **0.153 dB** de la media de 31.5 y 50 (σ = 0.45); bins fraccionarios 0.39 / 0.79 / 0.99 a 4096/48 k |
| | Dibujable ⇔ crudo ≥ −90 dBFS **y** normalizada > −42 LU | lo que no entra en el plot corta la curva; comparable = las dos cosas de los dos lados | `[telescope][tonal]` — con una referencia con ruido a −50 dBFS, 21 de 30 bandas caen fuera de rango: **0 vértices en la fila del piso, 0 barras clavadas en ±12** |
| | La referencia mide con su propia FFT | orden 12 · Hann · 75 % · L+R, fija | `[telescope][ref][fixedfft]` |
| **VERDICT** | 21 reglas deterministas | tabla de umbrales con la fuente de cada número (`source/data/Rules.h`) | `[telescope][verdict]` — cada regla con su defecto y con material sano; 6 idiomas; archivo == vivo; **titular** con la cuenta (`VERDICT[titular]`: sano 18 dentro de rango · 0 para revisar), **dentro de rango** con número y límite (`VERDICT[rango]`: 12 filas con material sano, ninguna de una regla que se disparó), **huecos contiguos fusionados** (`VERDICT[hueco]`: 1 hallazgo ⚠, 630 Hz, tramo 400–1000 Hz, 0:20–0:35) y **número primero + dónde mirar** en las seis tablas (`VERDICT[tono]`) |
| | Historia por segundo | 30 bandas + loudness + estéreo + continua + tonalidad, 1 Hz | `[telescope][history]` — archivo == vivo **al bit**, 60/60 filas |

## Análisis de archivo

| Magnitud | Valor | Test |
|---|---|---|
| Identidad con el análisis en vivo | **al bit**: integrado, LRA, true peak, máximos M/S, las **30** bandas con medición (eran 29 hasta el solapamiento fraccionario del 57c) y las filas de la historia | `[telescope][file]` · `[telescope][history]` |
| Independencia del tamaño de bloque | bloques de 1 / 7 / 64 / 512 / 4096 → mismos resultados al bit | `[telescope][file]` |
| Velocidad | 60 s de audio en **0.24–0.63 s** en el M4 de la casa (243 ms con la máquina quieta, 633 ms con carga 6); el test exige **< 6 s** (10× tiempo real) | `[telescope][file]` |
| Cancelación | `cancel()` vuelve en **< 200 ms** (criterio del test; medido 2–3 ms) | `[telescope][file]` |
| Formatos | WAV · AIFF · FLAC · Ogg siempre; macOS: lo que lea CoreAudio (MP3, AAC, ALAC); Windows: WMA y MP3. Es lo que da JUCE de fábrica | — |
| Sample rate | el del archivo; **no se remuestrea** | `[telescope][file]` |

## Sample rates probados

| Dónde | Sample rates |
|---|---|
| Medición (la mayoría de los tests) | **48 kHz** |
| Vectores de la EBU y K-weighting | **44.1 y 96 kHz** además de 48 (`[telescope][ebu]`, `[telescope][kw]`) |
| True peak (los tres regímenes de sobremuestreo) | **44.1 · 48 · 88.2 · 96 · 176.4 · 192 kHz** (`[telescope][tp]`) |
| Análisis de archivo | **44.1 y 48 kHz** (`[telescope][file]`) |
| Instanciar / preparar / ciclo de vida | **44.1 · 48 · 96 kHz** × bloques de 64 / 512 / 2048 (`[telescope][smoke]`, `[telescope][lifecycle]`) |
| Render del host | hasta **192 kHz**, verificado por `auval` (11.025 · 22.05 · 44.1 · 48 · 96 · 192 kHz) |

No se declaran mediciones a 96 ni a 192 kHz más allá de eso: lo que no está en la tabla, no se probó.

## Sobremuestreo del true peak (BS.1770 Anexo 2)

| Sample rate | Sobremuestreo | Resultado |
|---|---|---|
| < 96 kHz | 4× | 176.4 / 192 / 352.8 kHz |
| 96 – 176.4 kHz | 2× | 192 – 352.8 kHz |
| ≥ 192 kHz | 1× | ya están a esa resolución |

**Sub-lectura conocida del 4×**, tabulada por el propio documento: el test 3341-17 (fs/6 a 60°) lee
**−6.3160 dBTP** contra un valor real de −6.0. Está dentro de la tolerancia del estándar
(+0.2 / −0.4 dBTP) y **es una sub-lectura real**.

## CPU de pintado

Criterio del spec: **mediana ≤ 4 ms y p95 ≤ 8 ms** por lente, en tamaño **L** (1025 × 702 px de área de
lente), sobre una `juce::Image` headless, con la capa estática ya calentada.

**Desde el 57b se mide a las DOS escalas**, y ese cambio no es cosmético: hasta entonces el banco pintaba
siempre a escala 1 y por eso nunca vio que las cachés de píxeles estuvieran asignadas en píxeles lógicos —
el defecto que Joaquín encontró abriendo el plugin en su DAW. Ahora cada lente se mide a escala **1** (la
ventana en un monitor común) y a escala **2** (Retina: cuatro veces los píxeles, que es donde de verdad
corre). El criterio a escala 2 es **mediana ≤ 6 ms y p95 ≤ 12 ms**: deja ≥ 10 ms libres de los 22.2 ms que
dura un frame a los 45 fps del editor.

Medido en el **M4 de la casa**, 2026-09-14, sobre el HEAD final del 57d, con `[telescope][budget]` y Live
cerrado. Es una máquina de trabajo y no un banco vacío: al empezar el load era 5.8 y al terminar 4.9, con una
VM de contenedores, un proceso de Python ajeno y WindowServer ocupando entre el 37 y el 47 % de un núcleo
cada uno — la condición en la que el plugin corre de verdad. Cada línea publica además el factor de carga
**`k = carga patrón medida / su referencia en reposo`**, y acá viaja en la tabla: el número no significa
nada sin la condición en la que se tomó. En esta corrida `k` fue **0.92–1.05** en las 26 probes, por debajo
del 1.30 a partir del cual el harness escalaría el criterio: el criterio **no se tocó** y estos son los
números tal cual.

| Lente | @1 mediana | @1 p95 | @2 mediana | @2 p95 | k |
|---|---:|---:|---:|---:|---:|
| LOUDNESS | 0.612 | 0.635 | 2.099 | 2.132 | 1.01 / 1.01 |
| DYNAMICS | 0.398 | 0.407 | 1.178 | 1.211 | 1.00 / 1.00 |
| SPECTRUM | 0.988 | 1.073 | 3.587 | 3.767 | 0.92 / 0.93 |
| SPECTROGRAM (redibujo completo) | 1.156 | 1.217 | 4.214 | 4.362 | 1.03 / 0.93 |
| WATERFALL (120 líneas, peor caso) | 1.839 | 2.297 | 4.472 | 4.664 | 0.93 / 0.94 |
| CQT | 1.880 | 1.961 | 4.415 | 4.540 | 0.96 / 0.95 |
| SPIRAL | 0.915 | 1.020 | 1.907 | 2.011 | 0.96 / 0.93 |
| SCOPE | 1.213 | 1.227 | 2.691 | 2.729 | 0.99 / 0.96 |
| BAND CORRELATION | 0.781 | 0.831 | 2.472 | 2.606 | 0.94 / 0.92 |
| STEREO SPECTROGRAM (redibujo completo) | 1.111 | 1.201 | 4.171 | 4.303 | 0.93 / 0.93 |
| FIELD | 1.754 | 1.846 | 3.884 | 4.046 | 0.95 / 1.05 |
| TONAL BALANCE | 0.774 | 0.862 | 2.521 | 2.689 | 1.05 / 1.00 |
| VERDICT | 0.322 | 0.342 | 0.886 | 1.076 | 0.94 / 1.03 |

Las dos lentes que escriben una pantalla entera de píxeles por frame publican además su **margen** del
80 % del criterio: 3.2 ms a escala 1 y **4.8 a escala 2**. FIELD lo **exige** (es la que más escribe — el
plano de adelante es una superficie con altura, luz y oclusión) y WATERFALL lo **imprime** sin exigirlo:
con 4.47 contra 4.8 hay un 7 % de aire, y un REQUIRE con ese aire falla por la carga de la máquina antes
que por una regresión. Dónde está el costo de WATERFALL, medido desactivando una cosa por vez a escala 2:
el **relleno 1.46 ms**, el **trazo 0.62** y el suavizado [1 2 1] de 256 puntos, **nada** (está bajo el
ruido). Con eso a la vista, la geometría de la lente pasó a resolverse cada dos columnas a escala ≥ 2 —una
línea son 256 puntos sobre 1 900 px— y el peor caso bajó de 6.15 a 5.00 ms **con la máquina cargada**.

El margen de FIELD con la máquina ocupada, medido en el 57d con cuatro hogs al lado: **4.2–4.9 ms** de típica a
escala 2, y **7 de 8** mediciones dentro de los 4.8. La que no dio 4.88 con `k` 1.27, justo debajo del 1.30 en
el que el harness escala: es la lente que la carga patrón no modela (lo dice el test). En reposo, 17–24 % de
aire.

En otra máquina, la referencia se fija con `TELESCOPE_BUDGET_REF_MS=<ms>` (el valor sale en la línea
`BUDGET_CALIBRACION`).

### Nitidez a escala física

`VISUAL[hd]` compara, sobre la misma señal, la energía del gradiente del render a escala 2 contra la del
render a escala 1 ampliado — que es lo que el sistema le hace a un buffer de 1× en un panel de 2×. Se mide
**sólo adentro del rectángulo de la caché** de cada lente: fuera de él viven la rejilla, los rótulos y los
trazos vectoriales, que se dibujan a escala física siempre y tapan lo que la caché hace. Las seis lentes
que rasterizan a mano, con y sin la mutación de control (`sNew = 1.0f` en `raster::Cache::prepare`, o sea
la caché de vuelta a píxeles lógicos):

| Lente | 1× ampliado | 2× real | razón (criterio 2.75) | con la caché mutada a 1× |
|---|---:|---:|---:|---:|
| SPECTROGRAM | 8.53 | 27.61 | **3.24** | 1.65 |
| STEREO SPECTROGRAM | 4.13 | 12.92 | **3.12** | 1.68 |
| WATERFALL | 28.49 | 123.64 | **4.34** | 2.21 |
| FIELD | 3.31 | 12.54 | **3.78** | 2.20 |
| SCOPE | 174.46 | 508.54 | **2.91** | 2.55 |
| SPECTRUM | 15.71 | 64.47 | **4.11** | 2.06 |

El criterio es **2.75** y no 1.50 porque el PISO de esta métrica es 2.0: repartir un escalón de A en dos de
A/2 baja ΣΔ² a la mitad, así que una imagen que no ganó nada de resolución ya da 2. Un criterio por debajo
del piso de su métrica no puede fallar nunca. Y el test no se queda en la estadística: verifica además, en
las seis, que la caché se haya horneado a la **escala física del pintado** (1.0 en el render de 1× y 2.0 en
el de 2×). Con la mutación eso da 1.0 donde tiene que dar 2.0 y las seis se ponen rojas — 12 aserciones,
dos por lente.

## Las paletas de los mapas de nivel

Las cuatro lentes en las que el color codifica NIVEL —SPECTROGRAM, STEREO SPECTROGRAM (sólo su eje de
nivel: la fase sigue siendo bipolar), WATERFALL y FIELD— comparten **una** rampa elegible, que persiste en
el estado como el idioma:

| Rampa | Qué es | Monótona en luminancia |
|---|---|---|
| `ovni` | la del sello, calculada desde el Theme (no tabulada, así sigue al tema) | sí |
| `inferno` | negro → púrpura → naranja → amarillo. Las 256 entradas exactas de matplotlib | sí |
| `viridis` | azul oscuro → verde azulado → amarillo; legible con daltonismo rojo-verde | sí |
| `spectrum` | azul → cian → verde → amarillo → rojo → blanco (el look de Ozone/Insight) | **no, a propósito** |

`spectrum` está exenta de la monotonía y el test la exime **nombrándola**: ahí lo que ordena el nivel es el
TONO, que es una secuencia que el ojo también lee sin rótulo. `inferno` y `viridis` son de Smith y van der
Walt (2015), CC0 — la atribución está en `NOTICE.md`.

## Memoria

Medida con `sizeof` sobre los headers reales, con las banderas de compilación del build:

| Estructura | Capacidad | Bytes |
|---|---|---:|
| `SecondHistory` — historia por segundo de VERDICT | **1 Hz · 600 filas = 10 minutos** (`SecondRow` = 420 B: 3 × 30 bandas + loudness + estéreo + continua + tonalidad) | **252 008 B** (246.1 KB) |
| `LoudnessHistory` — historia de LOUDNESS | **10 Hz · 6 000 puntos = 10 minutos** (momentary + short-term) | **48 008 B** (46.9 KB) |
| `ClipHistory` — línea de tiempo de DYNAMICS | **1 Hz · 600 puntos = 10 minutos** | **2 408 B** (2.4 KB) |
| `SpectrumFrame` (peor caso, orden 15) | 16 385 bins × 2 espectros × 2 arrays, en un `TripleBuffer` de 3 slots | ≈ 260 KB por slot (≈ 790 KB) |
| Anillo del espectrograma | 60 s × 60 columnas/s × 512 filas de 1 byte, fijo | ≈ 1.8 MB |

El integrado y el LRA se calculan sobre **todos** los bloques desde el último RESET, no sobre un histograma
con bins de 0.1 dB: eso los hace exactos y cuesta ≈ **2.3 MB por hora** de medición continua. RESET lo
vacía. Los buffers del espectro están dimensionados al peor caso y **no crecen con el uso**.

## Idiomas

| Código | Idioma | Estado | Notas de nota musical |
|---|---|---|---|
| `en` | English | revisado (**default**) | letras (C D E) |
| `es` | Español | revisado | solfeo (Do Re Mi) |
| `pt` | Português | traducido, **pendiente de revisión nativa** | solfeo |
| `fr` | Français | traducido, **pendiente de revisión nativa** | solfeo |
| `de` | Deutsch | traducido, **pendiente de revisión nativa** | germánico (B = Si♭, H = Si) |
| `it` | Italiano | traducido, **pendiente de revisión nativa** | solfeo |

140 de 140 claves en cada tabla; fallback **por clave** a inglés. Las 13 lentes leen de ahí
(`[telescope][visual][i18n]`, `[telescope][settings][i18n]`, `[telescope][verdict][i18n]`). Matriz completa:
[`strings-matrix.md`](strings-matrix.md).

## Qué NO hace

- **No localiza fuentes.** FIELD y POLAR LEVEL miden energía por dirección de paneo. No hay HRTF, ni ITD, ni azimut.
- **Sólo estéreo.** Ni surround ni Ambisonics (el test 6 de EBU Tech 3341, de 5.0 canales, está fuera de
  alcance y así figura).
- **Ninguna métrica de inteligibilidad de voz o de diálogo.**
- **Sin IA, sin red, sin telemetría.** VERDICT es una tabla de umbrales determinista.
- **Los chequeos por dispositivo son genéricos.** No hay ninguna curva de respuesta de ningún parlante real
  adentro del plugin: cada caja es una definición de qué clase de sistema es. Sale rotulado como pronóstico.
- **"Sin hallazgos" no es "está terminado"**, y es lo que VERDICT imprime.
- Vectores de la EBU **pendientes**: Tech 3341 7-14 y 20-23, Tech 3342 5-6 (piden los WAV de programa real
  de la EBU o un resampler para sintetizar la señal). Figuran como pendientes, no como pasados.

---

# EN · English

## Identity

| | |
|---|---|
| Name | **TELESCOPE** |
| Version | **0.1.0** |
| Type | Audio analyser (it does not process) |
| Manufacturer | OVNI Audio |
| AU codes | `aufx` · plugin `Tlsc` · manufacturer `Ovni` |
| VST3 category | `Fx｜Analyzer` |
| macOS formats | **VST3 + AU**, universal binary `arm64 + x86_64`, minimum **macOS 11.0** |
| Windows format | **VST3 x64** — Windows 10+, unsigned ZIP (`OVNI-TELESCOPE-v0.1.0-Windows.zip` in the release; SmartScreen may warn) |
| Standalone | it is built (`telescope_Standalone`, the generic JUCE shell) but **the 0.1.0 installer does not ship it**: it installs VST3 + AU. TELESCOPE measures what goes through the DAW's chain; the standalone only listens to whatever audio input you pick |
| Channels | mono or stereo in → **stereo out** (the DAW instantiates mono→stereo) |
| Licence | **AGPLv3** · source: <https://github.com/ovniaudio/ovni> |

## Effect on the audio

| Quantity | Value | Test |
|---|---|---|
| Reported and real latency | **0 samples** | `[telescope][null]` |
| Tail | **0** | `[telescope][null]` |
| Pass-through | **bit-exact**: output is input, sample by sample, compared with `==` | `[telescope][null]` — bypass on/off, all 13 lenses, blocks of 1 / 7 / 64 / 4096, mono source: `NULL_MISMATCHES=0` in all 5 cases |
| Peak with a full-scale input | **1.000000** (identity, 0 differences) | `[gain][telescope]` |

The label's shared chassis (`PluginProcessorBase`) injects `inGain`, `output` and `monoSafe` into **every**
plugin in the catalog. TELESCOPE does not expose them in its UI because it does not process, but your host
**will list them**. At their defaults (0 dB, 0 dB, off) the audio is bit-exact — which is what the test
above verifies. Known chassis limitation.

## What each lens measures

| Lens | Quantity | Standard or definition | Test |
|---|---|---|---|
| **LOUDNESS** | Integrated / Short-term / Momentary (LUFS) | ITU-R BS.1770, double gate (absolute −70 LUFS, relative −10 LU) | `[telescope][ebu]` — Tech 3341 tests 1-5 |
| | LRA (LU) | EBU Tech 3342, gates −70 / −20 LU, P95 − P10 | `[telescope][ebu]` — Tech 3342 tests 1-4 |
| | True peak (dBTP) | BS.1770-5 Annex 2 §3, **literal** polyphase FIR table (4 phases × 12 taps) | `[telescope][tp]` — Tech 3341 tests 15-19 |
| | K-weighting | BS.1770 Annex 1, recalculated per sample rate from the analogue prototype | `[telescope][kw]` — max error **8.9 × 10⁻¹⁶** against the published 48 kHz table |
| **DYNAMICS** | PSR (dB) | max true peak of the last 3 s − short-term | `[telescope][dyn]` |
| | PLR (dB) | max true peak since RESET − integrated (AES TD1004) | `[telescope][dyn]` |
| | Clip events | opens on the first sample above the threshold, closes 100 ms below it | `[telescope][dyn]` — 10 bursts → **10 events** |
| **SPECTRUM** | dBFS per bin | `20·log10(2·|X_k| / (N·CG))`, `CG = Σw/N` | `[telescope][spectrum]` — sine on an exact bin: **−20.0000 dB** with all 3 windows |
| | ⅓-octave bands | ISO 266, 30 bands | `[telescope][spectrum]` |
| | Bark bands | Zwicker, 24 critical bands | `[telescope][spectrum]` — ±0.26 dB over pink; +1.008 dB/band over white (theory +1.003) |
| **SPECTROGRAM** | Level × time × frequency | instantaneous power, 512 log rows 20 Hz-20 kHz | `[telescope][spectrogram]` — monotonic sweep ρ **0.99998**; byte-for-byte determinism |
| **WATERFALL** | The same data in depth | oblique software projection, horizon occlusion | `[telescope][waterfall]` · `[telescope][waterfall][horizon]` — identical to the literal painter's algorithm |
| **CQT** | Spectrum by note | constant-Q, `B = 24`, `f_min = 27.5 Hz`, `Q = 34.127`; Brown & Puckette (1992) kernels pruned at `0.0054·max` | `[telescope][cqt]` — same dB in 3 octaves (−19.998 / −19.997 / −20.000) |
| | Chromagram | 12 classes, normalised to the maximum | `[telescope][cqt]` — neighbour at **0.196** (derived 0.20) |
| | Key | Pearson correlation against the 24 Krumhansl & Kessler (1982) profiles | `[telescope][cqt]` — always with confidence and share of time |
| **SPIRAL** | Note → (angle, radius) mapping | `turns(k) = log2(27.5/16.3516) + k/B`, constant = **exactly 0.75** | `[telescope][settings]` — `positionFor` to 10⁻⁹ |
| **SCOPE** | corr · width · balance · mono loss | `ΣLR/√(ΣLL·ΣRR)` · `√(ΣSS/ΣMM)` · `10log10(ΣRR/ΣLL)` · `10log10ΣMM − 10log10((ΣLL+ΣRR)/2)` | `[telescope][stereo]` |
| **POLAR LEVEL** (called HEMISPHERE until 57c; *hemisphere* stays as the name of the fold) | Envelope per direction, **one ray per degree** | `θ = 90° + 2·atan2(R−L, R+L)`; all **181 rays** from 0° to 180° are drawn, each a 1° wedge from the origin, in **two layers**: AVERAGE filled (the envelope averaged in TIME, τ = 0.3 s, in energy, bin by bin — never in angle, which would invent width) and PEAK as a thin outline (the peak-hold with its decay, 12/24/48 dB/s) | `[telescope][hemis]` — mono lights **1 of 181** rays above −20 dB relative and independent noise **181 of 181** above −12; mono 90°, L only 0°, R only 180°, L=−R 270°; decay with **0.00 %** error |
| **BAND CORRELATION** | The 5 sums per ⅓-octave band | the same formulas, over the STFT bins | `[telescope][bands]` — cross-check against the time-domain meter: Δcorr **0.00076**, Δmono loss **0.0033 dB** |
| **STEREO SPECTROGRAM** | Per-bin coherence (colour) + level (brightness) | `Σ Re(L_k·R_k*) / √(Σ|L_k|²·Σ|R_k|²)`, smoothed over the window | `[telescope][sgram-st]` — L=R → 255, L=−R → 0, silence → 128 |
| **FIELD** | Energy panning × frequency | `pan = (ΣRR−ΣLL)/(ΣRR+ΣLL) = −cos 2θ` under the constant-power law | `[telescope][pan]` — worst error **4.9 × 10⁻⁸** · `[telescope][field]` (64×96 grid) |
| **TONAL BALANCE** | Tilt delta at equal loudness | each curve minus its own integrated LUFS | `[telescope][ref]` · `[telescope][tonal]` — zero-sum identity with max error **0.006 dB** over 21 bands |
| | Band power by **fractional overlap** | `overlap_k = max(0, min(hi,(k+½)·binHz) − max(lo,(k−½)·binHz))`, `P_b = (Σ P_k·overlap_k/binHz)·(hi−lo)/Σ overlap_k` | `[telescope][tonal][bands]` — **30 of 30** bands measured (29 before: the 40 Hz band fell between two `ceil`s); 40 Hz within **0.153 dB** of the mean of 31.5 and 50 (σ = 0.45) |
| | Drawable ⇔ raw ≥ −90 dBFS **and** normalised > −42 LU | what does not fit the plot breaks the curve; comparable = both, on both sides | `[telescope][tonal]` — **0 vertices on the floor row, 0 bars pinned at ±12** |
| | The reference measures with its own FFT | order 12 · Hann · 75 % · L+R, fixed | `[telescope][ref][fixedfft]` |
| **VERDICT** | 21 deterministic rules | threshold table with the source of every number (`source/data/Rules.h`) | `[telescope][verdict]` — each rule against its defect and against healthy material; 6 languages; file == live; a **headline** with the count (`VERDICT[titular]`: healthy 18 within range · 0 to look at), **within range** lines with number and limit (`VERDICT[rango]`: 12 lines on healthy material, never one for a rule that fired), **contiguous dips merged** (`VERDICT[hueco]`: 1 finding ⚠, 630 Hz, 400–1000 Hz stretch, 0:20–0:35) and **number first + where to look** in all six tables (`VERDICT[tono]`) |
| | Per-second history | 30 bands + loudness + stereo + DC + key, at 1 Hz | `[telescope][history]` — file == live **to the bit**, 60/60 rows |

## File analysis

| Quantity | Value | Test |
|---|---|---|
| Identity with the live analysis | **to the bit**: integrated, LRA, true peak, M/S maxima, the **30** measured bands (29 until 57c's fractional overlap) and the history rows | `[telescope][file]` · `[telescope][history]` |
| Block-size independence | blocks of 1 / 7 / 64 / 512 / 4096 → same results to the bit | `[telescope][file]` |
| Speed | 60 s of audio in **0.24–0.63 s** on the house M4 (243 ms with the machine quiet, 633 ms under load 6); the test requires **< 6 s** (10× real time) | `[telescope][file]` |
| Cancellation | `cancel()` returns in **< 200 ms** (test criterion; measured 2–3 ms) | `[telescope][file]` |
| Formats | WAV · AIFF · FLAC · Ogg always; macOS: whatever CoreAudio reads (MP3, AAC, ALAC); Windows: WMA and MP3. That is what JUCE gives out of the box | — |
| Sample rate | the file's; **nothing is resampled** | `[telescope][file]` |

## Sample rates tested

| Where | Sample rates |
|---|---|
| Measurement (most tests) | **48 kHz** |
| EBU vectors and K-weighting | **44.1 and 96 kHz** as well as 48 (`[telescope][ebu]`, `[telescope][kw]`) |
| True peak (all three oversampling regimes) | **44.1 · 48 · 88.2 · 96 · 176.4 · 192 kHz** (`[telescope][tp]`) |
| File analysis | **44.1 and 48 kHz** (`[telescope][file]`) |
| Instantiate / prepare / lifecycle | **44.1 · 48 · 96 kHz** × blocks of 64 / 512 / 2048 (`[telescope][smoke]`, `[telescope][lifecycle]`) |
| Host render | up to **192 kHz**, verified by `auval` (11.025 · 22.05 · 44.1 · 48 · 96 · 192 kHz) |

No measurements at 96 or 192 kHz are claimed beyond that: what is not in the table was not tested.

## True-peak oversampling (BS.1770 Annex 2)

| Sample rate | Oversampling | Result |
|---|---|---|
| < 96 kHz | 4× | 176.4 / 192 / 352.8 kHz |
| 96 – 176.4 kHz | 2× | 192 – 352.8 kHz |
| ≥ 192 kHz | 1× | already at that resolution |

**Known 4× under-read**, tabulated by the document itself: test 3341-17 (fs/6 at 60°) reads
**−6.3160 dBTP** against a real −6.0. It is inside the standard's tolerance (+0.2 / −0.4 dBTP) and **it is
a real under-read**.

## Paint CPU

Spec criterion: **median ≤ 4 ms and p95 ≤ 8 ms** per lens, at size **L** (a 1025 × 702 px lens area), on a
headless `juce::Image`, with the static layer already warm.

**Since 57b it is measured at BOTH scales**, because until then the bench always painted at scale 1 and so
never saw that the pixel caches were allocated in *logical* pixels — the defect Joaquín found opening the
plug-in in his DAW. The criterion at scale 2 is **median ≤ 6 ms and p95 ≤ 12 ms**: it leaves ≥ 10 ms free
of the 22.2 ms a frame lasts at the editor's 45 fps.

Measured on the **house M4**, 2026-09-14, on the final 57d HEAD, with `[telescope][budget]` and Live closed.
It is a working machine, not an empty bench: load was 5.8 at the start and 4.9 at the end, with a container
VM, someone else's Python process and WindowServer each holding 37–47 % of a core — the condition the plug-in
actually runs in. Every line also publishes the load factor
**`k = measured standard load / its idle reference`**; in this run `k` was **0.92–1.05** across all 26 probes,
below the 1.30 at which the harness would scale the criterion — the criterion **was not touched** and these
are the numbers as they came.

| Lens | @1 median | @1 p95 | @2 median | @2 p95 | k |
|---|---:|---:|---:|---:|---:|
| LOUDNESS | 0.612 | 0.635 | 2.099 | 2.132 | 1.01 / 1.01 |
| DYNAMICS | 0.398 | 0.407 | 1.178 | 1.211 | 1.00 / 1.00 |
| SPECTRUM | 0.988 | 1.073 | 3.587 | 3.767 | 0.92 / 0.93 |
| SPECTROGRAM (full redraw) | 1.156 | 1.217 | 4.214 | 4.362 | 1.03 / 0.93 |
| WATERFALL (120 lines, worst case) | 1.839 | 2.297 | 4.472 | 4.664 | 0.93 / 0.94 |
| CQT | 1.880 | 1.961 | 4.415 | 4.540 | 0.96 / 0.95 |
| SPIRAL | 0.915 | 1.020 | 1.907 | 2.011 | 0.96 / 0.93 |
| SCOPE | 1.213 | 1.227 | 2.691 | 2.729 | 0.99 / 0.96 |
| BAND CORRELATION | 0.781 | 0.831 | 2.472 | 2.606 | 0.94 / 0.92 |
| STEREO SPECTROGRAM (full redraw) | 1.111 | 1.201 | 4.171 | 4.303 | 0.93 / 0.93 |
| FIELD | 1.754 | 1.846 | 3.884 | 4.046 | 0.95 / 1.05 |
| TONAL BALANCE | 0.774 | 0.862 | 2.521 | 2.689 | 1.05 / 1.00 |
| VERDICT | 0.322 | 0.342 | 0.886 | 1.076 | 0.94 / 1.03 |

The two lenses that write a whole screen of pixels per frame also publish their **margin** of 80 % of the
criterion (3.2 ms at scale 1, **4.8 at scale 2**). FIELD **requires** it; WATERFALL **prints** it without
requiring it — with 4.47 against 4.8 there is 7 % of air, and a REQUIRE with that much air fails because
of machine load before it fails because of a regression.

FIELD's margin on a busy machine, measured in 57d with four CPU hogs running: **4.2–4.9 ms** typical at scale 2,
and **7 of 8** measurements inside the 4.8. The one that was not read 4.88 with `k` 1.27, just below the 1.30
at which the harness scales — this is the lens the standard load does not model, as the test itself says. At
rest it has 17–24 % of air.

On another machine the reference is set with `TELESCOPE_BUDGET_REF_MS=<ms>` (the value is printed in the
`BUDGET_CALIBRACION` line).

## Memory

Measured with `sizeof` over the real headers, using the build's own compile flags:

| Structure | Capacity | Bytes |
|---|---|---:|
| `SecondHistory` — VERDICT's per-second history | **1 Hz · 600 rows = 10 minutes** (`SecondRow` = 420 B: 3 × 30 bands + loudness + stereo + DC + key) | **252,008 B** (246.1 KB) |
| `LoudnessHistory` — LOUDNESS's history | **10 Hz · 6,000 points = 10 minutes** (momentary + short-term) | **48,008 B** (46.9 KB) |
| `ClipHistory` — DYNAMICS's timeline | **1 Hz · 600 points = 10 minutes** | **2,408 B** (2.4 KB) |
| `SpectrumFrame` (worst case, order 15) | 16,385 bins × 2 spectra × 2 arrays, in a 3-slot `TripleBuffer` | ≈ 260 KB per slot (≈ 790 KB) |
| Spectrogram ring | 60 s × 60 columns/s × 512 rows of 1 byte, fixed | ≈ 1.8 MB |

The integrated value and LRA are computed over **all** blocks since the last RESET, not over a histogram
with 0.1 dB bins: that makes them exact and costs ≈ **2.3 MB per hour** of continuous measurement. RESET
empties it. The spectrum buffers are sized for the worst case and **do not grow with use**.

## Languages

| Code | Language | Status | Note-naming |
|---|---|---|---|
| `en` | English | reviewed (**default**) | letters (C D E) |
| `es` | Español | reviewed | solfège (Do Re Mi) |
| `pt` | Português | translated, **pending native review** | solfège |
| `fr` | Français | translated, **pending native review** | solfège |
| `de` | Deutsch | translated, **pending native review** | German (B = B♭, H = B natural) |
| `it` | Italiano | translated, **pending native review** | solfège |

140 of 140 keys in every table; **per-key** fallback to English. All 13 lenses read from it
(`[telescope][visual][i18n]`, `[telescope][settings][i18n]`, `[telescope][verdict][i18n]`). Full matrix:
[`strings-matrix.md`](strings-matrix.md).

## What it does NOT do

- **It does not localise sources.** FIELD and POLAR LEVEL measure energy by pan direction. No HRTF, no ITD, no
  azimuth.
- **Stereo only.** No surround, no Ambisonics (EBU Tech 3341 test 6, 5.0 channels, is out of scope and is
  listed as such).
- **No speech-intelligibility or dialogue metric.**
- **No AI, no network, no telemetry.** VERDICT is a deterministic threshold table.
- **The per-device checks are generic.** There is no response curve of any real speaker inside the plugin:
  each box is a definition of what class of system it is. It is printed as a forecast.
- **"No findings" is not "it's finished"**, and that is what VERDICT prints.
- **Pending** EBU vectors: Tech 3341 7-14 and 20-23, Tech 3342 5-6 (they need the EBU's real-programme WAVs,
  or a resampler to synthesise the signal). They are listed as pending, not as passing.
