#pragma once
#include <cmath>

namespace ovni::dsp {

// =====================================================================================
// De-zipper: rampas por-sample (la REGLA DE ORO anti-click).
// "TODO parámetro modulado (ganancia, coef de filtro, delay, ILD, paneo) debe rampearse
//  POR-SAMPLE, nunca aplicarse de golpe una vez por bloque." Un valor constante por bloque
//  que cambia entre bloques = escalón en el borde de cada bloque. A ~344-375 bordes/seg eso
//  es un tren de clicks audible ("crackle de vinilo"). En un plugin de movimiento los
//  parámetros se modulan TODO el tiempo, así que esto no es opcional.
//  Ver references/anti-click-clip-truepeak.md §0.
// =====================================================================================

// Rampa LINEAL por-sample, de 'value()' a un 'target', a lo largo de un bloque. Mantiene el
// último valor como continuidad C0 entre bloques. Para magnitudes donde la interpolación lineal
// es la natural: ganancias lineales, delay en samples, paneo ya resuelto a (gL, gR).
class LinearRamp
{
public:
    explicit LinearRamp (float initial = 0.0f) noexcept : current (initial) {}

    void  reset (float v) noexcept { current = v; }
    float value() const noexcept   { return current; }

    // Paso por-sample para recorrer current->target en n samples (0 si n<=0).
    float stepTo (float target, int n) const noexcept
    {
        return (n > 0) ? (target - current) / (float) n : 0.0f;
    }
    void advance (float step) noexcept { current += step; }

    // Aplica una ganancia rampeada in-place: buffer[i] *= g, con g recorriendo current->target.
    // Deja current = target (continuidad para el próximo bloque).
    void applyGain (float* buffer, int n, float target) noexcept
    {
        const float step = stepTo (target, n);
        float g = current;
        for (int i = 0; i < n; ++i) { buffer[i] *= g; g += step; }
        current = target;
    }

private:
    float current = 0.0f;
};

// Paso MULTIPLICATIVO (geométrico) por-sample: recorre current->target multiplicando por un ratio
// constante cada sample -> cambio LINEAL en dB (o en octavas para Hz). Para ganancias expresadas en
// dB y frecuencias de corte, donde lo perceptualmente uniforme es el dominio log, no el lineal.
// Devuelve el ratio por-sample (1 si alguno es <=0 o n<=0).
inline float multiplicativeStep (float current, float target, int n) noexcept
{
    if (n <= 0 || current <= 0.0f || target <= 0.0f) return 1.0f;
    return std::pow (target / current, 1.0f / (float) n);
}

// Paneo de POTENCIA CONSTANTE (ley-potencia): pan01 in [0,1] (0 = izquierda, 1 = derecha) ->
// (gL, gR) con gL^2 + gR^2 = 1. Energía constante al recorrer el paneo (sin bache de -3 dB en el
// centro). En el centro (0.5): gL = gR = cos(pi/4) ~ 0.707.
struct PanGains { float left = 0.70710678f; float right = 0.70710678f; };

inline PanGains constantPowerPan (float pan01) noexcept
{
    constexpr float kHalfPi = 1.57079632679489662f;
    const float p = (pan01 < 0.0f ? 0.0f : (pan01 > 1.0f ? 1.0f : pan01)) * kHalfPi;
    return { std::cos (p), std::sin (p) };
}

} // namespace ovni::dsp
