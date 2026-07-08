// Test [bench][pulsar] — CPU% de una instancia de PULSAR (house-standard §1/§2; budget Movimiento ≤ 3%).
// Corre 10.000 bloques 48k/512 por el PulsarProcessor REAL (processor + motor BINAURAL HRIR + SMEAR +
// limiter) en estado estacionario y mide el wall-clock vs el tiempo de audio. Imprime CPU_PCT= (uno de los
// 3 números públicos del Manifiesto #3) que measure-check.sh grepea.
//
// Mide el peor caso de costo: MOTION/SMEAR/WIDTH al máximo (binaural denso + reflexiones + cola = la
// convolución HRIR + la estela trabajando al máximo). El gate vs el budget Movimiento (≤3%) es WARN en
// validate.sh (el hardware del runner no es el de producción + el medidor infla, ver
// anti-click-clip-truepeak.md §7); acá REQUIRE finitud + cordura. Reusa la batería compartida
// (shared/template/tests/BenchStub.h) → medición uniforme con el resto del catálogo.
#define OVNI_PLUGIN_PROCESSOR  pulsar::PulsarProcessor
#define OVNI_PLUGIN_SLUG       "pulsar"
#define OVNI_PLUGIN_TAG        "[pulsar]"
#include "PluginProcessor.h"

// Peor caso de costo de PULSAR: binaural denso + estela + ancho al máximo, wet pleno.
#define OVNI_BENCH_SETUP(proc) do {                       \
        ovni::test::setParam ((proc), "mix",    1.0f);    \
        ovni::test::setParam ((proc), "motion", 1.0f);    \
        ovni::test::setParam ((proc), "smear",  1.0f);    \
        ovni::test::setParam ((proc), "width",  1.0f);    \
    } while (0)

#include "template/tests/BenchStub.h"
