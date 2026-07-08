# Avisos de terceros — Sello OVNI 🛸

Este proyecto se distribuye bajo **AGPLv3** (ver [`LICENSE`](LICENSE)). Más abajo
están las atribuciones de todo el software y los datos de terceros que usa o
incorpora. La marca OVNI se sostiene en **honestidad verificable**: si algo de acá
no se puede comprobar, es un bug — abrí un issue.

> **Licencia, en una línea:** OVNI free catalog: AGPLv3 (matches JUCE open-source
> terms). ORBIT (if sold closed): JUCE Starter free tier (<US$20k/yr).

> **Código fuente correspondiente (AGPLv3):** los 6 plugins del catálogo → este repo,
> **<https://github.com/ovniaudio/ovni>** · el flagship **ORBIT** → su propio repo,
> **<https://github.com/ovniaudio/orbita>**. El DMG `OVNI-v0.1.0.dmg` incluye los 7;
> el fuente de cada uno está en uno de esos dos repos.

> **Cómo se usa este archivo:** las mismas atribuciones viven, en forma compacta,
> en [`shared/legal/Attributions.h`](shared/legal/Attributions.h) (`kThirdPartyNotice`)
> para que la pantalla **About** del chasis las muestre dentro del plugin. Mantener
> ambos en sincronía: si tocás uno, tocá el otro.

---

## Decisión de licencia — resuelta

**JUCE 8.0.13 (la versión que este repo pinea) en su edición gratuita es AGPLv3**
(GNU Affero GPL v3) o licencia comercial. Lo dice el propio `LICENSE.md` del checkout
de JUCE 8: *"The JUCE Framework modules are dual-licensed under the AGPLv3 and the
commercial JUCE licence."*

**Decisión registrada:**

- **Catálogo gratis OVNI → AGPLv3.** Coincide con los términos open-source de JUCE
  (la edición gratuita de JUCE es AGPLv3), cumple el manifiesto (todo el código es
  público y auditable en el release), y la cláusula de red del AGPL (sección 13,
  *Remote Network Interaction*) no aplica a un plugin de escritorio que no se sirve
  por red. El `LICENSE` de la raíz ya es el texto oficial completo de la AGPLv3.
- **ORBIT (si se vende cerrado) → JUCE Starter.** El tier gratuito de JUCE Starter
  cubre hasta US$20k/año de facturación; pasado ese umbral, JUCE Indie (US$800 pago
  único). Esto sólo se documenta acá; no requiere cambios de código mientras ORBIT
  se distribuya gratis bajo AGPLv3 como el resto.

Distribuir un binario que enlaza JUCE 8 bajo AGPLv3 es conforme: AGPLv3 impone
obligaciones extra frente a GPLv3 (ofrecer el fuente también a usuarios que interactúan
por red), pero las cumplimos al publicar el código completo del repo en el release.

---

## Software

### JUCE
- **Origen / autor:** Raw Material Software Ltd. — <https://juce.com>
- **Versión:** 8.0.13 (pineada en `CMakeLists.txt`)
- **Licencia:** edición gratuita **AGPLv3** + licencia comercial (dual). El catálogo OVNI
  sale bajo AGPLv3 (ver "Decisión de licencia — resuelta" arriba).
- **Uso en el proyecto:** framework de audio/UI de todos los plugins del chasis
  (`juce_audio_processors`, `juce_dsp`, `juce_gui_basics`, etc.).

### libmysofa
- **Origen / autor:** Christian Hoene y colaboradores — <https://github.com/hoene/libmysofa>
- **Versión:** v1.3.4 (FetchContent en `CMakeLists.txt`)
- **Licencia:** **BSD-3-Clause**
- **Uso en el proyecto:** carga de HRTF en formato SOFA / AES69 en runtime
  (`shared/dsp/SofaLoader.h`). Forward-looking: los módulos binaurales (ÓRBITA / POLVO)
  cargan sus HRIR por acá. El motor de movimiento base **no** usa HRTF.

### Catch2
- **Origen / autor:** Catch2 contributors (catchorg) — <https://github.com/catchorg/Catch2>
- **Versión:** v3.8.1 (FetchContent, solo al compilar tests)
- **Licencia:** **BSL-1.0** (Boost Software License 1.0)
- **Uso en el proyecto:** framework de tests. **No** se enlaza en los binarios de release.

### Intel Integrated Performance Primitives (IPP)  ← **dependencia binaria de ORBIT (no de los 6 plugins OVNI)**
- **Origen / autor:** Intel Corporation — <https://www.intel.com/content/www/us/en/developer/tools/oneapi/ipp.html>
- **Versión:** IPP 2021.9.x (librerías estáticas redistribuibles de Intel para macOS x86_64).
- **Licencia / términos:** IPP se redistribuye bajo la licencia propietaria de Intel (Intel
  Simplified Software License / términos de redistribución de oneAPI), que permite incluir
  las librerías estáticas ya compiladas dentro de un producto binario. No reproducimos acá
  el texto de esa licencia; consultar los términos oficiales de Intel para el detalle.
- **Uso en el proyecto:** **sólo ORBIT** enlaza IPP, y sólo para acelerar DSP. IPP es
  **x86_64 únicamente**: en el binario universal de ORBIT las rutinas de IPP se compilan y
  enlazan **estáticamente en la porción Intel (x86_64)**; la porción Apple Silicon (arm64)
  usa JUCE/sistema puro, sin IPP. **Honestidad importante:** los otros **6 plugins del
  catálogo OVNI (AURORA, DUST, HALO, HORIZON, NEBULA, PULSAR) NO usan IPP** — corren sobre
  la API de JUCE pura. IPP figura acá porque **ORBIT viaja en este mismo DMG del catálogo**
  (`OVNI-v0.1.0.dmg`). El código fuente completo de ORBIT (AGPLv3) y su atribución
  detallada viven en su propio repositorio: **<https://github.com/ovniaudio/orbita>**
  (ver el `NOTICE.md` de ese repo).

---

## Datasets HRIR / HRTF

> **Dataset de producción elegido: SADIE II — KU100 (Apache-2.0).** El HRIR de
> producción del sello es **SADIE II KU100** (maniquí Neumann KU100, Univ. de York),
> bajo licencia **Apache-2.0** (redistribución comercial OK con atribución). Es la
> elección definitiva para los plugins que usan HRTF.
>
> **Honestidad sobre lo que está horneado HOY:** el HRIR fijo y el anillo de movimiento
> que viven horneados en este repo (`shared/data/HrirData.h`, `shared/data/HrirRing.h`,
> `shared/engines/binaural/HrirData.h`, `shared/engines/binaural/HrirRing.h`) **derivan
> de SADIE II KU100** (maniquí Neumann, Apache-2.0). El re-bake de producción se ejecutó
> offline con `tools/gen_hrir.cpp` (portado de `orbita/tests/GenHrir.cpp`), que abre el
> SOFA `D1_48K_24bit_256tap_FIR_SOFA.sofa` (KU100 = sujeto **D1** de SADIE II, 48 kHz,
> bajado de Zenodo record 12092466) y aplica el mismo pipeline offline: simetrización L/R
> (casi un no-op en un maniquí ya simétrico), ecualización de campo difuso parcial
> band-limitada y reconstrucción a fase mínima. Resultado: **256 taps** por IR (el SOFA de
> SADIE los entrega así; CIPIC daba 218 — el código consume `kRingTaps`/`kHrirLength`
> dinámicamente, sin asumir un largo fijo).
>
> **Nota técnica honesta sobre el ITD:** el SOFA de SADIE D1 guarda `Data.Delay = 0` en
> todas las direcciones (el ITD vive como tiempo-de-llegada DENTRO de la IR, no en un campo
> de delay aparte, a diferencia del fixture CIPIC `…_itdInDelayField.sofa`). Como el
> pipeline reconstruye a fase mínima (que descarta el tiempo-de-llegada), los arrays
> `kRingDelayL/R` quedan en cero y el ITD por delay no se re-aplica; la imagen direccional
> se sostiene sobre el ILD por oído (magnitud), el `kILD` del motor, el decorrelador y el
> group-delay de fase mínima. Medido objetivamente con `[measure][pulsar]`, el resultado es
> **más simétrico y limpio que el CIPIC anterior** (BAL_dB más cerca de 0 en todos los
> casos), sin NaN y con la misma ganancia segura (PEAK=0.85). El re-bake está hecho y
> verificado; la receta completa sigue en `tools/gen-hrir.md`.
>
> Importante: **HALO NO usa HRIR** (es un pan binaural por ITD/ILD, sin convolución HRTF);
> el HRIR sólo lo consume **PULSAR** y futuros plugins HRTF.

### SADIE II Database — KU100  ← **dataset de PRODUCCIÓN (horneado HOY)**
- **Origen / autor:** The SADIE II Database, The Audio Lab, University of York —
  Cal Armstrong, Lewis Thresh, Damian Murphy, Gavin Kearney.
  <https://www.york.ac.uk/sadie-project/database.html>
- **Licencia / términos:** **Apache License 2.0**. *"Copyright 2018, University of York.
  All measurements are Copyright University of York."* Redistribución comercial permitida
  con atribución; el dataset original debe referenciarse siempre que se use en forma
  original o modificada.
- **Cita:** C. Armstrong, L. Thresh, D. Murphy, G. Kearney, *"A Perceptual Evaluation of
  Individual and Non-Individual HRTFs: A Case Study of the SADIE II Database"*, Applied
  Sciences, 8(11):2029, 2018. DOI: 10.3390/app8112029.
- **Uso en el proyecto:** medición del maniquí **KU100** (Neumann), SOFA/AES69 48 kHz
  (`D1_48K_24bit_256tap_FIR_SOFA.sofa`, sujeto D1, Zenodo record 12092466). **Es el HRIR
  horneado HOY** en los 4 headers — elegido por su licencia limpia y por ser un maniquí
  simétrico (la imagen binaural resultante es más simétrica que el CIPIC anterior). Re-bake
  offline con `tools/gen_hrir.cpp`; receta en `tools/gen-hrir.md`.

### CIPIC HRTF Database  ← **fixture HISTÓRICO (subject 003, de TEST) — ya NO horneado**
- **Origen / autor:** CIPIC Interface Laboratory, U.C. Davis — V. R. Algazi, R. O. Duda,
  D. M. Thompson, C. Avendano. <https://www.ece.ucdavis.edu/cipic/>
- **Licencia / términos:** distribución libre, de **dominio público para investigación**;
  pide atribución a los autores. Licencia gris para uso comercial — por eso **se reemplazó
  por SADIE II KU100** (Apache-2.0) en el re-bake de producción.
- **Cita:** V. R. Algazi, R. O. Duda, D. M. Thompson, C. Avendano, *"The CIPIC HRTF
  Database"*, Proc. 2001 IEEE Workshop on Applications of Signal Processing to Audio
  and Acoustics (WASPAA), pp. 99–102, 2001.
- **Uso en el proyecto:** **ya NO se usa.** Fue el fixture de test que se horneó de forma
  transitoria (sujeto 003 → HRIR fijo + anillo de 72 azimuts) hasta que se ejecutó el
  re-bake a SADIE II KU100. Sigue siendo el SOFA del test del `SofaLoader` (`OVNI_TEST_SOFA`),
  que es independiente del HRIR horneado.

### MIT KEMAR HRTF
- **Origen / autor:** Bill Gardner y Keith Martin, MIT Media Laboratory (1994).
  <https://sound.media.mit.edu/resources/KEMAR.html>
- **Licencia / términos:** *"Copyright 1994 by the MIT Media Laboratory. Provided free
  with no restrictions on use, provided the authors are cited when the data is used in
  any research or commercial application."* — comercial OK con atribución.
- **Cita:** W. G. Gardner, K. D. Martin, *"HRTF measurements of a KEMAR"*, J. Acoust.
  Soc. Am., vol. 97, no. 6, pp. 3907–3908, junio 1995.
- **Uso en el proyecto:** alternativa histórica de maniquí simétrico. **No es el dataset
  de producción** (se eligió SADIE II KU100) y **hoy NO está horneado**.

---

## Notas de cumplimiento

- **SAF (Spatial Audio Framework):** si en el futuro se incorpora, usar solo módulos
  permisivos (ISC); **no** activar `saf_tracker` ni `saf_hades` (vuelven todo GPLv2).
- **Impulse Responses (IRs):** solo IRs propias o CC0 / permisivas. Nunca IRs extraídas
  de productos comerciales.
- **El HRIR horneado** deriva de **SADIE II KU100** (Apache-2.0), bajado de su fuente
  canónica (Univ. de York vía Zenodo). El re-bake se ejecutó offline con
  `tools/gen_hrir.cpp`; el SOFA NO se commitea (vive fuera del repo). El SOFA de CIPIC que
  aún figura en el árbol es **sólo** el fixture del test del `SofaLoader` (`OVNI_TEST_SOFA`,
  dato de test de libmysofa), no la fuente del HRIR horneado. Receta completa en
  `tools/gen-hrir.md`.
