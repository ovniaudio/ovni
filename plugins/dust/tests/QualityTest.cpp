// Test [quality][dust] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  dust::DustProcessor
#define OVNI_PLUGIN_SLUG       "dust"
#define OVNI_PLUGIN_TAG        "[dust]"
#include "PluginProcessor.h"
#define OVNI_QUALITY_SHORT(proc) do { ovni::test::setParam ((proc), "density", 0.2f); ovni::test::setParam ((proc), "vida", 0.0f); } while (0)
#include "template/tests/QualityStub.h"
