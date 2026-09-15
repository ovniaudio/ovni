#pragma once
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <functional>
#include <limits>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

// ========================================================================================================
// Esperas DETERMINISTAS para los tests de TELESCOPE.
//
// Por qué existe (hallazgo L-3 del revisor del prompt 48): los tests del motor esperaban con `sleep` de
// tiempo fijo ("dormí 300 ms, el AnalysisThread ya digirió"). Eso es una apuesta sobre la carga de la
// máquina: en CI, o con otra obrera compilando al lado, el thread llega tarde y el test se pone rojo sin
// que nada esté roto. Un test que falla por estar ocupada la máquina deja de ser una señal.
//
// `waitUntil` espera a que se cumpla una CONDICIÓN, sondeando cada `pollMs`, hasta `timeoutMs`. El timeout
// es un tope de seguridad, no el tiempo de espera: si la condición se cumple a los 8 ms, vuelve a los 8 ms.
// Subir el timeout NUNCA hace el test más frágil — sólo más paciente.
//
// Para las aserciones NEGATIVAS ("en pausa el análisis NO avanza") se usa el mismo helper al revés:
//     REQUIRE_FALSE (waitUntil ([&]{ return avanzo(); }, 300));
// ahí el timeout es el tiempo que le damos al bug para aparecer: alargarlo hace el test MÁS estricto.
// ========================================================================================================
namespace telescope::test
{
inline bool waitUntil (std::function<bool()> pred, int timeoutMs, int pollMs = 2)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline)
    {
        if (pred()) return true;
        juce::Thread::sleep (juce::jmax (1, pollMs));
    }
    return pred();   // una última chance: puede haberse cumplido justo en el último sondeo
}

// Espera a que un valor deje de moverse (`stableMs` sin cambiar). Es lo que hace falta después de un
// PAUSE: el worker puede estar terminando el trozo que ya sacó del bus, y ese resto SÍ es legítimo.
// ========================================================================================================
// writePng — la captura de un componente a PNG que usan TODOS los tests de snapshot.
//
// Vivía copiada cuatro veces (UiSnapshotTest, WaterfallTest, FieldTest, TonalBalanceTest), palabra por
// palabra (LOW de los tres revisores). Cuatro copias de la misma función son cuatro oportunidades de que
// una capture a 1× o deje de verificar que el archivo se escribió, y nadie lo note hasta mirar un PNG
// vacío. Acá, una.
//
// 2× a propósito: las capturas se miran en una pantalla Retina y a 1× el texto de 9 pt no se lee.
inline void writePng (juce::Component& c, const juce::String& path, float scale = 2.0f)
{
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    REQUIRE (img.isValid());

    juce::File f (path);
    f.deleteFile();
    juce::FileOutputStream os (f);
    REQUIRE (os.openedOk());
    juce::PNGImageFormat png;
    REQUIRE (png.writeImageToStream (img, os));
    os.flush();
    REQUIRE (f.getSize() > 0);
    std::printf ("UISNAP %s  (%d×%d lógicos)\n", path.toRawUTF8(), c.getWidth(), c.getHeight());
}

inline bool waitStable (std::function<double()> value, int stableMs, int timeoutMs, int pollMs = 2)
{
    const int  needed = juce::jmax (1, stableMs / juce::jmax (1, pollMs));
    double     last   = std::numeric_limits<double>::quiet_NaN();
    int        run    = 0;

    return waitUntil ([&]
                      {
                          const double v = value();
                          run  = (v == last) ? run + 1 : 0;
                          last = v;
                          return run >= needed;
                      },
                      timeoutMs, pollMs);
}
}
