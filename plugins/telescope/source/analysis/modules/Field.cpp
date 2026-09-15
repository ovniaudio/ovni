#include "analysis/modules/Field.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
constexpr int kGridCells  = FieldFrame::kRows * FieldFrame::kDir;
constexpr int kTrailCells = FieldFrame::kTrailRows * FieldFrame::kTrailDir;
}

//======================================================================================== ciclo de vida
void Field::setDecayIndex (int i) noexcept
{
    const int v = std::clamp (i, 0, kNumDecayOptions - 1);
    if (v == decayIdx) return;
    decayIdx = v;
    updateDecay();   // la geometría no cambia: no hace falta rearmar la tabla bin→fila
}

// τ y la tasa de emisión mandan; la grilla NO se limpia (un promedio exponencial sabe cambiar de
// constante en caliente, y limpiarla borraría el segundo de música que el usuario está mirando).
void Field::updateDecay() noexcept
{
    const double tau = (double) kDecaySecOptions[decayIdx];
    const double dt  = lastEmitRate > 0.0 ? 1.0 / lastEmitRate : 0.0;
    decayPerFrame = (float) std::exp (-dt / std::max (1.0e-6, tau));
    out.decaySec  = kDecaySecOptions[decayIdx];
}

void Field::reset()
{
    out = FieldFrame{};
    out.decaySec = kDecaySecOptions[decayIdx];
    emitted = 0;
    // La geometría (bins, tasa) sigue valiendo: sólo se recalcula si cambia de verdad.
}

// (Re)calcula la tabla bin→fila y el factor de decaimiento para la geometría que acaba de llegar.
void Field::rebuild (const StereoBands::FieldFrameInfo& info)
{
    lastBins     = info.numBins;
    lastBinHz    = info.binHz;
    lastEmitRate = info.emitRate;
    dirty = false;

    binRow.assign ((size_t) std::max (0, lastBins), -1);
    insideBins = 0;
    // El bin 0 es DC: no tiene frecuencia audible ni paneo con sentido, y queda en −1 (descartado).
    for (int k = 1; k < lastBins; ++k)
    {
        const int row = FieldFrame::rowForHz ((double) k * lastBinHz);
        binRow[(size_t) k] = row;
        if (row >= 0) ++insideBins;
    }

    updateDecay();
}

//======================================================================================== la estela
// `trail[0]` es SIEMPRE la más vieja. Cuando está llena se corre una posición a la izquierda: son 7 × 1 536
// floats (43 KB) por frame a ≤ 60 Hz, o sea 2.6 MB/s — nada al lado de tener que llevar un índice de anillo
// dentro de un POD que después la lente tendría que volver a desenrollar para dibujar en orden.
void Field::pushTrail()
{
    float* dst = &out.trail[0][0][0];

    if (out.trailCount >= FieldFrame::kTrail)
    {
        std::move (dst + kTrailCells, dst + FieldFrame::kTrail * kTrailCells, dst);
        out.trailCount = FieldFrame::kTrail - 1;
    }

    float* slot = dst + (size_t) out.trailCount * (size_t) kTrailCells;
    // Decimación 2×2 por MÁXIMO: promediar borraría justo los picos, que es lo único que se ve al fondo.
    for (int r = 0; r < FieldFrame::kTrailRows; ++r)
        for (int c = 0; c < FieldFrame::kTrailDir; ++c)
        {
            const float a = std::max (out.grid[2 * r][2 * c],     out.grid[2 * r][2 * c + 1]);
            const float b = std::max (out.grid[2 * r + 1][2 * c], out.grid[2 * r + 1][2 * c + 1]);
            slot[(size_t) (r * FieldFrame::kTrailDir + c)] = std::max (a, b);
        }

    ++out.trailCount;
}

//======================================================================================== un frame
void Field::stereoBandsFrameEmitted (const StereoBands::FieldFrameInfo& info)
{
    if (info.pan == nullptr || info.energy == nullptr || info.numBins <= 0 || info.emitRate <= 0.0
        || info.binHz <= 0.0)
        return;

    if (dirty || info.numBins != lastBins || info.binHz != lastBinHz || info.emitRate != lastEmitRate)
        rebuild (info);

    // La grilla que se va: la estela guarda la HISTORIA, nunca el frame que se está por acumular (si no,
    // el dibujo de adelante y la primera lámina del fondo serían la misma cosa dos veces).
    if (emitted > 0) pushTrail();

    // ---- decaimiento exponencial ----
    float* g = &out.grid[0][0];
    for (int i = 0; i < kGridCells; ++i) g[i] *= decayPerFrame;

    // ---- cada bin suma su energía en su celda, repartida entre las DOS columnas vecinas ----
    for (int k = 1; k < lastBins; ++k)
    {
        const int row = binRow[(size_t) k];
        if (row < 0) continue;                       // fuera de 20 Hz – 20 kHz: se descarta

        const double e = info.energy[k];
        if (! (e > kTinyEnergy)) continue;           // sin energía no hay dirección que dibujar

        const double pos = FieldFrame::columnPos ((double) info.pan[k]);
        int c0 = (int) pos;
        c0 = std::clamp (c0, 0, FieldFrame::kDir - 2);
        const double t = std::clamp (pos - (double) c0, 0.0, 1.0);

        out.grid[row][c0]     += (float) (e * (1.0 - t));
        out.grid[row][c0 + 1] += (float) (e * t);
    }

    float peak = 0.0f;
    for (int i = 0; i < kGridCells; ++i) peak = std::max (peak, g[i]);
    out.maxCell   = peak;
    out.decaySec  = kDecaySecOptions[decayIdx];
    out.frameIndex = emitted++;
}
}
