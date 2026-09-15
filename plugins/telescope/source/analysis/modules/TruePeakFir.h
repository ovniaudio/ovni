#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// ========================================================================================================
// True-peak — ITU-R BS.1770, Anexo 2 ("Guidelines for accurate measurement of true-peak level").
//
// El pico REAL de la forma de onda continua puede estar ENTRE dos muestras: un medidor de picos de muestra
// no lo ve y el conversor del oyente sí (por eso las plataformas piden techo en dBTP, no en dBFS).
// El Anexo especifica: sobremuestrear ×4 (48 kHz → 192 kHz), filtrar paso-bajos, valor absoluto, 20·log10.
//
// LOS 48 COEFICIENTES DE ABAJO SON LA TABLA LITERAL del Anexo 2 (orden 48, 4 fases × 12 taps), transcrita
// del documento de la ITU (Rec. ITU-R BS.1770-5, Anexo 2, §3, pp. 18-19). No es un diseño propio.
//
// La atenuación de −12.04 dB del diagrama de bloques del Anexo NO se aplica acá: el propio documento dice
// que ese paso existe sólo para dar headroom a la aritmética entera y que "no es necesario si los cálculos
// se hacen en punto flotante" (que es el caso).
//
// Sobremuestreo por sample rate — el objetivo del estándar es llegar a ≥ 192 kHz:
//   < 96 kHz   → 4×  (44.1 → 176.4 · 48 → 192 · 88.2 → 352.8)
//   96–176.4   → 2×  (el documento: "para 96 kHz un sobremuestreo 2× sería suficiente")
//   ≥ 192 kHz  → 1×  (las muestras YA están a la resolución que pide el estándar)
// ========================================================================================================
namespace telescope
{
class TruePeakFir
{
public:
    static constexpr int kTaps   = 12;
    static constexpr int kPhases = 4;

    // Tabla literal — Rec. ITU-R BS.1770-5, Anexo 2, §3 (Phase 0 · Phase 1 · Phase 2 · Phase 3).
    static const double (&table())[kPhases][kTaps]
    {
        static const double t[kPhases][kTaps] = {
            {  0.0017089843750,  0.0109863281250, -0.0196533203125,  0.0332031250000,
              -0.0594482421875,  0.1373291015625,  0.9721679687500, -0.1022949218750,
               0.0476074218750, -0.0266113281250,  0.0148925781250, -0.0083007812500 },
            { -0.0291748046875,  0.0292968750000, -0.0517578125000,  0.0891113281250,
              -0.1665039062500,  0.4650878906250,  0.7797851562500, -0.2003173828125,
               0.1015625000000, -0.0582275390625,  0.0330810546875, -0.0189208984375 },
            { -0.0189208984375,  0.0330810546875, -0.0582275390625,  0.1015625000000,
              -0.2003173828125,  0.7797851562500,  0.4650878906250, -0.1665039062500,
               0.0891113281250, -0.0517578125000,  0.0292968750000, -0.0291748046875 },
            { -0.0083007812500,  0.0148925781250, -0.0266113281250,  0.0476074218750,
              -0.1022949218750,  0.9721679687500,  0.1373291015625, -0.0594482421875,
               0.0332031250000, -0.0196533203125,  0.0109863281250,  0.0017089843750 },
        };
        return t;
    }

    // Cuántas sub-muestras se generan por muestra de entrada a este sample rate (ver el encabezado).
    static int phasesForRate (double fs) noexcept
    {
        if (fs >= 192000.0) return 1;
        if (fs >=  96000.0) return 2;
        return 4;
    }

    void prepare (double fs) noexcept
    {
        activePhases = phasesForRate (fs);
        reset();
    }

    void reset() noexcept { hist.fill (0.0); }

    int phases() const noexcept { return activePhases; }

    // Empuja una muestra y devuelve el MÁXIMO |y| de las sub-muestras que genera. El llamador acumula el
    // máximo corrido; la fase del FIR no importa porque se toma el máximo sobre todas las salidas.
    float processSample (float x) noexcept
    {
        if (activePhases == 1)
            return std::abs (x);   // ≥ 192 kHz: el estándar no pide sobremuestrear

        // Desplazar la historia (hist[0] = la muestra más nueva).
        for (int i = kTaps - 1; i > 0; --i) hist[(size_t) i] = hist[(size_t) i - 1];
        hist[0] = (double) x;

        // 4× usa las 4 fases (t = 0, ¼, ½, ¾); 2× usa la 0 y la 2 (t = 0, ½).
        const int step = kPhases / activePhases;
        double mx = 0.0;
        for (int p = 0; p < kPhases; p += step)
        {
            const double* c = table()[p];
            double y = 0.0;
            for (int i = 0; i < kTaps; ++i) y += c[i] * hist[(size_t) i];
            mx = std::max (mx, std::abs (y));
        }
        return (float) mx;
    }

private:
    std::array<double, kTaps> hist {};
    int activePhases = 4;
};
}
