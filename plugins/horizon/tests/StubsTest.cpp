// =============================================================================
// StubsTest.cpp — los stubs PARAMETRIZADOS del chasis enganchados a HORIZON
// (contrato de 3 defines, shared/template/tests/_README.md):
//
//   ⚠ NullStub NO se engancha acá: ese stub exige bypass == entrada SIN retardo,
//     correcto para plugins de latencia 0 pero INCORRECTO para el eje espectral —
//     HORIZON compensa su bypass (dry retrasado N, PDC constante, micro-fade). El
//     [bypass][horizon] propio vive en BypassTest.cpp (dry retrasado N, bit-exact).
//
//   · [buffersize][horizon]  — invarianza al block-size (64 vs 512, ±eps). EN
//                              CONDICIONES REALES (mecanismo encendido): FREEZE on +
//                              SPREAD + MIX realista. WHISPER 0 (determinístico: la
//                              fase coherente no usa RNG → bit-exact garantizado; el
//                              freeze captura en el MISMO frame en ambas corridas y
//                              el conteo de frames del STFT es idéntico al block-size).
//   · [staterecall][horizon] — get/setStateInformation round-trip A NIVEL DE SEÑAL
//                              (recarga de sesión = mismo sonido).
// =============================================================================

#define OVNI_PLUGIN_PROCESSOR  horizon::HorizonProcessor
#define OVNI_PLUGIN_SLUG       "horizon"
#define OVNI_PLUGIN_TAG        "[horizon]"
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// [buffersize] en condiciones reales: FREEZE on + SPREAD + MIX 40 %, WHISPER 0
// (determinístico — sin RNG; la invarianza es estructural por la grilla de hop del STFT).
#define OVNI_BUFFERSIZE_SETUP(proc)                                           \
    do {                                                                      \
        namespace bspid = horizon::params::id;                               \
        ovni::test::setParam ((proc), bspid::FREEZE,  1.0f);                  \
        ovni::test::setParam ((proc), bspid::WHISPER, 0.0f);                  \
        ovni::test::setParam ((proc), bspid::SPREAD,  0.60f);                 \
        ovni::test::setParam ((proc), bspid::MIX,     0.40f);                 \
    } while (false)

#include "template/tests/BufferSizeStub.h"
#include "template/tests/StateRecallStub.h"
