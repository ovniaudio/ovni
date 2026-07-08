// Test [quality][nebula] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  nebula::NebulaProcessor
#define OVNI_PLUGIN_SLUG       "nebula"
#define OVNI_PLUGIN_TAG        "[nebula]"
#include "PluginProcessor.h"
#define OVNI_QUALITY_SHORT(proc) do { ovni::test::setParam ((proc), "decay", 0.15f); ovni::test::setParam ((proc), "breath", 0.0f); } while (0)
#include "template/tests/QualityStub.h"
