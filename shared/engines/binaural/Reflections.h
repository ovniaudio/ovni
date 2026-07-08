#pragma once
#include <vector>

namespace ovni::engines {

// Campo de reflexiones tempranas DECORRELADAS (velvet-noise) para externalización.
// Física (Leclère 2019, Best 2020, Catic 2015): la externalización ("fuera de la cabeza")
// la predice la COHERENCIA INTERAURAL (IC); bajarla de ~1.0 a 0.5–0.7 saca el sonido afuera.
// El mecanismo: reflexiones tempranas (<80 ms) DECORRELADAS entre oídos (L≠R). Dióticas
// (L=R) casi no externalizan. Usamos secuencias velvet-noise independientes por oído
// (Välimäki/Schlecht): tren de pulsos ±1 disperso -> espectro plano (sin coloración),
// barato (casi multiply-free), y IC baja por construcción.
//
// Latencia 0: sólo SUMA copias retardadas (delays positivos), no retarda el directo.
class Reflections
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Suma reflexiones (derivadas de monoIn, el punto sonoro) a outL/outR.
    // busGain ya incluye mix * room * trim de seguridad (0 = sin reflexiones).
    void process (const float* monoIn, float* outL, float* outR, int n, float busGain);

private:
    struct Tap { int delay; float gain; };       // gain ya trae el signo ±1 y el envelope
    void buildTaps (std::vector<Tap>& taps, unsigned int seed);

    double sampleRate = 48000.0;
    std::vector<float> ring;
    int writePos = 0, mask = 0;
    std::vector<Tap> tapsL, tapsR;               // secuencias velvet independientes -> IC baja

    // Filtros 1-polo por oído: HP (~300 Hz, anti-mud/mono-safe) + LP (~7 kHz, aire).
    float hpCoef = 0.0f, lpCoef = 0.0f;
    float hpLx = 0, hpLy = 0, hpRx = 0, hpRy = 0;
    float lpL = 0, lpR = 0;
};

} // namespace ovni::engines
