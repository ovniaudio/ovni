// Test [latency][pulsar] — latencia reportada == real (house-standard §1/§2: ≤ 1 sample). PULSAR es un FX
// de movimiento de latencia 0 (el motor binaural HRIR no usa lookahead; no llama setLatencySamples → el
// chasis reporta 0). Con MIX=0 (sólo el dry) el impulso debe salir en el sample 0 (± 1). El stub NO asume
// el valor: verifica la PROPIEDAD (real == reportada), así sirve igual para PULSAR (0) y para NÉBULA (~3 ms
// de lookahead). Imprime LATENCY_REPORTED= / LATENCY_REAL= (los números públicos) que measure-check.sh lee.
//
// Reusa la batería compartida (shared/template/tests/LatencyStub.h) → medición uniforme. El id del dry/wet
// de PULSAR es "mix" (default del stub), no hace falta override.
#define OVNI_PLUGIN_PROCESSOR  pulsar::PulsarProcessor
#define OVNI_PLUGIN_SLUG       "pulsar"
#define OVNI_PLUGIN_TAG        "[pulsar]"
#include "PluginProcessor.h"
#include "template/tests/LatencyStub.h"
