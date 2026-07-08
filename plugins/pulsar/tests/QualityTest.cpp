// Test [quality][pulsar] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  pulsar::PulsarProcessor
#define OVNI_PLUGIN_SLUG       "pulsar"
#define OVNI_PLUGIN_TAG        "[pulsar]"
#include "PluginProcessor.h"

#include "template/tests/QualityStub.h"
