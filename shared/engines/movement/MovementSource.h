#pragma once

namespace ovni::engines {

// Posición objetivo del punto sonoro.
//   azimuth  : rad, 0 = frente, + = CCW (hacia la izquierda).
//   elevation: normalizado [-1..1] (lo consume el módulo binaural; el motor de movimiento base lo ignora).
//   distance : normalizado [0..1], 0 = al oído (cerca) .. 1 = lejos.
struct SpatialTarget
{
    float azimuth   = 0.0f;
    float elevation = 0.0f;
    float distance  = 1.0f;
};

// Estado de transporte del host (lo llena el processor desde AudioPlayHead).
struct TransportInfo
{
    bool   isPlaying   = false;
    double bpm         = 120.0;
    double ppqPosition = 0.0;
    double timeSigNum  = 4.0;   // numerador del compás (p. ej. 4 en 4/4)
};

// Seam de extensibilidad: una FUENTE de movimiento produce azimut/distancia en el tiempo. Trajectory
// trae las Órbitas paramétricas; futuras fuentes (Audio-reactivo, Secuenciador, Física) implementan
// este mismo contrato y alimentan al MovementEngine igual.
class MovementSource
{
public:
    virtual ~MovementSource() = default;

    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;

    // Avanza numSamples de tiempo y devuelve el objetivo al FINAL del bloque. El motor interpola
    // desde su posición anterior hasta este objetivo (sin zipper).
    virtual SpatialTarget advance (int numSamples, const TransportInfo& transport) = 0;
};

} // namespace ovni::engines
