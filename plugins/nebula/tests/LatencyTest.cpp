// Test [latency][nebula] — latencia reportada == real (house-standard §1/§2: ≤ 1 sample). NÉBULA reporta
// al host (PDC) el lookahead del limiter de salida (~3 ms @48k ≈ 144 samples): ese limiter actúa sobre la
// SUMA dry+wet (FdnReverb §7c) → TODA la salida, incluido el dry, queda retardada por ese lookahead. Con
// MIX=0 (sólo el dry) el impulso debe salir en el sample == reportada (NO en 0): es la prueba de que lo que
// reportás al host es lo que pasa de verdad. El stub verifica la PROPIEDAD (real == reportada ±1), sin
// asumir el valor → mismo gate que HALO (reporta 0) y PULSAR (reporta 0). Imprime LATENCY_REPORTED= /
// LATENCY_REAL= (los números públicos) que measure-check.sh lee.
//
// (El GainTest.cpp de NÉBULA ya verifica que el valor reportado == round(3·sr/1000) a 44.1/48/96k; este
// test cierra el círculo midiendo el retardo REAL del impulso vs lo reportado.) Reusa la batería compartida
// (shared/template/tests/LatencyStub.h) → medición uniforme. El id del dry/wet de NÉBULA es "mix".
#define OVNI_PLUGIN_PROCESSOR  nebula::NebulaProcessor
#define OVNI_PLUGIN_SLUG       "nebula"
#define OVNI_PLUGIN_TAG        "[nebula]"
#include "PluginProcessor.h"
#include "template/tests/LatencyStub.h"
