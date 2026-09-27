#pragma once
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <juce_audio_basics/juce_audio_basics.h>
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
//
// F2 de la 0.2 · LAS OTRAS ESCALAS. Un usuario usa Windows al 125 % y a escala no entera la lente cae en un
// píxel fraccionario: eso no se ve en una foto a 2×. Con `OVNI_UISNAP_SCALES="1,1.25,1.5,2"` cada foto se
// saca además a esas escalas, como `<nombre>@1.25x.png` al lado de la de siempre. Sin la variable, el
// runner escribe lo mismo que antes (la de 2× con su nombre de siempre), así nadie tiene que cambiar nada.
// DÓNDE caen las fotos. Los casos escriben "/tmp/ovni_telescope_….png" (así las buscan los scripts de la casa),
// pero en Windows /tmp no existe. Con OVNI_UISNAP_DIR el prefijo /tmp/ se cambia por esa carpeta; sin ella,
// en Windows va a la carpeta temporal del sistema. En la Mac, sin la variable, queda exactamente como antes.
inline juce::String uisnapPath (const juce::String& path)
{
    if (! path.startsWith ("/tmp/")) return path;
    auto dir = juce::SystemStats::getEnvironmentVariable ("OVNI_UISNAP_DIR", {});
   #if JUCE_WINDOWS
    if (dir.isEmpty()) dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName();
   #endif
    if (dir.isEmpty()) return path;
    juce::File (dir).createDirectory();
    return juce::File (dir).getChildFile (path.fromFirstOccurrenceOf ("/tmp/", false, false)).getFullPathName();
}

inline void writePngAt (juce::Component& c, const juce::String& pathIn, float scale)
{
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    REQUIRE (img.isValid());

    const auto path = uisnapPath (pathIn);
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

inline void writePng (juce::Component& c, const juce::String& path, float scale = 2.0f)
{
    writePngAt (c, path, scale);

    const auto extra = juce::SystemStats::getEnvironmentVariable ("OVNI_UISNAP_SCALES", {});
    for (const auto& token : juce::StringArray::fromTokens (extra, ",", {}))
    {
        const float s = token.trim().getFloatValue();
        if (s <= 0.0f || std::abs (s - scale) < 1.0e-4f) continue;
        // La base ya TRADUCIDA: en Windows juce::File ("/tmp/…") es "D:\tmp\…", que ya no empieza con /tmp/ y
        // uisnapPath no lo reconocería (corrida 36225167713: 17 fotos extra que no se pudieron abrir).
        const juce::File base (uisnapPath (path));
        const auto tag = juce::String (s, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        writePngAt (c, base.getSiblingFile (base.getFileNameWithoutExtension() + "@" + tag + "x"
                                            + base.getFileExtension()).getFullPathName(), s);
    }
}

// ========================================================================================================
// F2 de la 0.2 · LA FOTO ESPERA A QUE EL MOTOR HAYA DIGERIDO TODO (veredicto 98, observación 2).
//
// Con la Mac cargada, 16 de 53 PNG de [uisnap] cambiaban entre dos corridas iguales. No era el dibujo: era
// CUÁNDO se sacaba la foto. Los casos esperaban una condición parcial ("más de 20 frames de espectro",
// "más de 40 de CQT") y fotografiaban con el hilo de análisis todavía comiendo, así que cuánto audio había
// adentro de la foto dependía de la carga de la máquina.
//
// La regla ahora: se empuja un número ENTERO de hops (100 ms) y se espera a que el motor marque exactamente
// ese tiempo analizado. El último hop se cierra con la última muestra empujada, y el worker alimenta el
// espectro con el trozo ANTES de cerrar los hops de ese trozo (AnalysisThread::run), así que cuando el frame
// dice ese tiempo, TODO lo empujado ya pasó por todos los módulos y ya se publicó.
template <typename Proc>
void waitDigested (Proc& proc, long long samplesPushed, double sampleRate, int timeoutMs = 30000)
{
    const auto hop = (long long) std::llround (sampleRate / 10.0);
    REQUIRE (samplesPushed % hop == 0);   // si no, la cola (< 1 hop) queda sin garantía
    const double target = (double) (samplesPushed / hop) * 0.1 - 0.05;
    REQUIRE (waitUntil ([&] { return proc.analysis().read().timeSeconds >= target; }, timeoutMs));
}

// El freno para que el bus no descarte (8 s de capacidad a 48 k): si el motor se atrasa más de 2 s, se
// espera a que se acerque. Sin REQUIRE en el camino feliz a propósito: el REQUIRE condicional que había
// antes hacía que el número de aserciones de [uisnap] cambiara con la carga (414 / 417 en dos corridas).
template <typename Proc>
void keepUp (Proc& proc, long long samplesPushed, double sampleRate, int timeoutMs = 30000)
{
    const double pushed = (double) samplesPushed / sampleRate;
    if (pushed - proc.analysis().read().timeSeconds <= 2.0) return;
    if (! waitUntil ([&] { return pushed - proc.analysis().read().timeSeconds <= 1.0; }, timeoutMs))
        FAIL ("el hilo de análisis no alcanzó al audio empujado");
}

// Empuja EXACTAMENTE `total` muestras en bloques de 512 (el último, más corto) y frena con keepUp. `fill`
// llena L y R de un bloque de `k` muestras. Devuelve lo empujado, para dárselo a waitDigested.
template <typename Proc, typename Fill>
long long pushExact (Proc& proc, long long total, double sampleRate, Fill&& fill)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (long long done = 0; done < total;)
    {
        const int k = (int) juce::jmin ((long long) 512, total - done);
        buf.setSize (2, k, false, false, true);
        buf.clear();
        fill (buf, k);
        proc.processBlock (buf, midi);
        done += k;
        keepUp (proc, done, sampleRate);
    }
    return total;
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
