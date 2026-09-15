# CHANGELOG — TELESCOPE 🔭

Todos los cambios del plugin. Formato [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/),
versionado [SemVer](https://semver.org/lang/es/).

La entrada de la versión publicada está **en inglés y en lenguaje de usuario** (es la que se lee desde el
release y desde el sitio). El **diario de construcción** por prompt queda debajo, en castellano, tal como
se escribió: es la historia de cómo se llegó acá, no la nota de release.

## 0.1.0 — 2026-09-15

First public release. TELESCOPE is a **free, open-source audio analyser that also concludes**: one
analysis engine and thirteen lenses, plus a rules engine that writes findings in plain words with the
number and the rule id next to each one.

It **does not touch your audio**. `processBlock` reads L and R, pushes them to a lock-free bus and writes
nothing: **0 samples of latency, 0 tail, bit-exact output**. Verified in `tests/PassThroughTest.cpp`
(`[telescope][null]`) with deterministic stereo noise, bypass on and off, each of the thirteen lenses
selected, blocks of 1 / 7 / 64 / 4096 samples and a mono source — `NULL_MISMATCHES=0` in every case.

### The thirteen lenses

| Lens | What it measures | Against what |
|---|---|---|
| **LOUDNESS** | Integrated / Short-term / Momentary LUFS, LRA, true peak, maxima since reset, 3-minute history, streaming target | ITU-R BS.1770 gating (absolute −70 LUFS, relative −10 LU); LRA per EBU Tech 3342; K-weighting recalculated per sample rate and checked against the published 48 kHz table (BS.1770-4 Annex 1) to 8.9·10⁻¹⁶; true peak with the **literal** 4×12 polyphase FIR table of BS.1770-5 Annex 2 §3 |
| **DYNAMICS** | PSR (last 3 s) and PLR (since reset), 61-bin short-term histogram, clip events with a 10-minute timeline | PLR as in AES TD1004; one clip event = first sample over the threshold, closed after 100 ms below it |
| **SPECTRUM** | FFT 1 024–32 768, three windows, three overlaps, five channel modes, averaging, peak hold, ⅓-octave and Bark | dB per bin referred to a full-scale sine, with exact coherent-gain correction; ⅓-octave centres per ISO 266; Bark = Zwicker's 24 critical bands |
| **SPECTROGRAM** | Level over time: log frequency (512 rows), 10 / 30 / 60 s of history, level as colour | Instantaneous power, never the average — averaging an evolution erases what it shows |
| **WATERFALL** | The same data in depth: frequency in X, **level as height**, time in Z, 60 / 90 / 120 lines | Oblique software projection (no GPU, so Windows and every host get the same product); occlusion proven identical to the painter's algorithm |
| **CQT** | Spectrum by **note**: 24 bins per octave from A0, one window per bin, chromagram and estimated key | Constant-Q with the spectral kernels of Brown & Puckette (1992); key by correlation against the 24 Krumhansl & Kessler (1982) profiles |
| **SPIRAL** | The same constant-Q **coiled**: one turn per octave, so equal notes line up on one radius | Same transform as CQT; angle = pitch class, radius = octave |
| **SCOPE** | Correlation, width, balance, mono loss on a 100 / 300 / 1000 ms window; Lissajous, **polar sample**, **polar level** and oscilloscope | `corr = ΣLR/√(ΣLL·ΣRR)`, `width = √(ΣSS/ΣMM)`, `balance = 10log₁₀(ΣRR/ΣLL)`, `monoLoss = 10log₁₀ΣMM − 10log₁₀((ΣLL+ΣRR)/2)` — the same maths as ORBIT's and PULSAR's measurement harness, so the numbers are directly comparable |
| **BAND CORRELATION** | Those same five sums **per ⅓-octave band**, so mono bass and open highs stop averaging into one meaningless number | The band-wide figure computed from the STFT bins matches the time-domain meter to 0.00076 in correlation and 0.0033 dB in mono loss |
| **STEREO SPECTROGRAM** | The sonogram with **phase as colour** (red out of phase, green wide, white mono) and level as brightness | Per-bin coherence smoothed over the same window; a cell with no energy is black, and silence reads "undefined", not "out of phase" |
| **FIELD** | Energy per **pan direction × frequency**, 64 × 96, with decay and trail | `pan = (ΣRR−ΣLL)/(ΣRR+ΣLL)`; with the constant-power law it equals `−cos 2θ` exactly (worst error 4.9·10⁻⁸). This is **energy panning L/R, not localisation** — a label you cannot switch off, and the readout is in per cent, never degrees |
| **TONAL BALANCE** | Your programme's average ⅓-octave curve against a reference track you load, plus the delta | Both curves normalised by **their own integrated LUFS**, so the comparison is of tilt at equal loudness and **the delta sums to zero**; the identity is checked band by band to 0.006 dB |
| **VERDICT** | 21 deterministic rules over a per-second history: within range · how it will feel · where it translates · what to check and where | A threshold table (`source/data/Rules.h`) with the source of every number. Every sentence carries its measurement and its rule id. VERDICT opens with what is within range and merges a dip that spans several bands into one finding |

**POLAR LEVEL** is SCOPE's third mode: a half-circle with mono at the top, L and R at the base and energy
drawn as **one ray per degree** — the engine's own 1° resolution, 181 rays from L to R — with
`θ = 90° + 2·atan2(R−L, R+L)`. Two layers sit on those rays: the envelope averaged in *time* (τ = 0.3 s,
filled) and the peak-hold with its decay (a thin outline). What is out of phase folds onto the base and is
printed as a percentage: it is what you lose when the mix is folded to mono. It reads the same frame as
the goniometer; only the projection changes.

### The rest of what's in 0.1.0

- **File analysis is bit-identical to live analysis.** Drop a track and TELESCOPE measures it offline with
  the *same* classes, not a second implementation: integrated, LRA, true peak, the 30 measured ⅓-octave
  bands and the 60 rows of per-second history come out **equal to the bit** against the live path, in
  blocks of 1 / 7 / 64 / 512 / 4096 samples. 60 s of audio in about a quarter of a second on an M4 (243 ms measured with the machine quiet; the test requires under 6 s), with progress and cancel.
- **A loaded reference stores the path, not the numbers.** Reopen a session and the file is re-analysed;
  if it is gone the lens says so and draws nothing, instead of showing 30 saved numbers as if they were
  still the file.
- **The key is never stated alone.** It always ships with its confidence and the share of time it held —
  `A minor · confidence 0.83 · 91 % of the time` — because the confidence has a floor (any chromagram
  wins one of 24 profiles by chance: 6 s of pink noise scored ≈ 0.54 in the reference run) and because genuinely ambiguous music
  exists. Silence reports **no key**, not "C major, confidence 0".
- **Six languages**, ISO 639-1 codes, **English by default**, per-key fallback so a missing phrase comes
  out in English instead of blank: `en` and `es` reviewed; **`pt`, `fr`, `de` and `it` are translated with
  the industry's terms but are pending native review.** Note names follow each language's convention —
  letters, solfège, and the German one where B is B♭ and the note above it is H.
- **Reduced motion is respected per lens**, and honestly: trails and smoothing are switched off, but the
  three time-axis lenses (spectrogram, stereo spectrogram, waterfall) keep running — freezing them would
  not be less motion, it would be showing less data.
- Every drawn number has a test against a synthetic signal, and every lens has a paint budget measured
  against 4 ms median / 8 ms p95 at size L — and, since the caches moved to physical scale, **also at
  scale 2** (6 / 12 ms), published with the load factor `k` of the machine that measured it.
- **It is drawn at your screen's real resolution.** Every lens that rasterises by hand — the two
  sonograms, WATERFALL, FIELD, SPECTRUM's curve and SCOPE's trail — bakes its pixel cache at the
  panel's *physical* scale, not in logical pixels stretched afterwards. On a Retina display that is
  four times the pixels, and in the two sonograms it also doubles the **time** resolution: each column of
  history can keep its own pixel. A test measures the gradient energy of each cache against the same lens
  drawn at half resolution and stretched, and checks the cache was allocated at the paint's scale.
- **Level maps have four colour ramps** — ovni, inferno, viridis and spectrum — shared by the
  four lenses where colour encodes level, and stored with the preset like the language. Three of them are
  monotonic in luminance, which is what lets you read a level without a legend.
- **LOUDNESS shows L and R.** Four bars: momentary and short-term in LUFS, **L and R in dBTP**, the last
  two from the same BS.1770 Annex 2 FIR that was already running — one maximum per channel instead of the
  maximum of the two. And no bar waits any more: while a window is filling (400 ms, 3 s) it shows the
  *partial* value, dimmed and outlined, which is the same calculation over the hops there are and matches
  the official number to the bit the moment the window fills.
- **TONAL BALANCE stopped breaking its own line.** Band power is now taken by **fractional overlap** with
  the FFT grid, so a ⅓-octave band narrower than one bin — 40 Hz is 0.79 of a bin at 4096/48 kHz — gets
  that bin's density instead of coming out empty; all 30 bands are measured at every sample rate. And a
  band whose normalised value falls outside the plot now **breaks** the curve instead of being clamped to
  the floor, which is where the vertical drops and the ±12 dB bars came from.

### What TELESCOPE does not do

- **It does not localise sources.** FIELD and POLAR LEVEL show pan direction by energy. There is no HRTF,
  no ITD, no azimuth: recovering source positions from a finished stereo mix has no unique solution.
- **Stereo only.** No surround, no Ambisonics. EBU Tech 3341 test 6 (5.0 channels) is out of scope and
  labelled as such.
- **No speech-intelligibility or dialogue metric.**
- **No AI, no network, no telemetry.** VERDICT is a deterministic rules engine: the same signal gives the
  same report, word for word, and nothing leaves your machine.
- **The device checks are generic and say so.** The six boxes (phone, headphones, laptop, car, club with
  mono sub, hi-fi) are definitions of what kind of system each one is — *there is no measured response
  curve of any real speaker inside the plugin*. They are a forecast, printed as a forecast.
- **"No findings" is not "it's finished."** When the rules find nothing, that is exactly what VERDICT
  says: the rules cover what they cover, and what is not in the table is not measured.
- **It states its own errors.** 4× oversampling has a known maximum under-read (EBU test 3341-17 reads
  −6.3160 dBTP against a true −6.0, inside the standard's own +0.2/−0.4 dBTP tolerance) and it is written
  down instead of rounded away. The bass end of CQT is inherently late (1.24 s at A0) and the plugin
  prints that latency on screen.
- **The chassis exposes three parameters the UI does not show.** `inGain`, `output` and `monoSafe` are
  injected into every plugin in the catalog, so your host will list them. At their defaults the audio is
  bit-exact — that is what the null test verifies.

### Platforms

- macOS 11.0+ · **VST3 + AU**, universal binary (Apple Silicon `arm64` + Intel `x86_64`).
- Windows 10+ · **VST3 x64**, as an unsigned ZIP in the same release (SmartScreen may warn).
- Licence **AGPLv3**, like the whole OVNI catalog.

---

# Diario de construcción (prompts 48 → 57d)

> Todo lo de abajo entró en **0.1.0**. Se conserva sin tocar: cada bloque es lo que reportó su prompt, con
> el detalle técnico y las decisiones que se tomaron. Para leer qué hace el plugin, la entrada de arriba;
> para leer cómo se construyó, esto.

### Cambiado (prompt 57d — VERDICT mide sin tirar para abajo, y FIELD con más luz)

Joaquín probó el 57c en su DAW: «perfecto, me encantó». Quedaban dos pedidos y con ellos se cierra 0.1.0.
El de VERDICT: «que el veredicto sea perfecto, que no tire tan para abajo al proyecto, o que sea más técnico».

- **Lo que mide no cambió; cambió cómo lo cuenta.** Los umbrales de `Rules.h` son los mismos, y los contratos
  también: `VERDICT[sano]` sigue en 0 hallazgos y `VERDICT[archivo]` sigue igual frase por frase.
- **Un titular con la cuenta, sin adjetivos**, fijo bajo la cabecera: «15 checks within range · 3 to look at,
  the first at 0:20». Dentro de rango = las filas de la sección nueva más las cajas en ✓ que tuvieron dato;
  para revisar = ⚠ y ●. Evidencia `headline · n/m`. `VERDICT[titular]`.
- **«Dentro de rango», primero.** Una fila compacta por regla que se evaluó y no se disparó, con el número y el
  límite («Transients have room: PSR 9.4 dB (floor 8.0 dB)»). Viven en `VerdictReport::strengths`, fuera de
  `findings`, así que no cuentan como hallazgos. Con la lente chica de verdad se colapsa a una fila.
  `VERDICT[rango]`.
- **Un pozo que atraviesa varias bandas es UN hallazgo.** El pozo de prueba (400–1 000 Hz sacados entre 0:20 y
  0:35) salía en cinco frases rojas, una por banda; ahora sale en una, ⚠ y no ●: «630 Hz dips 26.0 dB below its
  own average (deepest band of a 400-1000 Hz stretch that dips between 0:20 and 0:35)». El número es el de la
  banda más honda; la ventana, la del tramo entero. El ● queda para lo roto. `VERDICT[hueco]`.
- **Un revisor independiente sobre el commit de VERDICT** encontró cuatro cosas, las cuatro con test: sin un
  segundo analizado el titular contaba «1 para revisar» (la caja del celular sale ⚠ sin dato desde antes del
  57d) y ahora no cuenta nada; la ventana de un hueco fusionado era la de la banda más honda y achicaba el pozo
  cuando las bandas están corridas en el tiempo; `VERDICT[tono]` miraba el tono sólo en inglés y castellano y
  ahora mira las seis lenguas; y la forma breve de `harsh.ok` no tenía test.
- **El número primero, el término de mezcla entre paréntesis y dónde mirar al final** («Check …» / «Revisa …»),
  nunca qué hacer; las seis tablas barridas contra una lista de palabras de opinión (`VERDICT[tono]`). La
  evidencia gris suma los umbrales de la regla: `hole · -25.96 dB / 15 s · rule 6 dB / 10 s`.
- La frase de cuando no hay nada deja de sonar a reproche: «Nothing outside the ranges of these rules. They
  measure; what they can't hear is yours.» Sigue sin decir «listo».
- **FIELD, «suavizarlo un poco más y darle más brillo».** Los mismos datos, pintados de otra manera. La
  grilla de AHORA pasa por **dos** pasadas de [1 2 1]/4 en cada eje antes de extruir ([1 4 6 4 1]/16, σ = 1.0
  celda: 1/64 del recorrido L→R y 1/9.6 de octava), y el relieve gana luz por tres lados: lambert **0.60 +
  0.40** (la cara en sombra sube de 0.45 a 0.60 y la cara a la luz queda igual), el índice de paleta sale de
  **tv^0.8** (los medios suben ~18 puntos de índice; el alfa sigue al nivel crudo) y el piso de alfa pasa de
  **0.16 a 0.22**. `FIELD[brillo]` mide la luma media del plano de AHORA sobre la caché, con la señal de
  `field_wide` a escala 2: **32.25 → 41.13 (×1.275, criterio ≥ 1.20)**. `FieldFrame`, la lectura bajo el
  cursor, `FIELD[lente]` (17 046 px) y la estela quedan como estaban; `FIELD[estela]` 0.006 %, `VISUAL[hd]`
  FIELD 3.60.

### Arreglado (prompt 57c — la segunda vuelta del DAW)

Joaquín volvió a abrir TELESCOPE al lado de Insight: «ha mejorado bastante todo», y cuatro cosas más, con
foto. Ninguna de las cuatro era lo que parecía.

- **FIELD, «montañas y píxeles arriba».** Eran dos defectos con la misma foto. Los píxeles de arriba: la
  lámina de estela se estiraba por **vecino más cercano** desde 48×32 celdas, o sea ~50 px de pantalla por
  celda a escala 2 — rectángulos con borde duro, 31 escalones por fila. Ahora va bilineal, resuelta por
  tramos con una tabla de 256 entradas por plano (dentro de una columna de celda el nivel es una RECTA, así
  que el índice de paleta avanza en 16.16 entero) y a media resolución cuando la escala física es ≥ 2.
  Las montañas: el ruido de la ventana de 0.3 s dibujado crudo como altura. La grilla pasa por un núcleo
  **[1 2 1]/4** en paneo y en frecuencia antes de extruir — es cómo se dibuja, no lo que se mide: el frame,
  la lectura bajo el cursor y las nueve láminas quedan intactos. `FIELD[estela]` mide bordes duros sobre la
  caché: **0.686 % → 0.005 %**, criterio 0.300.
- **SCOPE: el hemisferio pasa a POLAR LEVEL.** «El polar level de Insight es más fino.» El motor siempre
  publicó 360 bins de un grado; el filtro estaba en la lente: un **máximo móvil circular de ±2°** antes de
  dibujar, que convierte cada púa en una meseta plana de 5°. El escalón era del ancho del filtro, no del
  dato. Ahora se dibujan los **181 rayos** con dos capas — promedio en el TIEMPO (τ = 0.3 s, en energía) y
  pico retenido como contorno — y nada se promedia en ángulo. Con mono se enciende **1** rayo de 181; con
  ruido independiente, **181**. El modo se llama POLAR LEVEL y el de puntos POLAR SAMPLE, en los seis
  idiomas (D-34); *hemisferio* queda como nombre del **plegado**, que es otra cosa.
- **TONAL BALANCE, «se corta la línea».** También dos defectos. La **rejilla**: la potencia de banda tomaba
  bins enteros con dos `ceil`, y cuando los dos caían en el mismo entero la banda quedaba vacía teniendo
  energía — la de 40 Hz mide 0.79 de un bin a 4096/48 k, y a 44.1 k el agujero se mudaba a 25 Hz. Ahora
  cada bin aporta en proporción a lo que **se solapa** con la banda: 30 de 30 bandas con medición, y la
  identidad offline = vivo sigue **al bit** (pasa por la misma clase). El **piso del plot**: una referencia
  medible pero veinte decibeles por debajo de −42 LU se dibujaba clampeada al borde — la raya vertical y la
  barra clavada en ±12. Dibujable es ahora crudo ≥ −90 dBFS **y** normalizada dentro del plot; lo que no
  entra corta la curva y el readout dice por qué.
- **LOUDNESS: L y R, y sin espera.** Cuatro barras — MOM y SHORT en LUFS, **L y R en dBTP** del mismo FIR
  del Anexo 2 que ya corría (calculaba los dos canales y se quedaba con el máximo; ahora los guarda). Y
  mientras una ventana se llena se muestra el **parcial**, que es la misma cuenta sobre los hops que hay e
  igual **al bit** al oficial en cuanto se llena. Las dos que había decían "M" y "S": debajo de dos
  medidores de nivel eso se lee mid/side, que es otra magnitud y está en SCOPE.
- **El test de nitidez se ponía verde con el defecto puesto.** Mutando `raster::Cache` de vuelta a 1×,
  `VISUAL[hd]` seguía pasando: medía el panel entero (donde la rejilla y los textos tapan la caché), cinco
  de las seis lentes no pasaban por esa clase, y el criterio (1.50) estaba por debajo del **piso** de su
  propia métrica (2.0). Las tres cosas cerradas: se mide sólo el rectángulo de la caché, las seis usan
  `raster::Cache`, el criterio es 2.75 y además se verifica estructuralmente la escala de horneado. Con la
  mutación: 12 aserciones rojas, dos por lente.

### Arreglado (prompt 57b — la pasada de calidad visual)

Joaquín abrió TELESCOPE en su DAW al lado de iZotope Insight y frenó el lanzamiento con una lista. Esto es
esa lista, con lo que resultó ser cada cosa.

- **«Spectrum se ve viejo, pixelado» y el pixelado de todas las lentes de mapa.** No era el algoritmo ni la
  paleta ni el antialiasing: era el TAMAÑO DEL BUFFER. Las cinco lentes que cachean píxeles
  (SPECTROGRAM, STEREO SPECTROGRAM, WATERFALL, FIELD y la estela de SCOPE) asignaban su `juce::Image` en
  píxeles **lógicos** y la dibujaban 1:1, así que en una pantalla de escala 2 el sistema la estiraba al
  doble y se tiraba la mitad de la resolución del panel. Desde el 57b la caché se hornea a
  `look::physicalScale (g)` (`lenses/Raster.h`), y en los dos sonogramas eso duplica también la resolución
  **temporal**. `VISUAL[hd]` lo mide: la energía del gradiente del render a 2× contra el render a 1×
  ampliado da entre **2.9 y 4.7** contra un criterio de 1.5.
- **SPECTRUM dejó de dibujarse con `fillRect` de 1 px por columna.** Rasterizador propio a escala física:
  interpolación Hermite **monótona** a resolución de dispositivo (la que no inventa sobrepicos en graves,
  donde un bin abarca varias columnas), suavizado de pantalla estilo SPAN (OFF · 1/24 · 1/12 · 1/6 oct,
  default 1/12, que **no** toca el readout ni el peak hold), trazo con cobertura vertical exacta, glow
  barato y relleno degradado desde la curva. Salió **más barato** que lo que reemplaza: 1.20 → 1.03 ms.
- **«Spectrograma no se entiende, todo el mismo color».** Había UNA rampa y era de un solo tono, así que el
  nivel sólo podía codificarse con el brillo — y el ojo distingue muchos menos escalones de brillo que de
  tono. Ahora hay **cuatro** rampas elegibles y compartidas por las cuatro lentes de nivel (`ovni`,
  `inferno`, `viridis`, `spectrum`; las dos del medio son las tablas exactas de matplotlib, CC0).
- **«Waterfall: color más profesional y por default muy inclinado».** El color de cada punto sale ahora de
  la rampa según **su** nivel (antes toda una línea era de un color, así que el color no decía nada), más
  niebla de profundidad, suelo con las décadas proyectadas y líneas suavizadas 1/12 oct. Abre en la
  inclinación más pronunciada.
- **«Field: no se nota lo 3D».** El plano de adelante dejó de ser una lámina coloreada: el nivel **levanta**
  la superficie y la superficie se ilumina con su propia normal, con oclusión por horizonte. Las filas se
  comprimen para que el relieve entre en la caja, y el eje de frecuencia usa la misma cuenta.
- **«En polar hay una S a la derecha que no va».** Estaba sola (el lado negativo no se rotulaba). Se fue.
- **«En el hemisferio la base está al medio y marca todo 0 donde Insight marca bien».** Dos cosas: la base
  pasa al PIE del panel y lo que está fuera de fase se **pliega** sobre ella (θ' = 360° − θ) con su
  porcentaje al lado; y el radio pasa a ser **amplitud relativa a la dirección más fuerte** — la escala
  anterior mapeaba −60…0 dB sobre el radio, así que con música real todas las direcciones caían entre 0.67
  y 1.0 y la envolvente se pegaba al arco exterior.
- **«ANCHO / BALANCE / PÉRDIDA MONO sin valor».** En modo hemisferio esos tres números **no se dibujaban**.
  Ahora sí, con el porcentaje fuera de fase como cuarto. (La correlación de 0.07 de la foto **no** era un
  bug: con mono el motor da +1.0000 exacto; el número dibujado tarda 0.43 s en llegar y una señal
  decorrelacionada de verdad da +0.09.)
- **«Tonal Balance: se rompen las líneas».** Una banda contaba como medida con `> −200 dB`, que es un piso
  de guarda y no un nivel: una banda a −123 dBFS —el redondeo del propio análisis— pasaba por medición, la
  curva la dibujaba cien decibeles por debajo del plot y el delta contra ella daba +106 dB. El criterio
  pasa a **−90 dBFS sobre la banda cruda**, más "al menos un bin a la resolución vigente".
- **El pulido v2 en LAS TRECE**, no sólo en las que se miraron ese día: cuarenta y una hairlines de
  `fillRect` con enteros convertidas a una que **snapea al píxel físico**, ningún relleno de dato plano,
  glow barato en la línea principal de cada lente, el teclado de CQT que se enciende con su cromagrama, el
  osciloscopio de SCOPE que dejó de ser una escalera y el resumen de BAND CORRELATION como curva. Lo
  sostiene `VISUAL[v2]`, un barrido de fuentes sobre las trece lentes.

### Cambiado (prompt 56c)

- **FIELD recorre celdas y no pantalla, y gana margen de presupuesto.** En las tres láminas de estela el
  valor de un píxel es exactamente el de *una* celda (sin bilineal los dos pesos valen cero), así que el
  bucle le preguntaba a cada uno de los ~600 000 píxeles de un plano a qué celda pertenecía para repetir la
  misma respuesta cientos de veces. Ahora se arman los tramos de píxeles de cada celda una vez por plano y
  se recorren las 32×48 celdas: **3.615 → 1.515 ms** de mediana en tamaño L, **sin cambiar un solo píxel**
  (los cuatro snapshots salen byte-idénticos). `BUDGET_FIELD` exige además mediana típica ≤ 3.2 ms — la
  auditoría del 56b la puso roja dos veces sin que el código cambiara, con la mediana pegada al criterio.
  Se descartaron las otras dos variantes: bajar de 3 a 2 láminas entra apenas (2.875) y además pierde
  profundidad; hacer las dos cosas gana 0.08 ms sobre ésta y paga la misma lámina.
- **El piso de la superficie de FIELD sube de 0.11 a 0.16.** En tamaño M con señal angosta quedaba al borde
  de lo visible y la lente volvía a leerse como un recorte flotando. Cambia el 37 % de los píxeles con un
  delta medio de 4 sobre 255: es un empujón, no un cambio de aspecto. La plata salió del blit por celdas.

### Arreglado (prompt 56c)

- **El idioma se aplicaba en el hilo que lo escribió.** `juce::ValueTree::Listener` notifica de forma
  síncrona, así que un preset cargado desde el hilo de automatización del host hacía que el editor tocara
  la tira y pidiera repaint desde ese hilo. Ahora pasa por el message thread con `SafePointer`, como ya
  hacía `parameterChanged`.
- **El idioma que restaura el host no llegaba al editor abierto.** `apvts.replaceState()` hace
  `state = newState`, y eso avisa por `valueTreeRedirected`, no por `valueTreePropertyChanged`. El editor
  sólo escuchaba lo segundo: el árbol decía "de" y la tira seguía en inglés, con 0 píxeles de cambio.
- **VERDICT decía la tónica en inglés siempre.** Tenía un array local de letras adentro de `paintHead()`,
  así que la misma tonalidad salía "Do menor" en CQT y "C menor" en VERDICT, en la misma pantalla y en
  cinco de los seis idiomas. Ahora las dos usan `keyLabel()`.
- **Faltaba la convención alemana de nombres de nota.** `Table::solfege` era una bandera de dos estados y
  el alemán la tenía en `false`, o sea que compartía el array del inglés: el plugin le mostraba **B** donde
  un alemán lee **H**, y **A#** donde lee **B**. En esa convención "B" *es* el Si bemol, así que la
  pantalla nombraba otra nota, un semitono arriba. Pasa a `NoteNaming { letters, solfege, german }`.
- **`DeviceProfiles.h` se contradecía a sí mismo.** El 56b sacó el array de curvas y escribió "acá no hay
  curvas", pero dejó la cabecera del archivo describiendo las cajas como curvas genéricas y citando un pie
  que ya no se usa. Se reescribió, y se sacó `kNumBands`, el último resto del array. Apareció la misma
  promesa en el diccionario: cuatro reglas por dispositivo daban como *fuente* de su umbral "curva genérica
  de …"; ninguna sale de una curva.
- **La tira apretaba distinto de como dibujaba.** `paint()` repartía las 13 filas con un piso de
  `kNumLenses` y `rowAt()` —el que decide qué lente eligió el clic— con un piso de 0. En los tamaños reales
  las dos daban el mismo número, así que nadie lo vio; por debajo de 39 px de alto el desfase es total.
  Ahora las dos salen de `listHeight()`.
- **El barrido de acentos no veía los escapes.** Un rótulo escrito `"a\xc3\xb1o"` —que es como este mismo
  repo escribe el punto medio y el em-dash— atravesaba entero el test que dice "ningún literal lleva un
  acento castellano". Ahora se decodifican los `\xHH` antes de mirarlos.
- **La cobertura de FIELD sólo tocaba los extremos de la estela.** El bug del 56b aparecía en los valores
  intermedios de `trailLayers` y no había test que los mirara: ahora se recorren los nueve inyectando
  `trailCount`. Reintroduciendo el bug, los valores 5, 7 y 8 quedan sin plano de presente.

### Agregado

- **Selector de idioma en la tira (prompt 56b, D-50)** — uno solo, al pie de la tira de lentes, con el
  endónimo del idioma ("Español", "Deutsch") y no el código. Reemplaza al botón LANGUAGE que vivía adentro
  de VERDICT: el idioma es del plugin, no de esa lente. El editor escucha la propiedad en el ValueTree, así
  que repintan la tira y la lente visible venga el cambio del chip, de un preset o del estado que restaura
  el host.
- **Las trece lentes rotulan desde `Strings.h`** — hasta el 56 sólo lo hacían SCOPE, FIELD y la tira, y las
  otras diez dibujaban literales en castellano: el idioma cambiaba tres lentes de trece. 17 claves nuevas
  × 6 idiomas (140 de 140 en cada tabla) y los rótulos de UI de VERDICT en `rules::`. Lo sostiene un test
  que lee los 35 archivos que dibujan; sobre el commit anterior encuentra 41 infracciones.
- **Los nombres de nota siguen la convención del idioma** — letras (C D E) o solfeo (Do Re Mi) según la
  tabla, y el sufijo mayor/menor sale de la misma tabla que usa VERDICT.

### Arreglado

- **FIELD no dibujaba el plano de AHORA.** Con la estela saturada en 8 láminas y un paso de 3, el bucle de
  capas iba 8 → 5 → 2 → −1 y nunca visitaba `layer == 0`: en uso normal se veían tres parches de estela
  sueltos y el dato del presente no se dibujaba nunca (0 celdas vivas; ahora 2 754). Nada lo atrapaba
  porque el conteo de celdas lo cumplen las estelas solas.
- **VERDICT entra al sistema visual y envuelve el texto de verdad.** Tokens de `Look.h`, métricas por
  tamaño, jerarquía de separadores, cabecera tabular; y un `juce::TextLayout` en vez de
  `drawFittedText (…, 3)`, que a partir de la tercera línea aprieta los glifos en vez de envolver
  (3 renglones donde el texto pedía 4, medido). Cero píxeles de texto en la columna de margen.
- **Tres nits de oído de productor**: la mitad de alerta del correlímetro del hemisferio ahora es
  proporcional a la pérdida mono medida y no un alpha fijo (0 px con L = R, 5 457 con L = −R); los rótulos
  de tiempo del waterfall dejan de dejar ver el relleno por detrás; los rótulos de octava de SPIRAL se leen
  siempre.
- **`DeviceProfiles.h` tenía una curva de 30 dB por dispositivo que ningún chequeo leía** (D-47): se saca.
  El pie del informe, el README y el diccionario dicen ahora "chequeos por dispositivo genéricos" en vez de
  "curvas genéricas", que prometía algo que no existía.
- Diez LOW más: `hi-fi.bad` con singular y plural, el techo de 0 dB en `stereoFrom`, el default 128 del
  grupo vacío en STEREO SPECTROGRAM, el aviso de referencia de TONAL BALANCE que ya no tapa el estado, y
  cinco comentarios que decían otra cosa que el código.

### Agregado (prompt 55)

- **VERDICT (lente 13, prompt 55)** — la única que no muestra: **dice**. Motor de reglas determinista
  (D-47): sin IA, sin red, 21 reglas en tres secciones (cómo se va a sentir · dónde traduce · qué falta y
  dónde), y **cada frase con el número que la sostiene y el id de la regla que la produjo**. Modo EN VIVO
  (desde el RESET) y modo ARCHIVO con drag & drop, que da los tiempos exactos. Pie fijo: *"Medición, no
  gusto. Curvas de dispositivos genéricas. Rehacé el análisis tras cada cambio."*
- **Seis idiomas (D-50)** — `language` es un código ISO 639-1 con **default `en`**; tabla por idioma con
  fallback a inglés por clave, y agregar un idioma es agregar una tabla. `en` y `es` revisados; `pt`,
  `fr`, `de` e `it` **pendientes de revisión de hablante nativo**.
- **`docs/telescope-diccionario.md`** (dentro de `plugins/telescope/`) — el gemelo legible de la tabla de reglas: cada umbral con su porqué
  y con lo que la regla NO hace.
- **Historia por segundo** (`analysis/SecondHistory.h`) — 10 minutos a 1 Hz desde el RESET: 30 bandas con
  nivel, correlación y pérdida al monoficar; loudness, picos y clips del segundo; estéreo de banda ancha;
  continua; tonalidad. **Las filas del archivo son las del vivo, iguales al bit** (60 de 60 sobre un tema
  de un minuto), y no dependen del tamaño de bloque (1 / 7 / 64 / 4 096).
- **Continua (DC)** en el medidor de loudness, medida sobre la señal cruda (el filtro K tiene un pasa-altos
  de 38 Hz que se comería justo lo que se quiere ver). Viaja en el `AnalysisFrame` junto con
  `secondsAnalysed`.
- `Spectrum::FrameInfo` publica la **posición del frame en el stream**, que es lo que permite repartir
  cada frame en el segundo que le corresponde y no en "el que estaba abierto cuando llegó".

### Corregido

- **La referencia de TONAL BALANCE medía con la FFT de la lente SPECTRUM** (MEDIUM del revisor del 54).
  Tocar el tamaño de FFT hacía que las dos curvas de la comparación quedaran medidas con dos resoluciones
  distintas y nada lo decía. Ahora el módulo tiene su propia instancia clavada en los settings del
  análisis de archivo: 29 de 29 bandas iguales al bit entre orden 12/Hann/75 % y orden 15/BH4/87.5 %/M
  (antes: 0 de 29, con hasta 1.54 dB de diferencia).
- **El horizonte de WATERFALL se bajaba con `y` en vez de con `top`** (MEDIUM del revisor del 53). Con un
  escalón espectral, una lámina lejana podía repintar sobre una cercana. Verificado píxel a píxel contra
  el algoritmo del pintor: 79 píxeles distintos antes, 0 después.
- **`out-of-phase` exigía sólo el signo de la correlación**, así que material simplemente decorrelacionado
  (una reverb ancha, dobles a los costados) lo disparaba la mitad del tiempo sin que hubiera nada roto.
  Ahora exige además que la correlación **media** de la banda sea ≤ −0.2. Sobre la señal de prueba, el
  informe pasó de 42 hallazgos a 13: los 29 que se fueron eran falsos positivos.
- **El aviso y el error de la referencia sobrevivían a quitarla** (LOW 3 del revisor del 54): un cartel
  verdadero sobre un archivo que ya no está es un cartel falso.
- `FieldFrame::rowForHz` incluye el borde exacto de 20 kHz; el piso de faldas del CQT suma una cota fija a
  la derivada del umbral; `writePng` queda en una sola copia; y el README dice lo que los tests exigen,
  no sólo lo que se midió.

### Agregado (prompts 48 - 54)

- **Chasis del plugin** (`Tlsc` / `Ovni`, VST3 · AU · Standalone). Categoría VST3 `Fx Analyzer`: TELESCOPE
  mide, no procesa, y el host lo agrupa con los medidores. Se registra solo por el GLOB del CMake raíz.
- **Exe de tests propio** `OvniTelescopeTests` (`ctest -R telescope`), espejando el bloque de
  `OvniSupernovaTests` sin tocar `tests/CMakeLists.txt` del raíz.
- **Pass-through bit-exacto**: `processAudio` lee L/R y no escribe una sola muestra. Latencia 0, cola 0.
- **Parámetros**: `bypass`, `lens` (las 13 lentes del diseño desde el día uno, índice congelado),
  `target` (7 plataformas), `preset` (4 vistas de fábrica).
- **Motor de análisis**: `AnalysisBus` estéreo lock-free (2 s a 192 kHz por canal, drop-on-full con
  contador), `AnalysisThread` que arma hops de 100 ms y corre sólo los módulos habilitados (lente a
  demanda), `TripleBuffer` latest-wins y `LoudnessHistory` (10 min a 10 Hz).
- **K-weighting** (BS.1770 Anexo 1) recalculado por sample rate desde el prototipo analógico; reproduce la
  tabla publicada a 48 kHz con error 8.9e-16.
- **True-peak** (BS.1770 Anexo 2) con la **tabla literal** de los 48 coeficientes del documento de la ITU.
  4× hasta 88.2 kHz, 2× de 96 a 176.4, 1× de 192 en adelante.
- **Medidor de loudness**: M / S / I con doble compuerta, LRA de Tech 3342, máximos desde el último reset.
  Determinista: la misma señal da los mismos números al bit venga en bloques de 1 o de 4096.
- **Lente LOUDNESS** con los tres números, medidores de barra con escala −60…0 LUFS, historia de 3 min,
  objetivo por plataforma con su delta, y botones RESET / PAUSE.
- **Tira de las 13 lentes**: las 12 que no existen van apagadas con su nombre y su punto, sin promesas.
- **Aviso de análisis atrasado** cuando el bus tuvo que descartar muestras.
- **Módulo `Stereo` de banda ancha** (`kStereo`, spec §5.3): las cinco sumas por hop
  (ΣLL ΣRR ΣLR ΣMM ΣSS) en `double` sobre una ventana deslizante de 100 / 300 / 1000 ms, y de ahí
  `corr`, `width`, `balanceDb` y `monoLossDb`. Misma matemática y mismo ruido rosa determinista que
  `orbita/tests/StereoMeasure.cpp`, así los números son comparables entre plugins del sello. Casos borde
  definidos y sin NaN: sin señal → 0 y "sin señal"; un canal mudo → `corr` 0 (no definida); L = −R →
  `width` al tope 10 y `monoLoss` al piso −60; balance clampeado a ±60.
- **`ScopeFrame`** por un segundo `TripleBuffer`: goniómetro (2 048 pares (L,R) del hop, decimados
  uniformemente sobre el hop entero) y osciloscopio (últimos 40 ms de M/L/R) con trigger en el primer
  cruce por cero ascendente. Va aparte del `AnalysisFrame` porque son ~40 KB que sólo SCOPE dibuja.
- **Lente SCOPE**: goniómetro Lissajous rotado 45° con estela de fósforo (se atenúa cada frame, nunca
  acumula) y modo polar, correlímetro −1…+1 con la mitad negativa en el color de alerta, lectura de
  WIDTH / BALANCE / MONO LOSS, osciloscopio de 40 ms y selector de ventana.
- **Datos de DYNAMICS** en el mismo medidor de loudness: `truePeakHop`, PSR (TP de 3 s − short-term),
  PLR (TP máx − integrado, AES TD1004), histograma de short-term en 61 bins de 1 LU, y eventos de clip
  sobre un umbral configurable en dBTP con `ClipHistory` (ring de 1 Hz × 10 min).
- **Lente DYNAMICS**: PSR héroe con la línea de referencia de 8 dB rotulada (referencia, no veredicto),
  PLR, histograma con el bin actual resaltado, contador de clips, knob de umbral (−3…0 dBTP) y la línea
  de tiempo de 10 minutos.
- **Settings de lente en el ValueTree del APVTS** (`stereoWindowMs`, `scopePolar`, `scopeTrigger`,
  `clipThresholdDbtp`): persisten con el estado y los presets y el host no los lista ni los automatiza.
  `setStateInformation` los vuelve a empujar al motor, no sólo al árbol.
- **`tests/TestSignals.h`** con el ruido rosa de la casa y **`tests/TestHelpers.h`** con esperas por
  condición (`waitUntil`, `waitStable`).

### Corregido

- `TripleBuffer` (copia de SUPERNOVA): el original hacía que el productor leyera `readIdx`, una variable
  no atómica del consumidor, y podía escribir encima del slot que el otro estaba leyendo — el consumidor
  veía un frame con campos nuevos y viejos mezclados. Reemplazado por el esquema canónico de intercambio.
  **SUPERNOVA sigue con la versión original y está publicado**: reportado a la auditora.
- **M-1 (revisor del 48) · use-after-free latente al re-preparar.** `AnalysisThread::stop()` descartaba
  el retorno de `stopThread(1000)`, y `prepare()` lo llama y **acto seguido** reasigna `chunkL/R` y
  `hopL/R` — justo los vectores que el worker escribe. Si el worker no salía en ese segundo, la
  reasignación corrompía memoria dentro del DAW, en silencio (en release no crashea). Ahora `stop()`
  devuelve si salió, la gracia sube a 2 s, y si no salió hay `jassert` + `stopThread(-1)`: un cuelgue
  honesto, con el stack del worker a la vista, es infinitamente mejor que memoria corrupta. El camino de
  re-preparar **no tenía cobertura ninguna** — ningún test llamaba `prepareToPlay` dos veces sobre la
  misma instancia, que es lo que hace todo DAW al cambiar el device; ahora lo cubre `[lifecycle]`.
- **L-2 · `-fno-fast-math` explícito** en el plugin y en el exe de tests. Verificado que el build usaba
  sólo `-O3` y ninguna bandera de matemática relajada; queda escrito para que nadie las agregue "para
  acelerar" y rompa las comparaciones al bit de `[null]`, `casa-4`, `[stereo]` y `[dyn]`.
- **L-3 · esperas deterministas** (`tests/TestHelpers.h`): los tests esperaban con `sleep` de tiempo fijo
  y bajo carga se ponían rojos sin que nada estuviera roto. Al cambiarlos aparecieron **dos carreras
  reales del 48**: un test esperaba **un** hop y afirmaba que la historia tenía **seis** (el sleep de
  5 ms del sondeo viejo le daba ventaja al worker y tapaba el agujero), y el editor abría la lente
  leyendo `getParameterAsValue()`, que el APVTS vuelca al ValueTree en diferido — si el host ya había
  dejado `lens` en otra lente antes de abrir la ventana, el editor abría la equivocada.
- **Layout de SCOPE**: `zonesFor` leía `body.getX()` **después** de mutarlo con `removeFromLeft`, así que
  el goniómetro se dibujaba encima del panel de correlación.

### Verificado

- `[null]` `NULL_MISMATCHES=0` en default, bypass ON, las 13 lentes, bloques 1/7/64/4096 y fuente mono.
- `[ebu]` los 17 renglones de la tabla EBU, todos OK (3341 tests 1-5, 3342 tests 1-4, y 4 de casa).
- `[kw]` tabla a 48 kHz con error 8.9e-16; +0.6910 dB a 997 Hz, +4.0419 a 10 kHz, −5.9729 a 38.13 Hz.
- `[tp]` 16 fases dentro de 0.009 dB; fs/4 a 45° lee −5.9553 dBTP con pico de muestra −9.0103; los
  vectores 15-19 de Tech 3341 dentro del +0.2/−0.4 del documento.
- `[budget]` pintado de las tres lentes (criterio 4 / 8 ms): LOUDNESS 0.536 / 0.601 ·
  SCOPE 0.577 / 0.595 (con los 2 048 puntos del goniómetro) · DYNAMICS 0.355 / 0.371 (medida con
  `paintEntireComponent`: el knob de umbral es un componente hijo y forma parte de la lente).
- `[chain]` el bus descarta y cuenta; pause congela; reset invalida; el thread para en 2.5 ms.
- `[lifecycle]` 20 ciclos de 44.1k/512 → 48k/64 → 96k/4096 → releaseResources → 48k/512 sobre la misma
  instancia, midiendo −23 LUFS ±0.1 después de cada cambio y con `droppedSamples` en 0.
- `[stereo]` tabla `STEREO[...]`: L=R → corr +1.0000, width 0.0000, bal +0.000, mono +0.000 ·
  L=−R → corr −1.0000, mono −60.000 (clamp) · independientes → corr +0.0237, width 0.9766, mono −2.909 ·
  sólo L → corr 0, bal −60.000, width 1.0000 · silencio → todo 0 y finito ·
  ventana 1000 ms |corr| peor 0.2807 (< 0.30) y ventana 100 ms alternando entre > +0.9 y < −0.9 sin
  ningún valor en el medio · `ScopeFrame` xyCount 2048, oscCount 1920, trigger 480 ·
  bloques 1/7/64/4096 por el processor real: los cuatro números **idénticos al bit**.
- `[dyn]` tabla `DYN[...]`: seno 997 Hz −6 dBFS → TP −5.991, S = I = −6.000, PSR +0.009, PLR +0.009 ·
  10 ráfagas de 50 ms a −0.5 dBFS sobre lecho rosa a −40 → 10 eventos con umbral −1.0 dBTP y 0 con 0.0,
  ring de 1 Hz con exactamente 10 segundos de 1 evento · histograma 20 s a −20 + 20 s a −30 → bins 174 y
  171 y Σ bins = 371 = hops con S válido · reset borra todo · bloques 1/7/64/4096 idénticos al bit.
- `[settings]` round-trip de los cuatro settings por get/setStateInformation, que `setStateInformation`
  los empuja al **motor** y no sólo al árbol, y la lente a demanda (LOUDNESS y DYNAMICS piden `kLoudness`
  y no `kStereo`; SCOPE al revés, y `kStereo` se apaga al salir de SCOPE).

- **Módulo `Spectrum`** (`kSpectrum`, spec §5.2): FFT de 1 024 a 32 768, ventanas Hann / Blackman-Harris 4 /
  Kaiser β = 9 (periódicas), solapes 50 / 75 / 87.5 %, canales L · R · M · S · L+R, promediado
  (ninguno / exponencial / infinito), peak hold con caída configurable, bandas de ⅓ de octava (ISO 266,
  30 centros) y Bark (Zwicker, 24 bandas). Referencia de dB **dB_k = 20·log10(2·|X_k| / (N·CG))**: un seno
  centrado en un bin lee exactamente su amplitud, con las tres ventanas y sin ajustes.
- **Cadencia propia del espectro.** El `AnalysisFrame` sigue saliendo por hops de 100 ms (10 Hz), que es lo
  que necesita un medidor de loudness y es la muerte de un analizador de espectro. El módulo `Spectrum`
  come del **chunk** que se drena del bus (cada ≤ 2 ms) y decide sus propias posiciones de frame contando
  muestras desde el reset. Los dos caminos comen las mismas muestras en el mismo orden: `[ebu] casa-4` y
  `[stereo] bloque` siguen al bit. Medido con reloj de pared: **44 frames distintos en 1 s**, motor
  emitiendo a **46.88 Hz**.
- **`SpectrumFrame`** por su propio `TripleBuffer` (~260 KB por slot; se copia sólo el prefijo vivo) y
  **emisión decimada por un entero**, nunca por encima de 60 Hz.
- **Lente SPECTRUM**: eje log 20 Hz–20 kHz con rejilla de décadas y ⅓ de octava, eje de dB con el rango
  elegido (60 / 90 / 120), área rellena + línea (dos colores del tema en L+R), peak hold como línea fina,
  modos ⅓ de octava y Bark en barras del ancho real de cada banda, lectura al pasar el mouse con
  frecuencia, **nota y cents** (A4 = 440) y el dB de lo que hay debajo, y nueve controles que ciclan,
  agrupados en análisis (arriba) y presentación (abajo).
- **`SpectrogramRing`** (1.8 MB fijos: 60 s × 60 columnas/s × 512 filas) con un solo escritor e índice
  atómico publicado después de copiar los bytes, y la **columna de espectrograma** en el módulo: 512 filas
  log de 20 Hz a 20 kHz, dB mapeado a 0–255 sobre `[−rango, 0]`, potencia **instantánea** (nunca la
  promediada) y máximo de los bins por celda con interpolación sólo donde la celda no tiene ninguno.
- **Lente SPECTROGRAM**: sólo se pintan las columnas nuevas (la imagen se corre con `moveImageSection` y lo
  nuevo entra por la derecha, con escritura directa de ARGB), paleta de 256 entradas armada desde el Theme,
  eje de frecuencia log con marcas al costado, eje de tiempo rotulado con lo que realmente se ve, lectura
  al pasar el mouse (tiempo, frecuencia, dB) y controles de historia / rango / canal.
- **`kLoudness` siempre encendido** (`kAlwaysOnModules`): el integrado, el LRA, el histograma y el contador
  de clips acumulan desde el reset, y apagarlos mientras se mira otra lente les dejaría un agujero
  invisible. La lente a demanda sigue valiendo para los módulos caros.
- **Los módulos con ventana deslizante se limpian al reactivarse.** Volver a SCOPE ya no mezcla hasta 1 s de
  audio de antes de apagarse: medido +0.83 antes del arreglo, **−1.00** ahora.
- Tests de **reduced-motion para SCOPE, DYNAMICS y SPECTRUM** con un helper genérico que avanza **y pinta**
  cada frame (sin pintar frame a frame la estela de fósforo nunca existe y el test no podría notar si
  alguien le saca el reduced-motion). Verificado con dientes: sacar el `if (prefersReducedMotion())` de
  cualquiera de las tres pone el test en rojo.
- Con reduced-motion la auto-escala del goniómetro también salta; el **espectrograma sigue corriendo**
  (su eje X es el tiempo: congelarlo sería dejar de mostrar el dato).
- `DynamicsLens::advanceFrame()` reporta cambio también por clips, histograma, bin actual y PLR.
- `AnalysisThread::stop()` loguea antes de esperar sin plazo (en release el `jassert` no existe).

### Verificado (prompt 50)

- `[spectrum]` tabla `SPEC[...]`: bin exacto **−20.0000 dB** con las tres ventanas · scalloping 0.63 dB
  (Hann) y 0.37 (BH4), topes 1.42 / 0.83 · Parseval **+0.049 %** · ventanas medidas por su propia DTFT:
  Hann 4.00 bins / −31.5 dB, BH4 8.00 / −92.0, Kaiser β9 6.06 / −66.3 · resolución 2 picos con orden 15 y
  1 con orden 10 · ⅓ de octava sobre el rosa de la casa peor desvío **0.259 dB** y sobre el blanco
  **+1.0078 dB por banda** (teórico +1.003) · slope 3 sobre el rosa: pendiente **−0.0072 dB/oct** ·
  peak hold **12.032 dB/s** · promedio exponencial **61.4 %** en τ · L = R da S en el piso, sólo L da R en
  el piso y M a −6.0206 dB · frame 20 **idéntico al bit** en bloques de 1, 7, 64 y 4 096.
- `[spectrogram]` tabla `SGRAM[...]`: barrido log 100 Hz → 10 kHz con Spearman **0.99998** y **100 %** de
  pares no decrecientes · silencio con máximo **0** · tono de −20 dBFS con rango 90 → **198** exacto ·
  anillo de 469 columnas (46.875 col/s × 10 s) con la columna 0 en el tono que corresponde ·
  determinismo **byte a byte**, también entre bloques de 512 y de 64.
- `[budget]`: `BUDGET_SPECTRUM` **1.4–3.3 ms** de mediana en el peor caso (orden 15, dos espectros, hold),
  según la carga de la máquina; `BUDGET_SPECTROGRAM` 0.4–0.9 ms con columnas nuevas y 1.2–2.4 ms de
  redibujo completo a ancho lleno. Criterio 4 / 8 ms para todos.
- **Módulo `StereoBands`** (`kStereoBands`, spec §5.3): el estéreo POR BANDA DE FRECUENCIA. Las mismas
  cinco sumas del módulo `Stereo` llevadas a los bins de la STFT (Parseval) para cada una de las 30 bandas
  de ⅓ de octava, sobre una ventana deslizante de 0.3 / 1 / 3 s. `ΣMM` y `ΣSS` no se guardan: salen de las
  otras tres. Mismos casos borde y mismos clamps que el de banda ancha, en `double`, sin un solo NaN.
  Cruzado contra el medidor del tiempo: **Δcorr 0.00076** y **Δmono 0.0033 dB** (criterios 0.02 / 0.1).
- **Coherencia por bin** (`StereoBandsFrame`, por su propio `TripleBuffer`): `Σ Re(L·R*)/√(ΣLL·ΣRR)`
  suavizada sobre la misma ventana, 0 **por definición** cuando un canal no tiene energía en ese bin.
- **`Spectrum::FrameSink`**: el consumidor de todos los frames CALCULADOS (no sólo los emitidos), que
  recibe los complejos de L y R ya transformados. El estéreo por banda no rehace una FFT que ya está
  hecha; con un sink enganchado el módulo calcula siempre L y R, aunque la lente de espectro mire otro
  canal. Sin sink no se paga nada.
- **`SpectrogramRing` pasa a plantilla por bytes por celda**: 1 (el sonograma de nivel, sin un cambio de
  comportamiento) y 2 (el estéreo: coherencia + energía). Plantilla y no campo, para que el almacenamiento
  siga siendo un array fijo — `configure()` corre en el worker mientras la UI lee.
- **Lente BAND CORRELATION** (la 9): 30 barras bipolares de correlación por ⅓ de octava sobre el eje log,
  con la mitad negativa teñida, líneas de referencia rotuladas en +0.5 / 0 / −0.5, y una segunda fila
  seleccionable (MONO LOSS 0…−12 dB, WIDTH 0…2, BALANCE ±12 dB). Resumen en texto de la banda más fuera de
  fase y la de mayor pérdida mono; lectura al pasar el cursor con los cuatro números y **cuántos bins**
  midieron la banda. Una banda sin ningún bin se marca distinto, nunca en cero.
- **Lente STEREO SPECTROGRAM** (la 10): el sonograma donde el **color es la fase** (rojo fuera de fase ·
  verde ancho · blanco mono) y el **brillo es el nivel**, con leyenda fija y lectura de tiempo, frecuencia,
  coherencia y dB. Paleta bipolar de 256×256 armada del tema del sello.
- **`lenses/SpectrogramScroll.h`**: el mapeo tiempo→píxel (Bresenham), el mapeo fila→píxel, el registro de
  fuentes de la lectura y el desplazamiento incremental de la imagen, compartidos por las dos lentes de
  sonograma. La lente 4 se refactorizó encima sin mover un byte de sus tests.
- **Settings de lente** `bandsWindowIndex` (0.3 / 1 / 3 s) y `bandsRow`, con su round-trip de estado.
- **Módulo `Cqt`** (`kCqt`, spec §5.4): el análisis MUSICAL. Transformada de **Q constante** (Brown 1991;
  Brown & Puckette 1992) con `B = 24` bins por octava desde A0 = 27.5 Hz hasta `min(20 kHz, 0.45·fs)` —229
  bins a 48 kHz—, `Q = 34.127` y una ventana de Hann propia por bin (`N_k = round(Q·fs/f_k)`). **Kernels
  dispersos**: una sola FFT de 2^16 por frame y un producto interno por bin, guardando sólo los
  coeficientes por encima de `0.0054·max` (medido: 187 930 coeficientes, **2.50 %** del denso, armados en
  102 ms una única vez, en el worker y sólo cuando la lente está a la vista). Referencia de dB idéntica a
  la de SPECTRUM, con la ganancia coherente calculada **por bin**: un seno de −20 dBFS lee −19.998 / −19.997
  / −20.000 dB en A0, A4 y A7. Frames a hop de 100 ms sobre un ring propio, con las posiciones contadas
  desde el reset: el frame 20 sale **idéntico al bit** en bloques de 1, 7, 64 y 4 096.
- **El soporte del kernel va pegado al FINAL del bloque**, no al centro: es lo que hace que la latencia sea
  la que se declara. Centrados, todos los bins mirarían 0.68 s en el pasado; pegados al final, cada bin ve
  las últimas `N_k` muestras — 1.2410 s el de A0, 0.0194 s el de A6 — y sólo el grave paga su ventana.
  `lowestBinLatencySec` viaja en el frame y se rotula en el pie de las dos lentes.
- **Cromagrama** (12 clases): potencia por clase de nota sumada sobre todas las octavas, normalizada al
  máximo, con versión suavizada sobre 0.5 / 2 / 5 s. La clase del bin `k` es `(⌊k·12/B⌋ + 9) mod 12`.
- **Tonalidad estimada** (Krumhansl-Schmuckler): correlación de Pearson del cromagrama suavizado contra los
  24 perfiles de **Krumhansl & Kessler (1982)** rotados. Salida = tónica + modo + **confianza** + **% del
  tiempo** desde el reset en que esa tonalidad fue la mejor. Los cuatro números viajan también en el
  `AnalysisFrame` (16 bytes), que es de donde los leerá VERDICT.
- **Lente CQT** (la 6): el espectro por notas, con marca en cada Do, **teclado de 114 teclas** a escala
  debajo del eje, peak hold (mismo setting que SPECTRUM), lectura de nota + octava + **cents** + dB, y el
  cromagrama en doce barras con la tónica en ámbar y la tonalidad con sus dos números.
- **Lente SPIRAL** (la 7): el mismo constant-Q enrollado, una vuelta por octava, con las notas iguales
  alineadas sobre el mismo radio. Rueda de croma en el centro (doce sectores en el mismo ángulo que las
  púas) y lectura que toma la octava del radio y la clase del ángulo.
- **`lenses/NoteName.h`**: la fórmula de nota + cents (`cents = 1200·log2(f/f_nota)`, A4 = 440,
  numeración científica) se mudó de `SpectrumLens` a un helper común ahora que la piden dos lentes más.
  `SpectrumLens::noteFor` queda como el nombre por el que ya la llamaban su test y la propia lente.
- **Settings de lente** `cqtChannel` (L / R / M) y `cqtChromaSec` (0.5 / 2 / 5 s), con su round-trip de
  estado. El decaimiento del peak hold **no** es un setting propio: es el mismo de SPECTRUM.
- `[cqt]` tabla `CQT[...]`: nivel A0/A4/A7 **−19.998 / −19.997 / −20.000 dB** · medio bin fuera de centro
  pierde **1.361 dB** (scalloping de Hann derivado 1.424) · un semitono da **2** máximos y 25 cents da
  **1** · tríada de Do mayor → `Do = Mi = Sol = 1.000` con los vecinos en 0.196 (derivado 0.20) · cinco A
  en cinco octavas → `La = 1.000`, `Sol# = 0.196`, el resto < 0.005 · tonalidad Do mayor **0.845 / 98.3 %**
  y La menor **0.868 / 100 %** · cruce con el FFT del 50 sobre el mismo seno de 1 kHz: **0.016 dB** ·
  latencia declarada 1.2410 s y 90 % del nivel alcanzado a 1.00 s (derivado 0.92) · piso de faldas de la
  poda **67.5 dB** por debajo del pico · frame 20 idéntico al bit · silencio con 229/229 bins en el piso,
  sin tonalidad y sin NaN.
- `[budget]`: `BUDGET_CQT` **0.768 ms** de mediana (p95 0.855) y `BUDGET_SPIRAL` **0.527 ms** (p95 0.580),
  criterio 4 / 8.
- `[uisnap]`: `ovni_telescope_cqt_{S,M,L}.png` y `ovni_telescope_spiral_{S,M,L}.png` con la tríada de Do
  mayor y el bajo en C2. Las dos fotos afirman algo del **dato** antes de dispararse: la primera que el
  cromagrama tenga exactamente tres clases arriba y la tonalidad sea Do mayor; la segunda que los cuatro Do
  estén fuertes y en el **mismo ángulo** del mapeo, que es la única razón de ser de esa lente.
- **El harness de presupuesto se calibra.** Alrededor de cada medición se corre una carga patrón fija (10
  pasadas por columna sobre una imagen de 1024×700, con una tabla de 64 K de por medio) y el factor
  `k = medido / referencia` se imprime en cada línea `BUDGET_*`. Con `k ≤ 1.3` el criterio no se toca; con
  `k > 1.3` se escribe WARN y se compara contra `criterio × min(k, 2.0)`; con `k > 2.0` el test **sigue
  fallando**, que es lo que impide que la calibración esconda una lente lenta de verdad. Verificado: suite
  entera 3/3 en verde con 4 hogs, imprimiendo `k = 1.13 … 1.77`.

### Arreglado

- **El redibujo completo del espectrograma estéreo era el pintado más caro del plugin** (2.83 ms sin carga,
  4.29–5.40 con seis procesos ocupando núcleos). Dos causas: se construía una `juce::Image::BitmapData`
  **por columna de píxel** (~950 por redibujo) en vez de una por redibujo, y la fila se resolvía una vez por
  píxel en vez de una por grupo, con dos lecturas de byte y una rama por celda. Ahora la celda viaja
  empaquetada como `(energía << 8) | coherencia` —empaquetado explícito, sin castear la memoria del anillo—
  y con la energía en el byte alto un `max` de `uint16` **es** "la celda de más energía", sin rama.
  Medido: redibujo **2.83 → 1.258 ms**, columnas nuevas **0.95 → 0.358 ms**. El mismo defecto estaba en el
  sonograma de nivel: **1.63 → 1.17 ms**.
- **`SpectrogramRing::configure()` escribía cuatro campos no atómicos desde el worker** mientras el message
  thread los lee en cada pintado. Ahora son `std::atomic` relaxed, con `static_assert` de lock-free.
- **El comentario de la ventana deslizante de `StereoBands` decía que la resta "cancela exacto"**, que no es
  lo que IEEE-754 garantiza. Lo exacto es la conversión `float → double`; `(a + x) − x` no devuelve `a`.
  El comentario dice ahora el número real: hasta 1.1e-16 relativo por operación, ~1.5e-10 de cota para una
  hora de sesión a 375 frames/s, y por qué no se re-suma.
- **`[sgram-st]` no puede detectar un L↔R invertido, y ahora lo dice con un número.** La coherencia y la
  energía son simétricas por construcción, así que ningún caso de esa tabla podría ponerse rojo por eso. El
  caso nuevo alimenta la misma señal por L (con R mudo) y por R (con L mudo) y comprueba que las dos
  columnas son **idénticas byte a byte** — y que `balanceDb`, que es la red que sí lo atrapa, da −60 de un
  lado y +60 del otro.
- **`[bands]` imprimía `BW·T` rotulado "gl"**, que es la mitad de los grados de libertad que usa su propia
  sigma. Ahora imprime los dos y el nombre coincide con el número: `gl = 2·BW·T`.
- **La lente CQT dibujaba las barras corridas media celda** respecto de su propia frecuencia: parado en el
  centro visual de la barra de A4 la lectura decía "A4 +20 cents". La celda del bin `k` va ahora de `k−0.5`
  a `k+0.5`, centrada en `f_k`.
- **Los dos espectrogramas pasan a mejor de tres tandas** como el resto de las lentes: eran los dos únicos
  que medían una sola tanda, o sea los dos únicos que un desalojo del scheduler podía poner en rojo sin que
  el código hubiera cambiado.
- **Al cerrar la ventana, el motor vuelve a los módulos siempre-activos.** Cerrar el plugin con
  SPECTRUM / SPECTROGRAM / SCOPE a la vista dejaba el módulo caro corriendo para siempre (FFT de hasta
  orden 15 a ~375 frames/s) sin nadie mirando. El processor cuenta ventanas: con dos abiertas, cerrar una
  no le apaga el módulo a la otra.
- **Cambiar la historia del espectrograma ya no reinicia el análisis.** Estirar la ventana de 10 a 60 s
  tiraba el promedio acumulado y el peak hold; ahora sólo se rehace (y limpia) el anillo.
- **La lectura de dB del espectrograma sale del dato, no del color.** La paleta tiene diez pares de
  entradas con el mismo ARGB tras redondear a 8 bits, así que buscar "el color más parecido" devolvía
  siempre el índice más bajo del par y mentía un escalón entero (**0.353 dB** con rango 90, medido).
- **La caja de lectura ya no se sale del área** cuando el texto es más ancho que el plot (helper único en
  `lenses/LensReadout.h`, usado por las cuatro lentes que la tienen).
- **El snapshot de SCOPE mide lo que dice**: empujaba el audio antes de crear el editor, o sea con
  `kStereo` apagado, y que la foto saliera bien dependía de que quedaran muestras sin drenar en el bus.
  Los cinco snapshots afirman ahora algo del dato además del tamaño del archivo.
- `BandCorrelationLens::paintLive` mutaba el rectángulo de su cabecera en cada pintado (lo cazó el test de
  reduced-motion): el resumen se corría solo hacia la izquierda.

### Conocido / pendiente

- Sólo **7** de las 13 lentes están construidas (prompts 52-55).
- El **resumen** de BAND CORRELATION nombra la primera banda con la correlación mínima. Cuando muchas
  empatan en −1.00 —una mezcla entera fuera de fase arriba— cuál nombra no está definido. Es una
  referencia, no un veredicto: desempatar por energía necesita el nivel por banda en el frame, y eso es
  trabajo de VERDICT (lente 13).
- El anillo de sumas por bin del estéreo por banda está **acotado a 6 MB**: por encima del tope los frames
  se agrupan y la ventana se cuantiza. A 48 kHz nunca pasa (la ventana es rectangular exacta de K frames);
  a 192 kHz con 87.5 % de solape la cuantización es de 4 frames sobre ~4 500, medio milisegundo sobre tres
  segundos. Lo que se publica es siempre la ventana efectiva.
- El **redibujo completo** del espectrograma estéreo sigue siendo el pintado más caro del plugin, pero ya
  no por poco: 1.26 ms de mediana contra los 2.83 de antes (criterio 4 / 8, techo propio de 2.5).
- **El cromagrama no puede separar una nota de su vecina de abajo.** Con `B = 24` y el `Q` canónico —los
  mismos que fijan el scalloping de 1.42 dB— la falda de Hann deposita 0.25 de la potencia en el bin de al
  lado, y ese bin cae en la clase de abajo: la relación derivada es 0.20 y es la que se mide. Bajarla
  pediría otro `Q` (y perder resolución de altura) o tres bins por semitono.
- **La tonalidad estimada es una correlación, no un análisis armónico.** No mira acordes, ni funciones, ni
  modulaciones: mira doce números. Con material ambiguo —una tríada pelada sin bajo— puede dar la mediante
  menor con más correlación que la tónica mayor, y con material sin altura definida da ~0.5 de confianza
  porque es el máximo de 24 correlaciones. Por eso la confianza y el % del tiempo se muestran siempre.
- **El primer frame de CQT cuesta ~100 ms de worker** (los ~229 kernels). Es una vez por sesión y por
  sample rate, corre fuera del audio thread y sólo si alguien abre las lentes 6 o 7 — pero durante ese rato
  el análisis se atrasa (el bus aguanta 8 s a 48 kHz, así que no se descarta nada).
- **SPIRAL no dibuja la vuelta completa hasta 20 kHz "por octava exacta"**: a 48 kHz el eje llega a 9.5
  vueltas (A0 → ~20 kHz), así que la última vuelta está cortada por la mitad. Es el eje del módulo, no un
  recorte de la lente.
- **PLR sobre material de picos**: el enunciado del prompt 49 esperaba > 30 dB para la señal de ráfagas
  "porque el integrado lo domina el lecho". No puede pasar: el integrado de BS.1770 tiene compuerta
  **relativa** a −10 LU, que existe justamente para sacar del promedio las partes calladas, así que el
  lecho queda afuera. Medido: integrado −9.637, PLR +9.215. El test afirma lo que la métrica sí dice.
- El osciloscopio usa escala fija ±1.0 (fondo de escala): con material muy bajo el trazo queda finito.
  El goniómetro sí se auto-escala, rotulado. Un control de ganancia del osciloscopio queda para v2.
- Por encima de ~51.2 kHz el osciloscopio muestra menos de 40 ms (el buffer topa en 2 048 muestras).
- El chasis expone `inGain` / `output` / `monoSafe` al host aunque la UI no los muestre (lista v2).
- Vectores de la EBU que faltan: 3341 6-14 y 20-23, 3342 5-6 (necesitan WAV de la EBU o un resampler).
- Sin smoke en DAW real todavía (`auval -v aufx Tlsc Ovni` y pluginval los corre la auditora).
- El **modo de bandas** (⅓ oct / Bark) se calcula siempre, aunque la lente esté en modo FFT: cuesta un
  recorrido de los bins y deja los datos listos para BAND CORRELATION y VERDICT. Si algún día pesa, es un
  `if`.
- **El espectrograma se dibuja a un color por nivel**, no reasignado ni con fase: el reasignado está en la
  lista v2 del tablero.
- El eje de tiempo del espectrograma reparte los grupos de columnas con un acumulador entero (1 o 2 píxeles
  por grupo): a historias muy cortas en paneles muy anchos, el ancho de columna alterna y se nota de cerca.

---

### Agregado (prompt 53 — las dos lentes 3D)

- **Paneo por energía por bin** en el módulo `StereoBands`:
  `pan_k = (ΣRR_k − ΣLL_k)/(ΣRR_k + ΣLL_k) ∈ [−1, +1]`, de las **mismas sumas** de la ventana deslizante
  con las que ya se calcula la coherencia — un cociente más, ni una multiplicación de más, y por lo tanto
  el mismo determinismo y la misma ventana efectiva. Viaja al final del `StereoBandsFrame`.
- **Módulo `Field`** (`kField`), con su `FieldFrame` propio: grilla de 96 filas de frecuencia (log,
  20 Hz – 20 kHz por bordes) × 64 columnas de dirección, con decaimiento exponencial configurable
  (0.5 / 1 / 2 s) y estela de las 8 grillas anteriores decimadas a 48×32. Cuelga del `FieldSink` nuevo de
  `StereoBands`, o sea de sus frames **emitidos**: deterministas igual que los calculados (posiciones
  fijas del stream) pero 60 por segundo en vez de 375. Cero asignaciones por frame.
- **Lente 5 · WATERFALL**: el espectrograma en profundidad (X frecuencia log, Y nivel, Z tiempo), 60 / 90
  / 120 líneas × ≤ 256 puntos, tres inclinaciones, ejes de frecuencia / nivel / tiempo y lectura de la
  línea de adelante.
- **Lente 11 · FIELD**: la grilla del campo como escenario 2.5D, con la estela alejándose, tope de 4 096
  puntos, contador de puntos a la vista, lectura de dirección / frecuencia / dB relativo, y el rótulo
  fijo **"paneo por energía L/R · no localización"** debajo del eje.
- **`lenses/Projection2p5.h`**: la proyección 2.5D **oblicua** que comparten las dos, pura y testeable.
  Sin GPU (D-46): sin división por z, tres multiplicaciones por punto, monótona en los tres ejes y
  siempre dentro del área.
- Settings nuevos en el `ValueTree` (persisten con el estado y los presets): `waterfallLinesIndex`,
  `waterfallTiltIndex` y `fieldDecayIndex`.

### Verificado (prompt 53)

- `[pan]` — la tabla de θ (0 / 22.5 / 45 / 67.5 / 90° → −1 / −0.707 / 0 / +0.707 / +1) con **error peor
  de 4.9·10⁻⁸**; independientes: medio +0.004 con dispersión 0.137; silencio: 0 bins con paneo distinto
  de 0 y sin NaN; bloques 1 / 7 / 64 / 4096 → los 2 049 paneos **idénticos al bit**.
- `[field]` — ley de paneo (θ = 0 / 22.5 / 45 / 90° → columnas 0 / 9 / 31 / 63, todas dentro de ±1); dos
  fuentes (100 Hz sólo L → columna 0, 5 kHz sólo R → columna 63, **centro al 0.00 %** de esas filas);
  independientes (peor concentración **12.2 %** en 32 filas por encima de 2 kHz, criterio < 15 %);
  decaimiento (0.512 / 1.003 / 2.005 s contra 0.5 / 1 / 2, error < 2.4 %); estela (**8/8 láminas
  idénticas al bit** a los frames `n−8 … n−1`, y la más vieja distinta de la más nueva); silencio (0
  celdas distintas de 0, sin NaN); bloques 1 / 7 / 64 / 4096 → las **6 144 celdas idénticas al bit** en
  el frame 30.
- `[waterfall]` — proyección (1 927 muestras monótonas y dentro del área, las dos esquinas exactas,
  inversa exacta); columnas (el mismo anillo → las mismas columnas, la línea 0 es la más vieja que entra,
  los tres topes); barrido (**Spearman 0.99996**, 100 % de pares no decrecientes); lectura (pico en
  979 Hz a −20.5 dB sobre un tono de 1 kHz a −20 dBFS).
- `[budget]`, con la máquina a load 12 y las **11 lentes** midiéndose: WATERFALL **1.334 / 1.641 ms**
  (mediana / p95) con las 120 líneas y 0.959 / 2.523 con 90; FIELD **0.437 / 0.503 ms** con el tope de
  4 096 puntos ejercido y las 8 láminas. Criterio 4 / 8 ms.
- Reduced-motion de FIELD verificado **por píxel**: en la franja del dibujo a la que sólo puede llegar la
  estela quedan **0 píxeles** de la paleta encendidos (3 306 sin reduced-motion).
- Round-trip de estado de los tres settings nuevos, y lente a demanda: `kField` sólo con FIELD a la vista
  (STEREO SPECTROGRAM no lo paga), `kSpectrum` sólo con WATERFALL (que no necesita `kStereoBands`).
- La batería del 48–51 sigue **intacta**: 10 575 aserciones en 75 casos.

### Conocido / pendiente (prompt 53)

- **FIELD no distingue mono de fuera de fase.** `L = R` y `L = −R` tienen la misma energía en los dos
  canales, así que las dos dan `pan = 0` y se dibujan en el centro. Es correcto —mide balance de nivel,
  no fase— y es justo lo que muestran las lentes 9 y 10; las tres se leen juntas. Está en el README.
- **La resolución de la grilla en los graves.** Por debajo de ~1.5 kHz una fila es más angosta que un bin
  de la STFT, así que un tono grave reparte su energía en dos filas y **lee más bajo** que un tono agudo
  de la misma amplitud (medido: 5 kHz a 0.0 dB rel, 100 Hz a −2.9). Se arregla con una FFT más grande;
  compensarlo en el dibujo sería inventar energía que la medición no separó.
- **El ancho de la nube de FIELD depende de la ventana**: con material decorrelacionado la dispersión del
  paneo por bin es la del estimador (0.137 con 1 s), no una medida de ancho estéreo. `VENTANA` la mueve.
- **WATERFALL dibuja del frente al fondo con un horizonte**, no con el relleno del algoritmo del pintor.
  El dibujo es el mismo (la demostración está en el header de la lente) pero la implementación literal
  del prompt costaba decenas de millones de píxeles por frame y no entraba en el presupuesto.
- `tests/CMakeLists.txt` del plugin **no se tocó**: la lista de fuentes es un `file(GLOB … CONFIGURE_DEPENDS)`
  y los dos archivos nuevos entran solos. Agregar una lista explícita habría cambiado el comportamiento.
- El helper `writePng` de los snapshots está **duplicado** en `WaterfallTest.cpp` y `FieldTest.cpp`: el
  original es estático del namespace anónimo de `UiSnapshotTest.cpp`, que este prompt no podía tocar.
  Al fusionar conviene subirlo a `TestHelpers.h` y dejar una sola copia.
- Sólo **9** de las 13 lentes están construidas (faltan CQT, SPIRAL, TONAL BALANCE y VERDICT).
## Prompt 54 — TONAL BALANCE, el módulo `Reference` y el `FileAnalyzer`

### Agregado

- **`FileAnalyzer`** (`analysis/FileAnalyzer.*`): el **mismo motor** sobre un archivo, offline y más rápido
  que tiempo real (60 s en 243 ms en el M4, 247×). `juce::Thread` propio con instancias **propias** de
  `Loudness` y `Spectrum` —nunca las del análisis en vivo: meterle un archivo al medidor del usuario le
  rompería el integrado sin que nada en pantalla lo dijera—, progreso atómico, `cancel()` que vuelve en
  2 ms y el sample rate **del archivo** (no se remuestrea). Formatos: WAV / AIFF / FLAC / Ogg y lo que sepa
  el sistema (CoreAudio en macOS, WMA/MP3 en Windows).
- **`FileAnalysis`** (`analysis/FileAnalysis.h`): integrado, LRA, true-peak, máximos M/S, las 30 bandas de
  ⅓ de octava y **dos series temporales de cadencia declarada** para VERDICT — `shortTermHistory` a 10 Hz
  exactos y `truePeakPerSecond` a 1 Hz exacto.
- **`ThirdOctaveAverage`**: el acumulador infinito de potencia por banda, en **un solo lugar** para los dos
  lados (archivo y vivo). Que sea uno es lo que hace que la promesa de identidad sea estructural y no una
  coincidencia que alguien pueda romper sin enterarse.
- **Módulo `Reference`** (`kReference`, `analysis/modules/Reference.*`): el lado vivo. Cuelga del
  `FrameSink` del espectro y consume **todos** los frames calculados (con 87.5 % de solape se perderían 6
  de cada 7, y el promedio dependería de en qué pedazos entró el audio en vez de del audio). Publica
  `ReferenceFrame` por su propio `TripleBuffer`.
- **`FrameSinkFanout`**: el sink del espectro es **uno solo** y desde el prompt 51 lo ocupa `StereoBands`.
  En vez de apoyarse en que las dos lentes nunca están visibles a la vez (verdad hoy, casualidad de la UI),
  se le cuelgan los dos consumidores.
- **Lente TONAL BALANCE** (lente 12): las dos curvas normalizadas, el delta en barras de ±12 dB con banda
  de referencia de ±3 dB **rotulada**, lectura al pasar con la cuenta de bins, drag & drop de archivo,
  CARGAR / QUITAR / RESET y barra de progreso durante el análisis. Escala **fija** de +6 a −42 dB.
- El **path** de la referencia persiste en el estado y al restaurar se **re-analiza**; si el archivo no
  está, `referencia no encontrada: <nombre>` y ninguna curva dibujada.

### Corregido / decidido

- **Corrección de ancho cubierto en las bandas de ⅓ de octava** (hallazgo de `FILE[sr]`): sumar crudo los
  bins de una banda hacía que el mismo ruido rosa leyera **3.415 dB distinto** entre 44.1 k y 48 k, porque
  *cuáles* bins caen dentro depende de la rejilla. La potencia de banda se estima ahora como
  densidad × ancho nominal: el peor caso baja a **0.333 dB** (0.195 con 4 bins o más). SPECTRUM no lo hace
  —sus barras son la suma cruda, la convención de un RTA— y está documentado en el README.

### Sabido / lista v2

- **El delta suma cero, y eso hay que entenderlo antes de mirarlo.** Normalizar por loudness hace que un
  shelf de +6 dB en los agudos se vea como **+1.70 arriba y −4.30 abajo** (la loudness subió 4.30). Es la
  misma verdad contada a igual volumen. Está en pantalla, en el README y verificado como identidad exacta
  (`delta[b] + ΔL == respuesta del filtro`, error máximo 0.006 dB sobre 21 bandas).
- Las bandas de **menos de 4 bins** (por debajo de ~200 Hz con FFT de 4 096) son un muestreo casi puntual
  de la densidad: entre dos sample rates distintos llegan a diferir 0.699 dB. Se reportan aparte y la
  lectura de la lente dice cuántos bins midieron cada banda.
- El análisis de archivo usa settings **fijos** (orden 12 · Hann · 75 % · L+R) para que una referencia dé
  el mismo número siempre; el lado vivo usa los de la lente. La identidad al bit se verifica con SPECTRUM
  en su default, que es el caso real de TONAL BALANCE.
- La referencia se guarda como **ruta**: si el archivo se mueve, hay que volver a cargarlo. Guardar los
  números sería mostrar una referencia que ya no existe.
- El `FrameSinkFanout` tiene dos slots. Si los prompts 52 (CQT) y 53 (FIELD) también cuelgan del
  `FrameSink`, hay que llevarlo a un array chico en el merge, no pelearse por el slot.

## Prompt 56 — pasada visual "ultra", HEMISFERIO e idioma

Encargo de Joaquín, textual: *"no parece de ultra calidad en lo que respecta a los visuales; debería ser
algo de ultra calidad, más detalle; pensalo como si fueras productor: necesitás algo que sea claro y a la
perfección"*. Y sobre la vista de mezcla estéreo: *"esa vista que sería en mezcla estéreo tenemos que
ponerla también, además de la que es 360"*.

### El sistema visual (`source/lenses/Look.h`)

- Los tokens de las doce lentes en un solo archivo, derivados del `Theme` del sello. **Fuera de `Look.h`
  ninguna lente escribe un color literal**, y un test lo verifica leyendo las fuentes.
- Cuatro contratos medidos: jerarquía de rejilla · pixel snapping (2 filas físicas llenas, 0 parciales,
  contra 2 a medio alpha sin snappear) · dígitos tabulares (spread **0.0000 px**, contraejemplo
  proporcional 9.442 px) · rampa secuencial monótona en luminancia (**0 caídas** en 256 entradas).
- La rampa secuencial estaba **copiada** en `SpectrogramLens.cpp` y `FieldLens.cpp`. Ahora se arma una vez.

### Idioma (D-50)

- `language` es un código ISO 639-1, no un enum. Default inglés. **Fallback por clave**: un idioma a medio
  traducir sigue siendo usable y nunca deja una etiqueta en blanco.
- Completos: **en · es · pt · fr · de · it** (123/123 claves). Los cuatro últimos, **revisión pendiente de
  hablante nativo**.

### HEMISFERIO

- Tercer modo de SCOPE. Convención `θ = 90° + 2·atan2(R−L, R+L)`, verificada caso por caso, y atada al
  paneo por energía de FIELD por `θ = arccos(−pan)` (las dos dan 45.00° para una fuente a 22.5°).
- Envolvente idéntica **al bit** con bloques de 1 / 7 / 64 / 4096 muestras. Decaimiento con **0.00 %** de
  error en los tres ritmos.
- `scopeMode` es nuevo en el estado; cuando falta se deriva del viejo `scopePolar`, así un preset guardado
  antes de este modo abre en la vista en la que se guardó.

### La pasada lente por lente

| Lente | Qué se veía | Qué se cambió |
|---|---|---|
| **FIELD** | "73/4096 pts": dos hilos de cuadraditos en una caja vacía | superficie de calor, la grilla entera rasterizada. 73 puntos → **100 225 px con dato** |
| **WATERFALL** | doce curvas de alambre flotando | relleno de la curva al horizonte, trazo de adelante más brillante |
| **SPIRAL** | púas de 0.8 px: el concepto se entendía, el dato no se veía | púas de 1.5–4.5 con glow sobre las fuertes, rueda más ancha, tonalidad en display grande |
| **CQT** | teclado de 22 px sin coordenadas | teclado de 34 px con **marca de acento** en cada Do, topes redondeados, tónica con el acento (los nombres `C1 … C8` van arriba, en las marcas del plot: en 114 teclas no entran) |
| **LOUDNESS** | verde a cualquier nivel | zonas de color, tick de pico, historia rellena, línea de objetivo rotulada con su valor |
| **DYNAMICS** | barra plana | zonas de color en el PSR, bin actual con el acento |
| **SCOPE** | estela gruesa que empastaba la nube | fósforo más fino (decaimiento 184 → 170) |
| **SPECTRUM** y familia | rejilla de un solo peso, cursor sin altura | jerarquía mayor/menor, crosshair con el punto en el valor, readout unificado |
| **tira de lentes** | tres estados con casi el mismo peso | barra de acento en la activa, anillo vacío en la no construida, nombres en el idioma elegido |

### Dos hallazgos de honestidad, encontrados MIRANDO y no leyendo

- **El umbral de alerta de pérdida mono de BAND CORRELATION estaba en −3 dB**, y −3.01 dB es exactamente
  lo que da material decorrelacionado por construcción: M = (L+R)/2 de dos señales independientes pierde
  3 dB, y lo **exige** `tests/StereoBandsTest.cpp:263` para ruido independiente. O sea que la lente
  marcaba en alerta la física normal de cualquier mezcla ancha. Pasa a −6 dB, donde se pierde el doble y
  hay cancelación de verdad. Un medidor que se alarma con lo normal enseña a no mirarlo.

  *Corrección de lo que dice el commit de esa tanda:* llegué a esto mirando el panel rojo de la hoja de
  contacto y **atribuí el rojo a la señal equivocada**. La captura `bands_M` no es de ruido independiente:
  su readout dice `corr −1.00 · mono −60.0 dB`, o sea material deliberadamente fuera de fase, y ahí el
  rojo es correcto y sigue siéndolo después del cambio. El defecto del umbral es real y el arreglo también
  —lo sostiene el test del motor, no la captura—, pero esa captura no era la prueba.
- **El hemisferio, en su primera versión**, pintaba la zona fuera de fase a alpha llena y convertía una
  mezcla con correlación **+0.99** en una pantalla de emergencia. Ahora el énfasis es proporcional a la
  pérdida mono medida.

### Presupuesto de pintado

Las doce lentes siguen dentro de 4 / 8 ms en tamaño L. FIELD es la que más se movió, y pasa al harness
calibrado porque ahora sí es una lente a la que la carga de la máquina le mueve el número:

```
drawImage alta calidad, 8 láminas ......... 18.809 ms
drawImage media calidad, 8 láminas ........ 18.987 ms   (no era el filtro)
blit propio, 8 láminas .....................  5.240 ms
blit propio + piso aparte, 4 láminas .......  4.680 ms
blit propio, 3 láminas .....................  2.687 ms mediana · 2.881 p95 · k = 1.15
```

### Presupuesto: dos casos pasan al harness calibrado

FIELD y WATERFALL. Los dos cambios visuales de esta tanda (superficie de calor y relleno bajo la curva)
hicieron que el costo de esas dos lentes dependa de la carga de la máquina, que es exactamente el caso
para el que existe el harness. **No afloja el criterio**: 4 y 8 siguen siendo 4 y 8 con la máquina sana,
y con k por encima de 2.0 el test **falla** en vez de esconderlo. Lo encontré corriendo la suite cinco
veces seguidas, no leyendo el código: con el 55 compilando al lado, WATERFALL a 120 líneas dio p95 =
11.596 ms contra un criterio sin escalar de 8.

### Lo que cambió de sentido en tests existentes

`FieldLens::pointsDrawn()` ya no cuenta cuadraditos sino celdas de grilla con energía, y el tope de 4 096
puntos dejó de aplicar: el costo dejó de depender de la señal, así que la cota pasó a ser **estructural**
(los planos que existen) en vez de un tope que hay que acordarse de aplicar. Los dos `REQUIRE` que
afirmaban el tope viejo (en `FieldTest.cpp` y `LensBudgetTest.cpp`) se actualizaron a esa cota más
`kDenseCells`, para que el número del presupuesto siga hablando del peor caso.
