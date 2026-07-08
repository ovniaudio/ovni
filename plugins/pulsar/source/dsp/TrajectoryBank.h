#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

// =============================================================================
// PULSAR — Banco de sistemas dinámicos (el carácter del plugin). El motor
// compartido ovni::engines::MovementEngine NO trae generador de trayectoria:
// recibe (azimuthRad, distance01, width01, doppler01) ya resueltos. Este banco
// es ese generador — el "20% propio" de PULSAR.
//
// Rediseño "campo de caos": en vez de un switch entre 4 sistemas, integra los
// CUATRO siempre (Orbit/Pendulum/Lorenz/Rössler) y CRUZA-FADE-ea entre los dos
// adyacentes según SHAPE (morph continuo 0..1). MOTION acopla velocidad+caos de
// cada sistema (una órbita más energética es más rápida y más excéntrica). El
// CENTRO (fieldX/fieldY) corre el atractor en el plano L/R × cerca-lejos.
//
// Cada sistema queda normalizado a [-1,1] -> el crossfade y el sesgo de centro
// quedan acotados. DSP puro (sin JUCE GUI) -> testeable sin UI.
// =============================================================================
namespace pulsar::dsp
{

class TrajectoryBank
{
public:
    void prepare (double sampleRateHz) noexcept
    {
        sampleRate = sampleRateHz > 0.0 ? sampleRateHz : 48000.0;
        reset();
    }

    void reset() noexcept
    {
        phase = 0.0;
        // Condiciones iniciales fuera del origen para Lorenz/Rössler (si arrancan
        // en (0,0,0) se quedan en el punto fijo).
        lx = 0.1; ly = 0.0; lz = 0.0;
        rx = 0.1; ry = 0.0; rz = 0.0;
        for (int i = 0; i < 4; ++i) { ox[i] = 0.0f; oy[i] = 0.0f; }
        outX = 0.0f; outY = 0.0f;
    }

    // SHAPE: 0..1 -> posición en [Orbit, Pendulum, Lorenz, Rössler].
    void setShape (float s) noexcept { shape = juce::jlimit (0.0f, 1.0f, s); }

    // CENTRO del campo: corre el atractor en el plano L/R × cerca-lejos (lo setea el visual).
    void setCenter (float cx, float cy) noexcept
    {
        centerX = juce::jlimit (-1.0f, 1.0f, cx);
        centerY = juce::jlimit (-1.0f, 1.0f, cy);
    }

    // Avanza la integración `numSamples` y deja el estado en el último sample del
    // bloque. rateHz = frecuencia del ciclo base; motion01 = energía/wildness (acopla
    // velocidad + caos). El de-zipper de los destinos (azimut/distancia) vive en el
    // motor (rampa por-sample DENTRO de process()); acá entregamos el valor de fin de bloque.
    void advanceBlock (int numSamples, float rateHz, float motion01) noexcept
    {
        const double dt = 1.0 / sampleRate;
        const double f  = (double) clampf (rateHz, 0.01f, 10.0f);
        const double mo = (double) clampf (motion01, 0.0f, 1.0f);

        for (int n = 0; n < numSamples; ++n)
            stepOne (dt, f, mo);
    }

    // Salida normalizada del último sample, ambas en [-1,1].
    float x() const noexcept { return outX; }   // azimut (izq/der)
    float y() const noexcept { return outY; }   // profundidad/frente-atrás (-> distancia)

private:
    static float clampf (float v, float lo, float hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static double clampd (double v, double lo, double hi) noexcept
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Integra los 4 sistemas un sample (cada uno con su estado) y deja sus salidas
    // normalizadas en ox[i]/oy[i]. `mo` = MOTION 0..1 acopla velocidad + caos.
    void stepOne (double dt, double f, double mo) noexcept
    {
        constexpr double kTwoPi = 6.283185307179586;

        // MOTION escala el paso efectivo de integración: más energía -> avanza más por sample.
        const double speedScale = 0.25 + 1.75 * mo;

        // --- 0) Orbit: elipse newtoniana excéntrica (excentricidad sube con MOTION) ---
        {
            phase += kTwoPi * f * dt * speedScale;
            if (phase > kTwoPi) phase -= kTwoPi;
            const double ecc = 0.15 + 0.6 * mo;          // 0.15..0.75
            ox[0] = (float) (std::cos (phase));
            oy[0] = (float) ((1.0 - ecc) * std::sin (phase));
        }

        // --- 1) Pendulum: oscilador no-lineal; MOTION sube la amplitud (más no-lineal) ---
        {
            const double amp = 0.5 + 0.5 * mo;
            const double theta = amp * std::sin (phase);
            ox[1] = (float) std::sin (theta * 1.5707963);  // mapea a [-1,1]
            oy[1] = (float) (0.45 * std::cos (phase));      // poca excursión radial
        }

        // --- 2) Lorenz: σ=10, β=8/3; MOTION empuja ρ al régimen caótico ---
        {
            const double sigma = 10.0, beta = 8.0 / 3.0;
            const double rho   = 22.0 + 14.0 * mo;         // 22..36 (28 nominal)
            const double h = dt * f * 12.0 * speedScale;   // paso efectivo
            const double dx = sigma * (ly - lx);
            const double dy = lx * (rho - lz) - ly;
            const double dz = lx * ly - beta * lz;
            lx += h * dx; ly += h * dy; lz += h * dz;
            lx = clampd (lx, -30.0, 30.0); ly = clampd (ly, -30.0, 30.0); lz = clampd (lz, -5.0, 60.0);
            ox[2] = (float) clampd (lx / 20.0, -1.0, 1.0);
            oy[2] = (float) clampd ((lz - 25.0) / 25.0, -1.0, 1.0);
        }

        // --- 3) Rössler: a=0.2, b=0.2; MOTION sube c (más caótico) ---
        {
            const double a = 0.2, b = 0.2;
            const double c = 4.5 + 2.2 * mo;               // ~5.7 nominal
            const double h = dt * f * 18.0 * speedScale;
            const double dx = -(ry + rz);
            const double dy = rx + a * ry;
            const double dz = b + rz * (rx - c);
            rx += h * dx; ry += h * dy; rz += h * dz;
            rx = clampd (rx, -12.0, 12.0); ry = clampd (ry, -12.0, 12.0); rz = clampd (rz, -2.0, 24.0);
            ox[3] = (float) clampd (rx / 9.0, -1.0, 1.0);
            oy[3] = (float) clampd (ry / 9.0, -1.0, 1.0);
        }

        // --- morph: cross-fade entre los dos sistemas adyacentes según SHAPE ---
        const float pos = shape * 3.0f;            // 0..3
        const int   i0  = juce::jlimit (0, 3, (int) pos);
        const int   i1  = juce::jmin (3, i0 + 1);
        const float fr  = pos - (float) i0;
        outX = ox[i0] * (1.0f - fr) + ox[i1] * fr;
        outY = oy[i0] * (1.0f - fr) + oy[i1] * fr;

        // --- centro del campo: sesga la salida hacia (centerX, centerY) y reacota ---
        outX = juce::jlimit (-1.0f, 1.0f, outX * (1.0f - 0.5f * std::abs (centerX)) + centerX);
        outY = juce::jlimit (-1.0f, 1.0f, outY * (1.0f - 0.5f * std::abs (centerY)) + centerY);
    }

    double sampleRate = 48000.0;
    float  shape      = 0.66f;            // SHAPE 0..1 (default Lorenz)
    float  centerX = 0.0f, centerY = 0.0f; // CENTRO del campo (fieldX/fieldY)

    double phase = 0.0;                   // órbita / péndulo
    double lx = 0.1, ly = 0.0, lz = 0.0;  // estado Lorenz
    double rx = 0.1, ry = 0.0, rz = 0.0;  // estado Rössler

    float ox[4] = { 0.0f, 0.0f, 0.0f, 0.0f };  // salidas normalizadas por sistema
    float oy[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float outX = 0.0f, outY = 0.0f;
};

} // namespace pulsar::dsp
