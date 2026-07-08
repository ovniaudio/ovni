#include "engines/movement/Trajectory.h"
#include <cmath>
#include <algorithm>

namespace ovni::engines {

static constexpr double kTwoPi      = 6.283185307179586;
static constexpr double kMaxElevRad = 1.0471975512; // 60°

void Trajectory::prepare (double sr)
{
    sampleRate = (sr > 0.0 ? sr : 48000.0);
    reset();
}

void Trajectory::reset()
{
    phase       = 0.0;
    spiralPhase = 0.0;
    jitterCur   = 0.0f;
    jitterCur2  = 0.0f;
    rngState    = 0x9E3779B9u;
}

float Trajectory::nextWhite() noexcept
{
    // LCG determinista -> [-1, 1]
    rngState = rngState * 1664525u + 1013904223u;
    return (float) ((double) rngState / 4294967295.0 * 2.0 - 1.0);
}

static double quartersPerRev (int rate, double timeSigNum) noexcept
{
    switch (rate)
    {
        case Quarter: return 1.0;
        case Half:    return 2.0;
        case Bar:     return (timeSigNum > 0.0 ? timeSigNum : 4.0);
        default:      return 4.0; // Free no usa esto
    }
}

SpatialTarget Trajectory::advance (int numSamples, const TransportInfo& t)
{
    if (params.rate == Fixed)
    {
        double az = (double) params.fixedAzRad;
        az -= kTwoPi * std::floor (az / kTwoPi);   // wrap a [0, 2π)
        SpatialTarget out;
        out.azimuth   = (float) az;                                   // quieto, sin fase ni caos
        out.elevation = (float) ((double) params.height * kMaxElevRad);
        out.distance  = params.radius01;   // Fijo: sin modulación Doppler (no hay v_radial)
        return out;
    }

    const double dt  = (double) numSamples / sampleRate;
    const double qpr = quartersPerRev (params.rate, t.timeSigNum);
    const bool   synced = (params.rate != Free) && t.isPlaying;

    // Caos OVNI: dos ruidos suavizados deterministas (1 = velocidad errática, 2 = bamboleo).
    if (params.chaos01 > 0.0001f)
    {
        const float k = (float) std::min (1.0, dt * 5.0);
        jitterCur  += (nextWhite() - jitterCur)  * k;
        jitterCur2 += (nextWhite() - jitterCur2) * k;
    }

    if (synced)
    {
        // Fase bloqueada a la línea de tiempo (determinista; banca cambios de BPM).
        const double ppqEnd = t.ppqPosition + (t.bpm / 60.0) * dt; // ppq al fin del bloque
        const double revs   = ppqEnd / qpr;
        phase = revs - std::floor (revs);
    }
    else
    {
        // Free-run: velocidad propia (Speed/Hz en modo Free) o derivada del BPM si está parado.
        const double rps = (params.rate == Free) ? (double) params.freeHz
                                                  : (t.bpm / 60.0) / qpr;
        // Caos: la velocidad dispara y frena (darting OVNI).
        const double spd = std::max (0.05, 1.0 + (double) (params.chaos01 * 1.8f * jitterCur));
        phase += rps * spd * dt;
        phase -= std::floor (phase);
    }

    const double sign = (params.dir == CW) ? -1.0 : 1.0;
    const double psi  = sign * phase * kTwoPi; // ángulo paramétrico

    double az;
    if (params.shape == Ellipse)
    {
        // Elipse: modula la velocidad angular (linger en los extremos del eje largo).
        const double ecc = (double) params.spread01 * 0.9; // 0..0.9
        const double b   = 1.0 - ecc;
        az = std::atan2 (b * std::sin (psi), std::cos (psi));
    }
    else if (params.shape == Pendulum)
    {
        // Hamaca FRENTE: el azimut OSCILA ±swing alrededor de 0 (frente) en vez de girar —
        // cruza el frente, frena en los extremos (péndulo natural: velocidad máx en el centro).
        az = (double) shapeTune.pendulumSwingRad * std::sin (psi);
    }
    else if (params.shape == PendulumBack)
    {
        // Hamaca ATRÁS: igual que Pendulum pero alrededor de π (atrás) — cruza por DETRÁS de la
        // cabeza (de un costado al otro pasando por el fondo). El wrap normaliza π±swing a [0,2π).
        az = kTwoPi * 0.5 + (double) shapeTune.pendulumSwingRad * std::sin (psi);
    }
    else
    {
        // Círculo y Espiral: azimut uniforme (la Espiral evoluciona el RADIO, no el ángulo).
        az = psi;
    }

    // Espiral = vórtice: el radio "respira" lento (entra y sale), independiente del fly-by por-vuelta
    // del Doppler. Seno suave -> sin saltos de distancia (sin clicks). Sólo en Spiral.
    double radiusBase = (double) params.radius01;
    if (params.shape == Spiral)
    {
        spiralPhase += (double) shapeTune.spiralRateHz * dt;
        spiralPhase -= std::floor (spiralPhase);
        radiusBase += (double) shapeTune.spiralDepth01 * std::sin (kTwoPi * spiralPhase);
    }

    // Caos: bamboleo del azimut (más dramático a caos alto -> sensación OVNI).
    az += (double) (params.chaos01 * 1.6f * jitterCur2);

    az -= kTwoPi * std::floor (az / kTwoPi); // wrap a [0, 2π)

    SpatialTarget out;
    out.azimuth   = (float) az;
    out.elevation = (float) ((double) params.height * kMaxElevRad);
    // Doppler: excentricidad de distancia. d(az) = radiusBase - A*cos(az): cerca al frente (az=0),
    // lejos atrás (az=π). A = excentricidad máx * skew(perilla). A doppler=0 -> A=0 -> distancia ==
    // radiusBase (Spiral respira; el resto constante).
    const double A    = (double) dopplerTune.maxEcc01 * std::pow ((double) params.doppler01, (double) dopplerTune.curve);
    const double dist = radiusBase - A * std::cos (az);
    out.distance  = (float) std::clamp (dist, 0.0, 1.0);
    return out;
}

} // namespace ovni::engines
