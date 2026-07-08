// Test [quality][horizon] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  horizon::HorizonProcessor
#define OVNI_PLUGIN_SLUG       "horizon"
#define OVNI_PLUGIN_TAG        "[horizon]"
#include "PluginProcessor.h"
#define OVNI_QUALITY_WET(proc) do { ovni::test::setParam ((proc), "freeze", 0.0f); } while (0)
#include "template/tests/QualityStub.h"
