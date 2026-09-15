# TELESCOPE 🔭 — el analizador espacial del sello OVNI

**Un analizador de audio que además concluye.** Gratis, AGPLv3. **macOS 11.0+ universal (arm64 +
x86_64), VST3 + AU**; VST3 x64 para Windows 10+ en ZIP sin firma, en el mismo release.
Es el 9º plugin del sello. El diseño (motor + 13 lentes, honestidad medible) está desarrollado en este mismo documento, en la [ficha técnica](docs/ficha.md) y en el [manual](docs/manual/README.md); el spec interno de diseño no viaja en el repo público.

> **Estado: 0.1.0 — las 13 lentes construidas.** LOUDNESS · DYNAMICS · SPECTRUM · SPECTROGRAM ·
> WATERFALL · CQT · SPIRAL · SCOPE (con HEMISFERIO) · BAND CORRELATION · STEREO SPECTROGRAM · FIELD ·
> TONAL BALANCE · VERDICT. No hay "coming soon": lo que está en la tira, mide.
> La versión vive en [`VERSION`](VERSION) y de ahí la leen el CMake, el test `telescope-version`, el
> `SOURCE.txt` del `.pkg`, el [CHANGELOG](CHANGELOG.md) y la [ficha](docs/ficha.md).

---

## Build

```bash
cd ovni                                   # tu clon de github.com/ovniaudio/ovni
cmake --preset release-universal          # distribución: Release, arm64 + x86_64, deployment 11.0
#     --preset dev                        # iteración en Apple Silicon: arm64-only, ~2× más rápido
ninja -C build telescope_VST3 telescope_AU telescope_Standalone OvniTelescopeTests
ctest --test-dir build -R telescope --output-on-failure
```

Cuatro targets y nada más: el árbol es un monorepo y `ninja` pelado construiría también los otros ocho
plugins (que, con `COPY_PLUGIN_AFTER_BUILD`, pisarían los bundles instalados en `~/Library`).

`ctest -R telescope` corre tres cosas: la batería entera del exe (`telescope`), la guardia de versión
(`telescope-version`, que compara los `Info.plist` de los tres bundles —y el CHANGELOG y la ficha— contra
[`VERSION`](VERSION)) y la de frescura de la matriz de idiomas (`telescope-strings-matrix`).

Filtros de Catch2 sobre el exe (`build/plugins/telescope/tests/OvniTelescopeTests_artefacts/Release/OvniTelescopeTests`):
la tabla completa está en [Verificación](#verificación), al final.

**El presupuesto de pintado se calibra contra la máquina donde corre.** El criterio (4 ms de mediana,
8 de p95 en tamaño L) no se toca; lo que se mide es cuánto está frenada la máquina, y sale como `k` en
cada línea `BUDGET_*`. La referencia por defecto es la del M4 de la casa (22.5 ms). En otra máquina:

```bash
TELESCOPE_BUDGET_REF_MS=41.8 ./OvniTelescopeTests "[budget]"   # el valor sale en BUDGET_CALIBRACION
```

Documentación de usuario: **[manual ilustrado](docs/manual/README.md)** ([castellano](docs/manual/README.es.md)) ·
**[ficha técnica](docs/ficha.md)** · [diccionario de VERDICT](docs/telescope-diccionario.md) ·
[matriz de idiomas](docs/strings-matrix.md).

---

## Qué hace

**No toca el audio.** TELESCOPE sólo escucha: `processAudio` lee L y R, los empuja a un bus lock-free y
no escribe una sola muestra del buffer. Latencia 0, cola 0, salida **bit-exacta** — verificado en
`tests/PassThroughTest.cpp` con ruido estéreo determinista, con bypass encendido y apagado, con cada una
de las 13 lentes seleccionada, con bloques de 1, 7, 64 y 4096 muestras, y con fuente mono.
`NULL_MISMATCHES=0` en los cinco casos.

El análisis corre en su propio hilo (`TelescopeAnalysis`): el audio nunca espera por una medición.

### La lente LOUDNESS

| Muestra | Definición |
|---|---|
| **INTEGRATED** (LUFS) | ITU-R BS.1770, doble compuerta: absoluta −70 LUFS, relativa 10 LU bajo la media de los que pasan |
| **MOMENTARY** (LUFS) | ventana de 400 ms |
| **SHORT-TERM** (LUFS) | ventana de 3 s, a 10 Hz |
| **LRA** (LU) | EBU Tech 3342: compuertas −70 y −20 LU, P95 − P10 de los short-term |
| **TP MAX** (dBTP) | true-peak, BS.1770 Anexo 2, medido ANTES del K-weighting |
| **M MAX / S MAX** | máximos desde el último RESET |
| Historia | short-term de los últimos 3 minutos, con la momentary detrás |
| Objetivo | plataforma elegida: línea en el medidor y en la historia + el delta contra el integrado |

RESET y PAUSE son botones de la lente, no parámetros: no tiene sentido automatizar "poné el medidor en
cero" ni guardarlo en un preset. En PAUSA el motor sigue drenando el bus pero deja de integrar, así el
contador de descartes no miente. Los tres botones son los mismos en LOUDNESS y en DYNAMICS: el análisis
es uno solo.

### La lente DYNAMICS

No mide cuán **fuerte** está la mezcla (eso es LOUDNESS): mide cuánto **margen** le queda.

| Muestra | Definición |
|---|---|
| **PSR** (dB) | true-peak máximo de los **últimos 3 s** − short-term. La lectura "en vivo" (la que popularizó el Dynameter). Válido sólo si hay short-term |
| **PLR** (dB) | true-peak máximo **desde el RESET** − integrado. Es el *peak-to-loudness ratio* de AES TD1004. Válido sólo si el integrado ya pasó la compuerta |
| Histograma | short-term desde el RESET en **61 bins de 1 LU**; el bin *i* está centrado en (*i* − 60) LUFS |
| Clips | eventos por encima del umbral en dBTP (default −1.0, rango −3…0) desde el RESET |
| Línea de tiempo | 10 minutos, una marca por segundo en el que hubo eventos, el ahora a la derecha |

**Por qué un seno da PSR y PLR = 0.0.** Para un seno de 997 Hz el true-peak en dBTP y el loudness en LUFS
dan **el mismo número**: el K-weighting aporta ahí exactamente los +0.691 dB que la constante de BS.1770
resta, así que LUFS = dBFS pico. Un seno estable no tiene margen de pico sobre su propio nivel — PSR y PLR
valen 0.0 y ésa es la respuesta correcta, no una casualidad. Es la identidad que verificó el prompt 48 y
la que hace que los dos números signifiquen algo.

**Qué cuenta como UN evento de clip.** No una racha de muestras sobremuestreadas sobre el umbral: un seno
de 997 Hz que se pasa durante 50 ms se pasa **una vez por ciclo**, o sea unas 50 rachas para lo que
cualquier oído llama *un* clip. Un evento se abre en la primera muestra sobre el umbral y **no se cierra
hasta 100 ms por debajo** (`Loudness::kClipHoldMs`); las rachas más juntas que eso son el mismo evento.
Medido: 10 ráfagas de 50 ms separadas 1 s → **10 eventos**, y el ring de 1 Hz marca exactamente 10
segundos con 1 evento cada uno.

**Cambiar el umbral reinicia el conteo** y la línea de tiempo. Un contador que mezcla eventos medidos
contra dos techos distintos no querría decir nada.

**La línea de referencia de 8 dB es referencia, no veredicto.** El spec §5.8 usa "PSR medio < 8 dB" como
umbral de *aplastado*. La lente dibuja la línea, la rotula y muestra dónde cae el número — no colorea el
número de rojo ni escribe una conclusión. Quien concluye es VERDICT (lente 13), con la regla a la vista.

### La lente SCOPE

Las tres formas clásicas de **mirar** el estéreo, con la matemática de `Stereo` (banda ancha, spec §5.3)
sobre una ventana deslizante de **100 / 300 / 1000 ms** (default 300):

| Salida | Fórmula | Qué dice |
|---|---|---|
| **CORR** | ΣLR / √(ΣLL·ΣRR) | +1 mono · 0 decorrelacionado · −1 fuera de fase |
| **WIDTH** | √(ΣSS / ΣMM) | 0 mono · 1 dos fuentes independientes · ↑ el lado domina |
| **BALANCE** (dB) | 10·log10(ΣRR / ΣLL) | + = R más fuerte, − = L más fuerte |
| **MONO LOSS** (dB) | 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2) | 0 si L=R · −3.01 si independientes · −∞ si L=−R |

con **M = (L+R)/2** y **S = (L−R)/2**, acumuladas en `double` por hop de 100 ms. Es exactamente la
matemática de `orbita/tests/StereoMeasure.cpp` y se alimenta con el **mismo ruido rosa determinista**
(Paul Kellet "economy" sobre un LCG de semilla fija), así los números de TELESCOPE son **directamente
comparables** con los de ÓRBITA y PULSAR. Única diferencia declarada: acá el balance va R sobre L (como
el spec §5.3); el de ÓRBITA imprime L sobre R — el mismo número con el signo dado vuelta.

**Casos borde, nunca NaN ni infinito** (la UI dibuja estos números):

| Situación | Qué se publica |
|---|---|
| Sin señal (ΣLL+ΣRR ≈ 0) | todo en 0 y "sin señal" — que **no** es lo mismo que "mono perfecto" |
| Un canal mudo (ΣLL·ΣRR ≈ 0) | `corr = 0`: no hay correlación **definida**, no es que valga cero |
| L = −R (ΣMM ≈ 0) | `width` al tope **10** y `monoLoss` al piso **−60 dB** |
| Desbalance extremo | `balance` clampeado a **±60 dB** |

**El goniómetro se auto-escala, y lo dice.** El anillo exterior vale el **pico del hop** (suavizado,
ataque rápido y caída lenta), rotulado abajo: *"lissajous · borde = pico −24.8 dBFS"*. Sin eso una mezcla
a −20 dBFS se dibuja como un punto en el centro y el goniómetro deja de servir para lo que sirve: leer la
**forma** del estéreo, no el nivel — que ya lo dicen LOUDNESS y el balance. La amplificación tiene tope en
−40 dBFS: estirar la nube de una señal que no está sería exactamente la clase de mentira que este plugin
no hace. En modo **POLAR** el ángulo es el mismo pero el radio es el nivel en dB desde −60 hasta el borde.

El **osciloscopio** muestra 40 ms (a 48 kHz, 1 920 muestras; el buffer topa en 2 048, así que por encima
de ~51.2 kHz la ventana visible se acorta). Con **trigger** dibuja desde el primer cruce por cero
ascendente de M dentro de los primeros 20 ms; sin trigger, desde el arranque del hop, y se ve nadar —
que es la verdad de lo que llega. Se dibuja con **envolvente min/max por columna de píxel**: los picos no
se pierden, y es lo que baja el pintado de 7.19 ms a 0.59 ms.

Con **reduced-motion** el goniómetro no deja estela: se dibuja sólo el hop actual, un cuadro estático
coherente.


### La lente SPECTRUM

El analizador de espectro. FFT de **1 024 a 32 768** (órdenes 10 a 15), tres ventanas, tres solapes,
cinco modos de canal, promediado, peak hold y tres modos de banda.

#### La referencia de dB, y lo que se sigue de ella

Cada bin se publica en **dBFS referido a un seno de escala completa**:

> **dB_k = 20 · log10( 2 · |X_k| / (N · CG) )**  con **CG = Σw / N** (ganancia coherente de la ventana)

Un seno de amplitud **A** (pico) centrado en un bin lee **exactamente 20·log10(A)**: un seno de escala
completa lee 0 dBFS. No es una calibración a ojo — la corrección de ganancia coherente es exacta, y se
verifica con las tres ventanas (`SPEC[bin exacto]`: −20.0000 dB con Hann, Blackman-Harris y Kaiser).

De esa definición se siguen dos cosas que conviene saber antes de mirar la pantalla:

- **El ruido blanco a escala completa NO lee 0 dBFS por bin.** Su energía está repartida entre los
  N/2+1 bins, así que cada bin lee mucho más abajo. Ningún analizador serio dice otra cosa; nosotros
  además lo escribimos.
- **Cambiar el tamaño de FFT mueve el piso de ruido, no el de los tonos.** Duplicar N parte cada bin en
  dos: un TONO sigue leyendo su amplitud (toda su energía cae en un bin) pero el RUIDO baja 3 dB por
  bin, porque a cada bin le toca la mitad de banda. Por eso una FFT grande "limpia" el fondo sin que
  nada haya cambiado en el audio.

Un seno que cae **entre** bins pierde algo (*scalloping*): como mucho **1.42 dB** con Hann y **0.83 dB**
con Blackman-Harris, y nunca lee de más. Medido a 1 000 Hz con FFT de 4 096 a 48 k (bin 85.33, un tercio
de bin fuera del centro): 0.63 dB con Hann, 0.37 con BH4.

#### Las tres ventanas, medidas

Cada ventana cambia el compromiso entre **separar** dos tonos cercanos y **no ensuciar** con lo que tiene
al lado. Los números son los de este código, medidos por la DTFT de la propia ventana (`SPEC[ventana]`,
`tests/SpectrumTest.cpp`), no copiados de una tabla:

| Ventana | Lóbulo principal | Primer lóbulo lateral | Para qué |
|---|---|---|---|
| **Hann** (default) | 4.00 bins | −31.5 dB | el compromiso de todos los días |
| **Blackman-Harris 4** | 8.00 bins | −92.0 dB | ver algo muy bajo al lado de algo muy fuerte |
| **Kaiser β = 9** | 6.06 bins | −66.3 dB | el término medio |

Las tres se usan en su forma **periódica** (denominador N, no N−1), que es la correcta para análisis
espectral: con ella un seno centrado en un bin y ventana Hann ocupa **exactamente tres bins**.

El tamaño de FFT decide qué se puede separar: dos tonos a 1 000 y 1 008 Hz (8 Hz de distancia) salen como
**dos** picos con orden 15 (bin de 1.46 Hz) y como **uno solo** con orden 10 (bin de 46.9 Hz).

#### El slope, y por qué el default es 3

El *slope* inclina el dibujo **+dB por octava**, con pivote en 1 kHz. No toca los datos: es una
transformación de display, y el número que se muestra al pasar el mouse es el ya inclinado.

Existe porque un bin de FFT mide una franja de ancho **fijo** (Δf = sr/N), mientras que el oído (y la
música) trabajan por octavas, cuyo ancho crece con la frecuencia. El ruido rosa —que tiene la misma
energía por octava, el material "neutro" de referencia— cae 3 dB por octava en un espectro por bin. Con
**slope = 3** el rosa se ve **plano**, que es lo que uno espera de una referencia. Medido: pendiente
**−0.0072 dB/oct** sobre 10 s de ruido rosa entre 100 Hz y 10 kHz.

**El slope NO se aplica a los modos de banda.** Una banda de ⅓ de octava ya integra un ancho
proporcional a la frecuencia: hace por construcción lo mismo que el slope hace a mano. Aplicar los dos
inclinaría el rosa 3 dB por octava hacia arriba, y el RTA mostraría una escalera donde hay una línea
recta.

#### Los modos de banda

**⅓ de octava (ISO 266), 30 bandas.** Cada banda suma la **potencia** de los bins en
`[fc·2^(−1/6), fc·2^(+1/6))` — la potencia antes del dB, porque sumar decibeles sería sumar logaritmos.
Centros: 25 · 31.5 · 40 · 50 · 63 · 80 · 100 · 125 · 160 · 200 · 250 · 315 · 400 · 500 · 630 · 800 ·
1k · 1.25k · 1.6k · 2k · 2.5k · 3.15k · 4k · 5k · 6.3k · 8k · 10k · 12.5k · 16k · 20k Hz.

**Bark (Zwicker), 24 bandas críticas**, con bordes en 20 · 100 · 200 · 300 · 400 · 510 · 630 · 770 ·
920 · 1 080 · 1 270 · 1 480 · 1 720 · 2 000 · 2 320 · 2 700 · 3 150 · 3 700 · 4 400 · 5 300 · 6 400 ·
7 700 · 9 500 · 12 000 · 15 500 Hz.

Verificado sobre el ruido rosa de la casa (10 s, promediado infinito, orden 15): las 24 bandas de 50 Hz a
10 kHz dentro de **±0.26 dB** de su media. Y sobre ruido blanco, la pendiente teórica de
10·log10(2^(1/3)) = **+1.003 dB por banda**: medido **+1.008**.

**Una banda sin ningún bin adentro se marca distinto de una banda en cero.** Con FFT de 4 096 a 48 kHz el
bin mide 11.7 Hz y la banda de 40 Hz mide 9.3: no entra ni un bin. No es que no haya energía — es que a
esa resolución la banda no se puede medir, y la lente lo dibuja como lo que es. Para leer graves, FFT
grande.

#### Lo demás de la lente

- **Promediado en potencia**, nunca en dB: ninguno / exponencial (τ de 0.1 a 10 s) / infinito desde el
  reset. El exponencial llega al **63 %** del recorrido en τ (medido: 61.4 % con τ = 1 s).
- **Peak hold** con caída de 0 (infinito) a 60 dB/s, default 12. Medido: **12.03 dB/s**.
- **Canales**: L · R · M · S · L+R (dos espectros con los dos colores del tema). Con L = R el lado (S)
  cae al piso; con sólo L, el canal derecho cae al piso y **M = L − 6.02 dB**, que es la definición.
- **Dibujo por columna de píxel con max-hold**: con orden 15 hay 16 385 bins para ~1 000 columnas; tomar
  una muestra por píxel se comería 15 de cada 16 picos. Cada columna toma el máximo de los bins que le
  tocan, y donde sobran píxeles por bin interpola en dB.
- **Lectura al pasar el mouse**: frecuencia, **nota con sus cents** (`cents = 1200·log2(f / f_nota)`,
  A4 = 440 Hz, numeración científica) y el dB del bin o de la banda que hay debajo. 996.09 Hz → **B5
  +15 ¢**.
- Con **reduced-motion** no hay suavizado entre frames: se dibuja el frame tal cual llega. (Sin
  reduced-motion la curva sube de una y baja suave, como cualquier medidor: un pico que se pierde entre
  dos frames es un pico que no existió.)

### La lente SPECTROGRAM

El sonograma: **frecuencia** en Y (log, 20 Hz – 20 kHz, 512 filas), **tiempo** en X (10 / 30 / 60 s de
historia hasta "ahora" a la derecha) y el **nivel en el color**, mapeado sobre `[−rango, 0]` con el rango
elegido (60 / 90 / 120 dB). La paleta de 256 entradas sale entera del tema del sello: piso = fondo,
medio = el acento de la familia, tope = brillo.

Tres cosas que son del **dato**, no del dibujo:

- **Usa la potencia instantánea, nunca la promediada.** Un espectrograma es la evolución en el tiempo:
  promediarlo borraría exactamente lo que muestra. (El promediado sigue vivo para la curva de SPECTRUM.)
  Con el canal en L+R la columna es M = (L+R)/2, calculada de los complejos sin una tercera FFT.
- **Cada fila toma el máximo de los bins de su celda**; sólo cuando la celda no contiene ningún bin
  (graves con FFT chica, donde las filas son más densas que los bins) se interpola linealmente en dB
  entre los vecinos. Interpolar siempre atenuaría los picos angostos hasta 4 dB, que es justo lo que un
  espectrograma existe para mostrar.
- **Cambiar el tamaño de FFT, el solape, el canal, el rango o la historia limpia el espectrograma.**
  Mezclar columnas medidas con dos mapeos distintos sería un dibujo que miente sobre lo que ya pasó.
  Cambiar sólo la **historia** limpia el anillo pero **no reinicia el análisis**: la FFT, el promediado y
  el peak hold siguen donde estaban — estirar la ventana para mirar más atrás no puede costar el número
  que se estaba mirando.

La **lectura al pasar el cursor** (tiempo, frecuencia, dB) sale del **dato**: se relee el byte del anillo
con la misma agrupación con la que se pintó la columna, no del color del píxel. La paleta redondea a
8 bits y tiene diez pares de entradas con el mismo color, así que buscar "el color más parecido" mentía
un escalón (rango/255 = 0.35 dB con rango 90) en esos diez niveles. Si la columna que hay bajo el cursor
ya salió del anillo, no hay lectura: sin dato no se inventa un número.

Los settings del espectro (tamaño de FFT, ventana, solape, canal, promediado, rango, historia) viajan al
motor por atomics y **se aplican con la granularidad del chunk de drenaje**: el worker los lee una vez por
trozo drenado, o sea cada ≤ 100 ms. Un cambio hecho con el mouse tarda eso en verse; no hay forma de que
un frame se calcule con dos mitades de estados distintos que importe para el dibujo.

Verificado: un barrido logarítmico de 100 Hz a 10 kHz sube monótonamente (Spearman **0.99998**, **100 %**
de pares consecutivos no decrecientes); el silencio da todas las columnas en **0**; un tono de −20 dBFS
con rango 90 vale **198** en su fila (= `round(255·(1 − 20/90))`); el anillo guarda exactamente la
historia que dice; y dos corridas de la misma señal dan espectrogramas **idénticos byte a byte**, venga
el audio en bloques de 512 o de 64.

**Con reduced-motion el espectrograma sigue corriendo.** Su eje X *es* el tiempo: congelarlo no sería
menos movimiento, sería dejar de mostrar el dato. Lo que se apaga en las otras lentes son estelas y
suavizados, y acá no hay ninguno de los dos.

### El estéreo por banda (lentes BAND CORRELATION y STEREO SPECTROGRAM)

Un correlímetro de banda ancha sobre una mezcla con los graves mono y los agudos abiertos da un número
intermedio que no dice nada. Medido en la señal de prueba de la casa —seno de 80 Hz en L = R más ruido
rosa pasa-altos de 2 kHz con R = −L— la banda ancha da **corr +0.24 y mono −2.1 dB**, mientras que la
banda de 80 Hz da **+1.00** y la de 4 kHz **−1.00**. No es que el medidor de banda ancha esté mal: es que
hay dos cosas distintas pasando y no puede verlas.

**Las cinco sumas, por banda.** Para la banda `b` (⅓ de octava ISO 266, las 30 de siempre) sobre los `K`
frames de la ventana y los bins `k` que caen dentro de ella, con `M = (L+R)/2` y `S = (L−R)/2`:

```
ΣLL = Σ|L_k|²   ΣRR = Σ|R_k|²   ΣLR = Σ Re(L_k·R_k*)   ΣMM = Σ|M_k|²   ΣSS = Σ|S_k|²

corr_b     = ΣLR / √(ΣLL·ΣRR)                        +1 mono · 0 sin correlación · −1 fuera de fase
width_b    = √(ΣSS / ΣMM)                            0 mono · 1 independientes · tope 10
balance_b  = 10·log10(ΣRR / ΣLL)                     + = R más fuerte, clampeado a ±60 dB
monoLoss_b = 10·log10(ΣMM) − 10·log10((ΣLL+ΣRR)/2)   0 si L=R · −3.01 indep · piso −60 dB
```

Es **exactamente** la matemática del módulo `Stereo` de banda ancha (la de `orbita/tests/StereoMeasure.cpp`,
que a su vez viene de PULSAR) llevada a los bins de la STFT, que es lo que Parseval permite. Los mismos
casos borde y los mismos clamps: sin energía todo en 0 (no "mono perfecto": *no hay medición*), un canal
mudo da `corr` 0 (no hay correlación **definida**), `L = −R` da `width` al tope y `monoLoss` al piso.

**Y coincide con el medidor del tiempo, que es la prueba de que las dos cuentas son la misma.** La banda
ancha calculada desde los bins (todos los bins, todos los frames de 1 s) contra el módulo `Stereo` en el
dominio del tiempo con la misma ventana, sobre el ruido rosa independiente de la casa:

| | corr | width | balance | mono loss |
|---|---|---|---|---|
| desde los bins | −0.01464 | 1.01475 | −0.1478 dB | −3.0744 dB |
| desde el tiempo | −0.01389 | 1.01398 | −0.1190 dB | −3.0710 dB |
| **diferencia** | **0.00076** | 0.00077 | 0.0288 dB | **0.0033 dB** |

(criterios: 0.02 en `corr` y 0.1 dB en `monoLoss`). Si no coincidieran, uno de los dos estaría mal.

**Una banda sin ningún bin no vale cero.** Con FFT de 4 096 a 48 kHz la banda de 40 Hz mide 9.3 Hz de
ancho y el bin mide 11.7: no entra ninguno. Eso no es "correlación cero", es *a esta resolución no se
puede medir*, y las dos lentes lo marcan distinto. La lectura al pasar el cursor dice **cuántos bins**
midieron cada banda, porque en los graves pueden ser uno solo.

**La ventana (0.3 / 1 / 3 s) es la que dice.** `K = round(segundos × frames por segundo)` frames del módulo
de espectro, cuyas posiciones son fijas en el stream: el determinismo se hereda, y las 30 bandas y las
2 049 coherencias salen **idénticas al bit** venga el audio en bloques de 1 o de 4 096. Lo que se publica
es la ventana **efectiva** (0.2987 / 1.0027 / 3.0080 s medidos), no la pedida. Al volver a la lente después
de estar en otra, la ventana arranca limpia: el primer número no mezcla audio de antes de apagarse.

**La dispersión de los graves no es un defecto del módulo.** Un estimador de correlación sobre ruido tiene
`σ ≈ 1/√(2·BW·T)`, y una banda de ⅓ de octava mide `BW = 0.2316·fc`. Con 1 s de ventana, la banda de
10 kHz junta 2 316 grados de libertad (σ = 0.015) y la de 50 Hz junta 12 (σ = 0.21). Abajo hay menos señal
por segundo, y ningún analizador puede inventarla; lo que sí se puede es no esconderlo.

#### El espectrograma estéreo, y por qué su color no se puede leer de a un frame

La lente 10 es el mismo sonograma de la 4 con el color cambiado de significado:

- **el color es la fase** (coherencia por bin): rojo = fuera de fase · verde = ancho · blanco = mono;
- **el brillo es el nivel**, con el mismo mapeo de dB sobre `[−rango, 0]`. Una celda sin energía es
  **negra**, no "roja apagada": sin eso, el piso de ruido —donde la fase es puro azar— pintaría la
  pantalla de colores que no quieren decir nada.

La coherencia por bin es `coh_k = Σ Re(L_k·R_k*) / √(Σ|L_k|²·Σ|R_k|²)` **suavizada sobre la misma ventana
de la lente 9**, y es 0 **por definición** cuando un canal no tiene energía en ese bin: no hay dos fases
que comparar. En silencio la celda queda en el centro de la escala (128), que quiere decir *sin definir* —
un 0 diría "fuera de fase", que sobre silencio sería una alarma inventada.

**El suavizado no es cosmético.** La coherencia de un frame solo, sobre dos fuentes independientes, da
valores repartidos por todo `[−1, +1]`: el dibujo sería confeti. Sobre la ventana se queda alrededor de 0
**con dispersión** (medido: media 125.15 sobre 255 y desvío 16.32, o sea ≈ −0.02 ± 0.13 en coherencia), que
es lo que un estimador de coherencia hace de verdad. Con `L = R` lee **255 en toda celda con energía** y
con `L = −R` lee **0**: mono perfecto es +1, no "casi".

La **energía**, en cambio, NO se suaviza: es el brillo de un espectrograma y su eje X es el tiempo,
promediarla borraría justo lo que muestra (misma decisión que la columna del sonograma de nivel). Ojo con
compararla contra SPECTRUM: acá se suman **los dos canales** (`E_k = |L_k|² + |R_k|²`), así que una señal
mono lee 3.01 dB por encima de lo que lee el canal L solo.

Cuando varias columnas o varias filas del anillo caen en el mismo píxel, **gana la celda de más energía** y
sus dos números viajan juntos: promediar coherencias de celdas con niveles muy distintos dejaría que un bin
vacío decidiera el color de un píxel que en realidad tiene una sola cosa adentro.

Verificado: la señal mixta deja las filas de ≤ 160 Hz en **255** (criterio ≥ 240) y las de ≥ 2.5 kHz en
**0** (criterio ≤ 15); el silencio da energía **0** y coherencia **128** exactos; y dos corridas de la
misma señal dan anillos **idénticos byte a byte** en bloques de 512 y de 64.

### El análisis musical (lentes CQT y SPIRAL)

Las nueve lentes anteriores miden **física**: hertz, decibeles, fase, tiempo. Estas dos miden **música**, y
para eso hay que cambiar la transformada.

#### Por qué un FFT no alcanza para mirar notas

Un FFT reparte sus bins a distancias **iguales en hertz**. Con 4 096 puntos a 48 kHz cada bin mide 11.7 Hz,
y eso quiere decir dos cosas a la vez:

| Registro | Un semitono mide | Bins del FFT que le tocan |
|---|---:|---:|
| C1 → C#1 (32.7 Hz)  | 2.0 Hz  | 0.17 — **dos notas distintas caen en el mismo bin** |
| C4 → C#4 (261.6 Hz) | 15.6 Hz | 1.3 |
| C7 → C#7 (2 093 Hz) | 124 Hz  | 10.6 |
| C9 → C#9 (8 372 Hz) | 498 Hz  | 42 — **cuarenta bins para un semitono** |

Un espectro de FFT es un instrumento de física perfectamente bien construido. La música, en cambio, se mide
en **octavas**: un semitono es siempre el mismo intervalo, suene donde suene.

#### La transformada constant-Q

La transformada de Q constante (Brown 1991; Brown & Puckette 1992) reparte los bins a distancias iguales en
octavas y le da a **cada bin su propia ventana**, tan larga como haga falta para que su ancho de banda
relativo sea siempre el mismo:

```text
B     = 24 bins por octava (dos por semitono, o sea un cuarto de tono por bin)
f_min = 27.5 Hz  (A0, la nota más grave de un piano)
f_max = min (20 kHz, 0.45·fs)          → a 48 kHz, 229 bins
f_k   = f_min · 2^(k/B)
Q     = 1 / (2^(1/B) − 1) = 34.127     → el mismo Q en las nueve octavas
N_k   = round (Q · fs / f_k)           ventana de Hann, una por bin
```

El tope de 0.45·fs no es superstición: el bin más agudo es el de ventana más **corta** y por lo tanto el de
lóbulo más **ancho**, y ese lóbulo tiene que caber por debajo de Nyquist.

#### La latencia es inherente, y por eso se declara

Cada bin ve `N_k` muestras hacia atrás. Como `N_k = Q·fs/f_k`, la latencia **se divide por dos en cada
octava** (medido a 48 kHz):

| Nota | Frecuencia | Ventana | Latencia |
|---|---:|---:|---:|
| A0 | 27.5 Hz    | 59 567 muestras | **1.2410 s** |
| A2 | 110 Hz     | 14 892          | 0.3103 s |
| A4 | 440 Hz     | 3 723           | 0.0776 s |
| A6 | 1 760 Hz   | 931             | 0.0194 s |

No hay forma de tener resolución de un cuarto de tono en 27.5 Hz sin escuchar un segundo largo: es física,
no una decisión de implementación. TELESCOPE la **declara** —en el frame, en el rótulo `A0 · 1.24 s` del pie
de las dos lentes y acá— en vez de esconderla. La parte grave del dibujo está mostrando un momento del audio
que ya pasó, y eso hay que poder saberlo.

Lo que sí es una decisión, y va en la dirección correcta, es dónde se apoya la ventana de cada bin: el
soporte del kernel va pegado al **final** del bloque, no al centro. Centrados, todos los bins —incluso los
de 20 kHz— estarían mirando 0.68 s en el pasado; pegados al final, cada bin ve las últimas `N_k` muestras y
nada más, así que los agudos son casi instantáneos y **sólo el grave paga su ventana**.

#### Cómo se calcula, y qué cobra la poda

Hacer 229 filtros a mano sería inviable. En su lugar (Brown & Puckette 1992) se transforma **una sola vez**
el bloque entero —una FFT de 2^16 a 48 kHz, la mínima que contiene la ventana de A0— y cada bin sale de un
producto interno con su **kernel espectral**:

```text
X_cq[k] = Σ_j  X[j] · conj (K_k[j])          sólo sobre los j guardados
```

De cada `K_k` se guardan sólo los coeficientes por encima de `0.0054 · max|K_k|` (el umbral del paper).
Medido a 48 kHz: **187 930 coeficientes, el 2.50 % del denso**, armados en **102 ms** una única vez. Sin esa
poda habría que multiplicar 229 × 32 769 complejos por frame.

La poda cobra un **piso de faldas**: al recortar el kernel, su respuesta gana un fondo del orden del propio
umbral. El derivado sería 20·log10(0.0054) = −45.35 dB; medido sobre un A4 solo, la peor falda queda **67.5
dB por debajo** del pico. Alrededor de una nota fuerte no hay silencio absoluto, y eso que se ve en pantalla
tiene nombre y número.

#### La referencia de dB: la misma de SPECTRUM

Un seno de amplitud `A` exactamente en `f_k` lee **20·log10(A)**, igual que en la lente 3 y por la misma
cuenta, pero con la ganancia coherente **de la ventana de ese bin** (`CG_k = Σw/N_k`, calculada por bin, no
copiada de una tabla: cada bin tiene una ventana distinta). Medido con un seno a −20 dBFS:

| Nota | Bin | Lectura |
|---|---:|---:|
| A0 | 0   | −19.998 dB |
| A4 | 96  | −19.997 dB |
| A7 | 168 | −20.000 dB |

**El mismo número en tres octavas** es toda la prueba de que la normalización es por bin.

Y el cruce que obliga a que las dos lentes no se contradigan en pantalla: el mismo seno de 1 kHz a −20 dBFS,
leído por el constant-Q y por el FFT del prompt 50, corregido cada uno por su propio scalloping de Hann,
difiere en **0.016 dB**.

El eje muestrea, así que un tono entre dos bins lee un poco menos: medio bin de corrimiento pierde **1.361
dB** (el scalloping de Hann, cuyo tope derivado es 1.424). Y la resolución es la que promete: un semitono
(dos bins) se ve como **dos** máximos separados; 25 cents (medio bin), como **uno solo**.

#### El cromagrama

Doce números: cuánta potencia hay en cada **clase de nota**, sumada sobre todas las octavas. El bin `k` cae
en el semitono `⌊k·12/B⌋` y su clase es `(semitono + 9) mod 12` — el bin 0 es A0, y el +9 lleva el La a la
posición 9 de la numeración `Do=0 … Si=11`. Se normaliza al máximo (0…1), y hay además una versión suavizada
sobre 0.5 / 2 / 5 s, que es la que alimenta la tonalidad.

Los vecinos **no quedan en cero, y eso es física**: con `B = 24` y el `Q` canónico, un bin del eje es un bin
de la DFT de su propia ventana, así que la falda de Hann deposita 0.25 de la potencia en el bin de al lado.
Como la clase de la nota se queda además con su propio vecino de arriba, la relación **derivada** es
`0.25 / 1.25 = 0.20`, y es exactamente lo que se mide: cinco A en cinco octavas dan `La = 1.000` y
`Sol# = 0.196`, con las otras diez por debajo de 0.005 (ése es el valor **medido**; el margen que el test
exige es 0.05, diez veces más laxo — LOW/NIT 3 del revisor del 52). La tríada de Do mayor da `Do = Mi = Sol = 1.000` y
sus tres vecinos de abajo en 0.196.

#### La tonalidad estimada, y por qué nunca es una certeza

Se correlaciona el cromagrama suavizado contra los 24 perfiles de **Krumhansl & Kessler (1982)** rotados a
cada tónica (el método de Krumhansl-Schmuckler), con correlación de Pearson, y gana el más alto:

```text
mayor   6.35  2.23  3.48  2.33  4.38  4.09  2.52  5.19  2.39  3.66  2.29  2.88
menor   6.33  2.68  3.52  5.38  2.60  3.53  2.54  4.75  3.98  2.69  3.34  3.17
```

La lente muestra **siempre** tres cosas juntas —`La menor · confianza 0.83 · 91 % del tiempo`— y nunca la
tonalidad sola. Las dos razones son medibles:

**1. La confianza tiene piso.** Es el máximo de 24 correlaciones, así que aun con un cromagrama
perfectamente plano alguna de las 24 gana por azar. Ruido rosa de 6 s da **0.541** con un cromagrama plano
(3.87 dB de máximo/mínimo): no porque tenga media tonalidad, sino porque eso es lo que da cualquier cosa.
Con 12 s de material la confianza se cae sola a **0.381**. Lo que de verdad delata al ruido es el **% del
tiempo**: 37 % y 3 % contra el 98–100 % de una tonalidad de verdad.

**2. Hay música genuinamente ambigua, y el estimador no puede resolverla.** Una tríada **pelada** de tres
notas —C-E-G, todas al mismo nivel, sin bajo— correlaciona 0.790 con Do mayor y **0.809 con Mi menor**: gana
Mi menor. No es un error; el perfil menor pondera su ♭6 con 3.98, y esas tres notas en un compás real
podrían ser las dos cosas. Con la tónica doblada en el bajo —la forma normal de tocar una tríada en estado
fundamental— Do mayor gana con **0.845** contra 0.613. Un instrumento que mostrara "Do mayor" a secas en el
primer caso estaría eligiendo por el usuario.

Los casos que sí se afirman, medidos: tríada de Do mayor con bajo → **Do mayor, 0.845, 98.3 % del tiempo**;
La menor sostenida con arpegio A-C-E-G → **La menor, 0.868, 100 %**; silencio → **sin tonalidad** (tónica y
modo en −1, confianza 0), porque "no sé" es un resultado y "Do mayor con confianza 0" sería peor que no
decir nada.

#### La lente CQT (la 6)

El espectro por notas: una barra por bin sobre el eje de notas —los bins **impares**, que son los cuartos de
tono entre notas, van más tenues— con el eje de dB de SPECTRUM, una marca en cada Do y un **teclado de 114
teclas** dibujado a escala debajo, para que cada barra esté parada sobre su propia tecla. El peak hold usa el
mismo setting de decaimiento que SPECTRUM: es la misma perilla, no una copia. Al pasar el cursor: nota,
octava, **cents** y el dB del bin — los cents salen de la *posición* del cursor y no del índice del bin (que
sólo podría dar 0 o 50), así que se lee "A4 +18 ¢". Abajo, el cromagrama en doce barras con la tónica en
ámbar y la tonalidad con sus dos números.

#### La lente SPIRAL (la 7)

El mismo constant-Q **enrollado**: si una octava es una vuelta, las notas iguales quedan en el mismo ángulo.

```text
ángulo = clase de nota   ·  Do arriba, en sentido horario, una vuelta = una octava
radio  = octava          ·  A0 adentro, el bin más agudo afuera, lineal por octava
púa    = magnitud        ·  brillo y grosor siguen el nivel; bajo el piso del rango no se dibuja nada
```

Un Do en C2, otro en C4 y otro en C6 dejan de ser tres barras lejanas en un eje largo y pasan a ser tres púas
**alineadas sobre el mismo radio**. Eso es lo que la lente 6 no puede mostrar y ésta sí: la estructura de
octavas de lo que está sonando, de un vistazo. El mapeo es `turns(k) = log2(27.5/16.3516) + k/B`, y como esa
constante vale **0.75 exacto**, la parte entera de `turns` es la octava científica y la fraccionaria es la
clase de nota.

En el centro, la rueda de croma: doce sectores en el **mismo ángulo** que las púas de su clase, con la tónica
en ámbar y la tonalidad adentro. La lectura toma la octava del **radio** y la clase del **ángulo**, que es
exactamente cómo se lee el dibujo.

Un detalle que conviene saber para no leer de más: la espiral es de **Arquímedes**, así que una
circunferencia la cruza en un solo punto —el rayo de Do—. Los círculos de guía no son "la octava n": son el
**radio donde empieza** la octava n, y por eso el rótulo va justo ahí.

---

### La lente WATERFALL

El mismo espectrograma de la lente 4 puesto **en profundidad**: frecuencia en X (log, 20 Hz – 20 kHz),
**nivel en Y** y tiempo en Z, con "ahora" adelante y el pasado alejándose al fondo.

**Qué agrega sobre el sonograma**, que muestra exactamente los mismos datos. El sonograma pone el nivel
en el *color*, y el ojo compara colores mal: dos verdes separados por 6 dB se ven casi iguales, y un pico
angosto de 12 dB sobre su entorno pasa desapercibido. Acá el nivel es **altura**, que es la magnitud que
el ojo compara mejor que ninguna otra. A cambio se pierde lo que el sonograma hace mejor: con 120 líneas
tapándose entre sí, un evento corto puede quedar escondido detrás de uno posterior. Son complementarias
y por eso están las dos.

**Las líneas.** `LINEAS` elige 60, 90 (default) o 120, repartidas uniformemente sobre la historia que
tenga el anillo (`HISTORIA`: 10 / 30 / 60 s). La línea del fondo es siempre la columna más vieja que
todavía está en el anillo y la de adelante la que se acaba de escribir; el reparto es aritmética entera
sobre el índice de escritura, así que el mismo anillo da **siempre** las mismas columnas. Si el anillo
tiene menos columnas que líneas pedidas se dibujan las que hay: repetir una columna dibujaría un relieve
que la señal no tiene. Cada línea son ≤ **256 puntos** (las 512 filas del anillo decimadas de a dos,
tomando el máximo del par — promediar borraría los picos angostos, que es lo que uno mira en un
waterfall).

**Por qué no hay GPU** (D-46 del diseño). Metal sólo existe en macOS y el contexto OpenGL de JUCE lo
pelean varios hosts con su propio dibujo. Un analizador que en Windows —o en el host equivocado— muestra
dos lentes menos no es el mismo producto. Así que la proyección es **oblicua por software**: sin división
por z, tres multiplicaciones por punto, con los conteos acotados por diseño (≤ 120 líneas × 256 puntos).
Es oblicua y no en perspectiva porque una perspectiva real divide por z, y esa división rompe dos cosas
que en un instrumento valen más que el realismo: que la misma distancia en frecuencia mida lo mismo a
cualquier profundidad, y que la proyección sea monótona (no hay punto de fuga adentro del dibujo).

**La oclusión.** Cada línea tapa lo que tiene detrás. El algoritmo del pintor literal —de atrás hacia
adelante, cada línea rellenando por debajo con el fondo antes de trazarse— da el dibujo correcto y es
imposible de pagar: con 120 líneas sobre un plot de 950×400 el relleno son decenas de millones de píxeles
por frame, contra un presupuesto de 4 ms. Se dibuja **del frente al fondo con un horizonte**, y el
resultado en pantalla es **el mismo**: como la proyección es estrictamente decreciente en z, el suelo de
un plano lejano está siempre por encima del de uno cercano, y ninguna curva baja de su propio suelo; por
lo tanto una línea es visible exactamente donde queda por encima de todas las que tiene delante. Medido:
**1.334 ms de mediana y 1.641 de p95 con las 120 líneas**, criterio 4 / 8.

**Con reduced-motion la lente sigue corriendo**, igual que los dos sonogramas: su eje de profundidad *es*
el tiempo. Y no hay nada suavizado que apagar — cada línea es una columna del anillo tal cual.

La lectura al pasar el cursor da frecuencia y dB **de la línea de adelante**, la única que se puede leer
sin ambigüedad: en profundidad una misma columna de píxel cae sobre varias líneas a la vez. El número
sale del byte del anillo con la misma decimación con la que se dibujó, no del color del píxel.

### La lente FIELD

Energía por **dirección de paneo × frecuencia**, con decaimiento y estela temporal, en 2.5D.

#### Qué mide, y qué NO

Mide **paneo por energía**. Por bin `k` de la STFT, sobre la misma ventana deslizante con la que se
calcula la coherencia:

```
pan_k = (ΣRR_k − ΣLL_k) / (ΣRR_k + ΣLL_k) ∈ [−1, +1]
        −1 = sólo L   ·   0 = centro (o sin energía)   ·   +1 = sólo R
```

Con la ley de potencia constante (`L = cos θ·x`, `R = sin θ·x`) el número es exacto y redondo:
`pan = sin²θ − cos²θ = −cos 2θ`. Verificado bin a bin con **error peor de 4.9·10⁻⁸**:

| θ | 0° | 22.5° | 45° | 67.5° | 90° |
|---|---|---|---|---|---|
| `pan` | −1.000 | −0.707 | 0.000 | +0.707 | +1.000 |

**NO mide la posición de las fuentes.** De una mezcla estéreo terminada ese problema no tiene solución
única, y es exactamente la trampa en la que cayó el ITD de ÓRBITA: el retardo interaural implementado
valía el 8 % del físico y con el signo invertido, y el plugin lo llamaba "posición" (auditoría del
2026-09-03). Un analizador del mismo sello no puede repetir ese error en la lente que más invita a
cometerlo. Por eso, y sin poder apagarse:

- el eje se rotula **L … C … R**, nunca `−90° … +90°` como si fuera azimut binaural;
- debajo del eje va, fijo, **"paneo por energía L/R · no localización"**;
- la lectura da el paneo en **porcentaje**, no en grados.

**Tampoco distingue mono de fuera de fase.** Una señal con `L = R` y una con `L = −R` tienen la misma
energía en los dos canales, así que las dos dan `pan = 0` y las dos se dibujan en el centro. Es correcto
—FIELD mide balance de nivel, no fase— y es justo lo que las lentes 9 (BAND CORRELATION) y 10 (STEREO
SPECTROGRAM) sí muestran. Las tres se leen juntas.

**Lo que sí muestra y ninguna otra puede.** Los cuatro números de banda ancha de una mezcla con dos
fuentes duras en canales opuestos son casi idénticos a los del ruido decorrelacionado (`corr ≈ 0`,
`width ≈ 1`): para un correlímetro las dos "suenan igual de anchas". En FIELD una es dos manchas en
esquinas opuestas y la otra una nube pareja de lado a lado. Esa diferencia es todo el punto de la lente.

#### La grilla y sus bordes

96 filas × 64 columnas.

- **Frecuencia**: 96 filas log sobre 20 Hz – 20 kHz (tres décadas exactas), por **bordes**: la fila `r`
  cubre `[20·1000^(r/96), 20·1000^((r+1)/96))`, o sea 7.48 % de ancho relativo cada una — 32 filas por
  década ≈ 9.63 por octava. Todas miden lo mismo; no hay media celda en los extremos. El centro
  geométrico (el que rotula el eje y devuelve la lectura) es `20·1000^((r+0.5)/96)`.
- **Lo que queda afuera se descarta.** Un bin por debajo de 20 Hz o por encima de 20 kHz **no** se apila
  en la fila del borde. Apilarlo encendería la fila 0 con el DC y los subgraves inaudibles, y el grave
  más fuerte del dibujo sería algo que nadie escucha. Con FFT de orden 12 a 48 kHz son los bins 0 y 1.
- **Dirección**: 64 columnas sobre `pan ∈ [−1, +1]`, por **centros**: la columna `c` está en
  `−1 + 2c/63`. La 0 es "sólo L", la 63 "sólo R" y el centro exacto (`pan = 0`) cae **entre** la 31 y la
  32. Cada bin reparte su energía **linealmente entre sus dos columnas vecinas y nada más**: sin kernel
  más ancho, porque ensanchar una fuente puntual sería dibujar una anchura que la medición no tiene.

**Un límite honesto de esa resolución**: por debajo de ~1.5 kHz una fila de la grilla es *más angosta que
un bin* de la STFT (a 100 Hz la fila mide ~7.5 Hz y el bin de una FFT de 4 096 a 48 kHz mide 11.72 Hz).
Un tono grave cae entre dos bins que van a parar a filas distintas y su energía queda repartida en dos,
así que **lee más bajo que un tono agudo de la misma amplitud**, que se lleva sus dos bins en una sola
fila. Medido con dos tonos idénticos: el de 5 kHz lee 0.0 dB rel y el de 100 Hz, −2.9.

#### El decaimiento y la estela

Por frame, `grid *= exp(−dt/τ)` y después cada bin suma su energía. Es un promediado exponencial: sin
señal nueva la grilla cae a `1/e` en exactamente τ. `DECAIMIENTO` elige 0.5 / **1** / 2 s; medido
0.512 / 1.003 / 2.005 s. No es decoración — a 21 ms por frame, sin él la lente sería confeti.

La estela son las **8 grillas anteriores** decimadas a 48×32 (máximo de cada bloque 2×2), alejándose y
apagándose; se dibujan **3 de las 8** (ocho sábanas translúcidas apiladas cuestan mucho y se leen como
niebla). La lámina más vieja es la del frame `n−8`; ninguna es la grilla de ahora, que se dibuja adelante y
aparte. Con **reduced-motion la estela no se dibuja** (queda sólo la grilla actual) y el suavizado de la
normalización se apaga: son las dos únicas cosas de esta lente que se mueven sin ser el dato.

Verificado por píxel **en las dos direcciones**: en la franja a la que sólo puede llegar la estela no queda
encendido ni un punto de la paleta con reduced-motion, y en la franja de abajo a la que sólo puede llegar
el plano de AHORA sí lo hay. Las dos mitades hacen falta. Hasta el prompt 56b faltaba la segunda, y por eso
nadie vio esto: con la estela saturada en 8 láminas y un paso de 3, el bucle de capas recorría 8 → 5 → 2 →
−1 y **nunca visitaba la grilla del presente**. En uso normal la lente mostraba tres parches de estela
sueltos y el dato de ahora no se dibujaba nunca. El conteo de celdas no lo delataba porque las estelas
solas ya superan el criterio; hoy se cuentan aparte (**0 celdas vivas antes, 2 754 después**).

**El costo dejó de depender de la señal** al pasar de nube de puntos a superficie: se dibujan siempre las
mismas cuatro imágenes chicas —el plano de adelante 64×96 bilineal más tres láminas 32×48 por vecino más
cercano— estiradas al rectángulo de su profundidad. El peor caso y el mejor son el mismo caso, que es una
cota estructural y no un tope que haya que acordarse de aplicar.

**Las tres láminas de estela se recorren por CELDAS, no por pantalla** (prompt 56c). Sin bilineal, el valor
de un píxel es exactamente el de *una* celda: los dos pesos de interpolación valen cero. O sea que el bucle
le preguntaba a cada uno de los ~600 000 píxeles de un plano a qué celda pertenecía, y pagaba dos búsquedas
de tabla, cuatro cargas, tres interpolaciones y una entrada de paleta para repetir la misma respuesta
cientos de veces seguidas. Como el mapeo píxel → celda es monótono en los dos ejes, cada celda cubre un
tramo contiguo de columnas y otro de filas: se arman los dos tramos una vez por plano y se recorren las
32×48 celdas. Las que están en cero —tres cuartos de un plano de estela— no cuestan un solo píxel, y las
que encienden pagan el índice de paleta y el alfa una sola vez, fuera del bucle interior.

Medido en el M4 de la casa, tamaño L, con la máquina quieta: **de 3.615 ms de mediana a 1.515**, criterio
4 / 8, k = 0.98. **No cambia un píxel**: los cuatro snapshots salen byte-idénticos a los de antes del
cambio, verificados con la misma invocación de Catch2 en las dos compilaciones. Y el presupuesto pasó a
exigir además **mediana típica ≤ 3.2 ms** (20 % de margen): la auditoría del 56b puso a esta lente roja dos
veces sin que el código cambiara —4.15 ms con un `pluginval` al lado, 5.82 con cuatro procesos comiendo
núcleos— porque con la mediana en 3.6 de un criterio de 4 cualquier ruido la cruzaba. Con el margen puesto,
las mismas condiciones dan 1.53–1.61.

**El dB de la lectura es RELATIVO** al máximo de la grilla, y está rotulado `dB rel`. La celda acumula
energía por un promedio exponencial, así que su valor absoluto depende de τ y de la tasa de frames;
convertirlo a dBFS pediría dividir por `τ · frames_por_segundo`, que es una aproximación válida sólo en
régimen. Un número absoluto aproximado en un medidor es peor que uno relativo exacto — y el nivel
absoluto ya lo dan SPECTRUM y SPECTROGRAM, que lo miden sin aproximar.

**El ancho de la nube depende de la ventana.** Con material decorrelacionado el paneo por bin se reparte
alrededor de 0 con una dispersión que es la del *estimador*: cuantos más frames entran en la ventana, más
angosta. Con 1 s la dispersión medida es 0.137. O sea que `VENTANA` (0.3 / 1 / 3 s, la misma de las
lentes 9 y 10) cambia cuánto se abre la nube. Lo que **no** es, es una medida de ancho estéreo en grados.

---

### La lente TONAL BALANCE (y el análisis de archivo)

La pregunta que contesta no es "¿cuánto suena?" sino **"¿de qué color suena, comparado con esto otro?"**.
Se carga un track de referencia —el tuyo, o uno comercial—, TELESCOPE lo analiza **entero, offline**, y la
lente compara tu programa en vivo contra él.

#### Se compara el tilt, no el nivel

Las dos curvas de ⅓ de octava se normalizan restándoles **su propio LUFS integrado**:

```
norm[b]  = curva[b] − LUFS_integrado          (cada lado con el suyo)
delta[b] = live_norm[b] − ref_norm[b]
```

El mismo ruido rosa a −14 y a −20 LUFS da **delta 0 en todas las bandas** (verificado: el peor caso sobre
24 bandas entre 50 Hz y 10 kHz es 0.66 dB, y ese resto es la varianza del propio ruido con 10 s). Si no se
normalizara, el gráfico contestaría "cuál está más fuerte", que ya lo dice el medidor de loudness.

#### Y la consecuencia, dicha en voz alta: **el delta suma cero**

Normalizar por loudness tiene un precio que hay que entender antes de mirar el gráfico. Si le subís 6 dB a
todo lo que está arriba de 2 kHz, **la loudness sube con eso**, y el delta no muestra "+6 arriba y 0
abajo". Muestra esto (medido, no estimado):

| | delta |
|---|---|
| el shelf aplicado | +6.00 dB arriba de 2 kHz |
| lo que subió la loudness integrada | +4.30 dB |
| delta que dibuja la lente ≥ 8 kHz | **+1.70 dB** |
| delta que dibuja la lente ≤ 500 Hz | **−4.30 dB** |

Los dos números son la **misma verdad**: la diferencia entre las dos regiones sigue siendo exactamente
6 dB. Lo que cambia es que está contada **a igual volumen**, que es como se compara una mezcla contra una
referencia. La identidad se verifica banda por banda en `REF[tilt]`:

```
delta[b] + (I_programa − I_referencia)  ==  respuesta del filtro en la banda b
```

y se cumple con un error máximo de **0.006 dB** sobre 21 bandas.

#### Lo que la lente dibuja

- **arriba** las dos curvas normalizadas sobre la rejilla logarítmica de SPECTRUM, cada una rotulada con
  su nombre y su integrado. Escala **fija** de +6 a −42 dB: una escala que se auto-ajusta hace que dos
  capturas de la misma mezcla no se puedan comparar, que es justo para lo que la lente existe.
- **abajo** el delta como barras de ±12 dB, con una banda de referencia de **±3 dB rotulada**. Es una
  **referencia, no un veredicto**: acá no dice "está mal", dice cuánto y dónde. Quien concluye —con el
  número y la regla al lado— es VERDICT.
- **al pasar el cursor**, la banda con sus tres números y **cuántos bins la midieron**.
- **RESET** reinicia el promedio del programa y **no** descarga la referencia: se tira lo medido, no lo
  que configuraste.

Un vértice por **banda** (30), no por píxel: ponerle más sería inventar resolución que el dato no tiene.
Una banda que sólo mide uno de los dos lados **no se dibuja en delta 0** —eso sería decir "coincide
perfecto"— sino que corta la curva y marca su barra en gris.

#### El análisis de archivo es idéntico al análisis en vivo

No "parecido": **idéntico al bit**. El motor decide las posiciones de hop y de frame contando muestras
desde el reset, no según el tamaño de bloque del host (contratos `casa-4` y `SPEC[bloque]`), y el análisis
offline usa **las mismas clases**, no una segunda implementación. Verificado por el camino completo
—`processBlock` → bus → `AnalysisThread` → `Spectrum` → `FrameSink` → `Reference`— contra el mismo WAV
leído por `FileAnalyzer`:

| | offline vs vivo |
|---|---|
| integrado, LRA, true-peak, máximos M/S | iguales **al bit** |
| las 29 bandas de ⅓ de octava con medición | iguales **al bit** |
| bloque de 4 096 vs 512 vs 64 vs 7 vs 1 | iguales **al bit** |

**Formatos**: WAV, AIFF, FLAC y Ogg siempre; en macOS además todo lo que lea CoreAudio (MP3, AAC, ALAC), y
en Windows WMA y MP3. Es lo que da JUCE de fábrica: no hay un decoder propio, y por lo tanto tampoco un
formato que "casi" se lee. Se acepta **cualquier** archivo arrastrado, no sólo las extensiones conocidas:
si no se puede leer, el mensaje lo dice. Rechazar en silencio un AIFF llamado `.dat` es peor que intentarlo.

**El sample rate es el del archivo.** No se remuestrea: el motor se prepara a la SR del archivo y lo mide
tal cual está. Ni los ⅓ de octava ni el LUFS dependen de la SR, así que una referencia a 44.1 k es
directamente comparable contra un programa a 48 k. Remuestrear sólo agregaría un filtro más entre el
archivo y su propia medición.

**Velocidad**: 60 s de audio en 243 ms en el M4 (247× tiempo real), con progreso y cancelación (`cancel()`
vuelve en 2 ms).

#### La corrección de ancho cubierto (y en qué se diferencia de SPECTRUM)

Una banda de ⅓ de octava en los graves se lleva **uno o dos bins**, y *cuáles* depende de dónde caiga la
rejilla de la FFT. Sumarlos crudo hacía que el mismo ruido rosa leyera **3.415 dB distinto** según el
sample rate del archivo — justo el sesgo que se comería el delta cuando el archivo y la sesión no están a
la misma SR. Por eso acá la potencia de la banda se estima como **densidad × ancho nominal**:

```
P_b = ( Σ_k P_k ) · (hi − lo) / ((k1 − k0) · binHz)
```

Con eso el peor caso baja a **0.333 dB**, y a **0.195 dB** en las bandas con 4 bins o más. Lo que queda no
es sesgo sino la **varianza del estimador**: dos bins sobre 10 s de ruido no miden con la misma precisión
que doscientos, y por eso las bandas de menos de 4 bins se reportan aparte (peor caso 0.699 dB a 50 Hz).

SPECTRUM **no** hace esta corrección: sus barras de ⅓ de octava son la suma cruda, que es la convención de
un RTA. En las bandas graves las dos lecturas difieren, y es a propósito: la de TONAL BALANCE es la que se
puede comparar entre sample rates.

#### El path persiste; los números no

Lo que se guarda en el estado es la **ruta** del archivo. Al abrir una sesión guardada, si el archivo sigue
estando se **vuelve a analizar**; si no está, la lente dice `referencia no encontrada: <nombre>` y no
dibuja ninguna curva. Guardar 30 números en el preset y dibujarlos como si fueran el archivo sería mostrar
una referencia que ya no existe, sin que nadie pudiera notarlo.

Los otros estados, todos con palabras: `sin referencia · arrastrá un archivo`, `analizando <nombre> · N %`
(con barra de progreso), y el error del análisis cuando el archivo no se puede leer.

#### Lo que `FileAnalyzer` deja para VERDICT (lente 13)

Además de la curva y los números de loudness, el análisis de archivo publica dos **series temporales de
cadencia fija y declarada**, que son las que le van a permitir a VERDICT decir "qué falta y **dónde**" con
tiempos exactos:

| serie | cadencia | contenido |
|---|---|---|
| `shortTermHistory` | **10 Hz exactos** (un hop de 100 ms) | LUFS short-term |
| `truePeakPerSecond` | **1 Hz exacto** (diez hops) | dBTP máximo de cada segundo |

Son las mismas posiciones del stream que usa el análisis en vivo — por eso VERDICT va a poder decir "en el
segundo 47" y que sea el mismo segundo 47 que se vería en tiempo real. La cola de menos de un segundo
también cuenta (un archivo de 10.4 s tiene once puntos de true-peak, el último incompleto): descartarla
escondería justo el final, que es donde suele estar el pico.


### La lente VERDICT (y la historia por segundo)

Doce lentes muestran. Ésta **dice**. Y la regla de hierro es que **cada frase lleva el número que la
sostiene y el id de la regla que la produjo**, los dos visibles al mismo tiempo.

El gemelo legible de la tabla de reglas —cada umbral con su porqué, en castellano— está en
[`docs/telescope-diccionario.md`](docs/telescope-diccionario.md).

#### Qué es, y qué NO es

**Es un motor de reglas determinista.** Sin IA, sin red, sin modelo: una tabla de umbrales
(`source/data/Rules.h`) y las condiciones que los leen. La misma señal da el mismo informe, palabra por
palabra, hoy y dentro de un año. Corre en la máquina y no manda nada a ningún lado.

**No opina.** Está prohibido que diga "suena profesional", "emociona" o "está listo". Si alguna vez lo
dice, es un bug.

**"Sin hallazgos" no es "está listo".** Cuando no encuentra nada, lo que dice es *"Nada fuera de rango en
estas reglas. Miden; lo que no escuchan es tuyo."* (desde el 57d; hasta ahí decía «eso no es lo mismo que
"está listo"»). La diferencia es todo el producto: las reglas cubren lo que cubren, y lo que no está en la
tabla no se mide.

Un **hallazgo** es ⚠ o ●. Las líneas ○ —la tonalidad, una caja que sale ✓, la distancia a la plataforma—
son informativas y no son defectos.

#### Contra qué compara

Las reglas de la sección 1 preguntan si una región sobra o falta. Sobra **respecto de qué**, y hay dos
respuestas — la frase dice siempre cuál usó:

| Situación | Contra qué | Qué significa |
|---|---|---|
| **con referencia cargada** | contra la referencia, comparadas por su **forma** (cada curva menos su propio nivel de banda ancha) | "esta región no se parece a la mezcla que elegiste como objetivo" |
| **sin referencia** | contra la **tendencia del propio material**: recta de mínimos cuadrados sobre la forma del programa en log de frecuencia, entre 50 Hz y 16 kHz | "esta región no se parece al resto de tu propia mezcla" |

La segunda es deliberada. La alternativa habitual —tener escondida en el código una "curva de una buena
mezcla" y medir contra ella— es opinar disfrazado de medir. Contra la propia tendencia no hay nada que
opinar. **A cambio es un instrumento grueso**: una mezcla con una intención espectral fuerte puede
disparar una regla sin que haya nada que arreglar. **Cargar una referencia es estrictamente mejor.**

#### Los chequeos por dispositivo son genéricos, y lo dice

Las seis cajas de la sección 2 (celular, auriculares, laptop, auto, club con sub mono, hi-fi) **no son
mediciones de ningún parlante real ni simulaciones, y no hay ninguna curva de respuesta adentro del
plugin**: cada caja es una definición de qué clase de sistema es —dónde empieza a responder, dónde se cae,
qué le pasa al estéreo— y su chequeo mira el material contra esa definición. Sirven para decir "esta mezcla
apoya casi todo donde un teléfono no llega". Eso es un pronóstico, y sale rotulado como pronóstico.

Hasta el prompt 56, `data/DeviceProfiles.h` traía además un array de 30 valores en dB por dispositivo que
**ningún chequeo leía**. Se sacó en el 56b: era código muerto que contaba una historia falsa — quien
abriera el archivo veía treinta números por caja y concluía que el plugin simula parlantes, que es
exactamente lo que D-47 prohíbe. Si algún día hay curvas medidas, entran con su procedencia.

#### El pie, que no se puede sacar

> **Medición, no gusto. Chequeos por dispositivo genéricos. Rehacé el análisis tras cada cambio.**

#### La historia por segundo

Es lo que hace posible el "dónde". `analysis/SecondHistory.h` guarda 10 minutos a 1 Hz desde el último
RESET: por segundo, las 30 bandas de ⅓ de octava con su nivel, su correlación y su pérdida al monoficar,
el short-term mínimo y máximo, el pico real, los clips, el estéreo de banda ancha, la continua y la
tonalidad.

**Las filas del análisis de archivo son las del análisis en vivo, iguales AL BIT** (test
`HISTORY[identidad]`, 60 de 60 filas sobre un tema de un minuto). Por eso "hay un hueco entre 0:20 y 0:35"
quiere decir lo mismo mirando el archivo que escuchándolo.

Lo que había que resolver para poder prometer eso: el medidor come **hops de 100 ms exactos** y el
espectro come el **chunk** que se acaba de drenar, así que el espectro va siempre adelantado hasta un
chunk — y el chunk mide 4 800 muestras en vivo y 4 096 en el archivo. Si la fila se armara con "los frames
que llegaron hasta que cerró el segundo", los dos lados sumarían conjuntos distintos. Cada frame va al
**bucket del segundo que le corresponde por su posición en el stream**, y el bucket se cierra cuando
termina el hop número 10 de ese segundo — momento en el que todos los frames de ese segundo llegaron
seguro. Bloques de 1, 7, 64 o 4 096 muestras dan las mismas filas al bit.

**La parte espectral de la fila se paga sólo con la lente abierta.** El bit `kReference` enciende una FFT
fija de 4 096 (orden 12 · Hann · 75 % · L+R, los mismos settings con los que se analiza un archivo), y es
la que alimenta tanto a TONAL BALANCE como a la historia por segundo. Con VERDICT o TONAL BALANCE
cerradas, las filas siguen guardando loudness y continua —que son baratos— y **declaran en
`measuredModules` que no tienen espectro**, en vez de publicar treinta ceros que parecerían una medición.
Consecuencia práctica: si abrís VERDICT a mitad de un tema, el informe empieza ahí. Para tiempos exactos
de un tema entero está el modo **ARCHIVO**.

**Lo único que puede diferir entre archivo y vivo** es la tonalidad de la fila: en vivo se calcula con los
settings vigentes del CQT (la lente 6 permite elegir el canal) y en el archivo con los de fábrica. El
resto de la fila no depende de ninguna lente.

#### El idioma

El setting `language` es un **código ISO 639-1**, no un interruptor de dos posiciones. **Default `en`**
(D-50). Una tabla por idioma, buscada por clave **con fallback a inglés**: si a un idioma le falta una
frase, sale en inglés — nunca vacía y nunca con la plantilla sin resolver.

Hoy hay **seis**: `en` y `es` revisados, y `pt`, `fr`, `de` e `it` traducidos con los términos de la
industria (Integrated, Short-term, True Peak, Correlation, Width, Mono compatibility, Tonal balance, Key)
y **pendientes de revisión de un hablante nativo**. Agregar un idioma es agregar una tabla, cero código.

## Sistema visual

Las **trece** lentes se dibujan con un solo juego de decisiones, en `source/lenses/Look.h`. Existe porque,
con las lentes escritas por tandas, cada una había resuelto por su cuenta lo mismo —cuánto alpha lleva una
línea de rejilla, qué verde es "el dato", de qué tamaño va el número grande, qué rampa usa el mapa de
calor— y el resultado se veía como doce plugins parecidos en vez de un instrumento.

VERDICT nació en paralelo a `Look.h` y entró al sistema en el prompt 56b: tokens, métricas por tamaño, y
la jerarquía de separadores (el de la cabecera es el peso mayor, los de sección el menor). Su panel de
texto **envuelve de verdad**, con un `juce::TextLayout` armado al mismo ancho con el que se dibuja, así
que el alto reservado y el alto pintado son el mismo número por construcción. Lo anterior era
`drawFittedText (…, 3)`, que a partir de la tercera línea deja de envolver y APRIETA los glifos: medido
sobre una frase de 300 caracteres en el panel de 420 px de tamaño S, dibujaba **3 renglones donde el texto
pedía 4**.

**La regla que lo sostiene, y que un test verifica:** fuera de `Look.h` ninguna lente escribe un color
literal. Todo color sale de un token de ahí, y todo token de ahí sale de `ui-kit/Theme.h`, que es del
sello. `[telescope][visual]` lee los archivos de verdad y falla listando archivo y línea de cada
infracción.

| Qué | Cómo se verifica |
|---|---|
| **Jerarquía de rejilla** — dos pesos declarados (mayor: ejes, décadas, 0 dB · menor: subdivisiones) en vez de una sola hairline para todo | inspección; antes toda línea era `theme::line` al 15 % |
| **Pixel snapping** — `snap1px` alinea la hairline a la grilla de píxeles FÍSICOS | a escala 2, siete coords fraccionarios dan **2 filas físicas llenas y 0 parciales**; el contraejemplo sin snappear reparte alpha en 2 filas |
| **Dígitos tabulares** — `tabularFont` es la mono del sello (JetBrains Mono) | `-14.6`, `-88.8`, `-00.0` miden lo mismo: **spread 0.0000 px**; el contraejemplo proporcional difiere 9.442 px |
| **Una sola rampa secuencial**, monótona en luminancia | **0 caídas** en 256 entradas. Hace falta porque el color CODIFICA dB: si dos entradas se cruzaran en brillo, dos niveles distintos se verían igual de fuertes |

La rampa **bipolar** (correlación, fase, balance) NO es monótona ni debe serlo: ahí el ojo busca el CERO,
no ordena magnitudes. Lo que se verifica de ella es que el centro sea el punto menos saturado.

`Look.h` aporta además el glow acotado (**uno por lente**, 2–4 px: un glow en todo es un glow en nada),
la cajita de lectura unificada (la geometría sigue siendo la de `LensReadout.h`, que es la única fuente de
esa cuenta), el crosshair, y métricas propias por tamaño S / M / L — el padding uniforme en todo es
exactamente el "look de plantilla" que este plugin no quiere.

### Idioma

El setting `language` es un **código ISO 639-1** (`en`, `es`, `pt`, `fr`, `de`, `it`…), no un interruptor
de dos posiciones. Por defecto **inglés**. Agregar un idioma es agregar una tabla en
`source/lenses/Strings.h`: cero código en otro lado.

El fallback es **por clave**, no por tabla: si a un idioma le falta una frase, esa frase sale en inglés y
el resto del idioma queda intacto. Nunca sale vacía — una etiqueta en blanco en un medidor es peor que
una en el idioma equivocado, porque no se sabe si es un bug o un valor que no existe.

Entregados completos (**140 de 140** claves cada uno): **en · es · pt · fr · de · it**. Los cuatro últimos
están traducidos con los términos de la industria pero **sin revisión de hablante nativo**.

**Las trece lentes leen de ahí** (prompt 56b). Hasta el 56 sólo lo hacían SCOPE, FIELD y la tira: las otras
diez dibujaban literales en castellano, así que cambiar el idioma cambiaba tres lentes de trece y el plugin
no cumplía D-50 aunque tuviera las seis tablas completas. Lo sostiene un test que lee los 35 archivos que
dibujan y hace dos barridos —ningún valor de la tabla `es` que difiera del de `en` puede aparecer como
literal, y ningún literal puede llevar acento castellano—; sobre el commit anterior encuentra **41
infracciones** con archivo y línea.

**El selector es uno solo y está en la tira**, al pie, debajo de VERDICT. Muestra el endónimo ("Español",
"Deutsch") y no el código: quien no lee inglés tampoco sabe que su idioma se llama `de`. Antes el único
control vivía adentro de VERDICT, así que para leer SPECTRUM en castellano había que entrar a otra lente,
cambiarlo y volver. El editor escucha la propiedad en el **árbol**, no en el control: así repintan la tira
y la lente visible venga el cambio de donde venga (el chip, un preset, o el estado que restaura el host).

Dos detalles de ese listener, que el prompt 56c cerró. `juce::ValueTree::Listener` avisa de forma
**síncrona y en el hilo que escribió**, así que aplicar el idioma —que toca la tira y pide repaint— cuelga
de ahí sólo si se pasa por el message thread; un preset cargado desde el hilo de automatización del host
tocaba componentes desde ese hilo. Y `apvts.replaceState()` —lo que corre cuando el host restaura la
sesión— avisa por `valueTreeRedirected`, **no** por `valueTreePropertyChanged`: no cambia ninguna
propiedad, cambia el árbol al que se apunta. Sin escuchar lo segundo, con la ventana abierta el idioma
guardado en la sesión no llegaba nunca a la pantalla.

**Los nombres de nota** siguen la convención del idioma, que no es una traducción sino **tres** sistemas
distintos y vivos: letras (C D E) en el mundo anglosajón, solfeo (Do Re Mi) en el románico, y el germánico,
que es el de letras salvo en dos posiciones — el Si se llama **H** y el Si♭ se llama **B**. Eso último no es
una preferencia de escritura: en esa convención "B" *es* el Si bemol, así que mostrarle "B" a un alemán
donde suena un Si le nombra otra nota, un semitono más arriba. Hasta el prompt 56b el alemán compartía la
tabla del inglés y hacía exactamente eso.

El sufijo mayor/menor sale de la misma tabla que usa VERDICT, y desde el 56c la **tónica también**: la
cabecera de VERDICT tenía su propio array de letras inglesas adentro del pintado, así que la misma
tonalidad salía "Do menor" en CQT y "C menor" en VERDICT, en la misma pantalla y en cinco de los seis
idiomas. Ahora las dos llaman a la misma función, y hay un test que compara las 24 tonalidades de las 6
lenguas contra una tabla escrita a mano —no basta con que las dos lentes coincidan, porque si se rompen
juntas la igualdad sigue en verde.

El nombre **con octava** (`A4`, `C#3`) se queda en anglosajón a propósito y no es una inconsistencia: ahí se
rotula un *eje*, y los ejes de los analizadores del mundo dicen C1, no Do1 ni H1.

Los términos son los de Insight / SPAN / BS.1770 (Integrated, Short-term, Momentary, Loudness Range,
True Peak, Correlation, Width, Mono compatibility, Tonal balance, Key, Confidence). En castellano se
traduce lo que se traduce en un estudio y se deja tal cual lo que nadie traduce: LUFS, True Peak, LRA,
PLR, PSR, Bark, Lissajous, loudness.


## HEMISFERIO — la vista de mezcla estéreo

El tercer modo de SCOPE, junto a Lissajous y Polar. Un semicírculo con **mono arriba**, **L y R en la
base**, la energía dibujada como una **envolvente rellena por dirección**, la nube instantánea encima y el
correlímetro vertical al lado.

Mira exactamente el mismo dato que el goniómetro —el mismo `ScopeFrame`, la misma ventana—: lo que cambia
es la proyección. La nube del Lissajous dice DÓNDE hay muestras; la envolvente dice CUÁNTO hay en cada
dirección, que es la pregunta de una mezcla ("¿el bajo está centrado?", "¿cuánto material tengo fuera de
fase?").

### La convención de ángulos

```
θ = 90° + 2·atan2(R − L, R + L)     ≡     2·atan2(R, L)      (mod 360°)

sólo L → 0°   ·   mono (L = R) → 90°   ·   sólo R → 180°   ·   L = −R → 270°
```

El factor **2** es lo que hace que el semicírculo de arriba cubra todo el estéreo en fase: el cuadrante
real de un vector (L, R) con las dos componentes positivas mide 90°, y acá se abre a 180°. Sin él, media
pantalla no se usaría nunca. Como consecuencia el ángulo es **lineal en el paneo**: una fuente de potencia
constante paneada α cae en 2α, así que las distancias en la pantalla se leen como distancias de paneo.

**Fuera de fase = hemisferio inferior.** Lo que cae por debajo de la base se dibuja hacia abajo, y es lo
único del dibujo que predice lo que se PIERDE al monoficar. El énfasis del color es proporcional a la
pérdida mono medida: toda mezcla con fuentes decorrelacionadas tiene muestras instantáneas fuera de fase
—es normal— y pintarlas como una emergencia enseñaría a desconfiar del medidor.

### Qué NO es

**No es localización.** Es dirección de paneo por energía instantánea, no de dónde viene el sonido en una
sala: no hay HRTF, ni ITD, ni nada por el estilo. Es la misma honestidad que el rótulo de FIELD.

### Lo verificado (`[telescope][hemis]`)

| Señal | Resultado medido |
|---|---|
| mono (L = R) | pico en **90°**, resto del círculo en el piso |
| sólo L / sólo R | **0°** / **180°**, lóbulos limpios |
| L = −R | **270°**; máximo del hemisferio superior = el piso, sin excepción |
| ruido independiente | abanico de 360°, sectores 0–90 y 90–180 iguales dentro de **0.844 dB** |
| paneo 22.5° potencia constante | **45°**, y el paneo por energía de FIELD da lo mismo vía `θ = arccos(−pan)` |
| nivel | es el radio del vector: mono a escala completa = **+3.0103 dB** (L y R suman en cuadratura) |
| bloques de 1 / 7 / 64 / 4096 muestras | envolvente idéntica **al bit** |
| decaimiento 12 / 24 / 48 dB/s | **0.00 %** de error |

**Reparto motor / vista.** El motor publica la envolvente del HOP sobre TODAS sus muestras —no los 2 048
decimados del goniómetro: una envolvente que se saltea el transitorio miente hacia abajo justo donde
interesa—. La memoria (el peak-hold que se desvanece) la pone la lente, como las líneas y la inclinación
de WATERFALL: es un setting que no cambia una sola cuenta. Decae por **frame**, así que "24 dB por
segundo" quiere decir lo mismo en el test que en la pantalla. Con reduced-motion no hay memoria: la
envolvente es el hop.

**Suavizado de dibujo.** La envolvente se dibuja con una media móvil circular de ±2° tomada por MÁXIMO
(promediar bajaría los picos). Sin ella, 360 bins contra un hop de 100 ms dejan bins vacíos entre bins
llenos y se dibuja un peine de púas que se lee como "energía en 47 direcciones puntuales", que es falso.
El dato publicado no se toca.


## Honestidad

### El true-peak usa la tabla LITERAL del estándar

Los 48 coeficientes del FIR polifásico (4 fases × 12 taps) están transcritos del documento original:
**Rec. ITU-R BS.1770-5, Anexo 2 §3, pp. 18-19**. No es un diseño propio ni una aproximación.
Ver `source/analysis/modules/TruePeakFir.h`.

La atenuación de −12.04 dB del diagrama de bloques del Anexo **no se aplica**, porque el propio documento
dice que existe sólo para dar headroom a la aritmética entera y "no es necesaria si los cálculos se hacen
en punto flotante" — que es el caso.

Sobremuestreo por sample rate (el objetivo del estándar es llegar a ≥ 192 kHz):

| Sample rate | Sobremuestreo | Resultado |
|---|---|---|
| < 96 kHz | 4× | 176.4 / 192 / 352.8 kHz |
| 96 – 176.4 kHz | 2× | 192 – 352.8 kHz |
| ≥ 192 kHz | 1× | las muestras ya están a esa resolución |

Un sobremuestreo 4× tiene una sub-lectura máxima conocida (el propio Anexo la tabula: 0.554 dB a
f<sub>norm</sub> = 0.45). Se ve en los vectores de la EBU: el test 3341-17 (fs/6 a 60°) lee −6.3160 dBTP
contra un valor real de −6.0. **Está dentro de la tolerancia del propio documento (+0.2 / −0.4 dBTP), pero
es una sub-lectura real**: si tu máster está a −1.0 dBTP medido acá, tratalo como si estuviera un poco más
arriba.

### El K-weighting se recalcula por sample rate

El estándar sólo publica la tabla de coeficientes a 48 kHz. Para cualquier otro sample rate hay que
recalcularlos desde el prototipo analógico (bilineal + prewarp). La prueba de que el prototipo es el
correcto: evaluado a 48 kHz **reproduce la tabla publicada con un error máximo de 8.9 × 10⁻¹⁶**.

### Los objetivos de plataforma: sólo Spotify publica el número

`source/data/StreamingTargets.h` lleva la fuente y la fecha de verificación de cada fila.

- **Spotify** publica su objetivo: −14 LUFS integrado, techo −1 dBTP (−2 dBTP si el máster es más fuerte
  que −14). Es la única página oficial con un número de LUFS.
- **Apple** publica el techo de −1 dBTP (Apple Digital Masters). El −16 LUFS de Sound Check es
  comportamiento **medido**, no una página oficial.
- **YouTube, Amazon Music, Tidal y Deezer** no publican un objetivo de LUFS. Los valores son los medidos
  por la industria sobre su normalización real. La UI los muestra marcados **"(estimado)"**.

Y sobre el delta: decir *"te suben X dB"* no es cierto en todas las plataformas. **Apple (Sound Check) y
Amazon sólo bajan**: un máster por debajo del objetivo se queda donde está. Del resto no se encontró
fuente verificable. Por eso la lente dice, según el caso:

- `te bajan X dB` — bajar lo fuerte lo hacen todas: es la definición de normalizar a un objetivo.
- `estás X dB por debajo · no te suben` — verificado que sólo atenúan.
- `estás X dB por debajo` — sin fuente: se dice la distancia medida y nada más.

### El chasis expone tres parámetros que la UI no muestra

`PluginProcessorBase` (compartido por todo el catálogo) inyecta `inGain`, `output` y `monoSafe` a **todo**
plugin del sello. TELESCOPE no los expone en su UI porque no procesa audio, pero **el host igual los va a
listar**. Con los tres en default (0 dB, 0 dB, off) el audio es bit-exacto — es lo que verifica el null
test. Es una limitación conocida del chasis, anotada para la v2 del tablero.

### Un layout mono 1→1 no existe

`PluginProcessorBase::isBusesLayoutSupported` exige salida **estéreo** (entrada mono o estéreo). El DAW
instancia mono→estéreo. Con una fuente mono, TELESCOPE mide el mismo canal en L y R: correlación +1 y
ancho 0, que es la verdad de esa señal.

### Memoria

El integrado y el LRA se calculan sobre **todos** los bloques desde el último RESET, no sobre un
histograma con bins de 0.1 dB. Eso los hace exactos y cuesta ~2.3 MB por hora de medición continua.
RESET lo vacía.

Los buffers del espectro están dimensionados al **peor caso** y no crecen con el uso: el `SpectrumFrame`
son ~260 KB por slot (16 385 bins × 2 espectros × 2 arrays) y viaja por su propio `TripleBuffer` (3
slots ≈ 790 KB) para que las otras lentes no copien bins que no dibujan; el anillo del espectrograma son
**1.8 MB** fijos (60 s × 60 columnas/s × 512 filas de un byte). Al publicar se copia sólo el prefijo vivo:
con FFT de 4 096 son 32 KB, no 260.

---

## Verificación

```bash
cd ovni                                   # tu clon de github.com/ovniaudio/ovni
cmake --preset dev
ninja -C build telescope_VST3 telescope_AU telescope_Standalone OvniTelescopeTests
ctest --test-dir build -R telescope --output-on-failure
```

Filtros de Catch2 sobre `$(find build -type f -name OvniTelescopeTests)`:

| Tag | Qué verifica |
|---|---|
| `[null]` | audio bit-exacto, latencia 0, cola 0, round-trip de estado |
| `[ebu]` | los vectores de EBU Tech 3341 (1-5) y 3342 (1-4) + los de casa |
| `[kw]` | el K-weighting contra la tabla publicada y contra su propia respuesta |
| `[tp]` | true-peak: 16 fases, fs/4 a 45°, ≥ pico de muestra, y los vectores 15-19 de Tech 3341 |
| `[chain]` | el camino completo de processBlock al frame; pause / reset / descartes |
| `[lifecycle]` | re-preparar el mismo processor 20 veces (sample rate y bloque cambiando en vivo) |
| `[stereo]` | las cinco sumas, la ventana, los casos borde, el ScopeFrame y la independencia del bloque |
| `[dyn]` | PSR, PLR, histograma, eventos de clip y su ring de 1 Hz |
| `[settings]` | round-trip de los settings de lente por estado + lente a demanda |
| `[spectrum]` | la referencia de dB, scalloping, Parseval, las ventanas, resolución, bandas, slope, hold, promediado, canales y la independencia del bloque |
| `[spectrogram]` | la columna de 512 filas, su escala, el anillo de historia y su determinismo |
| `[bands]` | las cinco sumas por banda de ⅓ de octava y el cruce contra el módulo de banda ancha |
| `[sgram-st]` | el espectrograma coloreado por ancho/fase y sus dos mapeos byte ↔ número |
| `[cqt]` | constant-Q: niveles por octava, resolución, cromagrama, tonalidad, piso de faldas |
| `[pan]` `[field]` | el paneo por energía (`p = −cos 2θ`) y la grilla del campo |
| `[waterfall]` `[horizon]` | la proyección 2.5D, la selección de columnas y la **oclusión contra el pintor** |
| `[ref]` `[tonal]` | la referencia, la normalización por loudness y el delta de suma cero |
| `[fixedfft]` | la referencia mide con SU propia FFT y no con la de la lente SPECTRUM |
| `[file]` | el análisis offline: identidad al bit con el vivo, cadencias, errores, determinismo |
| `[history]` | la historia por segundo: archivo == vivo al bit, el hueco y el tramo donde están, bloque |
| `[verdict]` | las 21 reglas: cada una con su defecto y con material sano; las 6 lenguas; archivo == vivo |
| `[verdict][uisnap]` | la lente 13 en pantalla: capturas, presupuesto, estado y round-trip (el archivo se llama `VerdictLensTest.cpp`; **no** hay un tag `[verdictlens]` — su cabecera lo nombra así pero los casos no lo llevan) |
| `[fanout]` | el fan-out del FrameSink a cuatro consumidores |
| `[budget]` | presupuesto de pintado de **las trece lentes** (una línea con su `k` cada una) + reduced-motion |
| `[uisnap]` | PNG del editor en S/M/L → `/tmp/ovni_telescope_<lente>_{S,M,L}.png` (+ `verdict_sano_M`) |
| `[smoke]` | instancia, prepare en 44.1/48/96 kHz × 64/512/2048, editor |
| `[gain]` | la puerta anti-clip del sello: full-scale por el processor ensamblado → `PEAK=1.000000` e identidad muestra a muestra. Es el test que `tools/gain-staging-check.sh` busca (0.1.0) |
| `[.][strings-dump]` | **no verifica: genera.** Vuelca `docs/strings-matrix.md` desde las tablas del código. Oculto con `[.]`, no corre en la suite normal (0.1.0) |

> Catch2 v3 combina varios tags con **coma**, no con espacios: `"[chain],[lifecycle]"` corre los dos;
> `"[chain]" "[lifecycle]"` pide los que tengan **ambos** tags y no corre nada.

Además del exe, `ctest -R telescope` corre dos guardias que no son tags de Catch2 porque miran cosas que
no están adentro del binario:

| Test de ctest | Qué verifica |
|---|---|
| `telescope-version` | los `Info.plist` de los tres bundles, el CHANGELOG y la ficha declaran lo que dice [`VERSION`](VERSION) |
| `telescope-strings-matrix` | `docs/strings-matrix.md` está al día: se regenera a un temporal y se compara |

**Determinismo.** Las garantías bit-exactas del plugin — el pass-through de `[null]`, el `casa-4` del
medidor, la independencia del tamaño de bloque de `[stereo]` y `[dyn]`, el frame 20 de `[spectrum]` y los
anillos de `[spectrogram]` — se comparan con `==`, no con tolerancia. Por eso el plugin y el exe de tests se compilan con **`-fno-fast-math` explícito**:
`-ffast-math` habilitaría reasociación y contracción a FMA (que rompen la igualdad al bit) y
`-ffinite-math-only` volvería código muerto los guardas de NaN/inf — y este plugin **publica** casos borde
como el −∞ clampeado del mono loss.

### El presupuesto de pintado se calibra contra la máquina donde corre

`[budget]` mide el pintado de cada lente y lo exige contra **4 ms de mediana y 8 ms de p95** (spec §6).
Ese criterio no se toca nunca; lo que se calibra es la MÁQUINA. Alrededor de cada medición se corre una
carga patrón fija y se publica el factor `k = medido / referencia` en la misma línea que el número, así el
número nunca viaja sin la condición en la que se tomó.

```bash
# la referencia por defecto es la del M4 de la casa (22.5 ms). En otra máquina, medila una vez en reposo
# —el valor sale en la línea BUDGET_CALIBRACION— y fijala:
TELESCOPE_BUDGET_REF_MS=41.8 ./OvniTelescopeTests "[budget]"
```

Sin esa variable el harness distingue **máquina lenta** de **máquina cargada** por la dispersión de las
tres tandas de la probe: si son parejas (dispersión < 10 %) y `k > 2`, la máquina es lenta pero está
quieta y el criterio se escala por `k` sin techo (`WARN maquina lenta`); si son dispares y `k > 2`, hay
otra cosa comiéndose los núcleos y el techo de 2× sigue puesto, así que el test **puede fallar** — que es
lo correcto: una máquina así no mide nada. El 10 % es convención de la casa (en reposo las tres tandas
caen dentro del 3-4 %; bajo carga de compilación se van al 25-60 %).

**Las esperas de los tests son por condición, no por reloj.** `tests/TestHelpers.h` da `waitUntil` y
`waitStable`: un test que falla porque la máquina estaba ocupada deja de ser una señal. Las aserciones
negativas ("en pausa el análisis no avanza") usan el mismo helper al revés, así alargar el timeout las
hace **más** estrictas, nunca más frágiles.

### Vectores de la EBU que pasan

| Documento | Tests | Estado |
|---|---|---|
| Tech 3341 §2.9 Tabla 1 | 1, 2, 3, 4, 5 (loudness M/S/I) | ✅ |
| Tech 3341 §2.9 Tabla 1 | 15, 16, 17, 18, 19 (true-peak) | ✅ |
| Tech 3342 §4 Tabla 1 | 1, 2, 3, 4 (LRA) | ✅ |
| Tech 3341 | 6 (5.0 canales) | ⚠ fuera de alcance: TELESCOPE es estéreo |
| Tech 3341 | 7, 8 · Tech 3342 5, 6 (programa real) | ⚠ necesitan los WAV de la EBU |
| Tech 3341 | 9-14 (M/S con timing) | ⚠ pendientes |
| Tech 3341 | 20-23 (true-peak resampleado) | ⚠ piden un resampler para SINTETIZAR la señal |

---

## Licencia

AGPLv3, como todo el catálogo OVNI. Ver [`LICENSE`](../../LICENSE) y [`NOTICE.md`](../../NOTICE.md).
