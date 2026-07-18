// Test [quality][halo] — la señal NUNCA pierde calidad (piso de silencio / DC / null MIX=0 / linealidad).
// Batería uniforme del sello: shared/template/tests/QualityStub.h (gates de transparencia UAD-grade).
#define OVNI_PLUGIN_PROCESSOR  halo::HaloProcessor
#define OVNI_PLUGIN_SLUG       "halo"
#define OVNI_PLUGIN_TAG        "[halo]"
#include "PluginProcessor.h"
#define OVNI_QUALITY_SHORT(proc) do { ovni::test::setParam ((proc), "decay", 0.0f); ovni::test::setParam ((proc), "shimmer", 0.0f); ovni::test::setParam ((proc), "freeze", 0.0f); } while (0)
#define OVNI_QUALITY_WET(proc)   do { ovni::test::setParam ((proc), "freeze", 0.0f); } while (0)
// El difusor FDN de HALO corre con decay INTERNO fijo (kFdnDecay01=0.95 → t60≈9.4 s: el "glacial"
// ES el diseño; DECAY del usuario gobierna el lazo, no el difusor). Con la ventana default de 10 s
// el gate del piso pescaba el FINAL de esa cola de diseño (−81.7 dBFS, a 1.7 dB del gate), no el
// piso real. A 14 s la cola ya murió y se mide el silencio de verdad (QA catálogo 2026-07-16).
#define OVNI_QUALITY_TAIL_SECS 14.0
#include "template/tests/QualityStub.h"
