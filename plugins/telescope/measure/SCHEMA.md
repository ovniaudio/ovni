# telescope-measure — el JSON, versión 1

La herramienta de consola con que EYEPIECE mide stems con el motor de TELESCOPE (contrato **D-100**,
`EYEPIECE-PLUGINS-PROPIOS.md` §2quater). EYEPIECE la lleva adentro y la llama; nunca linkea el motor.
Este archivo es la especificación: lo que el contrato no fijaba lo fija acá, y cada regla tiene su prueba en
`plugins/telescope/tests/MeasureTest.cpp` o en `measure/check-measure-cli.sh`.

```bash
printf '%s\n' '{"file":"/ruta/bajo.wav"}' '{"file":"/ruta/voz.wav","from_s":30,"to_s":45}' | telescope-measure
```

## 1. El proceso

- **Entrada**: JSON Lines por la entrada estándar, **un pedido por línea**, UTF-8. Se aceptan líneas con CRLF.
- **Salida**: **una línea por cada línea de entrada**, en el mismo orden, terminada en `\n` (también en Windows),
  UTF-8, sin espacios. Una línea en blanco también es una línea: recibe su negativa (`REQUEST_NOT_JSON`).
- Cada línea sale con `flush`: quien llama puede leer mientras la herramienta sigue midiendo.
- **Exit 0**: la herramienta corrió. Cada línea dice si midió (`"status":"measured"`) o se negó
  (`"status":"refused"`, con su código). Un pedido mal escrito es una negativa, **no** un exit distinto de 0.
- **Cualquier otro exit es una falla** (el 3 es «no pude escribir la salida»; un cuelgue da el que dé el
  sistema). EYEPIECE dice «el motor de medición falló» y no pone ningún número, tampoco de las líneas que ya
  llegaron.
- **Cancelar** es matar el proceso.
- **No escribe nada en disco** y no usa la red. No busca nada en el `PATH` ni en el disco del usuario: lee
  sólo los archivos que se le piden.
- Una ruta relativa se resuelve contra el directorio de trabajo de la herramienta.
- **`telescope-measure --version`** (desde la F4 de la 0.2) imprime una línea, `telescope-measure <versión> <sha>`,
  con la versión y el sha del objeto `engine`, y sale 0 **sin leer la entrada**. No cambia el esquema: los
  pedidos siguen yendo por la entrada estándar y cualquier otro argumento se ignora.

## 2. El pedido

| clave | tipo | | qué es |
|---|---|---|---|
| `file` | string | obligatoria | la ruta del archivo de audio (WAV, AIFF, FLAC, Ogg; en macOS además MP3, AAC y ALAC por CoreAudio; en Windows MP3/WMA) |
| `from_s` | número | opcional | el comienzo del tramo, en **segundos del archivo** |
| `to_s` | número | opcional | el final del tramo (no entra), en segundos del archivo |
| `id` | string ≤ 256 bytes | opcional | vuelve tal cual en la respuesta, también en las negativas |

- Sin `from_s` ni `to_s`, se mide **el archivo entero**. Sin `from_s`, desde el principio; sin `to_s`, hasta
  el final. `null` es lo mismo que no mandarla.
- **Una clave que no está en la tabla es una negativa** (`REQUEST_UNKNOWN_FIELD`), no se ignora: un `"to":30`
  mal escrito no puede terminar midiendo el archivo entero sin que nadie se entere.
- `from_s`/`to_s` que no sean números finitos, o que pasen de 10⁹ s en valor absoluto, son `REQUEST_BAD_FIELD`.

## 3. La respuesta

Las claves salen **siempre todas y siempre en este orden**, se mida o no. Lo que no se sabe o no se puede medir
va en `null`, **nunca 0 ni −inf**, y su motivo está en `refusal` o en `warnings`.

| clave | unidad | qué es | `null` cuando… |
|---|---|---|---|
| `schema` | — | `1` | nunca |
| `engine.name` | — | `"telescope-measure"` | nunca |
| `engine.version` | — | la versión de TELESCOPE (`plugins/telescope/VERSION`) | nunca |
| `engine.sha` | — | 12 hex del commit con que se compiló; `+dirty` si el árbol tenía cambios; `unknown` sin git | nunca |
| `id` | — | el del pedido | no vino, o vino mal |
| `status` | — | `"measured"` o `"refused"` | nunca |
| `refusal` | — | el código de la negativa (§6) | midió |
| `file.name` | — | el **nombre** del archivo, **nunca la ruta** (la ruta lleva el nombre del usuario, y la foto se pega en una IA) | el pedido no traía un `file` legible |
| `file.sha256` | — | sha256 de los bytes del archivo en el disco (el archivo entero, no el tramo) | no existe o no se pudo leer |
| `file.sample_rate_hz` | Hz | la del archivo; entera si lo es, con dos decimales si no | no se pudo abrir como audio |
| `file.channels` | — | los del archivo (se miden los dos primeros) | ídem |
| `file.duration_s` | s | la duración del archivo entero | ídem |
| `range.requested_from_s` / `requested_to_s` | s | lo pedido, sin recortar | no vino |
| `range.from_s` / `to_s` | s | lo medido: `from_sample / sr` y `to_sample / sr` | no midió |
| `range.from_sample` / `to_sample` | muestras | el tramo exacto `[from_sample, to_sample)` | no midió |
| `seconds_measured` | s | cuánto audio entró de verdad: `(to_sample − from_sample) / sr` | no midió |
| `integrated_lufs` | LUFS | loudness integrada, ITU-R BS.1770-4 / EBU R 128 (compuertas −70 LUFS y −10 LU) | `INTEGRATED_TOO_SHORT`, `INTEGRATED_BELOW_GATE` |
| `lra_lu` | LU | loudness range, EBU Tech 3342 | `LRA_TOO_SHORT`, o el integrado es `null` |
| `true_peak_dbtp` | dBTP | true peak máximo de los dos canales (FIR de BS.1770 Anexo 2: 4× por debajo de 96 kHz, 2× de 96 a 176,4 kHz y sin sobremuestreo desde 192 kHz) | `TRUE_PEAK_TOO_SHORT`, `TRUE_PEAK_NO_SIGNAL` |
| `correlation` | −1…+1 | correlación de banda ancha del tramo (§5) | `MONO_SOURCE`, `CORRELATION_NO_SIGNAL` |
| `bands_hz` | Hz | los 30 centros ISO 266 de ⅓ de octava, 25 Hz … 20 kHz (fijos) | nunca |
| `bands_db_rel_integrated` | dB | las 30 bandas de TONAL BALANCE, relativas al integrado (§4) | el integrado es `null`; `BAND_BELOW_FLOOR` |
| `warnings` | — | los avisos (§6), en el orden de la tabla, sin repetir | nunca (lista vacía) |

**Los números** (lo hace la herramienta, no quien lee):

| magnitud | decimales |
|---|---|
| LUFS, LU, dBTP, dB de banda | 0,1 |
| correlación | 0,01 |
| segundos | 0,01 |
| Hz | entero (31,5 con su decimal) |
| muestras, canales | entero |

- Se redondea al más cercano, **con los medios lejos del cero** (`llround`).
- El texto se arma con enteros: **no depende del locale** (siempre punto decimal) y **nunca sale `-0.0`**.
- **El mismo pedido, con el mismo archivo y el mismo motor, da los mismos bytes.**

## 4. Las bandas: la referencia de su dB

`bands_db_rel_integrated[b]` es **el número que dibuja TONAL BALANCE** para una referencia cargada
(`ReferenceFrame::refNorm`, calculado en `Reference::fill`), con la misma cuenta en `float`:

```
banda_dBFS[b] − integrated_LUFS          (los dos del mismo tramo)
```

- **`banda_dBFS`**: promedio **infinito de potencia** sobre todos los cuadros de una STFT de 4 096 muestras,
  ventana Hann y solapamiento del 75 % (los defaults del motor, fijos: no dependen de cómo mire el usuario la
  lente). Suma **L y R** (una señal mono lee 3,01 dB más que su canal L solo).
- La potencia de cada bin tiene la referencia de SPECTRUM: **un seno de escala completa centrado en un bin lee
  0 dBFS** (`20·log10(2·|X|/(N·CG))`, con CG la ganancia coherente de la ventana).
- Cada bin aporta a la banda en proporción a lo que se solapa con ella (`[fc·2^−1/6, fc·2^+1/6)`), como
  **densidad × ancho nominal**: ninguna banda queda vacía por la rejilla de la FFT, y el mismo ruido lee igual
  a 44,1 y a 48 kHz.
- **Se normaliza por el integrado** (se compara el *tilt*, no el nivel): el mismo material a −14 y a −20 LUFS da
  las mismas bandas. Por eso, sin integrado no hay bandas.
- **`null` con `BAND_BELOW_FLOOR`** cuando la banda cruda queda por debajo de **−90 dBFS**, el piso de lo medible
  de TONAL BALANCE (una banda de ⅓ de octava de música real no baja de −80 ni en silencio). Por ejemplo, la de
  20 kHz de un archivo a 44,1 kHz con poco aire, o todas las que no toca un tono puro.
- La lente además corta lo que no entra en su gráfico (−42 dB); **la herramienta no**: eso es del dibujo, no de
  la medición.
- Lo prueba MEASURE[bandas]: el número de la herramienta y el de la lente, para el mismo archivo, son iguales.

## 5. El tramo

- **De segundos a muestras**: `muestra = llround(segundos × sr)`, al más cercano con los medios lejos del cero.
  El tramo es `[muestra(from_s), muestra(to_s))`: la muestra de `to_s` no entra.
- **Medir un tramo es medir un archivo que empieza y termina ahí**: el motor se resetea al comienzo del tramo y
  no ve nada de afuera. Lo prueba RANGE[corte]: medir `[a, b)` da los mismos números, al bit, que medir un
  archivo cortado en esas muestras.
- **Lo que se pasa del archivo se recorta y se avisa** (`RANGE_CLIPPED`): un `from_s` negativo va a 0 y un `to_s`
  más allá del final, al final. `range.requested_*` dice lo pedido y `range.from_*`/`to_*` lo medido.
- **Un tramo vacío es una negativa** (`RANGE_EMPTY`): `to_s ≤ from_s`, o un tramo que cae entero fuera del
  archivo. Nunca una medición de cero segundos.
- El medidor de loudness trabaja en bloques (hops) de 100 ms: **el último pedazo de menos de 100 ms del tramo no
  entra al integrado, al LRA ni al true peak** (es lo que hace TELESCOPE en vivo). Sí entra a las bandas y a la
  correlación. `seconds_measured` cuenta todo el audio del tramo.
- La **correlación** es la del módulo estéreo de TELESCOPE sobre el tramo entero, en el dominio del tiempo:
  `ΣL·R / √(ΣL²·ΣR²)`, sumado en `double` muestra por muestra. +1 = mono, 0 = sin relación, −1 = fuera de fase.
- **Qué se midió:** el archivo tal como está en el disco, antes del volumen y los efectos del proyecto. Un stem
  medido no es la mezcla. Pasar de compases a segundos del archivo es trabajo de EYEPIECE (D-100 §3).

## 6. Los códigos

Son **fijos**: EYEPIECE los traduce. Un código que EYEPIECE no conozca se muestra igual, como código; nunca se
calla. Sumar un código no sube `schema`; cambiar lo que significa uno, sí. MEASURE[codigos] exige que esta lista y
la del código sean la misma.

**Negativas** (`refusal`, con `"status":"refused"`):

| código | qué pasó |
|---|---|
| `REQUEST_NOT_JSON` | la línea no es un objeto JSON (incluye la línea en blanco) |
| `REQUEST_UNKNOWN_FIELD` | el pedido trae una clave que no está en §2 |
| `REQUEST_BAD_FIELD` | una clave con un tipo o un valor que no sirve (`from_s` no numérico o no finito, `file` que no es texto, `id` largo) |
| `REQUEST_NO_FILE` | falta `file`, o está vacío |
| `FILE_NOT_FOUND` | el archivo no existe (o es una carpeta) |
| `FILE_NOT_AUDIO` | existe, pero ningún lector lo reconoce como audio |
| `FILE_NO_AUDIO` | se abre, pero no tiene muestras |
| `FILE_READ_ERROR` | la lectura se cortó a mitad (del sha256 o del audio) |
| `RANGE_EMPTY` | el tramo, recortado al archivo, no tiene muestras |

**Avisos** (`warnings`, con `"status":"measured"`), en este orden:

| código | qué dice | deja en `null` |
|---|---|---|
| `RANGE_CLIPPED` | el tramo pedido se pasaba del archivo y se recortó | — |
| `CHANNELS_FIRST_TWO` | el archivo tiene más de dos canales: se midieron los dos primeros (L y R) | — |
| `INTEGRATED_TOO_SHORT` | menos de 400 ms (4 hops): no hay un solo bloque de loudness | `integrated_lufs`, `lra_lu`, bandas |
| `INTEGRATED_BELOW_GATE` | todo el tramo queda bajo la compuerta de −70 LUFS (silencio, o casi) | `integrated_lufs`, `lra_lu`, bandas |
| `LRA_TOO_SHORT` | menos de 10 s (100 hops) | `lra_lu` |
| `TRUE_PEAK_TOO_SHORT` | menos de 100 ms (un hop): el medidor no llegó a medir | `true_peak_dbtp` |
| `TRUE_PEAK_NO_SIGNAL` | ninguna muestra distinta de cero | `true_peak_dbtp` |
| `MONO_SOURCE` | el archivo es mono: L = R y la correlación no dice nada | `correlation` |
| `CORRELATION_NO_SIGNAL` | un canal (o los dos) sin energía: la correlación no está definida | `correlation` |
| `BAND_BELOW_FLOOR` | al menos una banda quedó bajo −90 dBFS (las que están en `null`) | esas bandas |

**Por qué el LRA pide 10 s.** El LRA es la distancia entre los percentiles 10 y 95 de la loudness de 3 s, y con
menos de 10 s hay apenas tres ventanas de 3 s que no se pisan: el número sale (con un solo valor da 0,0), pero no
describe una distribución. TELESCOPE en vivo lo muestra desde el principio porque se va llenando delante del
usuario; una foto no tiene ese contexto.

## 7. Mac y Windows

La misma herramienta, compilada en la Mac (clang, arm64) y en Windows (MSVC, x64), mide **el mismo juego fijo de
archivos** (`measure/fixtures/`, generado sólo con enteros para que los bytes sean los mismos en las dos) y el
workflow `telescope-measure.yml` imprime el sha256 de cada archivo y de cada línea. La comparación está en el
reporte del prompt 101. Las pruebas de EYEPIECE de «la misma foto en los dos» usan un JSON de fixture, no esta
comparación (D-100 §2).
