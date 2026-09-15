# El diccionario de conclusiones de TELESCOPE 🔭

*El gemelo legible de [`plugins/telescope/source/data/Rules.h`](../plugins/telescope/source/data/Rules.h) y de
[`DeviceProfiles.h`](../plugins/telescope/source/data/DeviceProfiles.h). Acá está cada umbral con su porqué,
en castellano, para que se pueda leer y corregir sin abrir un `.h`. Si un número de acá no coincide con el
del código, el que manda es el código y esta página está desactualizada — decilo.*

*Prompt 55 · 2026-09-08 · D-47 (VERDICT es un motor de reglas determinista) y D-50 (idiomas). Prompt 57d ·
2026-09-14 · cómo lo cuenta (§4): el titular, "Dentro de rango", los huecos fusionados y el tono.*

---

## Las tres cosas que hay que entender antes de leer una sola regla

### 1 · VERDICT mide, no opina

No hay IA, no hay red, no hay modelo. Hay una tabla de umbrales y las condiciones que los leen. La misma
señal da el mismo informe, palabra por palabra, hoy y dentro de un año. **Cada frase lleva el número que la
sostiene y el id de la regla que la produjo**, y las dos cosas se muestran juntas en pantalla.

Está **prohibido** que diga "suena profesional", "emociona" o "está listo". Si alguna vez lo dice, es un bug.

### 2 · Contra qué se compara — la decisión que hace honesto todo lo demás

Las reglas de la sección 1 preguntan si una región *sobra* o *falta*. Sobra ¿respecto de qué? Hay dos
respuestas y el motor usa la que haya, y **la frase siempre dice cuál usó**:

| Situación | Contra qué compara | Qué significa el número |
|---|---|---|
| **Con referencia cargada** | contra la referencia, comparadas por su **forma** (cada curva menos su propio nivel de banda ancha) | "esta región no se parece a la mezcla que elegiste como objetivo" |
| **Sin referencia** | contra la **tendencia del propio material**: una recta de mínimos cuadrados ajustada a la forma del programa en log de frecuencia, entre 50 Hz y 16 kHz | "esta región no se parece al resto de tu propia mezcla" |

La segunda es deliberada y hay que entender por qué. La alternativa habitual —tener escondida en el código
una "curva de una buena mezcla" y medir contra ella— es opinar disfrazado de medir, que es exactamente lo
que D-47 prohíbe. Contra la propia tendencia no hay nada que opinar: un pozo de 6 dB en los medios graves
es un pozo **respecto del resto de ese material**, y eso es un hecho.

**A cambio es un instrumento grueso.** Una mezcla con una intención espectral fuerte y deliberada puede
disparar una regla de la sección 1 sin que haya nada que arreglar. **Cargar una referencia es
estrictamente mejor**, y es lo que hay que hacer cuando el informe importa.

### 3 · Un hallazgo es ⚠ o ●. Las líneas ○ son informativas

La tonalidad, el pronóstico de un dispositivo que sale ✓ y la distancia a la plataforma son **○
informativas**: no son defectos. Cuando no hay hallazgos quiere decir cero ⚠ y cero ● — y **no** quiere
decir "está terminado". Lo dice con esas palabras: *"Nada fuera de rango en estas reglas. Miden; lo que no
escuchan es tuyo."*

### 4 · Cómo lo cuenta: el titular, "Dentro de rango" y el tono (prompt 57d)

Lo que VERDICT mide no cambió en el 57d: los umbrales de abajo son los mismos. Cambió **cómo lo cuenta**,
porque la versión anterior abría con una línea gris, seguía con seis pronósticos y terminaba en una lista
roja donde un solo pozo aparecía una vez por banda.

**El titular.** Arriba de todo y fijo, una línea sin adjetivos: *"15 chequeos dentro de rango · 3 para
revisar, el primero en 0:20"*.

- **dentro de rango** = cada regla que se evaluó y **no** se disparó (una fila de "Dentro de rango") más cada
  caja de "Dónde traduce" que salió ✓ **con dato para decidir**. Sin espectro, una caja sale ✓ porque su
  exceso vale 0 por defecto: eso no es un chequeo dentro de rango y no se cuenta;
- **para revisar** = los hallazgos ⚠ y ●, ya fusionados. Las ○ no cuentan. "El primero" es el que empieza
  antes en el tiempo.

Sin un solo segundo analizado el titular no aparece y **no se cuenta nada**, ni n ni m: "0 chequeos dentro
de rango · nada fuera de estas reglas" sería decir que se midió (y sin filas la caja del celular sale ⚠ sin
dato, que no es algo para revisar). Su evidencia gris es `headline · n/m`.

**Dentro de rango.** Es la primera sección, compacta: una fila por regla que se evaluó y quedó del lado
sano del umbral, con **el número medido y el límite de la regla** (clave `<regla>.ok`). La ✓ va en gris, no
en verde: es una medición, no un premio. Tienen fila las doce reglas de forma y de tiempo:

| Regla | Qué dice su fila |
|---|---|
| `crushed` | el PSR medio y el piso de 8 dB |
| `thin` · `muddy` · `no-air` | el residuo de la región contra la línea de base, **con su signo**, y el umbral (−3 / +3 / −3 dB) |
| `harsh` | el residuo de 2–5 kHz y el umbral; si sobra pero no sostenido, cuánto tiempo pasó contra el 30 % (`harsh.ok.brief`) |
| `hollow-centre` | el width de medios y de agudos, con los dos topes (0.4 y 1.2) |
| `hole` | que ninguna banda cae 6 dB durante 10 s, y la racha más larga que hubo |
| `quiet-section` | el LRA, y que ningún tramo cae 3 LU durante 8 s |
| `peaks` | que no hay ráfagas sobre el umbral, y cuántos eventos sueltos hubo |
| `out-of-phase` | la correlación media más baja entre las bandas con señal |
| `imbalance` | el balance medio, y el umbral de 3 dB durante 20 s |
| `dc` | la continua de cada canal y el límite de 0.01 |

Las ○ informativas (`key`, `loud-section`, `platform`) no tienen fila: no hay un rango del que estar adentro.
Las cajas de dispositivo tampoco: su ✓ ya está en "Dónde traduce". Una regla que **no pudo evaluarse** (sin
espectro, sin loudness) no dice nada: "dentro de rango" es una medición, no un valor por defecto. Cuando el
alto de la lente no alcanza, la sección se colapsa a UNA fila con el nombre corto de cada regla
(`within.<regla>`): *"Within range: transients · body · low mids · …"*. Ojo: los zooms S / M / L del editor
escalan el lienzo entero, así que en el editor la lente mide lo mismo en los tres y la sección va
desplegada; se colapsa con el lienzo flexible en una ventana chica.

**El tono de una frase de hallazgo.** El número va **antes del primer punto**, el término de mezcla **entre
paréntesis**, y la frase termina diciendo **dónde mirar** ("Check …" / "Revisa …") — nunca qué hacer:
"cortá 2 dB" sería gusto. Ninguna de las seis tablas puede usar *profesional*, *professional*, *listo*,
*malo*, *mal*, *bien hecho*, *arruina*, *ruins*, *bad mix* ni *amateur*; lo barre `VERDICT[tono]`, palabra
por palabra ("mal" no es "normal").

**La evidencia dice también la regla.** Al lado del número medido van los umbrales de la tabla, con sus
unidades: `hole · -25.96 dB / 15 s · rule 6 dB / 10 s`. Es lo técnico sin abrir este documento.

---

## Sección 1 · Cómo se va a sentir

Vocabulario de mezcla ↔ número. Todas comparan contra la línea de base de la tabla de arriba.

| Regla | Se dispara cuando | Umbral | De dónde sale el umbral |
|---|---|---|---|
| **aplastado** `crushed` | el **PSR medio** (pico real del segundo − short-term del segundo) está por debajo del umbral | **< 8 dB** | PSR es de AES TD1004. El 8 es **convención de la casa**: por debajo de ahí los transitorios de una mezcla moderna ya no tienen recorrido. Se agrava a ● por debajo de 6 dB. |
| **delgado** `thin` | falta nivel en **150–400 Hz** (bandas de ⅓ de octava de 160 a 400) | **≥ 3 dB de déficit** | **Convención de la casa**: 3 dB sostenidos en una región ancha es el escalón que se escucha; menos que eso es la variación normal entre mezclas. Se agrava a ● a partir de 6 dB. |
| **turbio** `muddy` | sobra nivel en **200–500 Hz** | **≥ 3 dB de exceso** | Misma escala que `thin`. Es la región que tapa todo lo que tiene encima. |
| **áspero** `harsh` | sobra nivel en **2–5 kHz** *y* pasa **sostenidamente** | **≥ 3 dB** durante **≥ 30 %** del tiempo | El 30 % es **convención de la casa**: un pasaje brillante no es una mezcla áspera. Se agrava a ● si además el integrado supera **−10 LUFS** (fuerte *y* filoso es la combinación que cansa). |
| **sin aire** `no-air` | falta nivel **por encima de 10 kHz** | **≥ 3 dB de déficit** | Misma escala que `thin`. |
| **centro vacío** `hollow-centre` | el **width** de los medios es chico *y* el de los agudos es grande | medios (315 Hz–2 kHz) **< 0.4** y agudos (2.5–20 kHz) **> 1.2** | `width = √(ΣSS/ΣMM)` (spec §5.3): 0 = mono, 1 = canales independientes. Los dos topes son **convención de la casa**. El width por banda no se guarda: sale exacto de la pérdida al monoficar, `width² = 10^(−m/10) − 1`. |
| **tonalidad** `key` | siempre que haya estimación | ○ informativa | Correlación con los perfiles de Krumhansl-Schmuckler. **Nunca se muestra como certeza**: sale con su confianza y con el % del tiempo en que fue la mejor. |

---

## Sección 2 · Dónde traduce

**Esto es un pronóstico con chequeos genéricos, y la lente lo dice siempre.** Las seis cajas no son
mediciones de ningún parlante real ni simulaciones, **y no hay ninguna curva de respuesta adentro del
plugin**: cada caja es una definición de qué clase de sistema es —dónde empieza a responder, dónde se cae,
qué le pasa al estéreo— y su chequeo mira el material contra esa definición, no contra una curva. (Hasta el
56 `DeviceProfiles.h` traía un array de 30 dB por dispositivo que ningún chequeo leía; se sacó en el 56b,
justamente porque un lector del repo veía esos números y concluía que el plugin simula parlantes.)
Cualquier frase del tipo "así suena en un iPhone" sería mentira y está prohibida.

Cada caja sale **✓ / ⚠ / ✗ con su número**.

| Caja (y el `id` que sale en la evidencia) | Qué la define | Chequeo | Umbral |
|---|---|---|---|
| **celular** `phone` | altavoz sin respuesta útil por debajo de 300 Hz | fracción de energía bajo 300 Hz **y** nivel de los armónicos del bajo en 315 Hz–1.25 kHz respecto de esa energía | ✗ si **> 60 %** de la energía está abajo **y** los armónicos están **< −20 dB**. Con una sola de las dos, ⚠. |
| **auriculares** `headphones` | full-range, pero el estéreo se escucha **separado** | exceso en 2–5 kHz, o alguna banda por encima de 8 kHz **cuya correlación media sea ≤ −0.2** | ✗ si el exceso es **≥ 3 dB** o si una de esas bandas está en negativo **> 20 %** del tiempo. La condición de la media es la **misma** que la de `out-of-phase`, y por el mismo motivo: un auricular separa el estéreo, no inventa una contrafase que no está. |
| **laptop** `laptop` | altavoz chico: todo el peso en la presencia | presencia en 2–4 kHz contra la línea de base | ✗ si está **≥ 3 dB por debajo** |
| **auto** `car` | el habitáculo **realza** los graves por sus modos | exceso por debajo de 100 Hz | ✗ si es **≥ 4 dB** (el 4 y no el 3: el habitáculo agrega lo suyo, así que el margen tiene que ser mayor antes de avisar) |
| **club (sub mono)** `club` | el sub va en **mono** por diseño | pérdida al monoficar por debajo de 120 Hz | ✗ si es **≤ −3 dB**. Lo que esté fuera de fase abajo, en un club no suena. |
| **hi-fi** `hi-fi` | full-range plano: no esconde nada | **no tiene chequeo propio**: hereda los hallazgos de la sección 1 | ⚠ si la sección 1 encontró algo |

Las bandas por debajo de 20 % de energía útil **no cuentan** para las reglas de fase: la correlación del
piso de ruido es un número al azar, y reportarla sería reportar ruido. El corte es **40 dB por debajo de
la banda más fuerte** del programa.

---

## Sección 3 · Qué revisar y dónde

Acá cada hallazgo lleva **`mm:ss–mm:ss`** y, cuando corresponde, la banda. Los tiempos salen de la historia
por segundo, que es **idéntica al bit** en el análisis en vivo y en el de archivo (test `HISTORY[identidad]`):
por eso "entre 0:20 y 0:35" quiere decir lo mismo mirando el archivo que escuchándolo.

| Regla | Se dispara cuando | Umbral | De dónde sale |
|---|---|---|---|
| **hueco** `hole` | una banda cae por debajo de **su propia media en el tema**, sostenido | **≥ 6 dB** durante **≥ 10 s** | **Convención de la casa**: 6 dB durante 10 s es un hueco, no una variación de arreglo. Se compara la *forma* de cada segundo contra la forma media, así que un pasaje suave no dispara huecos en todas las bandas. **⚠, no ●** (57d): un pozo medido es algo para revisar; ● queda para lo roto. **Bandas contiguas cuyos tramos se solapan son UN hueco** (57d): la frase dice la banda más honda y su caída, el rango de bandas y la ventana de tiempo del tramo entero (la unión de las ventanas de sus bandas: desde la primera que cae hasta la última que vuelve) — el pozo de prueba de 400–1 000 Hz entre 0:20 y 0:35 da una frase, no cinco. |
| **sección baja** `quiet-section` | el short-term queda por debajo del integrado, sostenido | **≥ 3 LU** durante **≥ 8 s** | 3 LU es el escalón de la escala EBU; los 8 s son **convención de la casa**. |
| **sección alta** `loud-section` | lo mismo, del otro lado | **≥ 3 LU** durante **≥ 8 s** | ○ informativa: una sección más fuerte no es un defecto. |
| **picos** `peaks` | ráfaga de eventos de clip sobre el umbral vigente | **≥ 5 eventos** en **5 s** | **Convención de la casa**. Se reporta la **ráfaga entera** (su tramo y su total), no la primera ventana que cruza el umbral. |
| **fuera de fase** `out-of-phase` | la correlación **media** de la banda es claramente negativa **y además** pasa buena parte del tiempo en negativo | correlación **media ≤ −0.2** **y** **corr < 0** más del **20 %** del tiempo | `corr < 0` significa que esa banda **se cancela** al monoficar (spec §5.3). Las **dos** condiciones son **convención de la casa**, y la de la media es la que evita el falso positivo: material simplemente decorrelacionado (una reverb ancha, dobles) oscila alrededor de 0 y pasa la mitad del tiempo en negativo sin que haya nada roto. −0.2 equivale a perder ~1 dB **más** que la decorrelación pura al monoficar. |
| **desbalance** `imbalance` | el balance L/R se va y se queda | **> 3 dB** durante **≥ 20 s** | **Convención de la casa**: 3 dB durante 20 s ya no es un pasaje, es la mezcla. |
| **continua** `dc` | media de las muestras distinta de cero | **|dc| > 0.01** | 0.01 son −40 dBFS de continua: no se escucha y se come headroom. **Convención de la casa.** Se mide sobre la señal **cruda**, antes del filtro K (que tiene un pasa-altos de 38 Hz y se comería justo lo que se quiere ver). |
| **plataformas** `platform` | el integrado se aleja del objetivo, o el pico real pasa el techo | **≥ 0.5 dB** de distancia | Los números salen de [`StreamingTargets.h`](../plugins/telescope/source/data/StreamingTargets.h), verificados el 2026-09-07, **con su honestidad puesta**: sólo Spotify y Apple publican algo; el resto es medición de terceros. Donde la plataforma **sólo atenúa** (Apple, Amazon), la frase lo dice en vez de prometer que te va a subir. |

---

## El pie, que no se puede sacar

> **Medición, no gusto. Chequeos por dispositivo genéricos. Rehacé el análisis tras cada cambio.**

Sale en todos los informes, en el idioma elegido, siempre. Está en la tabla de frases como cualquier otra:
si algún día no aparece, el test `VERDICT[plantillas]` se pone rojo.

---

## Los idiomas (D-50)

El setting `language` es un **código ISO 639-1** (`en`, `es`, `pt`, `fr`, `de`, `it`…), no un interruptor de
dos posiciones. **Default `en`.** Una tabla por idioma, buscada **por clave con fallback a inglés**: si a un
idioma le falta una frase, sale en inglés — nunca vacía y nunca con la plantilla sin resolver.

**Agregar un idioma es agregar una tabla.** Cero código.

Hoy hay seis. `en` y `es` están revisados; **`pt`, `fr`, `de` e `it` están traducidos con los términos de la
industria (Integrated, Short-term, True Peak, Correlation, Width, Mono compatibility, Tonal balance, Key) y
esperan revisión de un hablante nativo.**

---

## Lo que VERDICT NO hace

- **No estima la posición de las fuentes.** Eso no tiene solución única a partir de una mezcla terminada
  (spec §3). FIELD muestra el paneo por energía, que sí es medible.
- **No dice si una mezcla es buena.** Dice qué números tiene y qué reglas cruzó.
- **No sabe de género.** Un umbral que sirve para un tema de folk y uno de dubstep no existe; por eso la
  comparación por defecto es contra el propio material, y por eso cargar una referencia es mejor.
- **No manda nada a ningún lado.** Corre en la máquina. Sin red, sin telemetría, sin excepciones.
