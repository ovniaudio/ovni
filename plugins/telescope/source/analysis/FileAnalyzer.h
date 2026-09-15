#pragma once
#include <atomic>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include "analysis/FileAnalysis.h"
#include "analysis/modules/Cqt.h"
#include "analysis/modules/Loudness.h"
// FrameSinkFanout vive en Reference.h (es el fan-out del FrameSink de Spectrum; ver el prompt 55/1e).
#include "analysis/modules/Reference.h"
#include "analysis/modules/Spectrum.h"

// ========================================================================================================
// FileAnalyzer — EL MISMO MOTOR sobre un archivo, offline (spec §5.7, prompt 54).
//
// Carga un track (el tuyo, o uno comercial), lo analiza ENTERO más rápido que tiempo real y deja un
// FileAnalysis. TONAL BALANCE (lente 12) lo usa como referencia; VERDICT (lente 13) va a usar sus series
// temporales para decir "qué falta y dónde" con tiempos exactos.
//
// ES UN THREAD PROPIO, CON INSTANCIAS PROPIAS de Loudness y Spectrum. Nunca las del AnalysisThread: esas
// están midiendo el programa en vivo, y meterle un archivo por el medio le rompería el integrado al
// usuario sin que nada en pantalla lo dijera.
//
// EL SAMPLE RATE ES EL DEL ARCHIVO, no el de la sesión. No se remuestrea: el motor se prepara a la SR del
// archivo y lo analiza tal cual está. Ni los ⅓ de octava ni el LUFS dependen de la SR (los primeros son
// bandas en Hz; el segundo tiene su filtro K especificado por SR en BS.1770), así que la referencia de un
// archivo a 44.1 k es directamente comparable contra un programa en vivo a 48 k. Remuestrear sólo
// agregaría un filtro más entre el archivo y su propia medición.
//
// CANALES. Mono (1 canal) → se duplica a L y R, exactamente como hace `processAudio` con una fuente mono.
// Estéreo → L y R. MÁS DE DOS → se miden los dos primeros y queda dicho en `warning`: un análisis de 5.1
// mirando sólo L y R es una medición legítima de L y R, pero llamarla "la referencia" sin avisar no lo es.
//
// CADENCIA DE LAS SERIES. `shortTermHistory` sale de los hops de 100 ms del medidor (10 Hz exactos) y
// `truePeakPerSecond` de agrupar diez de esos (1 Hz exacto). Son las mismas posiciones del stream que usa
// el análisis en vivo — por eso los números coinciden al bit y por eso VERDICT puede decir "en el
// segundo 47" y que sea el mismo segundo 47 que vería en tiempo real.
//
// NADA MODAL. La elección de archivo (FileChooser) la hace la UI de forma asíncrona; acá entra un
// juce::File ya elegido. Eso es también lo que hace que los tests puedan cargar una referencia sin
// fabricar diálogos.
// ========================================================================================================
namespace telescope
{
class FileAnalyzer : private juce::Thread
{
public:
    // Bloque de lectura. 4 096 muestras: suficientemente grande para que el I/O no domine y chico para
    // que `cancel()` conteste rápido (se chequea la salida una vez por bloque).
    static constexpr int kReadBlock = 4096;

    FileAnalyzer();
    ~FileAnalyzer() override;

    // Arranca el análisis de `f`. Cancela y ESPERA al análisis anterior si había uno (dos análisis a la
    // vez sobre el mismo objeto darían un resultado de cuál de los dos, nadie sabe).
    void start (const juce::File& f);

    // Corta el análisis en curso y espera a que el worker salga. Idempotente.
    void cancel();

    bool  busy() const noexcept     { return running.load (std::memory_order_acquire); }
    float progress() const noexcept { return progressValue.load (std::memory_order_acquire); }

    // El último resultado. Mientras `busy()` es true, es el resultado ANTERIOR (o uno vacío): el worker
    // publica una sola vez, al final. Copia bajo lock — la llama el message thread, no el audio thread.
    FileAnalysis result() const;

    // Cambia de 0 a 1 cada vez que se PUBLICA un resultado nuevo. La UI compara contra el suyo para saber
    // si tiene que releer, sin tener que copiar el FileAnalysis en cada frame.
    juce::uint32 revision() const noexcept { return rev.load (std::memory_order_acquire); }

    // El archivo que se está analizando (o el último analizado). Para el texto "analizando <nombre>".
    juce::String currentName() const;

    // Se llama UNA VEZ, DESDE EL THREAD DEL ANALIZADOR, en cuanto el resultado está publicado. Existe para
    // que el resultado llegue al motor sin depender de que alguien esté bombeando el message thread: en un
    // host con la ventana cerrada no lo bombea nadie, y la referencia se quedaría cargada a medias sin que
    // nada lo dijera. Quien la use tiene que ser seguro de llamar desde otro hilo (Reference lo es).
    std::function<void()> onFinished;

    void run() override;

private:
    // Toda la parte que no depende de JUCE-el-thread, para que se lea de arriba a abajo.
    FileAnalysis analyse (const juce::File& f);
    void         publish (const FileAnalysis& a);

    juce::AudioFormatManager formats;

    // El motor: instancias PROPIAS (ver el encabezado).
    Loudness           loudness;
    Spectrum           spectrum;
    ThirdOctaveAverage bands;
    // ===== 55: la historia POR SEGUNDO y el constant-Q, para que el archivo traiga LAS MISMAS FILAS que
    // el análisis en vivo. El builder cuelga del MISMO FrameSink que el promedio de ⅓ de octava (por eso
    // el fan-out) y el CQT come de los hops de 100 ms, igual que en el AnalysisThread.
    SecondBuilder      secondBuilder;
    Cqt                cqt;
    FrameSinkFanout    fanout;

    juce::File                pending;      // lo que hay que analizar (lo fija start(), lo lee run())
    mutable juce::CriticalSection lock;      // protege `latest` y `pendingName`
    FileAnalysis              latest;
    juce::String              pendingName;

    std::atomic<bool>         running       { false };
    std::atomic<float>        progressValue { 0.0f };
    std::atomic<juce::uint32> rev           { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileAnalyzer)
};
}
