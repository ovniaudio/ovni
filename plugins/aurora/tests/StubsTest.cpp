// =============================================================================
// StubsTest.cpp — los stubs PARAMETRIZADOS del chasis enganchados a AURORA
// (contrato de 3 defines, shared/template/tests/_README.md):
//
//   ⚠ NullStub NO se engancha acá: ese stub exige bypass == entrada SIN retardo,
//     que es lo correcto para plugins de latencia 0 (PULSAR/NEBULA/HALO/DUST)
//     pero INCORRECTO para el eje espectral — AURORA compensa su bypass (dry
//     retrasado N, PDC constante, micro-fade). El [null][aurora] propio vive en
//     BypassTest.cpp: bypass == entrada RETRASADA getLatencySamples(), bit-exact.
//
//   · [buffersize][aurora]  — invarianza al block-size (64 vs 512, ±eps). EN
//                             CONDICIONES REALES (lección §2): mecanismo
//                             ENCENDIDO (despliegue + MOTION + DUCK + Mono
//                             Safe) y MIX realista 40 % — no el caso
//                             neutralizado. El DUCK entra porque su detector
//                             quedó anclado a la grilla de HOP del STFT
//                             (AuroraEngine: envNorm por frontera de hop), no
//                             al bloque del host: la invarianza es estructural.
//   · [staterecall][aurora] — get/setStateInformation round-trip A NIVEL DE
//                             SEÑAL (recarga de sesión = mismo sonido).
//
// El barrido fino 32→2048 + sample-rates vive en ConsistencyTest.cpp.
// =============================================================================

#define OVNI_PLUGIN_PROCESSOR  aurora::AuroraProcessor
#define OVNI_PLUGIN_SLUG       "aurora"
#define OVNI_PLUGIN_TAG        "[aurora]"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// [buffersize] en condiciones reales: TODO el mecanismo encendido + MIX 40 %.
// (MOTIONRATE queda en su default 0.3 Hz — determinístico: la fase avanza por
// frame del STFT, no por wall-clock.)
#define OVNI_BUFFERSIZE_SETUP(proc)                                           \
    do {                                                                      \
        namespace bspid = aurora::params::id;                                 \
        ovni::test::setParam ((proc), bspid::SPREAD,      0.70f);             \
        ovni::test::setParam ((proc), bspid::TILT,        0.75f); /* +50 */   \
        ovni::test::setParam ((proc), bspid::MOTION,      0.50f);             \
        ovni::test::setParam ((proc), bspid::MONOSAFEAMT, 0.50f);             \
        ovni::test::setParam ((proc), bspid::DUCK,        0.60f);             \
        ovni::test::setParam ((proc), bspid::MIX,         0.40f);             \
    } while (false)

#include "template/tests/BufferSizeStub.h"
#include "template/tests/StateRecallStub.h"
