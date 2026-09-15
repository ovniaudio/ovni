#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>

// ========================================================================================================
// SpectrogramRing — la historia del espectrograma: una columna de 512 filas por frame emitido.
//
// Por qué no va por TripleBuffer como todo lo demás: el espectrograma no muestra el ÚLTIMO frame, muestra
// los últimos 10 / 30 / 60 segundos. Un latest-wins perdería columnas y el eje de tiempo sería una mentira
// (los huecos no se verían: el dibujo simplemente comprimiría el tiempo sin avisar).
//
// UN SOLO ESCRITOR (el AnalysisThread) y un solo lector (el message thread). El escritor copia los 512
// bytes de la columna y RECIÉN DESPUÉS publica el índice con `release`; el lector carga el índice con
// `acquire` y sólo mira columnas estrictamente por detrás. El almacenamiento físico tiene dos columnas más
// que la capacidad lógica, así que la más vieja que el lector puede pedir sigue estando dos posiciones por
// delante de la que el escritor está pisando.
//
// El margen REAL entre el escritor y la columna más vieja que el lector puede pedir es `kSlack` columnas
// (el almacenamiento físico tiene kSlack más que la capacidad lógica), no "capacity": copyColumn rechaza
// todo lo anterior a `writeIndex - capacity`, así que el lector nunca pide la que el escritor está
// pisando ni la siguiente. El único riesgo que queda es que el lector se atrase tanto que la columna que
// pidió se vuelva ilegible ENTRE la comprobación y la copia — hacen falta kSlack columnas de atraso justo
// ahí. Si pasara, lo peor es una columna con dos mitades de momentos distintos: un píxel de ancho, sin
// crash y sin corromper nada.
//
// Cambiar el tamaño de FFT, el solape, el canal, el rango o la historia LIMPIA el ring: mezclar columnas
// medidas con dos mapeos distintos sería un dibujo que miente sobre lo que ya pasó.
//
// BYTES POR CELDA (prompt 51). El sonograma de nivel guarda UN byte por celda (el dB mapeado a 0-255); el
// espectrograma ESTÉREO guarda DOS (coherencia y energía), porque su color es la fase y su brillo es el
// nivel: son dos números distintos por celda y ninguno se deduce del otro. Se resuelve por PLANTILLA y no
// por un campo, para que el almacenamiento siga siendo un array FIJO: `configure()` se llama desde el
// worker mientras la UI puede estar leyendo, y un `std::vector` que se redimensiona ahí sería un
// use-after-free del lector. El de 1 byte se comporta exactamente igual que antes.
//
//     SpectrogramRing        = 60 s × 60 col/s × 512 × 1 = 1.8 MB
//     StereoSpectrogramRing  = 60 s × 60 col/s × 512 × 2 = 3.6 MB
// ========================================================================================================
namespace telescope
{
template <int kBytesPerCell>
class SpectrogramRingT
{
public:
    static constexpr int kRows      = 512;
    static constexpr int kBytes     = kBytesPerCell;
    static constexpr int kCellBytes = kRows * kBytesPerCell;   // bytes de UNA columna
    // 60 s de historia por 60 columnas por segundo (el tope de emisión del módulo Spectrum).
    static constexpr int kMaxColumns = 60 * 60;
    static constexpr int kSlack      = 2;      // margen entre el escritor y la columna más vieja legible

    static constexpr int kNumHistoryOptions = 3;
    static constexpr int kHistoryOptions[kNumHistoryOptions] = { 10, 30, 60 };

    // Fija la capacidad (historia × columnas por segundo, acotada al tope) y LIMPIA.
    //
    // LOS CUATRO CAMPOS SON ATÓMICOS (LOW de la auditora del 51). `configure()` corre en el WORKER —
    // cambiar el tamaño de FFT, el solape o la historia la llama— y el message thread lee `capacity()`,
    // `historySeconds()` y `columnsPerSecond()` en cada pintado de las lentes 4 y 10. Con enteros y
    // doubles comunes eso es una carrera de datos formal (UB), aunque en arm64 se lea siempre un valor
    // entero. `relaxed` alcanza: acá no se ordena nada respecto de los datos —de eso se encarga
    // `writePos` con su release/acquire—, sólo se pide que la lectura sea un valor y no medio.
    void configure (int historySeconds, double columnsPerSecond) noexcept
    {
        const int    hs = historySeconds > 0 ? historySeconds : kHistoryOptions[1];
        const double cs = columnsPerSecond > 0.0 ? columnsPerSecond : 1.0;
        const int    cap = std::clamp ((int) std::llround (cs * (double) hs), 1, kMaxColumns);

        historySec  .store (hs,             std::memory_order_relaxed);
        colsPerSec  .store (cs,             std::memory_order_relaxed);
        capacityCols.store (cap,            std::memory_order_relaxed);
        physical    .store (cap + kSlack,   std::memory_order_relaxed);
        clear();
    }

    void clear() noexcept { writePos.store (0, std::memory_order_release); }

    // Escritor: copia la columna y publica. `column` tiene kCellBytes bytes.
    void push (const juce::uint8* column) noexcept
    {
        const long long w = writePos.load (std::memory_order_relaxed);
        const auto phys = (long long) physical.load (std::memory_order_relaxed);
        auto* dst = data.data() + (size_t) (w % phys) * (size_t) kCellBytes;
        std::copy (column, column + kCellBytes, dst);
        writePos.store (w + 1, std::memory_order_release);   // recién ahora la columna existe para el lector
    }

    int       capacity() const noexcept { return capacityCols.load (std::memory_order_relaxed); }
    int       historySeconds() const noexcept { return historySec.load (std::memory_order_relaxed); }
    double    columnsPerSecond() const noexcept { return colsPerSec.load (std::memory_order_relaxed); }
    long long writeIndex() const noexcept { return writePos.load (std::memory_order_acquire); }
    int       count() const noexcept
    {
        return (int) std::min ((long long) capacity(), writePos.load (std::memory_order_acquire));
    }

    // Una columna por su índice ABSOLUTO (monotónico desde el clear). false si ya se perdió o no existe.
    bool copyColumn (long long absoluteIndex, juce::uint8* dst) const noexcept
    {
        const long long w = writePos.load (std::memory_order_acquire);
        if (absoluteIndex < 0 || absoluteIndex >= w || absoluteIndex < w - (long long) capacity()) return false;
        const auto* src = data.data()
                        + (size_t) (absoluteIndex % (long long) physical.load (std::memory_order_relaxed))
                        * (size_t) kCellBytes;
        std::copy (src, src + kCellBytes, dst);
        return true;
    }

    // Las últimas `maxCols` columnas en orden cronológico (la más vieja primero). Devuelve cuántas copió.
    int copyLatest (juce::uint8* dst, int maxCols) const noexcept
    {
        const long long w = writePos.load (std::memory_order_acquire);
        const int n = (int) std::min ((long long) std::min (maxCols, capacity()), w);
        for (int i = 0; i < n; ++i)
            copyColumn (w - n + i, dst + (size_t) i * (size_t) kCellBytes);
        return n;
    }

    // La frecuencia de una fila. Fila 0 = 20 Hz (el grave), fila 511 = 20 kHz: log-espaciadas.
    static double rowFrequency (int row) noexcept
    {
        const double t = (double) std::clamp (row, 0, kRows - 1) / (double) (kRows - 1);
        return kMinHz * std::pow (kMaxHz / kMinHz, t);
    }

    static constexpr double kMinHz = 20.0, kMaxHz = 20000.0;

private:
    std::array<juce::uint8, (size_t) (kMaxColumns + kSlack) * (size_t) kRows * (size_t) kBytesPerCell> data {};
    std::atomic<long long> writePos { 0 };
    // Los escribe `configure()` desde el worker y los lee la UI (ver la nota de configure).
    std::atomic<int>    capacityCols { kHistoryOptions[1] * 47 };
    std::atomic<int>    physical     { kHistoryOptions[1] * 47 + kSlack };
    std::atomic<int>    historySec   { kHistoryOptions[1] };
    std::atomic<double> colsPerSec   { 47.0 };
    // Si alguno de estos necesitara un mutex por debajo, `configure()` podría bloquear al worker: en este
    // plugin son 64 bits en arm64/x86-64 y se compilan a una instrucción, pero que lo diga el compilador.
    static_assert (std::atomic<int>::is_always_lock_free && std::atomic<double>::is_always_lock_free
                       && std::atomic<long long>::is_always_lock_free,
                   "SpectrogramRing necesita atomics lock-free: configure() corre en el worker");
};

// El sonograma de NIVEL (lente 4): un byte por celda, el dB sobre [-rango, 0].
using SpectrogramRing = SpectrogramRingT<1>;
// El espectrograma ESTÉREO (lente 10): dos bytes por celda, [coherencia, energía] en ese orden.
using StereoSpectrogramRing = SpectrogramRingT<2>;
}
