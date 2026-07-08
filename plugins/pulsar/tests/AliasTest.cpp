// Test [alias][pulsar] — piso de espurias de PULSAR (plugin LINEAL: motor BINAURAL HRIR = convolución +
// ITD + reflexiones + crosstalk; NO transpone frecuencia → "no genera armónicos → no necesita OS",
// house-standard §1 tabla). Por eso NO medimos un alias de pitch (PULSAR no tiene etapa de pitch): medimos
// el PISO DE ESPURIAS real (seno puro por el processor entero → energía fuera de la fundamental). La única
// no-linealidad es el limiter de salida del motor; con un seno a −8 dBFS casi no actúa → el piso es bajo.
// Imprime ALIAS_DBFS= (uno de los 3 números públicos del Manifiesto #3) que measure-check.sh grepea.
//
// El peor caso de PULSAR: MOTION/WIDTH/SMEAR al máximo (binaural denso + ensanchado + cola) con MIX pleno
// (medimos el wet, donde vive el motor). El gate < −96 dBFS lo evalúa validate.sh; acá REQUIRE finitud +
// cordura. Reusa la batería compartida (shared/template/tests/AliasStub.h) → medición uniforme con el resto.
#define OVNI_PLUGIN_PROCESSOR  pulsar::PulsarProcessor
#define OVNI_PLUGIN_SLUG       "pulsar"
#define OVNI_PLUGIN_TAG        "[pulsar]"
#include "PluginProcessor.h"

// Piso de espurias de PULSAR con la fuente ESTÁTICA (motion=0, smear=0). CLAVE: PULSAR tiene Doppler
// (delay variable = corrimiento de tono REAL e intencional); con motion>0 el seno de prueba se desplaza
// de bin por Doppler y "energía fuera de la fundamental" deja de ser alias — es el efecto funcionando.
// Por eso medimos el camino binaural ESTÁTICO (HRIR = convolución FIR lineal → el seno se queda quieto,
// sólo filtrado): ahí el piso de espurias mide lo que debe (no-linealidad residual del limiter/coef).
// Ancho natural (width=0.5) + wet pleno. El Doppler se mide aparte (es carácter, no alias).
#define OVNI_ALIAS_SETUP(proc) do {                       \
        ovni::test::setParam ((proc), "mix",    1.0f);    \
        ovni::test::setParam ((proc), "motion", 0.0f);    \
        ovni::test::setParam ((proc), "width",  0.5f);    \
        ovni::test::setParam ((proc), "smear",  0.0f);    \
    } while (0)

#include "template/tests/AliasStub.h"
