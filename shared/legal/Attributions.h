// =====================================================================================
// Attributions.h — avisos de terceros para la pantalla "About" del chasis OVNI.
//
// Header-only, sin dependencias: solo un string constexpr. Mantener EN SINCRONÍA con el
// NOTICE.md de la raíz del repo (es la misma información, en forma compacta para mostrar
// dentro del plugin). Marca del sello = honestidad verificable: si About y NOTICE.md no
// coinciden, es un bug.
//
// Uso típico:
//   #include "shared/legal/Attributions.h"
//   aboutTextEditor.setText (ovni::legal::kThirdPartyNotice);
// =====================================================================================
#pragma once

namespace ovni::legal {

inline constexpr const char* kThirdPartyNotice = R"(OVNI — Avisos de terceros

Este software se distribuye bajo AGPLv3 (ver LICENSE en el repo).

LICENCIA
  Catalogo gratis OVNI: AGPLv3 (coincide con los terminos open-source de JUCE).
  ORBIT (si se vende cerrado): JUCE Starter free tier (<US$20k/ano).
  JUCE 8.0.13 en su edicion gratuita es AGPLv3 o comercial (dual); el catalogo
  OVNI sale bajo AGPLv3 y publica todo el codigo en el release.

SOFTWARE
  JUCE 8.0.13 - Raw Material Software Ltd. - AGPLv3 + comercial (dual) - juce.com
    Framework de audio/UI de todos los plugins.
  libmysofa v1.3.4 - Christian Hoene y colaboradores - BSD-3-Clause
    Carga de HRTF en formato SOFA / AES69 (modulos binaurales).
  Catch2 v3.8.1 - catchorg contributors - BSL-1.0
    Framework de tests (no se enlaza en release).
  Syphon-Framework - Tom Butterworth & Anton Marini - BSD-2-Clause
    Servidor Syphon (macOS): SUPERNOVA publica su textura a OBS/Resolume (RF7).

DATASETS HRIR / HRTF
  Dataset de PRODUCCION: SADIE II KU100 (Apache-2.0). HALO NO usa HRIR (es un
  pan binaural por ITD/ILD); el HRIR solo lo usa PULSAR y futuros plugins HRTF.
  Estado real HOY: el HRIR horneado DERIVA de SADIE II KU100 (maniqui Neumann,
  sujeto D1, SOFA 48k 256 taps; simetrizado + campo difuso + fase minima, offline).
  Re-bake hecho y verificado (imagen mas simetrica que el CIPIC anterior, sin NaN).
  Receta en tools/gen-hrir.md; generador en tools/gen_hrir.cpp.

  SADIE II Database (KU100) - The Audio Lab, University of York
    (Armstrong, Thresh, Murphy, Kearney). Apache-2.0. Copyright 2018,
    University of York. Cita DOI: 10.3390/app8112029. <- HRIR horneado HOY.
  CIPIC HRTF Database - CIPIC Lab, U.C. Davis (Algazi, Duda, Thompson, Avendano)
    Libre para investigacion, pide atribucion. Cita: "The CIPIC HRTF Database",
    Proc. IEEE WASPAA 2001, pp. 99-102. <- fixture historico, ya NO horneado
    (solo es el SOFA del test del SofaLoader, OVNI_TEST_SOFA).
  MIT KEMAR HRTF - Bill Gardner y Keith Martin, MIT Media Laboratory (1994)
    Libre, comercial OK con atribucion. Cita: Gardner & Martin, JASA 97(6):3907-3908, 1995.

Detalle completo y enlaces: NOTICE.md en la raiz del repositorio.)";

} // namespace ovni::legal
