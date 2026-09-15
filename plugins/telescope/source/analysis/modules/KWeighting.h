#pragma once
#include <cmath>
#include <complex>

// ========================================================================================================
// K-weighting — ITU-R BS.1770, Anexo 1. Dos biquads en cascada:
//   1) shelf de agudos  (f0 ≈ 1681.9745 Hz, Q ≈ 0.7071752, G = +3.99984 dB)
//   2) paso-altos RLB   (f0 ≈ 38.13547 Hz,  Q ≈ 0.5003270)
//
// El estándar SÓLO publica la tabla de coeficientes a 48 kHz. Para cualquier otro sample rate hay que
// recalcularlos desde el PROTOTIPO ANALÓGICO por transformada bilineal con prewarp — que es lo que hacen
// libebur128 y pyloudnorm. La prueba de que el prototipo es el correcto: evaluado a 48 kHz reproduce la
// tabla publicada (KWeightingTest mide < 1e-6 por coeficiente; en la práctica da ~1e-15).
//
// Doble precisión en todo: el integrado se acumula sobre minutos de audio.
// Cabecera-only y sin JUCE: el DSP se testea headless.
// ========================================================================================================
namespace telescope
{
// Biquad normalizado (a0 = 1), forma directa: y = b0·x + b1·x₁ + b2·x₂ − a1·y₁ − a2·y₂.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct KCoeffs
{
    Biquad shelf, hp;
};

class KWeighting
{
public:
    // --- prototipo analógico (los mismos valores que usan libebur128 / pyloudnorm) ---
    static constexpr double kShelfF0 = 1681.9744509555319;
    static constexpr double kShelfQ  = 0.7071752369554193;
    static constexpr double kShelfG  = 3.999843853973347;    // dB
    static constexpr double kShelfVbExp = 0.4996667741545416;
    static constexpr double kHpF0    = 38.13547087602444;
    static constexpr double kHpQ     = 0.5003270373238773;

    // Recalcula los coeficientes para `fs` (bilineal + prewarp por tan(π·f0/fs)).
    static KCoeffs designAt (double fs) noexcept
    {
        KCoeffs c;

        {   // etapa 1 — shelf de agudos
            const double K  = std::tan (kPi * kShelfF0 / fs);
            const double Vh = std::pow (10.0, kShelfG / 20.0);
            const double Vb = std::pow (Vh, kShelfVbExp);
            const double a0 = 1.0 + K / kShelfQ + K * K;
            c.shelf.b0 = (Vh + Vb * K / kShelfQ + K * K) / a0;
            c.shelf.b1 = 2.0 * (K * K - Vh) / a0;
            c.shelf.b2 = (Vh - Vb * K / kShelfQ + K * K) / a0;
            c.shelf.a1 = 2.0 * (K * K - 1.0) / a0;
            c.shelf.a2 = (1.0 - K / kShelfQ + K * K) / a0;
        }
        {   // etapa 2 — paso-altos RLB
            const double K  = std::tan (kPi * kHpF0 / fs);
            const double a0 = 1.0 + K / kHpQ + K * K;
            c.hp.b0 =  1.0;
            c.hp.b1 = -2.0;
            c.hp.b2 =  1.0;
            c.hp.a1 = 2.0 * (K * K - 1.0) / a0;
            c.hp.a2 = (1.0 - K / kHpQ + K * K) / a0;
        }
        return c;
    }

    // La tabla LITERAL publicada por la ITU a 48 kHz (BS.1770-4 Anexo 1). Es la referencia contra la que
    // se valida `designAt` — no se usa para filtrar.
    static KCoeffs referenceAt48k() noexcept
    {
        KCoeffs c;
        c.shelf = { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 };
        c.hp    = { 1.0,              -2.0,              1.0,              -1.99004745483398, 0.99007225036621 };
        return c;
    }

    // |H(f)| en dB de la cascada, para tests y para dibujar la curva.
    static double magnitudeDb (const KCoeffs& c, double freq, double fs) noexcept
    {
        const std::complex<double> z = std::exp (std::complex<double> (0.0, -2.0 * kPi * freq / fs));
        const auto stage = [&z] (const Biquad& b)
        {
            return (b.b0 + b.b1 * z + b.b2 * z * z) / (1.0 + b.a1 * z + b.a2 * z * z);
        };
        return 20.0 * std::log10 (std::abs (stage (c.shelf) * stage (c.hp)));
    }

    void prepare (double fs) noexcept
    {
        c = designAt (fs);
        reset();
    }

    void reset() noexcept
    {
        xs1 = xs2 = ys1 = ys2 = 0.0;
        xh1 = xh2 = yh1 = yh2 = 0.0;
    }

    const KCoeffs& coeffs() const noexcept { return c; }

    double processSample (double x) noexcept
    {
        const double s = c.shelf.b0 * x + c.shelf.b1 * xs1 + c.shelf.b2 * xs2
                       - c.shelf.a1 * ys1 - c.shelf.a2 * ys2;
        xs2 = xs1; xs1 = x;
        ys2 = ys1; ys1 = s;

        const double h = c.hp.b0 * s + c.hp.b1 * xh1 + c.hp.b2 * xh2
                       - c.hp.a1 * yh1 - c.hp.a2 * yh2;
        xh2 = xh1; xh1 = s;
        yh2 = yh1; yh1 = h;

        return h;
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    KCoeffs c = designAt (48000.0);
    double xs1 = 0.0, xs2 = 0.0, ys1 = 0.0, ys2 = 0.0;   // estado del shelf
    double xh1 = 0.0, xh2 = 0.0, yh1 = 0.0, yh2 = 0.0;   // estado del paso-altos
};
}
