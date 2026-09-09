#pragma once
#include <algorithm>

// DISSOLVE — el FUNDIDO entre fotos. Puro: C++ sin JUCE ni GPU, testeable como PhotoSequence.
//
// El motor lo avanza con el `dt` QUE LE PASA EL RENDER, nunca con reloj de pared: el export rinde cuadro a
// cuadro (dt = 1/fps) y tiene que dar EXACTAMENTE lo mismo que la pantalla. La curva es un smoothstep: sale
// de 0 y llega a 1 con derivada nula, así el cambio no tiene escalón ni al empezar ni al terminar.
//
// Duración: `dissolveSecondsFor` la deriva del reloj de la secuencia — 45% del intervalo, con tope de 0.7 s
// y piso de 0.1 s. Con un gap de KICK de 0.25 s el fundido cierra en 0.1125 s, o sea ANTES del próximo kick:
// una ráfaga nunca se pisa consigo misma. Es una regla, no un ajuste: el usuario no configura nada.
namespace supernova
{
inline constexpr double kDissolveMaxSeconds   = 0.7;    // tope: más largo se siente lento, no suave
inline constexpr double kDissolveMinSeconds   = 0.1;    // piso: menos que esto vuelve a ser un corte
inline constexpr double kDissolveIntervalFrac = 0.45;   // fracción del intervalo del reloj

// Sin secuencia (drag&drop, relink, rotar, cue a mano): no hay intervalo del que derivar, se usa el tope.
inline constexpr double dissolveSecondsDefault() { return kDissolveMaxSeconds; }

// La regla de duración, pura: min(0.7 s, 0.45 × intervalo del reloj), nunca menos de 0.1 s.
// SECONDS → intervalSec · BEATS → intervalBeats × 60 / bpm (bpm ≤ 0 → 120) · KICK → kickGapSec.
//
// El reloj entra por TEMPLATE (siempre se instancia con supernova::SeqClock) para que este header quede
// sin JUCE: SeqClock vive en image/PhotoSequence.h, que arrastra juce_data_structures, y MetalRenderer.mm
// —que incluye este archivo— no tiene una sola línea de JUCE y no puede empezar a tenerla. Los
// enumeradores se nombran igual que siempre: nada de comparar contra enteros crudos.
template <typename SeqClockT>
inline double dissolveSecondsFor (SeqClockT clock, double intervalSec, double intervalBeats,
                                  double bpm, double kickGapSec)
{
    double interval = intervalSec;
    if      (clock == SeqClockT::Beats) interval = intervalBeats * 60.0 / (bpm > 0.0 ? bpm : 120.0);
    else if (clock == SeqClockT::Kick)  interval = kickGapSec;

    return std::clamp (interval * kDissolveIntervalFrac, kDissolveMinSeconds, kDissolveMaxSeconds);
}

class Dissolve
{
public:
    // Arranca (o RE-arranca) un fundido de `seconds`. Con 0 (o menos) es un CORTE: queda inactivo.
    // El re-arranque a mitad de otro devuelve la mezcla a 0 — la continuidad la da el caller, que hornea
    // la base ya mezclada antes de llamar (ver MetalRenderer::uploadImage); un mix que arrancara en el
    // valor viejo sobre una imagen NUEVA sería justamente el salto que este fundido viene a matar.
    void start (double seconds) noexcept
    {
        secs = seconds;
        t01  = 0.0;
        on   = seconds > 0.0;
    }

    // Avanza con el dt del render (el mismo que come la física). Satura en 1 y se apaga solo.
    void tick (double dt) noexcept
    {
        if (! on) return;
        t01 += dt / secs;
        if (t01 >= 1.0) { t01 = 1.0; on = false; }
    }

    bool   active()  const noexcept { return on; }
    double seconds() const noexcept { return secs; }

    // Mezcla [0..1] con smoothstep: derivada 0 en los dos bordes (arranque y llegada sin escalón).
    float mix01() const noexcept
    {
        const double t = std::clamp (t01, 0.0, 1.0);
        return (float) (t * t * (3.0 - 2.0 * t));
    }

private:
    double t01  = 0.0;
    double secs = 0.0;
    bool   on   = false;
};
}
