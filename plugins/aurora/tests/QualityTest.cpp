// Test [quality][aurora] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  aurora::AuroraProcessor
#define OVNI_PLUGIN_SLUG       "aurora"
#define OVNI_PLUGIN_TAG        "[aurora]"
#include "PluginProcessor.h"

#include "template/tests/QualityStub.h"
