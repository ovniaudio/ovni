#include "analysis/modules/StereoBands.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
constexpr double kTinyPower = 1.0e-20;   // -200 dB, el piso del frame

// Media celda de una fila del espectrograma, en factor de frecuencia (la misma del módulo Spectrum: las
// 512 filas cubren 3 décadas, así que cada una mide 10^(3/511) y su celda va de f/√eso a f·√eso).
const double kRowCellHalf = std::pow (10.0, 1.5 / (double) (StereoSpectrogramRing::kRows - 1));

constexpr double kSixthOctDown = 0.8908987181403393;   // 2^(-1/6)
constexpr double kSixthOctUp   = 1.1224620483093730;   // 2^(+1/6)

float powerToDb (double p) noexcept
{
    return p <= kTinyPower ? SpectrumFrame::kFloorDb
                           : (float) std::max ((double) SpectrumFrame::kFloorDb, 10.0 * std::log10 (p));
}

// 10·log10 con piso y techo (copia deliberada de la de Stereo.cpp: los dos módulos publican el MISMO
// número con las mismas reglas, y que se lean uno al lado del otro es parte del punto).
float ratioDb (double num, double den, float floorDb, float ceilDb) noexcept
{
    if (den <= StereoBands::kTinyEnergy) return num <= StereoBands::kTinyEnergy ? 0.0f : ceilDb;
    if (num <= StereoBands::kTinyEnergy) return floorDb;
    const auto db = (float) (10.0 * std::log10 (num / den));
    return std::clamp (db, floorDb, ceilDb);
}
}

//======================================================================================== mapeos byte
juce::uint8 StereoBands::coherenceToByte (double c) noexcept
{
    // 0 = −1 (fuera de fase) · 128 = 0 (decorrelacionado o sin definir) · 255 = +1 (mono).
    return (juce::uint8) std::clamp ((int) std::lround ((std::clamp (c, -1.0, 1.0) + 1.0) * 127.5), 0, 255);
}

juce::uint8 StereoBands::dbToByte (double db, double rangeDb) noexcept
{
    if (rangeDb <= 0.0) return 0;
    return (juce::uint8) std::clamp ((int) std::lround (255.0 * (db + rangeDb) / rangeDb), 0, 255);
}

//======================================================================================== ciclo de vida
void StereoBands::setWindowIndex (int i) noexcept
{
    const int w = std::clamp (i, 0, kNumWindowOptions - 1);
    if (w == windowIdx) return;
    windowIdx = w;
    dirty = true;    // el anillo cambia de largo: se rearma (y por lo tanto se limpia) en el próximo frame
}

void StereoBands::setSpectrogramRing (StereoSpectrogramRing* r) noexcept
{
    spectrogram = r;
    if (spectrogram != nullptr && emitRate > 0.0) spectrogram->configure (historySec, emitRate);
}

void StereoBands::setHistorySeconds (int seconds) noexcept
{
    if (seconds <= 0 || seconds == historySec) return;
    historySec = seconds;
    if (spectrogram != nullptr && emitRate > 0.0) spectrogram->configure (historySec, emitRate);
}

void StereoBands::reset()
{
    std::fill (total.begin(), total.end(), 0.0);
    std::fill (cur.begin(), cur.end(), 0.0);
    std::fill (ring.begin(), ring.end(), 0.0f);
    std::fill (inst.begin(), inst.end(), 0.0);
    writeGroup    = 0;
    groupsFilled  = 0;
    framesInGroup = 0;
    emitted       = 0;

    const double keepSr = out.sr;
    current = Result{};
    out = StereoBandsFrame{};
    out.numBins = bins;
    out.sr      = keepSr;
    out.binHz   = binHz;

    // Columna en "silencio con coherencia sin definir": energía 0 y coherencia en el centro (128).
    col.fill (0);
    for (int row = 0; row < kRows; ++row) col[(size_t) (2 * row)] = coherenceToByte (0.0);
    if (spectrogram != nullptr) spectrogram->clear();

    // Los bins/tasa siguen valiendo: no se marca `dirty` (rearmar de nuevo sería tirar el mismo trabajo).
    for (int b = 0; b < kNumBands; ++b) current.binsInBand[b] = std::max (0, bandK1[b] - bandK0[b]);

    if (fieldSink != nullptr) fieldSink->stereoBandsReset();   // ===== 53 =====
}

// (Re)dimensiona todo para la geometría del frame que acaba de llegar. Se llama cuando cambian los bins,
// la tasa de frames o la ventana: las tres cambian el largo del anillo, y sumar frames medidos con dos
// geometrías distintas no daría ninguna de las dos.
void StereoBands::rebuild (const Spectrum::FrameInfo& info)
{
    bins     = info.numBins;
    rate     = info.frameRate;
    emitRate = info.emitRate;
    binHz    = info.binHz;
    dirty    = false;

    windowTarget = std::max (1, (int) std::lround ((double) kWindowSecOptions[windowIdx] * rate));

    // Agrupación: sólo si el anillo exacto no entra en el tope de memoria (ver el header).
    const long long want = (long long) windowTarget * (long long) bins;
    group      = (int) std::max (1LL, (want + (long long) kMaxBinEntries - 1) / (long long) kMaxBinEntries);
    ringGroups = std::max (1, windowTarget / group);

    total.assign ((size_t) (3 * bins), 0.0);
    cur.assign   ((size_t) (3 * bins), 0.0);
    ring.assign  ((size_t) ringGroups * (size_t) (3 * bins), 0.0f);
    inst.assign  ((size_t) bins, 0.0);
    col.fill (0);

    // Los bins de cada banda de ⅓ de octava: [fc·2^(-1/6), fc·2^(+1/6)), igual que el módulo Spectrum.
    for (int b = 0; b < kNumBands; ++b)
    {
        const double lo = kThirdOctaveHz[b] * kSixthOctDown;
        const double hi = kThirdOctaveHz[b] * kSixthOctUp;
        bandK0[b] = std::max (0, (int) std::ceil (lo / binHz));
        bandK1[b] = std::min (bins, (int) std::ceil (hi / binHz));
    }

    out = StereoBandsFrame{};
    out.numBins = bins;
    out.sr      = info.sr;
    out.binHz   = binHz;

    if (spectrogram != nullptr && emitRate > 0.0) spectrogram->configure (historySec, emitRate);

    writeGroup    = 0;
    groupsFilled  = 0;
    framesInGroup = 0;
    emitted       = 0;
    current       = Result{};
    for (int b = 0; b < kNumBands; ++b) current.binsInBand[b] = std::max (0, bandK1[b] - bandK0[b]);
    for (int row = 0; row < kRows; ++row) col[(size_t) (2 * row)] = coherenceToByte (0.0);

    // ===== 53: rearmar NO pasa por reset(), así que el aviso al campo va también acá =====
    if (fieldSink != nullptr) fieldSink->stereoBandsReset();
}

//======================================================================================== entrada
void StereoBands::spectrumFrameComputed (const Spectrum::FrameInfo& info)
{
    if (info.left == nullptr || info.right == nullptr || info.numBins <= 0 || info.frameRate <= 0.0) return;

    if (dirty || info.numBins != bins || info.frameRate != rate || info.emitRate != emitRate
        || info.binHz != binHz)
        rebuild (info);

    // ---- las tres sumas del frame, por bin ----
    const double n = info.powerNorm;
    for (int k = 0; k < bins; ++k)
    {
        const double lre = (double) info.left[2 * k],  lim = (double) info.left[2 * k + 1];
        const double rre = (double) info.right[2 * k], rim = (double) info.right[2 * k + 1];

        const double ll = (lre * lre + lim * lim) * n;
        const double rr = (rre * rre + rim * rim) * n;
        const double lr = (lre * rre + lim * rim) * n;    // Re(L · conj(R))

        const auto i = (size_t) (3 * k);
        cur[i] += ll;
        cur[i + 1] += rr;
        cur[i + 2] += lr;
        inst[(size_t) k] = ll + rr;                        // energía del frame (el brillo del sonograma)
    }

    // ---- ventana deslizante: el grupo que se completa entra al anillo y el más viejo sale ----
    if (++framesInGroup >= group)
    {
        const auto slot = (size_t) (writeGroup % (long long) ringGroups) * (size_t) (3 * bins);
        if (groupsFilled >= ringGroups)
            for (int i = 0; i < 3 * bins; ++i) total[(size_t) i] -= (double) ring[slot + (size_t) i];

        for (int i = 0; i < 3 * bins; ++i)
        {
            // Se guarda el float Y se suma ESE MISMO float: la conversión float→double es exacta, así que
            // la resta de arriba saca de `total` exactamente el valor que había entrado. Lo que IEEE-754
            // NO garantiza es que (a + x) − x devuelva a: cada suma y cada resta redondean al double más
            // cercano, o sea hasta 2^-53 ≈ 1.1e-16 relativo POR OPERACIÓN. Una hora de sesión a 375
            // frames por segundo son ~1.4 millones de operaciones por acumulador: la deriva queda ACOTADA
            // por ~1.5e-10 relativo en el peor caso (y anda por 1e-13 en la práctica, porque los
            // redondeos no se alinean todos para el mismo lado). No se re-suma nunca: eso está diez
            // órdenes de magnitud por debajo del ruido de la propia medición, y rehacer 3·bins
            // acumuladores por frame costaría muchísimo más que el error que arreglaría.
            const float v = (float) cur[(size_t) i];
            ring[slot + (size_t) i] = v;
            total[(size_t) i] += (double) v;
        }

        ++writeGroup;
        groupsFilled = std::min (groupsFilled + 1, ringGroups);
        framesInGroup = 0;
        std::fill (cur.begin(), cur.end(), 0.0);
    }

    updateResult();

    if (! info.emitted) return;

    buildFrame (info);
    buildColumn (info);
    out.frameIndex = emitted++;
    if (spectrogram != nullptr) spectrogram->push (col.data());

    // ===== 53: el módulo Field come de acá (ver FieldSink en el header) =====
    if (fieldSink != nullptr)
    {
        FieldFrameInfo fi;
        fi.pan        = out.pan;
        fi.energy     = inst.data();
        fi.numBins    = bins;
        fi.sr         = info.sr;
        fi.binHz      = binHz;
        fi.emitRate   = emitRate;
        fi.frameIndex = out.frameIndex;
        fieldSink->stereoBandsFrameEmitted (fi);
    }
}

//======================================================================================== los cuatro números
// Los mismos casos borde que Stereo, y por los mismos motivos:
//   · sin energía → todo en 0 (no es "mono perfecto": es que no hay nada que medir)
//   · un canal mudo (ΣLL·ΣRR ≈ 0) → corr 0: no hay correlación DEFINIDA
//   · ΣMM ≈ 0 (L = −R) → width al tope y monoLoss al piso
void StereoBands::finish (const Sums& s, int frames, float& corr, float& width, float& balanceDb,
                          float& monoLossDb, float& energyDb) noexcept
{
    // |(L±R)/2|² = (|L|² + |R|² ± 2·Re(L·R*)) / 4 — no hace falta guardar ΣMM ni ΣSS.
    const double mm = 0.25 * (s.ll + s.rr + 2.0 * s.lr);
    const double ss = 0.25 * (s.ll + s.rr - 2.0 * s.lr);

    corr = width = balanceDb = monoLossDb = 0.0f;
    energyDb = SpectrumFrame::kFloorDb;

    if ((s.ll + s.rr) <= kTinyEnergy) return;

    energyDb = powerToDb ((s.ll + s.rr) / (double) std::max (1, frames));

    const double denom = s.ll * s.rr;
    corr = denom > kTinyEnergy ? std::clamp ((float) (s.lr / std::sqrt (denom)), -1.0f, 1.0f) : 0.0f;
    width = mm > kTinyEnergy ? std::min (kWidthMax, (float) std::sqrt (std::max (0.0, ss) / mm)) : kWidthMax;
    balanceDb  = ratioDb (s.rr, s.ll, kDbFloor, kDbCeil);
    monoLossDb = ratioDb (mm, 0.5 * (s.ll + s.rr), kDbFloor, 0.0f);
}

void StereoBands::updateResult()
{
    const int frames = groupsFilled * group + framesInGroup;
    current.windowFrames  = frames;
    current.windowSeconds = rate > 0.0 ? (float) ((double) frames / rate) : 0.0f;
    current.valid = 0;

    Sums wide { 0.0, 0.0, 0.0 };
    for (int k = 0; k < bins; ++k)
    {
        const auto s = sumsAt (k);
        wide.ll += s.ll; wide.rr += s.rr; wide.lr += s.lr;
    }
    float ignored = 0.0f;
    finish (wide, frames, current.wideCorr, current.wideWidth, current.wideBalanceDb,
            current.wideMonoLossDb, ignored);

    for (int b = 0; b < kNumBands; ++b)
    {
        current.binsInBand[b] = std::max (0, bandK1[b] - bandK0[b]);
        if (current.binsInBand[b] == 0)
        {
            // Banda sin ningún bin: a esta resolución NO SE PUEDE MEDIR. No es cero correlación.
            current.corr[b] = current.width[b] = current.balanceDb[b] = current.monoLossDb[b] = 0.0f;
            current.energyDb[b] = SpectrumFrame::kFloorDb;
            continue;
        }

        Sums s { 0.0, 0.0, 0.0 };
        for (int k = bandK0[b]; k < bandK1[b]; ++k)
        {
            const auto t = sumsAt (k);
            s.ll += t.ll; s.rr += t.rr; s.lr += t.lr;
        }
        finish (s, frames, current.corr[b], current.width[b], current.balanceDb[b],
                current.monoLossDb[b], current.energyDb[b]);
        if ((s.ll + s.rr) > kTinyEnergy) ++current.valid;
    }
}

//======================================================================================== por bin
void StereoBands::buildFrame (const Spectrum::FrameInfo& info)
{
    out.numBins       = bins;
    out.sr            = info.sr;
    out.binHz         = binHz;
    out.windowSeconds = current.windowSeconds;

    for (int k = 0; k < bins; ++k)
    {
        const auto s = sumsAt (k);
        const double denom = s.ll * s.rr;
        // 0 POR DEFINICIÓN cuando un canal no tiene energía en este bin: no hay dos fases que comparar.
        out.coh[k] = denom > kTinyEnergy
                       ? std::clamp ((float) (s.lr / std::sqrt (denom)), -1.0f, 1.0f)
                       : 0.0f;
        out.energyDb[k] = powerToDb (inst[(size_t) k]);   // INSTANTÁNEA: es el brillo de un espectrograma

        // ===== 53: el PANEO POR ENERGÍA, de las mismas sumas (ver StereoBandsFrame.h) =====
        // 0 POR DEFINICIÓN sin energía: no es "centrado", es que no hay nada que ubicar.
        const double e = s.ll + s.rr;
        out.pan[k] = e > kTinyEnergy ? std::clamp ((float) ((s.rr - s.ll) / e), -1.0f, 1.0f) : 0.0f;
    }
}

// La columna del espectrograma estéreo: 512 filas × 2 bytes, [coherencia, energía].
//
// La energía toma el MÁXIMO de los bins de la celda (igual que el sonograma del 50: promediar borraría
// los picos angostos). La coherencia, en cambio, se calcula con las SUMAS de la celda —
// Σ LR / √(ΣLL · ΣRR) sobre sus bins — que es la misma fórmula de una banda y pesa cada bin por su
// energía sola. Tomar el máximo de coherencias haría que un bin sin nada decidiera el color de la fila.
void StereoBands::buildColumn (const Spectrum::FrameInfo& info)
{
    const double range = (double) info.rangeDb;

    for (int row = 0; row < kRows; ++row)
    {
        const double f  = StereoSpectrogramRing::rowFrequency (row);
        const double lo = f / kRowCellHalf, hi = f * kRowCellHalf;

        const int k0 = std::max (0, (int) std::ceil (lo / binHz));
        const int k1 = std::min (bins, (int) std::ceil (hi / binHz));

        double coh = 0.0, db = (double) SpectrumFrame::kFloorDb;

        if (k1 > k0)
        {
            double e = 0.0;
            Sums s { 0.0, 0.0, 0.0 };
            for (int k = k0; k < k1; ++k)
            {
                e = std::max (e, inst[(size_t) k]);
                const auto t = sumsAt (k);
                s.ll += t.ll; s.rr += t.rr; s.lr += t.lr;
            }
            const double denom = s.ll * s.rr;
            coh = denom > kTinyEnergy ? std::clamp (s.lr / std::sqrt (denom), -1.0, 1.0) : 0.0;
            db  = (double) powerToDb (e);
        }
        else
        {
            // Celda sin ningún bin (los graves con FFT chica): se interpola entre los dos vecinos, igual
            // que el sonograma del 50. La coherencia se interpola linealmente porque es una magnitud
            // suave en [−1, 1]; el nivel, en dB.
            const double pos = f / binHz;
            const int    ka  = std::clamp ((int) std::floor (pos), 0, bins - 1);
            const int    kb  = std::clamp (ka + 1, 0, bins - 1);
            const double t   = std::clamp (pos - (double) ka, 0.0, 1.0);

            const double dbA = (double) powerToDb (inst[(size_t) ka]);
            const double dbB = (double) powerToDb (inst[(size_t) kb]);
            db = dbA + t * (dbB - dbA);

            const auto sa = sumsAt (ka);
            const auto sb = sumsAt (kb);
            const double da = sa.ll * sa.rr, dbn = sb.ll * sb.rr;
            const double ca = da > kTinyEnergy ? std::clamp (sa.lr / std::sqrt (da), -1.0, 1.0) : 0.0;
            const double cb = dbn > kTinyEnergy ? std::clamp (sb.lr / std::sqrt (dbn), -1.0, 1.0) : 0.0;
            coh = ca + t * (cb - ca);
        }

        col[(size_t) (2 * row)]     = coherenceToByte (coh);
        col[(size_t) (2 * row + 1)] = dbToByte (db, range);
    }
}
}
