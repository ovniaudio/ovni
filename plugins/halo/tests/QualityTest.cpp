// Test [quality][halo] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  halo::HaloProcessor
#define OVNI_PLUGIN_SLUG       "halo"
#define OVNI_PLUGIN_TAG        "[halo]"
#include "PluginProcessor.h"
#define OVNI_QUALITY_SHORT(proc) do { ovni::test::setParam ((proc), "decay", 0.0f); ovni::test::setParam ((proc), "shimmer", 0.0f); ovni::test::setParam ((proc), "freeze", 0.0f); } while (0)
#define OVNI_QUALITY_WET(proc)   do { ovni::test::setParam ((proc), "freeze", 0.0f); } while (0)
#include "template/tests/QualityStub.h"
