#pragma once
#include "engines/movement/MovementSource.h"

namespace ovni::engines {

// Cerebro de trayectoria paramétrica del punto sonoro (portado de ÓRBITA OrbitBrain).
// Produce azimut(t) + distancia(t): círculo/elipse/espiral/péndulo, sync al tempo o free-run,
// dirección, caos OVNI y excentricidad de distancia (fly-by Doppler). Desacoplado de APVTS: recibe
// todo por TrajectoryParams (no lee parámetros del host).
enum Shape { Circle = 0, Ellipse = 1, Spiral = 2, Pendulum = 3, PendulumBack = 4 };
enum Rate  { Quarter = 0, Half = 1, Bar = 2, Free = 3, Fixed = 4 };
enum Dir   { CW = 0, CCW = 1 };

struct TrajectoryParams
{
    int   shape      = Ellipse;
    int   rate       = Half;
    float radius01   = 0.60f;  // 0..1   distancia base
    float height     = 0.0f;   // -1..1  elevación (la consume el módulo binaural)
    float spread01   = 0.35f;  // 0..1   excentricidad de la elipse
    float chaos01    = 0.0f;   // 0..1   caos OVNI: velocidad errática + bamboleo
    int   dir        = CW;
    float freeHz     = 0.5f;   // vueltas/seg en modo Free
    float fixedAzRad = 0.0f;   // azimut fijo (rad) cuando rate == Fixed
    float doppler01  = 0.0f;   // 0..1   excentricidad de distancia (fly-by)
};

class Trajectory : public MovementSource
{
public:
    void prepare (double sampleRate) override;
    void reset() override;
    void setParams (const TrajectoryParams& p) noexcept { params = p; }

    // Afinable (geometría del fly-by). Default = arranque sensato (afinable por oído).
    //   maxEcc01: excentricidad máx (unidades radius01); curve: skew (tacto fino abajo).
    struct DopplerTuning { float maxEcc01 = 0.55f; float curve = 2.0f; };
    void setDopplerTuning (const DopplerTuning& t) noexcept { dopplerTune = t; }

    // Afinable: trayectorias nuevas. Spiral = vórtice (el radio respira lento, in/out);
    // Pendulum = hamaca (el azimut oscila ±swing, no rota). Defaults = arranque sensato.
    struct ShapeTuning
    {
        float spiralDepth01    = 0.35f;       // amplitud del respirar del radio en Spiral (unidades radius01)
        float spiralRateHz     = 0.16f;       // velocidad del vórtice (lento, independiente de la órbita)
        float pendulumSwingRad = 1.5707963f;  // ±amplitud del swing en Pendulum (90° por defecto)
    };
    void setShapeTuning (const ShapeTuning& t) noexcept { shapeTune = t; }

    SpatialTarget advance (int numSamples, const TransportInfo& transport) override;

private:
    double sampleRate = 48000.0;
    double phase      = 0.0;   // [0,1) vueltas — integrador free-run

    // Caos: dos ruidos suavizados deterministas (RT-safe). 1 = velocidad errática, 2 = bamboleo.
    unsigned int rngState = 0x9E3779B9u;
    float jitterCur = 0.0f, jitterCur2 = 0.0f;

    TrajectoryParams params;
    DopplerTuning     dopplerTune;
    ShapeTuning       shapeTune;
    double spiralPhase = 0.0;   // fase lenta del vórtice (Spiral)

    float nextWhite() noexcept;   // [-1, 1]
};

} // namespace ovni::engines
