// Test [bench][nebula] — CPU% de una instancia de NÉBULA (house-standard §1/§2; budget FDN ≤ 5%). Corre
// 10.000 bloques 48k/512 por el NebulaProcessor REAL (processor + motor REVERB FDN + breath + limiter de
// salida con lookahead) en estado estacionario y mide el wall-clock vs el tiempo de audio. Imprime CPU_PCT=
// (uno de los 3 números públicos del Manifiesto #3) que measure-check.sh grepea.
//
// Mide el peor caso de costo: SIZE/DECAY/MIX al máximo (cola larga + densa + wet pleno; el FDN corriendo a
// pleno + el limiter de lookahead trabajando sobre la suma dry+wet). El gate vs el budget FDN (≤5%) es WARN
// en validate.sh (el hardware del runner no es el de producción + el medidor infla, ver
// anti-click-clip-truepeak.md §7); acá REQUIRE finitud + cordura. Reusa la batería compartida
// (shared/template/tests/BenchStub.h) → medición uniforme con el resto del catálogo.
#define OVNI_PLUGIN_PROCESSOR  nebula::NebulaProcessor
#define OVNI_PLUGIN_SLUG       "nebula"
#define OVNI_PLUGIN_TAG        "[nebula]"
#include "PluginProcessor.h"

// Peor caso de costo de NÉBULA: cola larga + densa, wet pleno.
#define OVNI_BENCH_SETUP(proc) do {                      \
        ovni::test::setParam ((proc), "mix",   1.0f);    \
        ovni::test::setParam ((proc), "size",  1.0f);    \
        ovni::test::setParam ((proc), "decay", 0.9f);    \
    } while (0)

#include "template/tests/BenchStub.h"
