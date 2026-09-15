#include "analysis/modules/Stereo.h"
#include <algorithm>
#include <cmath>

namespace telescope
{
namespace
{
// 10·log10 con piso y techo: la UI dibuja estos números, así que -inf no es una opción.
float ratioDb (double num, double den, float floorDb, float ceilDb) noexcept
{
    if (den <= Stereo::kTinyEnergy) return num <= Stereo::kTinyEnergy ? 0.0f : ceilDb;
    if (num <= Stereo::kTinyEnergy) return floorDb;
    const auto db = (float) (10.0 * std::log10 (num / den));
    return std::clamp (db, floorDb, ceilDb);
}
}

void Stereo::prepare (double sampleRate)
{
    sr  = sampleRate > 0.0 ? sampleRate : 48000.0;
    hop = std::max (1, (int) std::llround (sr / 10.0));   // 100 ms

    // 40 ms de onda para el osciloscopio, y 20 ms de búsqueda de trigger dentro de esos 40.
    oscWindow     = std::min ({ ScopeFrame::kMaxOsc, hop, (int) std::llround (0.040 * sr) });
    triggerWindow = std::min (oscWindow, (int) std::llround (0.020 * sr));

    hopL.assign ((size_t) hop, 0.0f);
    hopR.assign ((size_t) hop, 0.0f);

    reset();
}

void Stereo::reset()
{
    acc  = {};
    fill = 0;
    ring.fill ({});
    ringWrite      = 0;
    hopsCompleted  = 0;
    current        = Result{};
    currentScope   = ScopeFrame{};
    std::fill (hopL.begin(), hopL.end(), 0.0f);
    std::fill (hopR.begin(), hopR.end(), 0.0f);
}

void Stereo::setWindowMs (int ms) noexcept
{
    const int hops = std::clamp ((int) std::llround ((double) ms / 100.0), 1, kMaxWindowHops);
    windowHops = hops;
}

void Stereo::process (const float* L, const float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const double l = (double) L[i];
        const double r = (double) R[i];
        const double m = 0.5 * (l + r);
        const double s = 0.5 * (l - r);

        acc.ll += l * l;
        acc.rr += r * r;
        acc.lr += l * r;
        acc.mm += m * m;
        acc.ss += s * s;

        hopL[(size_t) fill] = L[i];
        hopR[(size_t) fill] = R[i];

        if (++fill == hop)
            finishHop();
    }
}

void Stereo::finishHop()
{
    ring[(size_t) (ringWrite % kMaxWindowHops)] = acc;
    ++ringWrite;
    ++hopsCompleted;
    acc  = {};
    fill = 0;

    // ---- ventana deslizante: los últimos `windowHops` hops, sumados del más VIEJO al más nuevo ----
    // El orden fijo es lo que hace la suma reproducible al bit corrida tras corrida.
    const int n = std::min (windowHops, std::min ((int) hopsCompleted, kMaxWindowHops));
    HopSums w;
    for (int i = 0; i < n; ++i)
    {
        const auto& h = ring[(size_t) ((ringWrite - n + i) % kMaxWindowHops)];
        w.ll += h.ll; w.rr += h.rr; w.lr += h.lr; w.mm += h.mm; w.ss += h.ss;
    }

    Result out;
    out.windowSeconds = (float) ((double) n * (double) hop / sr);
    out.hasSignal     = (w.ll + w.rr) > kTinyEnergy;

    if (out.hasSignal)
    {
        // CORR — sin producto de energías no hay correlación DEFINIDA (un canal mudo): se publica 0, que
        // es lo que la lente rotula "sin correlación", no "descorrelacionado".
        const double denom = w.ll * w.rr;
        out.corr = denom > kTinyEnergy
                     ? std::clamp ((float) (w.lr / std::sqrt (denom)), -1.0f, 1.0f)
                     : 0.0f;

        // WIDTH — con ΣMM ≈ 0 (L = -R) el cociente se va a infinito: se publica el tope.
        out.width = w.mm > kTinyEnergy
                      ? std::min (kWidthMax, (float) std::sqrt (w.ss / w.mm))
                      : kWidthMax;

        out.balanceDb  = ratioDb (w.rr, w.ll, kDbFloor, kDbCeil);
        out.monoLossDb = ratioDb (w.mm, 0.5 * (w.ll + w.rr), kDbFloor, 0.0f);
    }

    current = out;
    buildScope();
}

// Los buffers que dibuja SCOPE. Se arman una vez por hop (10 Hz), no por frame de video.
void Stereo::buildScope()
{
    ScopeFrame f;

    // ---- goniómetro: pares (L,R) del hop. Si el hop tiene más de kMaxXy, decimación UNIFORME sobre el
    // hop entero (no los últimos 2 048: la nube tiene que representar los 100 ms, no su cola).
    f.xyCount = std::min (ScopeFrame::kMaxXy, hop);
    for (int i = 0; i < f.xyCount; ++i)
    {
        const auto src = (size_t) ((long long) i * (long long) hop / (long long) f.xyCount);
        f.xyL[(size_t) i] = hopL[src];
        f.xyR[(size_t) i] = hopR[src];
    }

    // ---- osciloscopio: los ÚLTIMOS 40 ms del hop (lo más nuevo es lo que el ojo espera ver) ----
    f.oscCount = oscWindow;
    const int start = hop - f.oscCount;
    for (int i = 0; i < f.oscCount; ++i)
    {
        const float l = hopL[(size_t) (start + i)];
        const float r = hopR[(size_t) (start + i)];
        f.oscL[(size_t) i] = l;
        f.oscR[(size_t) i] = r;
        f.oscM[(size_t) i] = 0.5f * (l + r);
    }

    // ---- trigger: primer cruce por cero ASCENDENTE de M en los primeros 20 ms ----
    f.trigger = -1;
    for (int i = 1; i < triggerWindow; ++i)
        if (f.oscM[(size_t) i - 1] < 0.0f && f.oscM[(size_t) i] >= 0.0f) { f.trigger = i; break; }

    buildHemisphere (f);   // 56

    currentScope = f;
}

// ========================================================================================================
// ===== 56: HEMISFERIO =====
//
// Por cada muestra del hop se calcula HACIA DÓNDE apunta el vector (L, R) y CUÁNTO mide, y se queda el
// máximo por grado. Dos decisiones que importan:
//
//   · SOBRE TODAS LAS MUESTRAS DEL HOP, no sobre los 2 048 decimados del goniómetro. La nube del
//     Lissajous se puede decimar porque son puntos sueltos que el ojo promedia; una ENVOLVENTE no: si la
//     decimación se saltea la muestra que tocó el borde, la envolvente miente hacia abajo justo en el
//     transitorio que uno está buscando. Es el mismo criterio del máximo por celda del sonograma.
//
//   · LAS MUESTRAS SIN ENERGÍA NO TIENEN DIRECCIÓN y se saltean. En el cruce por cero de una señal mono,
//     (L, R) = (0, 0) y atan2 devuelve 0 — o sea "izquierda", que es exactamente lo contrario de lo que
//     está pasando. Contar esas muestras pintaría un lóbulo espurio en 0° en CUALQUIER señal.
//
// El nivel es 20·log10 √(L² + R²): el RADIO del vector, no el nivel de un canal. Para una mono a escala
// completa da +3.01 dB, que es la verdad geométrica de ese vector (L y R suman en cuadratura) y es lo que
// hace que la escala del dibujo sea la misma para mono y para estéreo.
void Stereo::buildHemisphere (ScopeFrame& f) const
{
    for (int b = 0; b < ScopeFrame::kHemiBins; ++b)
        f.envelope[(size_t) b] = ScopeFrame::kHemiFloorDb;

    f.envelopePeakDb = ScopeFrame::kHemiFloorDb;

    // El piso en ENERGÍA (L² + R²) que corresponde al piso en dB: por debajo no hay dirección definida.
    const double floorEnergy = std::pow (10.0, (double) ScopeFrame::kHemiFloorDb / 10.0);

    for (int i = 0; i < hop; ++i)
    {
        const double l = (double) hopL[(size_t) i];
        const double r = (double) hopR[(size_t) i];
        const double e = l * l + r * r;

        if (e <= floorEnergy || e <= kTinyEnergy)
            continue;

        // θ = 90° + 2·atan2 (R − L, R + L). Ver ScopeFrame.h: mono arriba, sólo L en 0°, sólo R en 180°,
        // y lo que está fuera de fase abajo de la base.
        const double psi = std::atan2 (r - l, r + l);                    // −45° = L · 0° = mono · +45° = R
        double deg = 90.0 + 2.0 * psi * 180.0 / 3.14159265358979323846;
        deg = std::fmod (deg, 360.0);
        if (deg < 0.0) deg += 360.0;

        int bin = (int) deg;                                             // truncar: [0°,1°) → bin 0
        if (bin < 0) bin = 0;
        if (bin >= ScopeFrame::kHemiBins) bin = ScopeFrame::kHemiBins - 1;

        const auto db = (float) (10.0 * std::log10 (e));                 // = 20·log10 √(L²+R²)
        if (db > f.envelope[(size_t) bin]) f.envelope[(size_t) bin] = db;
        if (db > f.envelopePeakDb)         f.envelopePeakDb = db;
    }
}
}
