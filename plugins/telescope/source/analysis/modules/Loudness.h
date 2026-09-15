#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <vector>
#include "analysis/modules/KWeighting.h"
#include "analysis/modules/TruePeakFir.h"

// ========================================================================================================
// Loudness — ITU-R BS.1770 / EBU R 128, tal como lo verifican EBU Tech 3341 y 3342.
//
//   · bloques de 400 ms con 75 % de solapamiento (hop 100 ms)
//   · z_i = media cuadrática del canal K-ponderado en el bloque; G_L = G_R = 1
//   · L = -0.691 + 10·log10(Σ G_i·z_i)
//   · MOMENTARY  = ventana de 400 ms         · SHORT-TERM = ventana de 3 s (hop 100 ms)
//   · INTEGRATED = doble compuerta: absoluta -70 LUFS, relativa = (media de los que pasan) - 10 LU
//   · LRA (Tech 3342) sobre los short-term: compuertas -70 y -20 LU, LRA = P95 - P10
//   · TRUE-PEAK   medido ANTES del K-weighting, por el FIR del Anexo 2 (ver TruePeakFir.h)
//
// DATOS DE DYNAMICS (prompt 49) — todos derivados de lo de arriba, sin un segundo camino de medición:
//   · PSR = TP máximo de los últimos 3 s - short-term   (la lectura en vivo, la del Dynameter)
//   · PLR = TP máximo desde el reset - integrado        (AES TD1004, "peak-to-loudness ratio")
//   · histograma de short-term desde el reset: 61 bins de 1 LU centrados en -60…0
//   · eventos de CLIP sobre el umbral en dBTP (ver kClipHoldMs para qué cuenta como "un" evento)
//
// DETERMINISTA: `process` acepta cualquier cantidad de muestras y arma los hops internamente, así que la
// misma señal da los mismos números AL BIT venga en bloques de 1 o de 4096 (lo verifica casa-4). Toda la
// acumulación es en double: el integrado se suma sobre minutos de audio.
//
// MEMORIA: guarda un valor por hop desde el último reset (~2.3 MB por hora entre bloques y short-terms).
// RESET lo vacía. Es el precio de un integrado EXACTO en vez de por histograma con bins de 0.1 dB.
// ========================================================================================================
namespace telescope
{
class Loudness
{
public:
    static constexpr int kHistogramBins = 61;   // -60…0 LUFS, 1 LU por bin

    struct Result
    {
        float momentary       = -300.0f;
        float shortTerm       = -300.0f;
        float integrated      = -300.0f;
        float lra             = 0.0f;
        float truePeakMax     = -300.0f;
        float momentaryMax    = -300.0f;
        float shortTermMax    = -300.0f;
        bool  integratedValid = false;

        // ---- DYNAMICS (prompt 49) ----
        float truePeakHop = -300.0f;   // dBTP máximo del ÚLTIMO hop (para la línea de tiempo)
        float psr         = 0.0f;      // dB; válido sólo si hay short-term
        float plr         = 0.0f;      // dB; válido sólo si hay integrado
        bool  psrValid    = false;
        bool  plrValid    = false;

        std::uint32_t clipEvents    = 0;   // desde el reset
        std::uint32_t clipEventsHop = 0;   // los que EMPEZARON en el último hop (alimenta el ring de 1 Hz)

        // Un contador por bin; el bin i está centrado en (i - 60) LUFS. Los valores fuera de -60…0 caen
        // en el bin del borde, así Σ bins == shortTermHops SIEMPRE (un histograma que pierde muestras
        // en silencio miente sobre cuánto midió).
        std::array<std::uint32_t, kHistogramBins> histogram {};
        long long shortTermHops = 0;   // cuántos short-term válidos entraron al histograma

        // ---- 55: CONTINUA (DC) ----
        //
        // Dos escalas, porque hacen falta las dos y son distintas:
        //   · `dcSumHopL/R` es la SUMA de las muestras del ÚLTIMO hop. La historia por segundo suma diez
        //     de éstas y divide por las muestras del segundo, así que el número sale IGUAL AL BIT en vivo
        //     y en el análisis de archivo (los dos comen hops de 100 ms exactos, en el mismo orden).
        //     Se guarda la suma y no la media a propósito: promediar diez medias ya redondeadas no da lo
        //     mismo que dividir la suma una vez.
        //   · `dcL/R` es la media ACUMULADA desde el reset: es la que viaja en el AnalysisFrame y la que
        //     mira la regla de DC de VERDICT sobre todo el programa.
        //
        // Cuesta una suma por muestra por canal. Es, literalmente, el acumulador más barato del módulo.
        double dcSumHopL = 0.0, dcSumHopR = 0.0;
        float  dcL = 0.0f, dcR = 0.0f;

        // ========================================================================================================
        // ---- prompt 57c ---- APPEND-ONLY. Nada de lo de arriba cambia de nombre, de orden ni de valor.
        //
        // TRUE-PEAK POR CANAL. `process` ya calculaba `tpL` y `tpR` por muestra y se quedaba sólo con el
        // máximo de los dos; acá deja de tirarlos. Es el medidor que LOUDNESS no tenía: «sólo es M–S,
        // también debería poder ser L y R» (Joaquín, 12-sep). Mismo FIR del Anexo 2, misma señal cruda,
        // mismo hop — lo único que cambia es que el máximo se guarda por canal además de junto.
        float truePeakHopL = -300.0f, truePeakHopR = -300.0f;   // dBTP del último hop, por canal
        float truePeakMaxL = -300.0f, truePeakMaxR = -300.0f;   // dBTP desde el reset, por canal

        // LO PARCIAL, para que el medidor no tenga que esperar. `momentary` existe recién con 4 hops
        // (400 ms) y `shortTerm` con 30 (3 s); hasta entonces el frame lleva el piso y la lente imprime
        // "--.-". Insight y Youlean muestran el valor de la ventana INCOMPLETA mientras se llena, que es
        // una medición honesta de menos audio, no una medición peor. Estos dos son exactamente la misma
        // media de `msL`/`msR` pero sobre `min (hopsDone, N)` hops: válidos desde el PRIMER hop y, cuando
        // la ventana se llena, iguales AL BIT a los oficiales (lo verifica [ebu]).
        //
        // El INTEGRADO no tiene parcial y no lo va a tener: la compuerta es la compuerta.
        float momentaryPartial = -300.0f;
        float shortTermPartial = -300.0f;
    };

    static constexpr double kOffset      = -0.691;   // la constante de BS.1770
    static constexpr double kAbsGate     = -70.0;    // LUFS
    static constexpr double kRelGateI    = -10.0;    // LU (integrado)
    static constexpr double kRelGateLra  = -20.0;    // LU (LRA, Tech 3342)
    static constexpr float  kSilenceFloor = -300.0f;
    static constexpr int    kMomentaryHops = 4;      // 400 ms
    static constexpr int    kShortTermHops = 30;     // 3 s
    static constexpr int    kPsrHops       = 30;     // 3 s de TP para el PSR

    // --- umbral de "clip" en dBTP (setting de la lente DYNAMICS, prompt 49) ---
    static constexpr float  kDefaultClipThresholdDbtp = -1.0f;
    static constexpr float  kMinClipThresholdDbtp     = -3.0f;
    static constexpr float  kMaxClipThresholdDbtp     =  0.0f;
    // QUÉ CUENTA COMO **UN** EVENTO DE CLIP. Una racha de muestras sobre el umbral, sí — pero un seno de
    // 997 Hz que se pasa durante 50 ms se pasa una vez POR CICLO: cincuenta rachas para lo que cualquier
    // oído (y cualquier medidor serio) llama UN clip. Por eso las rachas separadas por menos de este
    // tiempo por debajo del umbral pertenecen al MISMO evento. 100 ms es el agrupamiento perceptual
    // habitual; con 0 esto degeneraría en la definición literal, que cuenta ciclos, no clips.
    static constexpr float  kClipHoldMs = 100.0f;

    void prepare (double sampleRate);
    void reset();

    // Umbral de clip en dBTP. CAMBIARLO REINICIA EL CONTEO: un contador que mezcla eventos medidos
    // contra dos techos distintos no querría decir nada. Poner el mismo valor no hace nada.
    void  setClipThresholdDbtp (float dbtp);
    float clipThresholdDbtp() const noexcept { return clipThreshold; }

    // Acepta CUALQUIER n. Los hops se arman adentro (ver la nota de determinismo del encabezado).
    void process (const float* L, const float* R, int n);

    Result result() const noexcept { return current; }

    int hopSamples() const noexcept { return hop; }

private:
    void finishHop();
    void recomputeIntegrated();
    void recomputeLra();
    void resetClipCount();

    static double meanOfLast (const std::deque<double>& d, int count) noexcept;
    static float  toLufs (double zSum) noexcept
    {
        return zSum > 0.0 ? (float) (kOffset + 10.0 * std::log10 (zSum)) : kSilenceFloor;
    }

    double sr  = 48000.0;
    int    hop = 4800;

    KWeighting  kwL, kwR;
    TruePeakFir tpL, tpR;

    double accL = 0.0, accR = 0.0;
    // ===== 55: DC. `hopSum*` se vacía en cada hop; `total*` acumula desde el reset. =====
    double dcHopSumL = 0.0, dcHopSumR = 0.0;
    double dcTotalL  = 0.0, dcTotalR  = 0.0;
    long long dcSamples = 0;   // cuántas muestras entraron en la suma de continua de arriba
    int    fill = 0;
    long long hopsDone = 0;

    std::deque<double> msL, msR;      // media cuadrática por hop (los últimos kShortTermHops)
    std::vector<double> blockZ;       // Σ G_i·z_ij de cada bloque de 400 ms (para el integrado)
    std::vector<float>  stValues;     // short-term LUFS (para el LRA)

    // ---- DYNAMICS ----
    float              hopTpLin = 0.0f;      // |y| máximo del hop en curso (lineal, antes del K-weighting)
    float              hopTpLinL = 0.0f, hopTpLinR = 0.0f;   // ídem, por canal (57c)
    std::deque<float>  hopTpDb;              // dBTP por hop, los últimos kPsrHops (3 s) → el máximo es el PSR
    float              clipThreshold = kDefaultClipThresholdDbtp;
    float              clipThresholdLin = 0.0f;
    int                clipHoldSamples = 4800;   // kClipHoldMs a esta SR
    int                belowRun = 0;             // muestras seguidas por DEBAJO del umbral
    std::uint32_t      pendingClipHop = 0;       // eventos abiertos DENTRO del hop en curso

    Result current;
};
}
